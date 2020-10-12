# tugboat (formerly statsrelay) (3.0)
A veritable toolkit, sidecar, or daemon(set) for sharding, aggregating, relaying
and working with statsd and Prometheus based metrics sources, at scale.

[![Build Status](https://travis-ci.org/lyft/statsrelay.svg?branch=master)](https://travis-ci.org/lyft/tugboat)


## License
MIT License
Copyright (c) 2015-2020 Lyft Inc.

Originally based on statsrelay:
Copyright (c) 2014 Uber Technologies, Inc.

## Whats different in this version



## Build

Dependencies:
- Rust (stable, 1.44+)

## Use

```
Usage: tugboat [options]
  -h, --help                   Display this message
  -v, --verbose                Write log messages to stderr in addition to syslog
                               syslog
  -l, --log-level              Set the logging level to DEBUG, INFO, WARN, or ERROR
                               (default: INFO)
  -p  --pid                    Path to the pid file

  -c, --config=filename        Use the given hashring config file
                               (default: /etc/tugboat.json)
  -t, --check-config=filename  Check the config syntax
                               (default: /etc/tugboat.json)
  --version                    Print the version
```

```
tugboat --config=/path/to/tugboat.json
```

This process will run in the foreground. If you need to daemonize, use
start-stop-script, daemontools, supervisord, upstart, systemd, or your
preferred service watchdog.

By default tugboat binds to 127.0.0.1:8125 for statsd proxying.

For each line that tugboat receives in the statsd format
"statname.foo.bar:1|c\n", the key will be hashed to determine which
backend server the stat will be relayed to. If no connection to that
backend is open, the line is queued and a connection attempt is
started. Once a connection is established, all queued metrics are
relayed to the backend and the queue is emptied. If the backend
connection fails, the queue persists in memory and the connection will
be retried after one second. Any stats received for that backend during
the retry window are added to the queue.

Each backend has its own send queue. If a send queue reaches
`max-send-queue` bytes (default: 128MB) in size, new incoming stats
are dropped until a backend connection is successful and the queue
begins to drain.

All log messages are sent to syslog with the INFO priority.

If SIGINT or SIGTERM are caught, all connections are killed, send
queues are dropped, and memory freed. tugboat exits with return
code 0 if all went well.

To retrieve server statistics, connect to TCP port 8125 and send the
string "status" followed by a newline '\n' character. The end of the
status output is denoted by two consecutive newlines "\n\n"

stats example:
```
$ echo status | nc localhost 8125

global bytes_recv_udp gauge 0
global bytes_recv_tcp gauge 41
global total_connections gauge 1
global last_reload timestamp 0
global malformed_lines gauge 0
global invalid_lines gauge 0
group:0 rejected_lines gauge 3
backend:127.0.0.2:8127:tcp bytes_queued gauge 27
backend:127.0.0.2:8127:tcp bytes_sent gauge 27
backend:127.0.0.2:8127:tcp relayed_lines gauge 3
backend:127.0.0.2:8127:tcp dropped_lines gauge 0
```

## Scaling With Virtual Shards

Statsrelay implements a virtual sharding scheme, which allows you to
easily scale your statsd backends by reassigning virtual
shards to actual statsd instance or servers. This technique
also applies to alternative statsd implementations like statsite.

Consider the following simplified example with this config file:

```json
{"statsd": {
    "bind": "127.0.0.1:8126",
    "shard_map": ["10.0.0.1:8128",
                  "10.0.0.1:8128",
                  "10.0.0.2:8128",
                  "10.0.0.2:8128"]
}
}
```

In this file we've defined two actual backend hosts (10.0.0.1 and
10.0.0.2). Each of these hosts is running two statsd instances, on
port 8128 (this is a good way to scale statsd, since statsd and
alternative implementations like statsite are typically single
threaded). In a real setup, you'd likely be running more statsd
instances on each server, and you'd likely have more repeated
lines to assign more virtual shards to each statsd instance. 

Internally tugboat assigns a zero-indexed virtual shard to each
line in the file; so 10.0.0.1:8128 has virtual shards 0 and 1,
10.0.0.2:8128 has virtual shards 2 and 3, and so on.

To do optimal shard assignment, you'll want to write a program that
looks at the CPU usage of your shards and figures out the optimal
distribution of shards. How you do that is up to you, but a good
technique is to start by generating a tugboat config that has many
virtual shards evenly assigned, and then periodically have a script
that finds which actual backends are overloaded and reassigns some of
the virtual shards on those hosts to less loaded hosts (or to new
hosts).

If you don't initially assign enough virtual shards and then later
expand to more, everything will work.
