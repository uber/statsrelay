use anyhow::Context;
use stream_cancel::Tripwire;
use structopt::StructOpt;
use tokio::runtime;
use tokio::select;
use tokio::signal::unix::{signal, SignalKind};

use env_logger::Env;
use log::{debug, info};
use metrics_runtime::Receiver;

use tugboat::backends;
use tugboat::statsd_server;

#[derive(StructOpt, Debug)]
struct Options {
    #[structopt(short = "c", long = "--config", default_value = "/etc/tugboat.json")]
    pub config: String,
}

fn main() -> anyhow::Result<()> {
    env_logger::from_env(Env::default().default_filter_or("info")).init();
    let opts = Options::from_args();

    info!(
        "tugboat loading - {} - {}",
        tugboat::built_info::PKG_VERSION,
        tugboat::built_info::GIT_COMMIT_HASH.unwrap_or("unknown")
    );

    let config = tugboat::config::load_legacy_config(opts.config.as_ref())
        .with_context(|| format!("can't load config file from {}", opts.config))?;
    info!("loaded config file {}", opts.config);
    debug!("bind address: {}", config.statsd.bind);

    let receiver = Receiver::builder().build()?;
    receiver.install();

    debug!("installed metrics receiver");

    let mut threaded_rt = runtime::Builder::new()
        .enable_all()
        .threaded_scheduler()
        .build()
        .unwrap();

    debug!("built tokio runtime");

    threaded_rt.block_on(async move {
        let backends = backends::Backends::new();
        if config.statsd.shard_map.len() > 0 {
            backends.add_statsd_backend(&tugboat::config::StatsdDuplicateTo::from_shards(
                config.statsd.shard_map.clone(),
            ));
        }
        let (sender, tripwire) = Tripwire::new();
        let run = statsd_server::run(tripwire, config.statsd.bind.clone(), backends);

        // Trap ctrl+c and sigterm messages and perform a clean shutdown
        let mut sigint = signal(SignalKind::interrupt()).unwrap();
        let mut sigterm = signal(SignalKind::terminate()).unwrap();
        tokio::spawn(async move {
            select! {
            _ = sigint.recv() => info!("received sigint"),
            _ = sigterm.recv() => info!("received sigterm"),
            }
            sender.cancel();
        });

        run.await;
    });

    drop(threaded_rt);
    info!("runtime terminated");
    Ok(())
}
