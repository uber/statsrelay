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
- Rust (stable, 1.46+)

## Use

```
statsrelay 3.1.0

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
structure. The original statsrelay configuration contract has been broken as of
version 3.1 in order to fix a number of features.

#### Basic structure

```json
{
    "statsd": {
        "bind": "127.0.0.1:8129",
        "validate": true,
        "backends": {
          "b1": {
            "shard_map": ["127.0.0.1:1234"],
            "prefix": "myapp.",
            "suffix": ".suffix"
          }
        }
    },
    "discovery": {
      "sources": {
        "source1": {
          "type": "s3",
          "bucket": "my-bucket",
          "key": "file.json",
          "interval": 10
        }
      }
    }
}
```

Statsd inputs and routing is defined in the outer `statsd` block.

- `bind`: sets the server bind address to accept statsd protocol messages.
  Statsrelay will bind on both UDP and TCP ports.
- `validate`: turns on extended, more expensive validation of statsd line
  protocol messages, such as parsing of numerical fields, which may not be
  required for a pure relaying case.
- `backends` forks the incoming statsd metrics down a number of parallel
  processing pipelines. By default, all incoming protocol lines from the statsd
  server are sent to all backends.

#### `backends` options

Each backend is named and can accept a number of options and rewrite steps for
sending and processing StatsD messages.

- `shard_map`: list of socket addresses that defines where to send statsd output
  to, from a list of servers. The same server can be specified more than once
  (allowing for virtual sharding). Output statsd lines are consistently hashed,
  and sent to the corresponding server based on a standard hash ring, in a
  compatible format to the original statsrelay code (Murmur3 hash). This list
  can be empty to not relay statsd messages.
- `shard_map_source`: string value which defines a discovery source to use
  in-lieu of `shard_map`.
- `prefix`: prepend this prefix string in front of every metric/statsd line before
  forwarding it to the `shard_map` servers. Useful for tagging metrics coming
  from a sidecar.
- `suffix`: append a suffix. Works like prefix, just at the end.

#### `discovery` options

Each key in the discovery sources section defines a source which can be used by
most backends to locate servers, sharding, or other network resources to
communicate with. Each named discovery source is listed in the `sources` subkey:

```json
{
  "sources": {
    "source_name_1": {
      "type": "static_file"
    },
    "source_name_2": {
      "type": "s3"
    }
  }
}
```

For sources supporting a file input (s3, static_file), the following schema is
assumed:

```json
{
  "hosts": ["host:port", "host:port"]
}
```

Some sources may support rewriting to transform the input string into an output
string (e.g., to add a port)

##### s3 source

An S3 source represents an AWS S3 compatible source. Statsrelay uses `rusoto_s3`
to access S3 and supports the vast majority of metadata sources, configuration,
and environment variables in order to locate credentials.

The following keys are supported for the S3 source:

- `bucket` - The S3 bucket where the file lives
- `key` - The key/path instead the S3 bucket
- `interval` - An integer number of seconds to wait before re-polling the
  contents of the S3 key to detect changes.
- `format` - A simple text subsitution to run on the incoming text, where `{}` is
  replaced by the value of each host entry. Valuable to append information, such
  as a port number by specifying `"format": "{}:8125"`
