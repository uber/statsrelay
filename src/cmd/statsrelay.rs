use anyhow::Context;
use futures::StreamExt;
use serde::__private::size_hint::cautious;
use stream_cancel::Tripwire;
use structopt::StructOpt;

use tokio::runtime;
use tokio::select;
use tokio::signal::unix::{signal, SignalKind};

use env_logger::Env;
use log::{debug, error, info};

use statsrelay::backends;
use statsrelay::discovery;
use statsrelay::statsd_server;

#[derive(StructOpt, Debug)]
struct Options {
    #[structopt(short = "c", long = "--config", default_value = "/etc/statsrelay.json")]
    pub config: String,

    #[structopt(short = "t", long = "--threaded")]
    pub threaded: bool,
}

fn main() -> anyhow::Result<()> {
    env_logger::Builder::from_env(Env::default().default_filter_or("info")).init();
    let opts = Options::from_args();

    info!(
        "statsrelay loading - {} - {}",
        statsrelay::built_info::PKG_VERSION,
        statsrelay::built_info::GIT_COMMIT_HASH.unwrap_or("unknown")
    );

    let config = statsrelay::config::load(opts.config.as_ref())
        .with_context(|| format!("can't load config file from {}", opts.config))?;
    info!("loaded config file {}", opts.config);
    debug!("bind address: {}", config.statsd.bind);

    debug!("installed metrics receiver");

    let mut builder = match opts.threaded {
        true => runtime::Builder::new_multi_thread(),
        false => runtime::Builder::new_current_thread(),
    };

    let runtime = builder.enable_all().build().unwrap();
    info!("tokio runtime built, threaded: {}", opts.threaded);

    runtime.block_on(async move {
        let backends = backends::Backends::new();

        let (sender, tripwire) = Tripwire::new();
        let run = statsd_server::run(tripwire, config.statsd.bind.clone(), backends.clone());

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

        // Trap sighup to support manual file reloading
        let mut sighup = signal(SignalKind::hangup()).unwrap();
        tokio::spawn(async move {
            let dconfig = config.discovery.unwrap_or_default();
            let discovery_cache = discovery::Cache::new();
            let mut discovery_stream =
                discovery::reflector(discovery_cache.clone(), discovery::as_stream(&dconfig));
            loop {
                info!("loading configuration and updating backends");
                let _ = load_config(&discovery_cache, &backends, opts.config.as_ref()).await;
                tokio::select! {
                    _ = sighup.recv() => {
                        info!("received sighup");
                    }
                    Some(event) = discovery_stream.next() => {
                        info!("updating discovery for map {}", event.0);
                    }
                };
            }
        });

        run.await;
    });

    drop(runtime);
    info!("runtime terminated");
    Ok(())
}

async fn load_config(
    discovery_cache: &discovery::Cache,
    backends: &backends::Backends,
    path: &str,
) -> anyhow::Result<()> {
    // Check if we have to load the configuration file
    let config = match statsrelay::config::load(path)
        .with_context(|| format!("can't load config file from {}", path))
    {
        Err(e) => {
            error!("failed to reload configuration: {}", e);
            return Err(e).context("failed to reload configuration file");
        }
        Ok(ok) => ok,
    };

    let duplicate = config.statsd.duplicate_to;
    for (idx, dp) in duplicate.iter().enumerate() {
        if let Err(e) = backends.replace_statsd_backend(idx, dp) {
            error!("failed to replace backend index {} error {}", idx, e);
            continue;
        }
    }
    if backends.len() > duplicate.len() {
        for idx in duplicate.len() + 0..backends.len() {
            if let Err(e) = backends.remove_statsd_backend(idx) {
                error!("failed to remove backend block {} with error {}", idx, e);
            }
        }
    }

    info!("backends reloaded");
    Ok(())
}
