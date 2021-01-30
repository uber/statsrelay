use crate::config::{Discovery, DiscoverySource, S3DiscoverySource};

use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;

use async_stream::stream;
use dashmap::DashMap;
use futures::{stream::Stream, StreamExt};
use log::warn;
use rusoto_s3::S3;
use serde::{Deserialize, Serialize};
use tokio::io::AsyncReadExt;
use tokio_stream::StreamMap;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Update {
    sources: Vec<String>,
}

impl Update {
    pub fn sources(&self) -> &Vec<String> {
        &self.sources
    }
}

impl Default for Update {
    fn default() -> Self {
        Update { sources: vec![] }
    }
}

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("reading a discovery source had no data")]
    EmptyObjectError,
}

async fn poll_s3_source(config: &S3DiscoverySource) -> anyhow::Result<Update> {
    let region = rusoto_core::Region::default();
    let s3 = rusoto_s3::S3Client::new(region);
    let req = rusoto_s3::GetObjectRequest {
        bucket: config.bucket.clone(),
        key: config.key.clone(),
        ..Default::default()
    };
    let resp = s3.get_object(req).await?;
    let mut buffer = Vec::with_capacity(resp.content_length.unwrap_or(0 as i64) as usize);
    match resp.body {
        Some(contents) => {
            contents.into_async_read().read_to_end(&mut buffer).await?;
            let update: Update = serde_json::from_slice(buffer.as_ref())?;
            return Ok(update);
        }
        None => {
            warn!("no cluster state located at {:?}", config.key);
            return Err(Error::EmptyObjectError.into());
        }
    };
}

fn s3_stream(config: S3DiscoverySource) -> impl Stream<Item = Update> {
    stream! {
        let mut last_update = Update::default();
        loop {
            let new_update = match poll_s3_source(&config).await {
                Err(e) => {
                    warn!("unable to fetch discovery source due to error {:?}", e);
                    continue;
                },
                Ok(update) => update,
            };
            if new_update != last_update {
                yield new_update.clone();
            }
            last_update = new_update;
            tokio::time::sleep(Duration::from_secs(config.interval as u64)).await;
        }
    }
}

pub fn as_stream(config: &Discovery) -> impl Stream<Item = (String, Update)> {
    let mut streams: StreamMap<String, Pin<Box<dyn Stream<Item = Update> + Send>>> = StreamMap::new();

    for (name, source) in config.sources.iter() {
        match source {
            DiscoverySource::S3(source) => {
                let ns = Box::pin(s3_stream(source.clone()));
                streams.insert(name.clone(), ns);
            }
            _ => (),
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
}

pub fn reflector<S>(cache: Cache, stream: S) -> impl Stream<Item = (String, Update)>
where
    S: Stream<Item = (String, Update)>,
{
    stream.inspect(move |event| cache.store(event))
}
