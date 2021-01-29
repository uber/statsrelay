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

impl StatsdDuplicateTo {
    pub fn from_shards(shards: Vec<String>) -> Self {
        StatsdDuplicateTo {
            shard_map: shards,
            shard_map_source: None,
            suffix: None,
            prefix: None,
            input_blocklist: None,
            input_filter: None,
        }
    }
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct StatsdConfig {
    pub bind: String,
    pub point_tag_regex: Option<String>,
    pub validate: Option<bool>,
    pub tcp_cork: Option<bool>,
    pub duplicate_to: Vec<StatsdDuplicateTo>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum DiscoverySource {
    StaticFile {
        path: String,
        interval: u32,
    },
    S3 {
        bucket: String,
        key: String,
        interval: u32,
    },
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
    for statsd_dupl in config.statsd.duplicate_to.iter() {
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
                "duplicate_to": [
                    {
                        "prefix": "test-1.",
                        "shard_map": [
                            "127.0.0.1:SEND_STATSD_PORT"
                        ],
                        "suffix": ".suffix"
                    },
                    {
                        "input_filter": "^(?=dontmatchme)",
                        "prefix": "test-2.",
                        "shard_map_source": "my_s3"
                    }
                ],
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
    }
}
