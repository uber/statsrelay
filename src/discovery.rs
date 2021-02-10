use crate::config::{
    Discovery, DiscoverySource, DiscoveryTransform, PathDiscoverySource, S3DiscoverySource,
};

use std::sync::Arc;
use std::time::Duration;
use std::{fs::File, ops::Add};
use std::{io::BufReader, pin::Pin};

use async_stream::stream;
use dashmap::DashMap;
use futures::{stream::Stream, StreamExt};
use log::warn;
use rusoto_s3::S3;
use serde::{Deserialize, Serialize};
use tokio::io::AsyncReadExt;
use tokio::time::Instant;
use tokio_stream::StreamMap;

// Transformer is a set of transformations to apply to a discovery set, for
// example formatting output or repeating elements
trait Transformer {
    fn transform(&self, input: &Update) -> Option<Update>;
}

/// Convert an update into another update based on a format string
fn transform_format(format: &String, input: &Update) -> Option<Update> {
    if !format.contains("{}") {
        return None;
    }
    Some(Update {
        hosts: input
            .hosts
            .iter()
            .map(|input| String::from(format).replace("{}", input))
            .collect(),
    })
}

/// A transformer which repeats each element count times, e.g. a,b count =2 would produce a,a,b,b
fn transform_repeat(count: u32, input: &Update) -> Option<Update> {
    match count {
        0 => None,
        1 => Some(input.clone()),
        n => Some(Update {
            hosts: input
                .hosts
                .iter()
                .map(|input| std::iter::repeat(input.clone()).take(n as usize))
                .flatten()
                .collect(),
        }),
    }
}

impl Transformer for DiscoveryTransform {
    fn transform(&self, input: &Update) -> Option<Update> {
        match self {
            DiscoveryTransform::Format { pattern } => transform_format(pattern, input),
            DiscoveryTransform::Repeat { count } => transform_repeat(*count, input),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Update {
    hosts: Vec<String>,
}

impl Update {
    pub fn sources(&self) -> &Vec<String> {
        &self.hosts
    }
}

impl Default for Update {
    fn default() -> Self {
        Update { hosts: vec![] }
    }
}

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("reading a discovery source had no data")]
    EmptyObjectError,
}

async fn poll_s3_source(config: S3DiscoverySource) -> anyhow::Result<Update> {
    let region = rusoto_core::Region::default();
    let s3 = rusoto_s3::S3Client::new(region);
    let req = rusoto_s3::GetObjectRequest {
        bucket: config.bucket.clone(),
        key: config.key.clone(),
        ..Default::default()
    };
    let resp = s3.get_object(req).await?;
    let mut buffer = Vec::with_capacity(resp.content_length.unwrap_or(0 as i64) as usize);
    let mut update = match resp.body {
        Some(contents) => {
            contents.into_async_read().read_to_end(&mut buffer).await?;
            let update: Update = serde_json::from_slice(buffer.as_ref())?;
            update
        }
        None => {
            warn!("no cluster state located at {:?}", config.key);
            return Err(Error::EmptyObjectError.into());
        }
    };

    for trans in config.transforms.unwrap_or_default().iter() {
        if let Some(new_update) = trans.transform(&update) {
            update = new_update;
        }
    }
    Ok(update)
}

async fn poll_file_source(config: PathDiscoverySource, path: String) -> anyhow::Result<Update> {
    let result = tokio::task::spawn_blocking(move || {
        let file = File::open(path)?;
        let reader = BufReader::new(file);
        let mut update: Update = serde_json::from_reader(reader)?;

        for trans in config.transforms.unwrap_or_default().iter() {
            if let Some(new_update) = trans.transform(&update) {
                update = new_update;
            }
        }
        Ok(update)
    })
    .await?;
    result
}

/// A generic stream which takes a callable async function taking an
/// update (or lack thereof), polling at the defined interval, emitting the
/// output when changed as a stream.
fn polled_stream<T, C>(config: T, interval: u64, callable: C) -> impl Stream<Item = Update>
where
    T: Clone + Send + Sync,
    C: Fn(T) -> Pin<Box<dyn futures::Future<Output = anyhow::Result<Update>> + Send>>,
{
    let mut last_update = Update::default();
    let duration = Duration::from_secs(interval as u64);
    let start = Instant::now().add(duration);
    stream! {

        let mut ticker = tokio::time::interval_at(start, duration);
        loop {
            let new_update = match callable(config.clone()).await {
                Err(e) => {
                    warn!("unable to fetch discovery source due to error {:?}", e);
                    ticker.tick().await;
                    continue;
                },
                Ok(update) => update,
            };
            if new_update != last_update {
                yield new_update.clone();
            }
            last_update = new_update;
            ticker.tick().await;
        }
    }
}

pub fn as_stream(config: &Discovery) -> impl Stream<Item = (String, Update)> {
    let mut streams: StreamMap<String, Pin<Box<dyn Stream<Item = Update> + Send>>> =
        StreamMap::new();

    for (name, source) in config.sources.iter() {
        match source {
            DiscoverySource::S3(source) => {
                let ns = Box::pin(polled_stream(
                    source.clone(),
                    source.interval as u64,
                    move |s| Box::pin(poll_s3_source(s)),
                ));
                //let ns = Box::pin(s3_stream(source.clone()));
                streams.insert(name.clone(), ns);
            }
            DiscoverySource::StaticFile(source) => {
                let cs = source.clone();
                let ns = Box::pin(polled_stream(
                    source.path.clone(),
                    source.interval as u64,
                    move |s| Box::pin(poll_file_source(cs.clone(), s)),
                ));
                //let ns = Box::pin(static_file_stream(source.clone()));
                streams.insert(name.clone(), ns);
            }
        }
    }
    streams
}

#[derive(Clone)]
pub struct Cache {
    cache: Arc<DashMap<String, Update>>,
}

impl Cache {
    pub fn new() -> Self {
        Cache {
            cache: Arc::new(DashMap::new()),
        }
    }

    pub fn store(&self, event: &(String, Update)) {
        self.cache.insert(event.0.clone(), event.1.clone());
    }

    pub fn get(&self, key: &String) -> Option<Update> {
        self.cache.get(key).map(|s| s.clone())
    }
}

pub fn reflector<S>(cache: Cache, stream: S) -> impl Stream<Item = (String, Update)>
where
    S: Stream<Item = (String, Update)>,
{
    stream.inspect(move |event| cache.store(event))
}

#[cfg(test)]
pub mod tests {
    use crate::config::DiscoveryTransform;

    use super::{Transformer, Update};

    #[test]
    fn format() {
        let o1 = Update {
            hosts: vec!["a", "b"].iter().map(|s| (*s).into()).collect(),
        };
        let transformer = DiscoveryTransform::Format {
            pattern: "{}hello".into(),
        };
        let f = transformer.transform(&o1).unwrap();
        assert_eq!(f.hosts[0], "ahello");
        assert_eq!(f.hosts[1], "bhello");

        let bad_transformer = DiscoveryTransform::Format {
            pattern: "foo".into(),
        };

        assert!(bad_transformer.transform(&o1).is_none());
    }

    #[test]
    fn repeat() {
        let o1 = Update {
            hosts: vec!["a", "b"].iter().map(|s| (*s).into()).collect(),
        };
        let transformer = DiscoveryTransform::Repeat { count: 4 };
        let f = transformer.transform(&o1).unwrap();
        assert_eq!(f.hosts, vec!["a", "a", "a", "a", "b", "b", "b", "b"]);

        let bad_transformer = DiscoveryTransform::Repeat { count: 0 };

        assert!(bad_transformer.transform(&o1).is_none());
    }
}
