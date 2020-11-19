use anyhow::Context;
use stream_cancel::Tripwire;
use structopt::StructOpt;
use tokio::runtime;
use tokio::select;
use tokio::signal::unix::{signal, SignalKind};

use env_logger::Env;
use log::{debug, error, info};
use metrics_runtime::Receiver;

use statsrelay::backends;
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

    let config = statsrelay::config::load_legacy_config(opts.config.as_ref())
        .with_context(|| format!("can't load config file from {}", opts.config))?;
    info!("loaded config file {}", opts.config);
    debug!("bind address: {}", config.statsd.bind);

    let receiver = Receiver::builder().build()?;
    receiver.install();

    debug!("installed metrics receiver");

    let mut builder = &mut runtime::Builder::new();
    builder = match opts.threaded {
        true => builder.threaded_scheduler(),
        false => builder.basic_scheduler(),
    };

    let mut runtime = builder.enable_all().build().unwrap();
    info!("tokio runtime built, threaded: {}", opts.threaded);

    runtime.block_on(async move {
        let backends = backends::Backends::new();
        // Load the default backend set, even if its zero sized, to avoid index
        // errors later
        if let Err(e) = backends.add_statsd_backend(
            &statsrelay::config::StatsdDuplicateTo::from_shards(config.statsd.shard_map.clone()),
        ) {
            error!("invalid backend created {}", e);
            return;
        }
        for duplicate_to_block in config.statsd.duplicate_to.map_or(vec![], |f| f) {
            if let Err(e) = backends.add_statsd_backend(&duplicate_to_block) {
                error!("invalid backend created {}", e);
                return;
            }
        }
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
            loop {
                sighup.recv().await;
                info!("reloading configuration and replacing backends");
                reload_config_from_file(&backends, opts.config.as_ref()).await;
            }
        });

        run.await;
    });

    drop(runtime);
    info!("runtime terminated");
    Ok(())
}

async fn reload_config_from_file(backends: &backends::Backends, path: &str) {
    let config = match statsrelay::config::load_legacy_config(path)
        .with_context(|| format!("can't load config file from {}", path))
    {
        Err(e) => {
            error!("failed to reload configuration: {}", e);
            return;
        }
        Ok(ok) => ok,
    };

    if let Err(e) = backends.replace_statsd_backend(
        0,
        &statsrelay::config::StatsdDuplicateTo::from_shards(config.statsd.shard_map.clone()),
    ) {
        error!("invalid backend created {}", e);
    }
    if let Some(duplicate) = config.statsd.duplicate_to {
        for (idx, dp) in duplicate.iter().enumerate() {
            if let Err(e) = backends.replace_statsd_backend(idx, dp) {
                error!("failed to replace backend index {} error {}", idx, e);
                continue;
            }
        }
        if backends.len() > duplicate.len() {
            for idx in duplicate.len()+1..backends.len() {
                if let Err(e) = backends.remove_statsd_backend(idx) {
                    error!("failed to remove backend block {} with error {}", idx, e);
                }
            }
        }
    }
    info!("backends reloaded");
}
