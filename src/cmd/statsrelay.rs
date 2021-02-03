use anyhow::Context;
use futures::StreamExt;
use stream_cancel::Tripwire;
use structopt::StructOpt;

use std::collections::HashSet;

use tokio::runtime;
use tokio::select;
use tokio::signal::unix::{signal, SignalKind};

use env_logger::Env;
use log::{debug, error, info};

use statsrelay::admin;
use statsrelay::backends;
use statsrelay::config;
use statsrelay::discovery;
use statsrelay::stats;
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

    let collector = stats::Collector::default();

    if let Some(admin) = &config.admin {
        admin::spawn_admin_server(admin.port, collector.clone());
        info!("spawned admin server on port {}", admin.port);
    }
    debug!("installed metrics receiver");

    let mut builder = match opts.threaded {
        true => runtime::Builder::new_multi_thread(),
        false => runtime::Builder::new_current_thread(),
    };

    let runtime = builder.enable_all().build().unwrap();
    info!("tokio runtime built, threaded: {}", opts.threaded);

    let scope = collector.scope("statsrelay");

    runtime.block_on(async move {
        let backends = backends::Backends::new(scope.scope("backends"));

        let backend_reloads = scope.counter("backend_reloads").unwrap();

        let (sender, tripwire) = Tripwire::new();
        let run = statsd_server::run(scope.scope("statsd_server"), tripwire, config.statsd.bind.clone(), backends.clone());

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
        // This task is designed to asynchronously build backend configurations,
        // which may in turn come from other data sources or discovery sources.
        // This inherently races with bringing up servers, to the point where a
        // server may not have any backends to dispatch to yet, if discovery is
        // very slow. This is the intended state, as configuration of processors
        // and any buffers should have already been performed.
        //
        // SIGHUP will attempt to reload backend configurations as well as any
        // discovery changes.
        tokio::spawn(async move {
            let dconfig = config.discovery.unwrap_or_default();
            let discovery_cache = discovery::Cache::new();
            let mut discovery_stream =
                discovery::reflector(discovery_cache.clone(), discovery::as_stream(&dconfig));
            loop {
                info!("loading configuration and updating backends");
                backend_reloads.inc();
                let config = load_backend_configs(&discovery_cache, &backends, opts.config.as_ref()).await.unwrap();
                let dconfig = config.discovery.unwrap_or_default();

                tokio::select! {
                    _ = sighup.recv() => {
                        info!("received sighup");
                        discovery_stream = discovery::reflector(discovery_cache.clone(), discovery::as_stream(&dconfig));
                        info!("reloaded discovery stream");
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

async fn load_backend_configs(
    discovery_cache: &discovery::Cache,
    backends: &backends::Backends,
    path: &str,
) -> anyhow::Result<config::Config> {
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

    let duplicate = &config.statsd.backends;
    for (name, dp) in duplicate.iter() {
        let discovery_data = if let Some(discovery_name) = &dp.shard_map_source {
            discovery_cache.get(discovery_name)
        } else {
            None
        };
        if let Err(e) = backends.replace_statsd_backend(name, dp, discovery_data.as_ref()) {
            error!("failed to replace backend index {} error {}", name, e);
            continue;
        }
    }
    let existing_backends = backends.backend_names();
    let config_backends: HashSet<String> = duplicate.keys().map(|s| s.clone()).collect();
    let difference = existing_backends.difference(&config_backends);
    for remove in difference {
        if let Err(e) = backends.remove_statsd_backend(remove) {
            error!("failed to remove backend {} with error {:?}", remove, e);
        }
    }

    info!("backends reloaded");
    Ok(config)
}
