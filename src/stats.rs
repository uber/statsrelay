use std::sync::Arc;

use dashmap::DashMap;
use prometheus::{Encoder, Registry, TextEncoder};

pub const SEP: &str = ":";
/// A wrapped stats implementation, to allow multiple backends to be used
/// instead of just prometheus, when required. Right now this implementation is
/// extremely simple and only works with prometheus exporting, and will require
/// some revisions to improve.
///
/// All types are clone-able - for the Collector and all built metric types,
/// they will continue to refer to the same set of names and values and do not
/// create new values. Scopes can be cloned, but do not share lineage and allow
/// sub-scopes to be made independently. Building a reference to the same
/// counter name will return the same underlying counter atomic.

#[derive(Clone, Debug)]
pub struct Collector {
    // Registry is an Arc<> locked type and thereform is freely cloneable
    registry: Registry,
    counters: Arc<DashMap<String, Counter>>,
}

impl Default for Collector {
    fn default() -> Self {
        Collector {
            registry: Registry::new(),
            counters: Arc::new(DashMap::new()),
        }
    }
}

impl Collector {
    pub fn scope(&self, prefix: &str) -> Scope {
        Scope {
            collector: self.clone(),
            scope: String::from(prefix),
        }
    }

    /// Generate and return a byte buffer containing a Prometheus formatted text
    /// output of the current contents of this collector.
    pub fn prometheus_output(&self) -> anyhow::Result<Vec<u8>> {
        let output = self.registry.gather();
        let encoder = TextEncoder::new();
        let mut buffer = vec![];

        encoder.encode(&output, &mut buffer)?;
        Ok(buffer)
    }

    /// Attempt to register a new counter. If the counter already exists, it
    /// will return the previously registered counter instead of the one passed
    /// in.
    fn register_counter(&self, c: Counter) -> anyhow::Result<Counter> {
        let counter = match self.counters.get(&c.name) {
            Some(counter) => counter.clone(),
            None => {
                self.registry.register(Box::new(c.clone().counter))?;
                self.counters.insert(c.name.clone(), c.clone());
                c
            }
        };

        Ok(counter)
    }
}

#[derive(Clone, Debug)]
pub struct Scope {
    collector: Collector,
    scope: String,
}

impl Scope {
    pub fn scope(&self, extend: &str) -> Scope {
        Scope {
            scope: format!("{}{}{}", self.scope, SEP, extend),
            collector: self.collector.clone(),
        }
    }

    /// Create a new counter with the given scope, or return an existing
    /// underlying counter
    pub fn counter(&self, name: &str) -> anyhow::Result<Counter> {
        let name = format!("{}{}{}", self.scope, SEP, name);
        let counter = Counter::new(name)?;
        Ok(self.collector.register_counter(counter.clone())?)
    }
}

#[derive(Clone, Debug)]
pub struct Counter {
    name: String,
    counter: prometheus::Counter,
}

impl Counter {
    fn new(name: String) -> anyhow::Result<Self> {
        let pcounter = prometheus::Counter::new(name.clone(), "a counter")?;
        Ok(Self {
            name: name,
            counter: pcounter,
        })
    }

    /// Increment a counter
    pub fn inc(&self) {
        self.counter.inc();
    }

    pub fn inc_by(&self, value: f64) {
        self.counter.inc_by(value);
    }

    /// Return the current counter value
    pub fn get(&self) -> f64 {
        self.counter.get()
    }
}

#[cfg(test)]
pub mod test {
    use super::*;

    #[test]
    pub fn test_counter() {
        let collector = Collector::default();
        let scope = collector.scope("prefix");
        let ctr1 = scope.counter("counter").unwrap();
        ctr1.inc();
        let ctr2 = scope.counter("counter").unwrap();
        // Ensure we have the same counter object
        assert_eq!(ctr2.get(), 1 as f64);
        ctr2.inc();
        assert_eq!(ctr1.get(), 2 as f64);
    }
}
