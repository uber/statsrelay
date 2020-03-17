#!/usr/bin/env python

import contextlib
import os.path
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest

from collections import defaultdict


SOCKET_TIMEOUT = 1

# While debugging, set this to False
QUIET = True
if QUIET:
    DEVNULL = open('/dev/null', 'wb')
    POPEN_KW = {'stdout': DEVNULL, 'stderr': DEVNULL}
else:
    POPEN_KW = {}


class TestCase(unittest.TestCase):

    def setUp(self):
        super(TestCase, self).setUp()
        self.tcp_cork = 'false'
        self.proc = None

    def tearDown(self):
        super(TestCase, self).tearDown()
        if self.proc:
            try:
                self.proc.kill()
            except OSError:
                pass

    def launch_process(self, config_path):
        args = ['./statsrelay', '--verbose', '--log-level=DEBUG']
        args.append('--config=' + config_path)
        self.proc = subprocess.Popen(args, **POPEN_KW)
        time.sleep(0.5)

    def reload_process(self, proc):
        proc.send_signal(signal.SIGHUP)
        time.sleep(0.1)

    def check_recv(self, fd, expected, size=1024):
        bytes_read = fd.recv(size)
        self.assertEqual(bytes_read, expected)

    def check_recv_in(self, fd, subset, size=512):
        bytes_read = fd.recv(size)
        self.assertIn(subset, bytes_read)

    def check_list_in_recv(self, fd, subset=list(), size=512):
        bytes_read = fd.recv(size)
        for line in subset:
            self.assertIn(line, bytes_read)

    def check_list_not_in_recv(self, fd, subset=list(), size=512):
        bytes_read = fd.recv(size)
        for line in subset:
            self.assertNotIn(line, bytes_read)

    def check_list_in_out_recv(self, fd, in_list=list(), out_list=list(), size=512):
        bytes_read = fd.recv(size)
        for line in in_list:
            self.assertIn(line, bytes_read)

        for line in out_list:
            self.assertNotIn(line, bytes_read)

    def recv_status(self, fd):
        return fd.recv(65536)

    @contextlib.contextmanager
    def generate_config(self, mode, filename, file_ext='.json'):
        config_path = 'tests/{0}{1}'.format(filename, file_ext)
        if not os.path.isfile(config_path):
            raise ValueError("file %s not found" % config_path)

        try:
            if mode.lower() == 'tcp':
                sock_type = socket.SOCK_STREAM
            elif mode.lower() == 'udp':
                sock_type = socket.SOCK_DGRAM
            else:
                raise ValueError()
            self.bind_statsd_port = self.choose_port(sock_type)
            self.statsd_listener = socket.socket(socket.AF_INET, sock_type)
            self.statsd_listener.bind(('127.0.0.1', 0))
            self.statsd_listener.settimeout(SOCKET_TIMEOUT)
            self.statsd_port = self.statsd_listener.getsockname()[1]

            if mode.lower() == 'tcp':
                self.statsd_listener.listen(1)

            new_config = tempfile.NamedTemporaryFile(suffix="{}{}".format(filename, file_ext))
            with open(config_path) as config_file:
                data = config_file.read()
            for var, replacement in [
                    ('BIND_STATSD_PORT', self.bind_statsd_port),
                    ('SEND_STATSD_PORT', self.statsd_port),
                    ('TCP_CORK', self.tcp_cork)]:
                data = data.replace(var, str(replacement))
            new_config.write(data)
            new_config.flush()
            yield new_config.name
        finally:
            self.statsd_listener.close()

    def connect(self, sock_type, port):
        if sock_type.lower() == 'tcp':
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        else:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.connect(('127.0.0.1', port))
        sock.settimeout(SOCKET_TIMEOUT)
        return sock

    def choose_port(self, sock_type):
        s = socket.socket(socket.AF_INET, sock_type)
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
        s.close()
        return port


class ConfigTestCase(TestCase):
    """Test config option parsing."""

    def test_invalid_config_file(self):
        """Test that directories are correctly ignored as config files."""
        self.launch_process('.')
        self.proc.wait()
        self.assertEqual(self.proc.returncode, 1)

        self.launch_process('/etc/passwd')
        self.proc.wait()
        self.assertEqual(self.proc.returncode, 1)

    def test_check_invalid_config_file(self):
        proc = subprocess.Popen(
            ['./statsrelay', '-t', '/etc/passwd'], **POPEN_KW)
        proc.wait()
        self.assertEqual(proc.returncode, 1)

    def test_valid_config_file(self):
        proc = subprocess.Popen(['./statsrelay', '-t', 'tests/statsrelay-no-shardmap.json'])
        proc.wait()
        self.assertEqual(proc.returncode, 0)

    def test_check_valid_tcp_file(self):
        with self.generate_config('tcp', filename='statsrelay') as config_path:
            proc = subprocess.Popen(['./statsrelay', '-t', config_path])
            proc.wait()
            self.assertEqual(proc.returncode, 0)

    def test_check_valid_udp_file(self):
        with self.generate_config('udp', filename='statsrelay_udp_with_validation') as config_path:
            proc = subprocess.Popen(['./statsrelay', '-t', config_path])
            proc.wait()
            self.assertEqual(proc.returncode, 0)

    def test_check_empty_file(self):
        proc = subprocess.Popen(['./statsrelay', '-c', 'tests/empty.json'])
        proc.wait()
        self.assertEqual(proc.returncode, 1)


class StatsdTestCase(TestCase):

    def test_tcp_with_cardinality_checks(self):
        with self.generate_config('tcp', filename="statsrelay-cardinality") as config_path:
            MAX_UNIQUE_METRICS = 5
            self.launch_process(config_path)
            fd, addr = self.statsd_listener.accept()
            sender = self.connect('tcp', self.bind_statsd_port)

            # test counter
            for i in range(0, 10):
                sender.sendall('test.{}:1|c\n'.format(i + 1))
                sender.sendall('test.{}:1|ms\n'.format(i + 1))
                sender.sendall('test.{}:1|g\n'.format(i + 1))

            expected = ['test-1.test.{}.suffix:1|g\n'.format(i + 1) for i in range(0, 5)]
            expected += ['test-1.test.{}.suffix:1|ms\n'.format(i + 1) for i in range(0, 5)]
            expected += ['test-1.test.{}.suffix:1|c\n'.format(i + 1) for i in range(0, 5)]

            unexpected = ['test-1.test.{}.suffix:1|g\n'.format(i + 1) for i in range(5, 10)]
            unexpected += ['test-1.test.{}.suffix:1|ms\n'.format(i + 1) for i in range(5, 10)]
            unexpected += ['test-1.test.{}.suffix:1|c\n'.format(i + 1) for i in range(5, 10)]

            self.check_list_in_out_recv(fd, expected, unexpected, 1024)

            sender.sendall('status\n')
            status = sender.recv(65536)
            sender.close()

            backends = defaultdict(dict)
            groups = defaultdict(dict)
            for line in status.split('\n'):
                if not line:
                    break
                if line.startswith('backend:'):
                    backend, key, valuetype, value = line.split(' ', 3)
                    backend = backend.split(':', 1)[1]
                    backends[backend][key] = int(value)
                elif line.startswith('group:'):
                    group, key, _, value = line.split(' ', 3)
                    group_idx = group.split(':', 1)[1]
                    groups[key][group_idx] = int(value)

            key = '127.0.0.1:%d:tcp' % (self.statsd_listener.getsockname()[1])
            self.assertEqual(backends[key]['relayed_lines'], 15)
            self.assertEqual(backends[key]['dropped_lines'], 0)
            self.assertEqual(groups['flagged_lines']['0'], 0)
            self.assertEqual(groups['flagged_lines']['1'], 15)

    def test_tcp_with_sampler(self):
        with self.generate_config('tcp', filename="statsrelay-sample") as config_path:
            self.launch_process(config_path)
            fd, addr = self.statsd_listener.accept()
            sender = self.connect('tcp', self.bind_statsd_port)
            for i in range(0, 5):
                sender.sendall('test:1|c\n')
                expected = 'test-1.test.suffix:1|c\n'
                self.check_recv(fd, expected, len(expected))

            # We should now be in sampling mode
            for i in range(0, 200):
                sender.sendall('test:1|c\n')

            time.sleep(4.0)
            self.check_recv(fd, 'test-1.test.suffix:1|c@0.005\n')

            # We should now be in sampling mode
            for i in range(0, 100):
                sender.sendall('test:1|c\n')

            self.check_recv(fd, 'test-1.test.suffix:1|c@0.01\n')

            sender.sendall('status\n')
            status = sender.recv(65536)
            sender.close()

            backends = defaultdict(dict)
            for line in status.split('\n'):
                if not line:
                    break
                if not line.startswith('backend:'):
                    continue
                backend, key, valuetype, value = line.split(' ', 3)
                backend = backend.split(':', 1)[1]
                backends[backend][key] = int(value)

            key = '127.0.0.1:%d:tcp' % (self.statsd_listener.getsockname()[1],)
            self.assertEqual(backends[key]['relayed_lines'], 7)
            self.assertEqual(backends[key]['dropped_lines'], 0)
            self.assertEqual(backends[key]['bytes_sent'], 172)
            self.assertEqual(backends[key]['bytes_queued'], 74)

    def test_tcp_with_gauge_sampler(self):
        with self.generate_config('tcp', filename="statsrelay-sample") as config_path:
            self.launch_process(config_path)
            fd, addr = self.statsd_listener.accept()
            sender = self.connect('tcp', self.bind_statsd_port)
            for i in range(0, 5):
                sender.sendall('test:1|g\n')
                expected = 'test-1.test.suffix:1|g\n'
                self.check_recv(fd, expected, len(expected))

            # We should now be in sampling mode
            for i in range(0, 200):
                sender.sendall('test:1|g\n')

            time.sleep(4.0)
            self.check_recv(fd, 'test-1.test.suffix:1|g\n')

            # We should now be in sampling mode
            for i in range(0, 100):
                sender.sendall('test:1|g\n')

            self.check_recv(fd, 'test-1.test.suffix:1|g\n')

            sender.sendall('status\n')
            status = sender.recv(65536)
            sender.close()

            backends = defaultdict(dict)
            for line in status.split('\n'):
                if not line:
                    break
                if not line.startswith('backend:'):
                    continue
                backend, key, valuetype, value = line.split(' ', 3)
                backend = backend.split(':', 1)[1]
                backends[backend][key] = int(value)

            key = '127.0.0.1:%d:tcp' % (self.statsd_listener.getsockname()[1],)
            self.assertEqual(backends[key]['relayed_lines'], 7)
            self.assertEqual(backends[key]['dropped_lines'], 0)
            self.assertEqual(backends[key]['bytes_sent'], 161)
            self.assertEqual(backends[key]['bytes_queued'], 63)


    def test_tcp_with_timer_sampler(self):
        with self.generate_config('tcp', filename="statsrelay-sample") as config_path:
            self.launch_process(config_path)
            fd, addr = self.statsd_listener.accept()
            sender = self.connect('tcp', self.bind_statsd_port)
            for i in range(0, 5):
                sender.sendall('test.srv.req:1.0|ms\n')
                expected = 'test-1.test.srv.req.suffix:1.0|ms\n'
                self.check_recv(fd, expected, len(expected))

            # We should now be in sampling mode
            for i in range(0, 200):
                sender.sendall('test.srv.req:1|ms|@0.2\n')

            time.sleep(4.0)

            # should have flushed the upper and lower values
            samples = [
                'test-1.test.srv.req.suffix:1|ms@0.00505051\n',
                'test-1.test.srv.req.suffix:1|ms@0.2\n',
            ]

            self.check_list_in_recv(fd, samples, 1024)

            #  We should now be in sampling mode
            for i in range(0, 200):
                sender.sendall('test.srv.req:%s|ms|@1.0\n' % str(i))

            time.sleep(5.0)

            samples = [
                'test-1.test.srv.req.suffix:199|ms@1\n',
                'test-1.test.srv.req.suffix:0|ms@1\n',
            ]

            # Ensure lower and upper timer values are being flushed.
            self.check_list_in_recv(fd, samples, 1024)

            # We should now be in non sampling mode
            for i in range(0, 100):
                sender.sendall('test.srv.req:1.0|ms\n')

            self.check_recv_in(fd, 'test-1.test.srv.req.suffix:1.0|ms\n')

            sender.sendall('status\n')
            status = sender.recv(65536)
            sender.close()

            backends = defaultdict(dict)
            for line in status.split('\n'):
                if not line:
                    break
                if not line.startswith('backend:'):
                    continue
                backend, key, valuetype, value = line.split(' ', 3)
                backend = backend.split(':', 1)[1]
                backends[backend][key] = int(value)

            key = '127.0.0.1:%d:tcp' % (self.statsd_listener.getsockname()[1])
            self.assertEqual(backends[key]['dropped_lines'], 0)


    def test_tcp_with_timer_sampler_no_flush(self):
        with self.generate_config('tcp', filename="statsrelay-sample-no-flush") as config_path:
            self.launch_process(config_path)
            fd, addr = self.statsd_listener.accept()
            sender = self.connect('tcp', self.bind_statsd_port)
            for i in range(0, 5):
                sender.sendall('test.srv.req:1.0|ms\n')
                expected = 'test-1.test.srv.req.suffix:1.0|ms\n'
                self.check_recv(fd, expected, len(expected))

            # We should now be in sampling mode
            for i in range(0, 200):
                sender.sendall('test.srv.req:1|ms|@0.2\n')

            time.sleep(4.0)

            # shouldn't have flushed the upper and lower values
            samples = [
                'test-1.test.srv.req.suffix:1|ms@0.2\n',
                'test-1.test.srv.req.suffix:1|ms@0.2\n',
            ]

            self.check_list_not_in_recv(fd, samples, 1024)

            sender.sendall('status\n')
            status = sender.recv(65536)
            sender.close()

            backends = defaultdict(dict)
            for line in status.split('\n'):
                if not line:
                    break
                if not line.startswith('backend:'):
                    continue
                backend, key, valuetype, value = line.split(' ', 3)
                backend = backend.split(':', 1)[1]
                backends[backend][key] = int(value)

            key = '127.0.0.1:%d:tcp' % (self.statsd_listener.getsockname()[1],)
            self.assertEqual(backends[key]['relayed_lines'], 10)
            self.assertEqual(backends[key]['dropped_lines'], 0)

    def test_tcp_with_ingress_blacklist(self):
        with self.generate_config('tcp', filename="statsrelay-blacklist") as config_path:
            self.launch_process(config_path)
            fd, addr = self.statsd_listener.accept()
            sender = self.connect('tcp', self.bind_statsd_port)
            for i in range(0, 6):
                if i % 2 == 0:
                    sender.sendall('redis.test.pipeline.execute_command.zrangebyscore:1|c\n')
                else:
                    sender.sendall('redis.test.pipeline.nonblacklisted.metric:1|c\n')
                    expected = 'test-production-iad.redis.test.pipeline.nonblacklisted.metric.instance.test-production-iad-canary:1|c\n'
                    self.check_recv(fd, expected, len(expected))

            sender.sendall('status\n')
            status = sender.recv(65536)
            sender.close()

            backends = defaultdict(dict)
            groups = defaultdict(dict)
            for line in status.split('\n'):
                if not line:
                    break
                if line.startswith('backend:'):
                    backend, key, valuetype, value = line.split(' ', 3)
                    backend = backend.split(':', 1)[1]
                    backends[backend][key] = int(value)
                elif line.startswith('group:'):
                    group, key, _, value = line.split(' ', 3)
                    group_idx = group.split(':', 1)[1]
                    groups[key][group_idx] = int(value)

            key = '127.0.0.1:%d:tcp' % (self.statsd_listener.getsockname()[1],)
            self.assertEqual(backends[key]['relayed_lines'], 3)
            self.assertEqual(backends[key]['dropped_lines'], 0)
            self.assertEqual(backends[key]['bytes_sent'], 306)
            self.assertEqual(backends[key]['bytes_queued'], 138)
            self.assertEqual(groups['rejected_lines']['0'], 0)
            self.assertEqual(groups['rejected_lines']['1'], 3)


    def test_tcp_with_ingress_blacklist_extra(self):
        with self.generate_config('tcp', filename="statsrelay-blacklist-extra") as config_path:
            self.launch_process(config_path)
            fd, addr = self.statsd_listener.accept()
            sender = self.connect('tcp', self.bind_statsd_port)
            for i in range(0, 6):
                if i % 2 == 0:
                    sender.sendall('redis.test.pipeline.execute_command.zrangebyscore:1|c\n')
                    sender.sendall('test.blacklistme.996a38e0-67c6-11ea-abc8-acde48001122:1|c\n')
                else:
                    sender.sendall('redis.test.pipeline.nonblacklisted.metric:1|c\n')
                    sender.sendall('test.git-server-6b22d668-67c6-11ea-abc8-acde48001122:1|c\n')
                    expected = (
                        'test-production-iad.redis.test.pipeline.nonblacklisted.metric.instance.test-production-iad-canary:1|c\n'
                        'test-production-iad.test.git-server-6b22d668-67c6-11ea-abc8-acde48001122.instance.test-production-iad-canary:1|c\n'
                    )
                    self.check_recv(fd, expected, len(expected))

            sender.sendall('status\n')
            status = sender.recv(65536)
            sender.close()

            backends = defaultdict(dict)
            groups = defaultdict(dict)
            for line in status.split('\n'):
                if not line:
                    break
                if line.startswith('backend:'):
                    backend, key, valuetype, value = line.split(' ', 3)
                    backend = backend.split(':', 1)[1]
                    backends[backend][key] = int(value)
                elif line.startswith('group:'):
                    group, key, _, value = line.split(' ', 3)
                    group_idx = group.split(':', 1)[1]
                    groups[key][group_idx] = int(value)

            key = '127.0.0.1:%d:tcp' % (self.statsd_listener.getsockname()[1],)
            self.assertEqual(backends[key]['relayed_lines'], 6)
            self.assertEqual(backends[key]['dropped_lines'], 0)
            self.assertEqual(backends[key]['bytes_sent'], 645)
            self.assertEqual(backends[key]['bytes_queued'], 309)
            self.assertEqual(groups['rejected_lines']['0'], 0)
            self.assertEqual(groups['rejected_lines']['1'], 6)


    def test_tcp_listener(self):
        with self.generate_config('tcp', filename='statsrelay') as config_path:
            self.launch_process(config_path)
            fd, addr = self.statsd_listener.accept()
            sender = self.connect('udp', self.bind_statsd_port)
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\ntest-1.test.suffix:1|c\n')
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\ntest-1.test.suffix:1|c\n')
            fd.close()
            time.sleep(6.0)
            sender.sendall('test:xxx\n')
            sender.sendall('test:1|c\n')
            fd, addr = self.statsd_listener.accept()
            self.check_recv(fd, 'test:1|c\ntest-1.test.suffix:1|c\n')
            sender.close()

            sender = self.connect('tcp', self.bind_statsd_port)
            sender.sendall('tcptest:1|c\n')
            self.check_recv(fd, 'tcptest:1|c\ntest-1.tcptest.suffix:1|c\n')

            sender.sendall('status\n')
            status = sender.recv(65536)
            sender.close()

            backends = defaultdict(dict)
            for line in status.split('\n'):
                if not line:
                    break
                if not line.startswith('backend:'):
                    continue
                backend, key, valuetype, value = line.split(' ', 3)
                backend = backend.split(':', 1)[1]
                backends[backend][key] = int(value)

            key = '127.0.0.1:%d:tcp' % (self.statsd_listener.getsockname()[1],)
            self.assertEqual(backends[key]['relayed_lines'], 8)
            self.assertEqual(backends[key]['dropped_lines'], 0)
            self.assertEqual(backends[key]['bytes_queued'],
                             backends[key]['bytes_sent'] - 56)

    def test_udp_listener(self):
        with self.generate_config('udp', filename='statsrelay_udp_with_validation') as config_path:
            self.launch_process(config_path)
            sender = self.connect('udp', self.bind_statsd_port)
            sender.sendall('test:1|c\n')
            fd = self.statsd_listener
            self.check_recv(fd, 'test:1|c\ntest-1.test.suffix:1|c\n')
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\ntest-1.test.suffix:1|c\n')
            sender.sendall('test:xxx\n')
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\ntest-1.test.suffix:1|c\n')
            sender.close()

            sender = self.connect('tcp', self.bind_statsd_port)
            sender.sendall('tcptest:1|c\n')
            self.check_recv(fd, 'tcptest:1|c\ntest-1.tcptest.suffix:1|c\n')

            sender.sendall('status\n')
            status = sender.recv(65536)
            sender.close()

            backends = defaultdict(dict)
            for line in status.split('\n'):
                if not line:
                    break
                if not line.startswith('backend:'):
                    continue
                backend, key, valuetype, value = line.split(' ', 3)
                backend = backend.split(':', 1)[1]
                backends[backend][key] = int(value)
            key = '127.0.0.1:%d:udp' % (self.statsd_listener.getsockname()[1],)
            self.assertEqual(backends[key]['relayed_lines'], 8)
            self.assertEqual(backends[key]['dropped_lines'], 0)
            self.assertEqual(backends[key]['bytes_queued'],
                             backends[key]['bytes_sent'] - 56)


    def test_udp_listener_invalid_lines_with_validate_flag(self):
        with self.generate_config('udp', filename='statsrelay_udp_with_validation') as config_path:
            self.launch_process(config_path)
            sender = self.connect('udp', self.bind_statsd_port)

            sender.sendall('test.invalid.__asg=rejectthis:1|c\n')
            sender.sendall('test.invalid.__host=i-1234:1|c\n')
            sender.sendall('test.valid.__status=accepted:1|c\n')
            fd = self.statsd_listener

            # shouldn't have metric flagged for bad tag usage
            samples = [
                'test-1.test.invalid.__asg=rejectthis:1|c\n',
            ]
            self.check_list_not_in_recv(fd, samples, 1024)

            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\ntest-1.test.suffix:1|c\n')
            # invalid 3
            sender.sendall('test:xxx\n')
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\ntest-1.test.suffix:1|c\n')
            sender.close()

            sender = self.connect('tcp', self.bind_statsd_port)
            sender.sendall('tcptest:1|c\n')
            self.check_recv(fd, 'tcptest:1|c\ntest-1.tcptest.suffix:1|c\n')

            sender.sendall('status\n')
            status = sender.recv(65536)
            sender.close()

            global_status = defaultdict(dict)
            backends = defaultdict(dict)
            for line in status.split('\n'):
                if not line:
                    break
                elif line.startswith('global'):
                    _, metric, _, value = line.split(' ', 3)
                    global_status[metric] = int(value)
                elif line.startswith('backend:'):
                    backend, key, valuetype, value = line.split(' ', 3)
                    backend = backend.split(':', 1)[1]
                    backends[backend][key] = int(value)

            key = '127.0.0.1:%d:udp' % (self.statsd_listener.getsockname()[1],)
            self.assertEqual(backends[key]['relayed_lines'], 8)
            self.assertEqual(backends[key]['dropped_lines'], 0)
            self.assertEqual(backends[key]['bytes_queued'],
                             backends[key]['bytes_sent'] - 56)
            self.assertEqual(global_status['invalid_lines'], 3)


    def test_udp_listener_invalid_lines_without_validate_flag(self):
        with self.generate_config('udp', filename='statsrelay_udp_no_validation') as config_path:
            self.launch_process(config_path)
            sender = self.connect('udp', self.bind_statsd_port)

            sender.sendall('test.invalid.__asg=rejectthis:1|c\ntest.invalid.__host2=i-1234:1|c\ntest.valid.__status=accepted:1|c\n')
            fd = self.statsd_listener

            # shouldn't have metric flagged for bad tag usage
            samples = [
                'test-1.test.invalid.__asg=rejectthis.suffix:1|c\n',
                'test-1.test.invalid.__host2=i-1234.suffix:1|c\n',
                'test-1.test.valid.__status=accepted.suffix:1|c\n',
            ]
            self.check_list_in_recv(fd, samples, 1024)

            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\ntest-1.test.suffix:1|c\n')
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\ntest-1.test.suffix:1|c\n')
            sender.close()

            sender = self.connect('tcp', self.bind_statsd_port)
            sender.sendall('tcptest:1|c\n')
            self.check_recv(fd, 'tcptest:1|c\ntest-1.tcptest.suffix:1|c\n')

            sender.sendall('status\n')
            status = sender.recv(65536)
            sender.close()

            global_status = defaultdict(dict)
            backends = defaultdict(dict)
            for line in status.split('\n'):
                if not line:
                    break
                elif line.startswith('global'):
                    _, metric, _, value = line.split(' ', 3)
                    global_status[metric] = int(value)
                elif line.startswith('backend:'):
                    backend, key, valuetype, value = line.split(' ', 3)
                    backend = backend.split(':', 1)[1]
                    backends[backend][key] = int(value)

            key = '127.0.0.1:%d:udp' % (self.statsd_listener.getsockname()[1],)
            self.assertEqual(backends[key]['relayed_lines'], 12)
            self.assertEqual(backends[key]['dropped_lines'], 0)
            self.assertEqual(backends[key]['bytes_queued'],
                             backends[key]['bytes_sent'] - 84)
            self.assertEqual(global_status['invalid_lines'], 0)


    def test_tcp_cork(self):
        if not sys.platform.startswith('linux'):
            return
        if sys.version_info[:2] < (2, 7):
            return
        cork_time = 0.200  # cork time is 200ms on linux
        self.tcp_cork = 'true'
        msg = 'test:1|c\ntest-1.test:1|c\n'
        with self.generate_config('tcp', filename='statsrelay') as config_path:
            self.launch_process(config_path)
            fd, addr = self.statsd_listener.accept()
            sender = self.connect('udp', self.bind_statsd_port)
            t0 = time.time()
            sender.sendall(msg)
            self.check_recv(fd, 'test:1|c\ntest-1.test.suffix:1|c\ntest-1.test:1|c\ntest-1.test-1.test.suffix:1|c\n')
            elapsed = time.time() - t0

            # ensure it took about cork_time ms
            self.assertGreater(elapsed, cork_time * 0.95)
            self.assertLess(elapsed, cork_time * 1.25)

            # try sending w/o corking... this assumes the mtu of the
            # loopback interface is 64k. need to send multiple
            # messages to avoid getting EMSGSIZE
            needed_messages = ((1 << 16) / len(msg)) / 2
            buf = msg * needed_messages
            t0 = time.time()
            sender.sendall(buf)
            sender.sendall(buf)
            sender.sendall(msg)
            self.assertEqual(len(fd.recv(1024)), 1024)

            # we can tell data wasn't corked because we get a response
            # fast enough
            elapsed = time.time() - t0

            self.assertLess(elapsed, cork_time / 2)

    def test_invalid_line_for_pull_request_35(self):
        with self.generate_config('udp', filename='statsrelay_udp_with_validation') as config_path:
            self.launch_process(config_path)
            sender = self.connect('udp', self.bind_statsd_port)
            sender.sendall('foo.bar:undefined|quux.quuxly.200:1c\n')
            sender.sendall('foo.bar:1|c\n')
            fd = self.statsd_listener
            self.check_recv(fd, 'foo.bar:1|c\ntest-1.foo.bar.suffix:1|c\n')
            self.assert_(self.proc.returncode is None)


class StathasherTests(unittest.TestCase):

    def get_foo(self, config):
        proc = subprocess.Popen(['./stathasher', '-c', config],
                                stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE)
        proc.stdin.write('foo\n')
        line = proc.stdout.readline()
        return line

    def test_stathasher(self):
        line = self.get_foo('tests/stathasher.json')
        self.assertEqual(line, 'key=foo statsd=127.0.0.1:3001 statsd_shard=1 send_health_metrics=false\n')  # noqa

    def test_stathasher_empty(self):
        line = self.get_foo('tests/empty.json')
        self.assertEqual(line, 'key=foo\n')

    def test_stathasher_just_statsd(self):
        line = self.get_foo('tests/stathasher_just_statsd.json')
        self.assertEqual(line, 'key=foo statsd=127.0.0.1:3001 statsd_shard=1 send_health_metrics=false\n')

    def test_stathasher_statsd_no_shardmap(self):
        line = self.get_foo('tests/statsrelay-no-shardmap.json')
        self.assertEqual(line, 'key=foo statsd=127.0.0.1:8128 statsd_shard=0 send_health_metrics=false\n')

    def test_stathasher_statsd_self_stats(self):
        line = self.get_foo('tests/statsrelay_statsd_self_stats.json')
        self.assertEqual(line, 'key=foo statsd=127.0.0.1:8128 statsd_shard=5 send_health_metrics=true\n')


def main():
    unittest.main()


if __name__ == '__main__':
    sys.exit(main())
