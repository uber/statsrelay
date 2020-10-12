use bytes::{BufMut, BytesMut};
use futures::stream::StreamExt;
use memchr::memchr;
use stream_cancel::StreamExt as Cancel;
use stream_cancel::Tripwire;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio::select;
use tokio::time::timeout;

use std::io::ErrorKind;
use std::net::UdpSocket;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;
use std::time::Duration;

use log::{info, warn};

use crate::backends::Backends;
use crate::statsd::StatsdPDU;

const TCP_READ_TIMEOUT: Duration = Duration::from_secs(62);

struct UdpServer {
    shutdown_gate: Arc<AtomicBool>,
}

impl Drop for UdpServer {
    fn drop(&mut self) {
        self.shutdown_gate.store(true, Relaxed);
    }
}

impl UdpServer {
    fn new() -> Self {
        UdpServer {
            shutdown_gate: Arc::new(AtomicBool::new(false)),
        }
    }

    fn udp_worker(&mut self, bind: String, backends: Backends) -> std::thread::JoinHandle<()> {
        let socket = UdpSocket::bind(bind.as_str()).unwrap();
        // We set a small timeout to allow aborting the UDP server if there is no
        // incoming traffic.
        socket
            .set_read_timeout(Some(Duration::from_secs(1)))
            .unwrap();
        info!("statsd udp server running on {}", bind);
        let gate = self.shutdown_gate.clone();
        std::thread::spawn(move || {
            loop {
                if gate.load(Relaxed) {
                    break;
                }
                let mut buf = BytesMut::with_capacity(65535);

                match socket.recv_from(&mut buf[..]) {
                    Ok((_size, _remote)) => {
                        let mut r = process_buffer_newlines(&mut buf);
                        for p in r.drain(..) {
                            backends.provide_statsd_pdu(p);
                        }
                        match StatsdPDU::new(buf.clone().freeze()) {
                            Some(p) => backends.provide_statsd_pdu(p),
                            None => (),
                        }
                        buf.clear();
                    }
                    Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => (),
                    Err(e) => warn!("udp receiver error {:?}", e),
                }
            }
            info!("terminating statsd udp");
        })
    }
}

fn process_buffer_newlines(buf: &mut BytesMut) -> Vec<StatsdPDU> {
    let mut ret: Vec<StatsdPDU> = Vec::new();
    loop {
        match memchr(b'\n', &buf) {
            None => break,
            Some(newline) => {
                let mut incoming = buf.split_to(newline + 1);
                if incoming[incoming.len() - 2] == b'\r' {
                    incoming.truncate(incoming.len() - 2);
                } else {
                    incoming.truncate(incoming.len() - 1);
                }
                StatsdPDU::new(incoming.freeze()).map(|f| ret.push(f));
            }
        };
    }
    return ret;
}

async fn client_handler(mut tripwire: Tripwire, mut socket: TcpStream, backends: Backends) {
    let mut buf = BytesMut::with_capacity(65535);
    loop {
        if buf.remaining_mut() < 65535 {
            buf.reserve(65535);
        }
        let result = select! {
            r = timeout(TCP_READ_TIMEOUT, socket.read_buf(&mut buf)) => {
                match r {
                    Err(_e)  => Err(std::io::Error::new(ErrorKind::TimedOut, "read timeout")),
                    Ok(Err(e)) => Err(e),
                    Ok(Ok(r)) => Ok(r),
                }
            },
            _ = &mut tripwire => Err(std::io::Error::new(ErrorKind::Other, "shutting down")),
        };

        match result {
            Ok(bytes) if buf.is_empty() && bytes == 0 => {
                info!(
                    "closing reader (empty buffer, eof) {:?}",
                    socket.peer_addr()
                );
                break;
            }
            Ok(bytes) if bytes == 0 => {
                let mut r = process_buffer_newlines(&mut buf);
                for p in r.drain(..) {
                    backends.provide_statsd_pdu(p);
                }
                let remaining = buf.clone().freeze();
                match StatsdPDU::new(remaining) {
                    Some(p) => {
                        backends.provide_statsd_pdu(p);
                        ()
                    }
                    None => (),
                };
                info!("remaining {:?}", buf);
                info!("closing reader {:?}", socket.peer_addr());
                break;
            }
            Ok(_bytes) => {
                let mut r = process_buffer_newlines(&mut buf);
                for p in r.drain(..) {
                    backends.provide_statsd_pdu(p);
                }
            }
            Err(e) if e.kind() == ErrorKind::Other => {
                // Ignoring the results of the write call here
                let _ = timeout(
                    Duration::from_secs(1),
                    socket.write_all(b"server closing due to shutdown, goodbye\n"),
                )
                .await;
                break;
            }
            Err(e) if e.kind() == ErrorKind::TimedOut => {
                info!("read timeout, closing {:?}", socket.peer_addr());
                break;
            }
            Err(e) => {
                warn!("socket error {:?} from {:?}", e, socket.peer_addr());
                break;
            }
        }
    }
}

pub async fn run(tripwire: Tripwire, bind: String, backends: Backends) {
    //self.shutdown_trigger = Some(trigger);
    let mut listener = TcpListener::bind(bind.as_str()).await.unwrap();
    let mut udp = UdpServer::new();
    let bind_clone = bind.clone();
    let udp_join = udp.udp_worker(bind_clone, backends.clone());
    info!("statsd tcp server running on {}", bind);

    let mut incoming = listener.incoming().take_until_if(tripwire.clone());
    async move {
        while let Some(socket_res) = incoming.next().await {
            match socket_res {
                Ok(socket) => {
                    info!("accepted connection from {:?}", socket.peer_addr());
                    tokio::spawn(client_handler(tripwire.clone(), socket, backends.clone()));
                }
                Err(err) => {
                    info!("accept error = {:?}", err);
                }
            }
        }

        info!("stopped tcp listener loop");
    }
    .await;
    drop(udp);
    tokio::task::spawn_blocking(move || {
        udp_join.join().unwrap();
    })
    .await
    .unwrap();
}

#[cfg(test)]
pub mod test {
    use super::*;
    #[test]
    fn test_process_buffer_no_newlines() {
        let mut b = BytesMut::new();
        // Validate we don't consume non-newlines
        b.put_slice(b"hello");
        let r = process_buffer_newlines(&mut b);
        assert!(r.len() == 0);
        assert!(b.split().as_ref() == b"hello");
    }

    #[test]
    fn test_process_buffer_newlines() {
        let mut b = BytesMut::new();
        // Validate we don't consume newlines, but not a remnant
        b.put_slice(b"hello:1|c\nhello:1|c\nhello2");
        let r = process_buffer_newlines(&mut b);
        assert!(r.len() == 2);
        assert!(b.split().as_ref() == b"hello2");
    }

    #[test]
    fn test_process_buffer_cr_newlines() {
        let mut found = 0;
        let mut b = BytesMut::new();
        // Validate we don't consume newlines, but not a remnant
        b.put_slice(b"hello:1|c\r\nhello:1|c\nhello2");
        let r = process_buffer_newlines(&mut b);
        for w in r {
            assert!(w.pdu_type() == b"c");
            assert!(w.name() == b"hello");
            found += 1
        }
        assert_eq!(2, found);
        assert!(b.split().as_ref() == b"hello2");
    }
}
