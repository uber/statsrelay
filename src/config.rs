use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct StatsdDuplicateTo {
    pub shard_map: Vec<String>,
    pub suffix: Option<String>,
    pub prefix: Option<String>,
    pub input_blacklist: Option<String>,
    /// Apologies for the name, need to preserve backwards compatibility
    pub input_filter: Option<String>,
    pub counter_cardinality: Option<u32>,
    pub sampling_threshold: Option<u32>,
    pub sampling_window: Option<u32>,

    pub gauge_cardinality: Option<u32>,
    pub gauge_sampling_threshold: Option<u32>,
    pub gauge_sampling_window: Option<u32>,

    pub timer_cardinality: Option<u32>,
    pub timer_sampling_threshold: Option<u32>,
    pub timer_sampling_window: Option<u32>,
    pub reservoir_size: Option<u32>,
}

impl StatsdDuplicateTo {
    pub fn from_shards(shards: Vec<String>) -> Self {
        StatsdDuplicateTo {
            shard_map: shards,
            suffix: None,
            prefix: None,
            input_blacklist: None,
            input_filter: None,
            counter_cardinality: None,
            sampling_threshold: None,
            sampling_window: None,
            gauge_cardinality: None,
            gauge_sampling_threshold: None,
            gauge_sampling_window: None,
            timer_cardinality: None,
            timer_sampling_threshold: None,
            timer_sampling_window: None,
            reservoir_size: None,
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub struct StatsdConfig {
    pub bind: String,
    pub point_tag_regex: Option<String>,
    pub validate: Option<bool>,
    pub tcp_cork: Option<bool>,
    pub shard_map: Vec<String>,
    pub duplicate_to: Option<Vec<StatsdDuplicateTo>>,
}
#[derive(Serialize, Deserialize, Debug)]
pub struct LegacyConfig {
    pub statsd: StatsdConfig,
}

pub fn load_legacy_config(path: &str) -> anyhow::Result<LegacyConfig> {
    let input = std::fs::read_to_string(path)?;
    let config: LegacyConfig = serde_json::from_str(input.as_ref())?;
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
                        "shard_map": [
                            "127.0.0.1:SEND_STATSD_PORT"
                        ]
                    }
                ],
                "point_tag_regex": "\\.__([a-zA-Z][a-zA-Z0-9_]+)=[a-zA-Z0-9_/-]+",
                "shard_map": [
                    "127.0.0.1:SEND_STATSD_PORT",
                    "127.0.0.1:SEND_STATSD_PORT",
                    "127.0.0.1:SEND_STATSD_PORT",
                    "127.0.0.1:SEND_STATSD_PORT",
                    "127.0.0.1:SEND_STATSD_PORT",
                    "127.0.0.1:SEND_STATSD_PORT",
                    "127.0.0.1:SEND_STATSD_PORT",
                    "127.0.0.1:SEND_STATSD_PORT"
                ],
                "tcp_cork": true,
                "validate": true
            }
        }
        "#;
        let mut tf = NamedTempFile::new().unwrap();
        tf.write_all(config.as_bytes()).unwrap();
        let config = load_legacy_config(tf.path().to_str().unwrap()).unwrap();
        assert_eq!(config.statsd.validate, Some(true));
    }
}
