use bytes::{BufMut, BytesMut};
use chrono::prelude::*;
use structopt::StructOpt;
use tokio::io::AsyncWriteExt;
use tokio::net::TcpStream;

const PRINT_INTERVAL: u64 = 1000000;

#[derive(StructOpt, Debug)]
struct Options {
    #[structopt(short = "e", long = "--endpoint", default_value = "localhost:8129")]
    pub endpoint: String,
}

#[tokio::main]
async fn main() {
    let options = Options::from_args();
    let mut stream = TcpStream::connect(options.endpoint).await.unwrap();
    let mut buf = BytesMut::with_capacity(131072);
    let mut counter = 0 as u64;
    let mut last_time = Local::now();
    loop {
        for _ in 0..1 {
            buf.put(
                format!(
                    "hello.hello.hello.hello.hello.hello.hello.hello.hello:{}|c\n",
                    counter
                )
                .as_bytes()
                .as_ref(),
            );
        }
        stream.write_buf(&mut buf).await.unwrap();
        counter += 1;

        if counter % PRINT_INTERVAL == 0 {
            let now_time = Local::now();
            let diff = now_time - last_time;
            last_time = now_time;
            println!(
                "{}: sent {:15} lines in {:5}ms ({:.0} l/s)",
                now_time.format("%H:%M:%S"),
                counter,
                diff.num_milliseconds(),
                PRINT_INTERVAL as f64 / (diff.num_milliseconds() as f64 / 1000.0)
            );
            //tokio::time::delay_for(Duration::from_millis(40)).await;
        };
    }
}
