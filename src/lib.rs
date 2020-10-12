pub mod backends;
pub mod config;
pub mod sampler;
pub mod shard;
pub mod statsd;
pub mod statsd_client;
pub mod statsd_server;
pub mod built_info {
    // The file has been placed there by the build script.
    include!(concat!(env!("OUT_DIR"), "/built.rs"));
}
