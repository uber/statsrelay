# statsrelay (3.0)
A veritable toolkit, sidecar, or daemon(set) for sharding, aggregating, relaying
and working with statsd and Prometheus based metrics sources, at scale.

## License
MIT License
Copyright (c) 2015-2020 Lyft Inc.

Originally based on statsrelay:
Copyright (c) 2014 Uber Technologies, Inc.

## Whats different in this version

Statsrelay 3.0 is a port of the original C statsrelay to Rust, with a number of
new features designed to improve the operatability and scalability of the
original daemon, moving beyond pure "relaying" and instead focusing on both
sharding, as well as cascading aggregation. The original C daemon in this fork
featured sampling support, but was limited by having the output format be
statsd.

## Build

Dependencies:
- Rust (stable, 1.44+)

## Use

```
statsrelay 3.0.0

USAGE:
    statsrelay [OPTIONS]

FLAGS:
    -h, --help       Prints help information
    -V, --version    Prints version information

OPTIONS:
    -c, --config <config>     [default: /etc/statsrelay.json]
```

Statsrelay logging is handled by the env_logger crate, which inherits a number of
logging options from the environment. Consult the [crate
documentation](https://docs.rs/env_logger/0.8.1/env_logger/#enabling-logging)
for more information on options you can set.

### Protocols

Statsrelay understands:

- statsd text line protocol
  - with sampling support (`@sampling`)
  - with extended data types (map, kv, sets, etc)
  - with "DogStatsD" extended tags (`|#tags`)
  - with Lyft internal tags (`metric.__tag=value`)

### Configuration file

The configuration file is a JSON file originating from the original statsrelay
structure. Currently, any valid Lyft statsrelay configuration file is accepted
as a statsrelay file.

#### Basic structure

```
{
    "statsd": {
        "bind": "127.0.0.1:8129",
        "shard_map": [
            "127.0.0.1:8122"
        ],
        "validate": true,
        "prefix": "myapp.",
        "suffix": ".suffix",
        duplicate_to: []
    }
}
```

Statsd inputs and routing is defined in the outer `statsd` block.

- `bind`: sets the server bind address to accept statsd protocol messages.
  Statsrelay will bind on both UDP and TCP ports.
- `shard_map`: defines where to send statsd output to, from a list of servers.
  The same server can be specified more than once (allowing for virtual
  sharding). Output statsd lines are consistently hashed, and sent to the
  corresponding server based on a standard hash ring, in a compatible format to
  the original statsrelay code (Murmur3 hash). This list can be empty to not
  relay statsd messages.
- `validate`: turns on extended, more expensive validation of statsd line
  protocol messages, such as parsing of numerical fields, which may not be
  required for a pure relaying case.
- `prefix`: prepend this prefix string in front of every metric/statsd line before
  forwarding it to the `shard_map` servers. Useful for tagging metrics coming
  from a sidecar.
- `suffix`: append a suffix. Works like prefix, just at the end.
- `duplicate_to` forks the incoming statsd metrics down a number of parallel
  processing pipelines.

#### `duplicate_to` options and sampling



