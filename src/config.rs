use std::collections::HashMap;

use serde::{Deserialize, Serialize};
use thiserror::Error;

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Processor {
    Sampler {
        counter_cardinality: Option<u32>,
        sampling_threshold: Option<u32>,
        sampling_window: Option<u32>,

        gauge_cardinality: Option<u32>,
        gauge_sampling_threshold: Option<u32>,
        gauge_sampling_window: Option<u32>,

        timer_cardinality: Option<u32>,
        timer_sampling_threshold: Option<u32>,
        timer_sampling_window: Option<u32>,
        reservoir_size: Option<u32>,
    },
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Processors {
    pub processors: std::collections::HashMap<String, Processor>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct StatsdDuplicateTo {
    #[serde(default)]
    pub shard_map: Vec<String>,
    pub shard_map_source: Option<String>,
    pub suffix: Option<String>,
    pub prefix: Option<String>,
    pub input_blocklist: Option<String>,
    pub input_filter: Option<String>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct StatsdConfig {
    pub bind: String,
    pub point_tag_regex: Option<String>,
    pub validate: Option<bool>,
    pub tcp_cork: Option<bool>,
    pub backends: HashMap<String, StatsdDuplicateTo>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct S3DiscoverySource {
    pub bucket: String,
    pub key: String,
    pub interval: u32,
    pub format: Option<String>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct PathDiscoverySource {
    pub path: String,
    pub interval: u32,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum DiscoverySource {
    StaticFile(PathDiscoverySource),
    S3(S3DiscoverySource),
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Discovery {
    pub sources: HashMap<String, DiscoverySource>,
}

impl Default for Discovery {
    fn default() -> Self {
        Discovery {
            sources: HashMap::new(),
        }
    }
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Config {
    pub statsd: StatsdConfig,
    pub discovery: Option<Discovery>,
    pub processor: Option<Processors>,
}

#[derive(Error, Debug)]
pub enum Error {
    #[error("could not locate discovery source {0}")]
    UnknownDiscoverySource(String),
}

pub fn check_config(config: &Config) -> anyhow::Result<()> {
    let default = Discovery::default();
    let discovery = &config.discovery.as_ref().unwrap_or(&default);
    // Every reference to a shard_map needs a reference to a valid discovery block
    for (_, statsd_dupl) in config.statsd.backends.iter() {
        if let Some(source) = &statsd_dupl.shard_map_source {
            if let None = discovery.sources.get(source) {
                return Err(Error::UnknownDiscoverySource(source.clone()).into());
            }
        }
    }
    Ok(())
}

pub fn load(path: &str) -> anyhow::Result<Config> {
    let input = std::fs::read_to_string(path)?;
    let config: Config = serde_json::from_str(input.as_ref())?;
    // Perform some high level validation
    check_config(&config)?;
    Ok(config)
}

#[cfg(test)]
pub mod test {
    use super::*;
    use std::io::Write;
    use tempfile::NamedTempFile;

    #[test]
    fn load_example_config() {
        let config = r#"
        {
            "statsd": {
                "bind": "127.0.0.1:BIND_STATSD_PORT",
                "backends": {
                    "test1":
                       {
                            "prefix": "test-1.",
                            "shard_map": [
                                "127.0.0.1:SEND_STATSD_PORT"
                            ],
                            "suffix": ".suffix"
                        },
                "mapsource":
                        {
                            "input_filter": "^(?=dontmatchme)",
                            "prefix": "test-2.",
                            "shard_map_source": "my_s3"
                        }
                },
                "point_tag_regex": "\\.__([a-zA-Z][a-zA-Z0-9_]+)=[a-zA-Z0-9_/-]+",
                "tcp_cork": true,
                "validate": true
            },
            "discovery": {
                "sources": {
                    "file": {
                        "type":"static_file",
                        "path":"/tmp/file",
                        "interval":5
                    },
                    "my_s3": {
                        "type": "s3",
                        "bucket": "foo",
                        "key": "bar",
                        "interval": 3
                    }
                }
            }
        }
        "#;
        let mut tf = NamedTempFile::new().unwrap();
        tf.write_all(config.as_bytes()).unwrap();
        let config = load(tf.path().to_str().unwrap()).unwrap();
        assert_eq!(config.statsd.validate, Some(true));

        // Check discovery
        let discovery = config.discovery.unwrap();
        assert_eq!(2, discovery.sources.len());
        let s3_source = discovery.sources.get("my_s3").unwrap();
        match s3_source {
            DiscoverySource::S3(source) => {
                assert!(source.bucket == "foo");
            }
            _ => panic!("not an s3 source"),
        };
    }
}
