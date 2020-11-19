use std::collections::HashMap;
use std::sync::atomic::AtomicU64;
use std::sync::Arc;

use parking_lot::RwLock;
use regex::bytes::RegexSet;
use thiserror::Error;

use crate::config::StatsdDuplicateTo;
use crate::shard::{statsrelay_compat_hash, Ring};
use crate::statsd::StatsdPDU;
use crate::statsd_client::StatsdClient;

use log::warn;

struct StatsdBackend {
    conf: StatsdDuplicateTo,
    ring: Ring<StatsdClient>,
    input_filter: Option<RegexSet>,
    warning_log: AtomicU64,
}

impl StatsdBackend {
    fn new(conf: &StatsdDuplicateTo, client_ref: Option<&StatsdBackend>) -> anyhow::Result<Self> {
        let mut filters: Vec<String> = Vec::new();

        // This is ugly, sorry
        if conf.input_blacklist.is_some() {
            filters.push(conf.input_blacklist.as_ref().unwrap().clone());
        }
        if conf.input_filter.is_some() {
            filters.push(conf.input_filter.as_ref().unwrap().clone());
        }
        let input_filter = if filters.len() > 0 {
            Some(RegexSet::new(filters).unwrap())
        } else {
            None
        };

        let mut ring: Ring<StatsdClient> = Ring::new();

        // Use the same backend for the same endpoint address, caching the lookup locally
        let mut memoize: HashMap<String, StatsdClient> =
            client_ref.map_or_else(|| HashMap::new(), |b| b.clients());
        for endpoint in &conf.shard_map {
            if let Some(client) = memoize.get(endpoint) {
                ring.push(client.clone())
            } else {
                let client = StatsdClient::new(endpoint.as_str(), 100000);
                memoize.insert(endpoint.clone(), client.clone());
                ring.push(client);
            }
        }

        let backend = StatsdBackend {
            conf: conf.clone(),
            ring: ring,
            input_filter: input_filter,
            warning_log: AtomicU64::new(0),
        };

        Ok(backend)
    }

    // Capture the old ring contents into a memoization map by endpoint,
    // letting us re-use any old client connections and buffers. Note we
    // won't start tearing down connections until the memoization buffer and
    // old ring are both dropped.
    fn clients(&self) -> HashMap<String, StatsdClient> {
        let mut memoize: HashMap<String, StatsdClient> = HashMap::new();
        for i in 0..self.ring.len() {
            let client = self.ring.pick_from(i as u32);
            memoize.insert(String::from(client.endpoint()), client.clone());
        }
        memoize
    }

    fn provide_statsd_pdu(&self, pdu: &StatsdPDU) {
        if !self
            .input_filter
            .as_ref()
            .map_or(true, |inf| inf.is_match(pdu.name()))
        {
            return;
        }

        let ring_read = &self.ring;
        let code = match ring_read.len() {
            0 => return, // In case of nothing to send, do nothing
            1 => 1 as u32,
            _ => statsrelay_compat_hash(pdu),
        };
        let client = ring_read.pick_from(code);
        let mut sender = client.sender();

        // Assign prefix and/or suffix
        let pdu_clone: StatsdPDU;
        if self.conf.prefix.is_some() || self.conf.suffix.is_some() {
            pdu_clone = pdu.with_prefix_suffix(
                self.conf
                    .prefix
                    .as_ref()
                    .map(|p| p.as_bytes())
                    .unwrap_or_default(),
                self.conf
                    .suffix
                    .as_ref()
                    .map(|s| s.as_bytes())
                    .unwrap_or_default(),
            );
        } else {
            pdu_clone = pdu.clone();
        }
        match sender.try_send(pdu_clone) {
            Err(_e) => {
                let count = self
                    .warning_log
                    .fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                if count % 1000 == 0 {
                    warn!(
                        "error pushing to queue full (endpoint {}, total failures {})",
                        client.endpoint(),
                        count
                    );
                }
            }
            Ok(_) => (),
        }
    }
}

#[derive(Error, Debug)]
pub enum BackendError {
    #[error("Index not valid for backend {0}")]
    InvalidIndex(usize),
}

struct BackendsInner {
    statsd: Vec<StatsdBackend>,
}

impl BackendsInner {
    fn new() -> Self {
        BackendsInner { statsd: vec![] }
    }

    fn add_statsd_backend(&mut self, c: &StatsdDuplicateTo) -> anyhow::Result<()> {
        self.statsd.push(StatsdBackend::new(c, None)?);
        Ok(())
    }

    fn replace_statsd_backend(&mut self, idx: usize, c: &StatsdDuplicateTo) -> anyhow::Result<()> {
        let backend = match self.statsd.get(idx) {
            None => return self.add_statsd_backend(c),
            Some(backend) => backend,
        };
        self.statsd[idx] = StatsdBackend::new(c, Some(backend))?;
        Ok(())
    }

    fn len(&self) -> usize {
        self.statsd.len()
    }

    fn remove_statsd_backend(&mut self, idx: usize) -> anyhow::Result<()> {
        if self.statsd.len() >= idx {
            return Err(anyhow::Error::new(BackendError::InvalidIndex(idx)));
        }
        self.statsd.remove(idx);
        Ok(())
    }

    fn provide_statsd_pdu(&self, pdu: StatsdPDU) {
        let _result: Vec<_> = self
            .statsd
            .iter()
            .map(|backend| backend.provide_statsd_pdu(&pdu))
            .collect();
    }
}

///
/// Backends provides a cloneable contaner for various protocol backends,
/// handling logic like sharding, sampling, and other detectors.
///
#[derive(Clone)]
pub struct Backends {
    inner: Arc<RwLock<BackendsInner>>,
}

impl Backends {
    pub fn new() -> Self {
        Backends {
            inner: Arc::new(RwLock::new(BackendsInner::new())),
        }
    }

    pub fn add_statsd_backend(&self, c: &StatsdDuplicateTo) -> anyhow::Result<()> {
        self.inner.write().add_statsd_backend(c)
    }

    pub fn replace_statsd_backend(&self, idx: usize, c: &StatsdDuplicateTo) -> anyhow::Result<()> {
        self.inner.write().replace_statsd_backend(idx, c)
    }

    pub fn remove_statsd_backend(&self, idx: usize) -> anyhow::Result<()> {
        self.inner.write().remove_statsd_backend(idx)
    }

    pub fn len(&self) -> usize {
        self.inner.read().len()
    }

    pub fn provide_statsd_pdu(&self, pdu: StatsdPDU) {
        self.inner.read().provide_statsd_pdu(pdu)
    }
}
