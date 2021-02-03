use log::info;

use hyper::service::{make_service_fn, service_fn};
use hyper::{Body, Method, Request, Response, Server};
use tokio::runtime;

use std::boxed::Box;
use std::convert::Infallible;

use crate::stats::Collector;

#[derive(Clone)]
struct AdminState {
    collector: Collector,
}

async fn metric_response(
    state: AdminState,
    _req: Request<Body>,
) -> Result<Response<Body>, Infallible> {
    let buffer = state.collector.prometheus_output().unwrap();
    Ok(Response::builder()
        .header(hyper::header::CONTENT_TYPE, prometheus::TEXT_FORMAT)
        .body(Body::from(buffer))
        .unwrap())
}

async fn request_handler(
    state: AdminState,
    req: Request<Body>,
) -> Result<Response<Body>, Infallible> {
    match (req.method(), req.uri().path()) {
        (&Method::GET, "/") => Ok(Response::builder()
            .body(Body::from("statsrelay admin server"))
            .unwrap()),
        (&Method::GET, "/healthcheck") => Ok(Response::builder().body(Body::from("OK")).unwrap()),
        (&Method::GET, "/metrics") => metric_response(state, req).await,
        _ => Ok(Response::builder()
            .status(404)
            .body(Body::from("not found"))
            .unwrap()),
    }
}

async fn hyper_server(port: u16, collector: Collector) -> Result<(), Box<dyn std::error::Error>> {
    let addr = format!("[::]:{}", port).parse().unwrap();
    let admin_state = AdminState {
        collector: collector,
    };
    let make_svc = make_service_fn(move |_conn| {
        let service_capture = admin_state.clone();
        async {
            Ok::<_, Infallible>(service_fn(move |req| {
                request_handler(service_capture.clone(), req)
            }))
        }
    });
    info!("admin server starting on port {}", port);
    Server::bind(&addr).serve(make_svc).await?;
    Ok(())
}

pub fn spawn_admin_server(port: u16, collector: Collector) {
    let rt = runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .unwrap();
    std::thread::spawn(move || rt.block_on(hyper_server(port, collector)).unwrap());
}
