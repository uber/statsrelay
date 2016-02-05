# statsrelay
A consistent-hashing relay for statsd and carbon metrics

[![Build Status](https://travis-ci.org/lyft/statsrelay.svg?branch=master)](https://travis-ci.org/lyft/statsrelay)


# License
MIT License
Copyright (c) 2014 Uber Technologies, Inc.

# Whats different in this version

This version differs from upstream in several key ways:

- The YAML configuration was removed in favor of JSON
- Relaying can now be done to one more more destinations, with:
  - Prefix and suffixing of any metric names
  - Ingress filtering based on a regular expression
- Several key bug fixes designed to improve operationability


# Build

Dependencies:
- automake
- pkg-config
- libev (>= 4.11)
- libjansson
- libpcre

```
apt-get install automake pkg-config libev-dev libjansson-dev libpcre3-dev

autoreconf --install
./configure
make clean
make
make check
make install
```

# Use

```
Usage: statsrelay [options]
  -h, --help                   Display this message
  -v, --verbose                Write log messages to stderr in addition to syslog
                               syslog
  -l, --log-level              Set the logging level to DEBUG, INFO, WARN, or ERROR
                               (default: INFO)
  -c, --config=filename        Use the given hashring config file
                               (default: /etc/statsrelay.json)
  -t, --check-config=filename  Check the config syntax
                               (default: /etc/statsrelay.json)
  --version                    Print the version
```

```
statsrelay --config=/path/to/statsrelay.json
```

This process will run in the foreground. If you need to daemonize, use
start-stop-script, daemontools, supervisord, upstart, systemd, or your
preferred service watchdog.

By default statsrelay binds to 127.0.0.1:8125 for statsd proxying, and
it binds to 127.0.0.1:2003 for carbon proxying.

For each line that statsrelay receives in the statsd format
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

Upon SIGHUP, the config file will be reloaded and all backend
connections closed. Note that any stats in the send queue at the time
of SIGHUP will be dropped. ** THIS IS CURRENTLY BROKEN **

If SIGINT or SIGTERM are caught, all connections are killed, send
queues are dropped, and memory freed. statsrelay exits with return
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
backend:127.0.0.2:8127:tcp bytes_queued gauge 27
backend:127.0.0.2:8127:tcp bytes_sent gauge 27
backend:127.0.0.2:8127:tcp relayed_lines gauge 3
backend:127.0.0.2:8127:tcp dropped_lines gauge 0

```

# Scaling With Virtual Shards

Statsrelay implements a virtual sharding scheme, which allows you to
easily scale your statsd and carbon backends by reassigning virtual
shards to actual statsd/carbon instance or servers. This technique
also applies to alternative statsd implementations like statsite.

Consider the following simplified example with this config file:

```json
{"carbon": {
    "bind": "127.0.0.1:2004",
    "shard_map": ["127.0.0.1:2000",
                  "127.0.0.1:2001",
                  "127.0.0.1:2002",
                  "127.0.0.1:2003"]
}
}
```

In this file we've defined two actual backend hosts (10.0.0.1 and
10.0.0.2). Each of these hosts is running two statsd instances, one on
port 9000 and one on port 9001 (this is a good way to scale statsd,
since statsd and alternative implementations like statsite are
typically single threaded). In a real setup, you'd likely be running
more statsd instances on each server, and you'd likely have more
repeated lines to assign more virtual shards to each statsd
instance. 

Internally statsrelay assigns a zero-indexed virtual shard to each
line in the file; so 10.0.0.1:9000 has virtual shards 0 and 1,
10.0.0.1:9001 has virtual shards 2 and 3, and so on.

To do optimal shard assignment, you'll want to write a program that
looks at the CPU usage of your shards and figures out the optimal
distribution of shards. How you do that is up to you, but a good
technique is to start by generating a statsrelay config that has many
virtual shards evenly assigned, and then periodically have a script
that finds which actual backends are overloaded and reassigns some of
the virtual shards on those hosts to less loaded hosts (or to new
hosts).

If you don't initially assign enough virtual shards and then later
expand to more, everything will work, but data migration for carbon
will be a bit trickier; see below.

## A Note On Carbon Scaling

Statsrelay can do relaying for carbon lines just like statsd. The
strategy for scaling carbon using virtual shards is exactly the
same. One important difference, however, is that when you move a
carbon shard you'll want to move the associated whisper files as
well. You can do this using the `stathasher` binary that is built by
statsrelay. By pointing that command at your statsrelay config, you
can send it key names on stdin and have the virtual shard ids printed
to stdout.

Using this technique you can script the reassignment of whisper
files. The general idea is to walk the filesystem and gather all of
the unique keys stored in carbon backends on a host. You can then get
an idea for how expensive each virtual shard is based on the storage
space, number of whisper files, and possibly I/O metrics for each
virtual shard. By gathering the weights for each virtual shard on a
host, you can figure out the optimal way to redistribute the mapping
of virtual shards to actual carbon backends.

Note that when you move carbon instances, you also probably want to
migrate the whisper files as well. This ensures that you retain
historical data, and that graphite will get the right answer if it
queries multiple carbon backends. You can migrate the whisper files by
rsyncing the files you've identified as belonging to a moved virtual
shard using the `stathasher` binary described above. Remember to take
care to ensure that the old whisper files are deleted on the old host.
