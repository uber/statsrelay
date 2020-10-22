use std::sync::atomic::AtomicU64;
use std::sync::Arc;

use parking_lot::RwLock;
use regex::bytes::RegexSet;

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
    fn new(conf: &StatsdDuplicateTo) -> anyhow::Result<Self> {
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

        let mut backend = StatsdBackend {
            conf: conf.clone(),
            ring: Ring::new(),
            input_filter: input_filter,
            warning_log: AtomicU64::new(0),
        };

        for endpoint in &conf.shard_map {
            backend
                .ring
                .push(StatsdClient::new(endpoint.as_str(), 100000));
        }

        Ok(backend)
    }

    fn provide_statsd_pdu(&self, pdu: &StatsdPDU) {
        if !self
            .input_filter
            .as_ref()
            .map_or(true, |inf| inf.is_match(pdu.name()))
        {
            return;
        }
        // All the other logic

        let code = match self.ring.len() {
            1 => 1 as u32,
            _ => statsrelay_compat_hash(pdu),
        };
        let client = self.ring.pick_from(code);
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

struct BackendsInner {
    statsd: Vec<StatsdBackend>,
}

impl BackendsInner {
    fn new() -> Self {
        BackendsInner { statsd: vec![] }
    }

    fn add_statsd_backend(&mut self, c: &StatsdDuplicateTo) {
        self.statsd.push(StatsdBackend::new(c).unwrap());
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

    pub fn add_statsd_backend(&self, c: &StatsdDuplicateTo) {
        self.inner.write().add_statsd_backend(c);
    }

    pub fn provide_statsd_pdu(&self, pdu: StatsdPDU) {
        self.inner.read().provide_statsd_pdu(pdu)
    }
}
