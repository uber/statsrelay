#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "stats.h"

// callback after bytes are sent
static int stats_sent(void *tcpclient,
		enum tcpclient_event event,
		void *context,
		char *data,
		size_t len) {
	stats_backend_t *backend = (stats_backend_t *) context;
	backend->bytes_sent += len;
	return 0;
}

// Add a backend to the backend list.
static int add_backend(stats_server_t *server, stats_backend_t *backend, hashring_type_t r_type) {
	stats_backend_t **new_backends;
	if (r_type == RING_MONITOR) {
		new_backends = realloc(
				server->backend_list_monitor,
				sizeof(stats_backend_t *) * (server->num_monitor_backends + 1));
	} else {
		new_backends = realloc(
				server->backend_list,
				sizeof(stats_backend_t *) * (server->num_backends + 1));
	}
	if (new_backends == NULL) {
		stats_log("add_backend: failed to realloc backends list %s\n",
				r_type == RING_MONITOR ? "monitor cluster" : "");
		return 1;
	}

	if (r_type == RING_MONITOR) {
		server->backend_list_monitor =  new_backends;
		server->backend_list_monitor[server->num_monitor_backends++] = backend;
	} else {
		server->backend_list = new_backends;
		server->backend_list[server->num_backends++] = backend;
	}
	return 0;
}

// Find a backend in the backend list; this is used so we don't create
// duplicate backends. This is linear with the number of actual
// backends in the file which should be fine for any reasonable
// configuration (say, less than 10,000 backend statsite.
// Also note that while this is linear, it only happens
// during statsrelay initialization, not when running.
static stats_backend_t *find_backend(stats_backend_t **backend_lists, size_t num_backends, const char *key) {
	for (size_t i = 0; i < num_backends; i++) {
		stats_backend_t *backend = backend_lists[i];
		if (strcmp(backend->key, key) == 0) {
			return backend;
		}
	}
	return NULL;
}

// Make a backend, returning it from the backend list if it's already
// been created.
static void* make_backend(const char *host_and_port, void *data, hashring_type_t r_type) {
	stats_backend_t *backend = NULL;
	char *full_key = NULL;
	char *full_key_metrics = NULL;
	int str_i = 0;


	// First we normalize so that the key is in the format
	// host:port:protocol
	char *host = NULL;
	char *port = NULL;
	char *protocol = NULL;

	const char *colon1 = strchr(host_and_port, ':');
	if (colon1 == NULL) {
		stats_log("failed to parse host/port in \"%s\"", host_and_port);
		goto make_err;
	}
	host = strndup(host_and_port, colon1 - host_and_port);
	if (host == NULL) {
		stats_log("stats: alloc error in host");
		goto make_err;
	}

	const char *colon2 = strchr(colon1 + 1, ':');
	if (colon2 == NULL) {
		port = strdup(colon1 + 1);
		protocol = strdup("tcp");  // use TCP by default
	} else {
		port = strndup(colon1 + 1, colon2 - colon1 - 1);
		protocol = strdup(colon2 + 1);
	}
	if (port == NULL || protocol == NULL) {
		stats_log("stats: alloc error in port/protocol");
		goto make_err;
	}

	const size_t hp_len = strlen(host_and_port);
	const size_t space_needed = hp_len + strlen(protocol) + 2;

	if (colon2 == NULL) {
		full_key = malloc(space_needed);

		if (full_key != NULL && snprintf(full_key, space_needed, "%s:%s", host_and_port, protocol) < 0) {
			stats_error_log("failed to snprintf");
			goto make_err;
		}
	} else {
		full_key = strdup(host_and_port);
	}

	if (full_key == NULL) {
		stats_error_log("failed to create backend key");
		goto make_err;
	}

	full_key_metrics = malloc(space_needed);

	for (str_i=0; str_i < hp_len; str_i++) {
		if (host_and_port[str_i] != '.' && host_and_port[str_i] != ':') {
			full_key_metrics[str_i] = host_and_port[str_i];
		} else {
			full_key_metrics[str_i] = '_';
		}
	}
	full_key_metrics[str_i++] = '.';
	full_key_metrics[str_i] = '\0';
	strncat(full_key_metrics, protocol, strlen(protocol));

	// Find the key in our list of backends
	stats_server_t *server = (stats_server_t *) data;

	if (r_type == RING_MONITOR) {
		backend = find_backend(server->backend_list_monitor, server->num_monitor_backends, full_key);
	} else {
		backend = find_backend(server->backend_list, server->num_backends, full_key);
	}
	if (backend != NULL) {
		free(host);
		free(port);
		free(protocol);
		free(full_key);
		free(full_key_metrics);
		return backend;
	}
	backend = malloc(sizeof(stats_backend_t));
	if (backend == NULL) {
		stats_log("stats: alloc error creating backend");
		goto make_err;
	}

	if (tcpclient_init(&backend->client,
				server->loop,
				backend,
				host,
				port,
				protocol,
				server->config)) {
		stats_log("stats: failed to tcpclient_init");
		goto make_err;
	}

	if (tcpclient_connect(&backend->client)) {
		stats_log("stats: failed to connect tcpclient");
		goto make_err;
	}
	backend->bytes_queued = 0;
	backend->bytes_sent = 0;
	backend->relayed_lines = 0;
	backend->dropped_lines = 0;
	backend->failing = 0;
	backend->key = full_key;
	if (full_key_metrics != NULL && full_key_metrics[0] != '\0') {
		backend->metrics_key = full_key_metrics;
	}

	stats_debug_log("metrics key is %s", backend->metrics_key);
	tcpclient_set_sent_callback(&backend->client, stats_sent);
	add_backend(server, backend, r_type);
	stats_debug_log("initialized new backend %s", backend->key);

	free(host);
	free(port);
	free(protocol);
	return backend;

make_err:
	free(host);
	free(port);
	free(protocol);
	free(full_key);
	return NULL;
}

/*
 * This function does nothing so we don't destroy a backend more than once
 */
static void nop_kill_backend(void *data) {
	return;
}

static void kill_backend(stats_backend_t *backend) {
	if (backend->key != NULL) {
		stats_log("killing backend %s", backend->key);
		free(backend->key);
	}
	tcpclient_destroy(&backend->client);
	free(backend);
}

static void group_destroy(stats_backend_group_t* group) {
	hashring_dealloc(group->ring);
	if (group->ingress_filter) {
		filter_free(group->ingress_filter);
		group->ingress_filter = NULL;
	}
	free(group);
}

static int group_filter_create(struct additional_config* dupl, stats_backend_group_t* group) {
	filter_t* filter;
	int st = filter_re_create(&filter, dupl->ingress_filter, NULL);
	if (st != 0) {
		stats_error_log("filter creation failed");
		return st;
	}
	stats_log("created ingress filter");
	group->ingress_filter = filter;
	return 0;
}

static void group_prefix_create(struct additional_config* config, stats_backend_group_t* group) {
	group->prefix = config->prefix;
	if (group->prefix)
		group->prefix_len = strlen(config->prefix);
	else
		group->prefix_len = 0;

	group->suffix = config->suffix;
	if (group->suffix)
		group->suffix_len = strlen(group->suffix);
	else
		group->suffix_len = 0;

}

static void flush_cluster_stats(struct ev_loop *loop, struct ev_timer *watcher, int events) {
	ev_tstamp timeout = 15.0;
	stats_server_t *server= (stats_server_t *)watcher->data;

	ev_timer_stop(loop, &server->stats_flusher);

	stats_backend_t *backend;

	char *head, *tail;
	size_t len;

	static char line_buffer[MAX_UDP_LENGTH + 2];

	buffer_t *response = create_buffer(MAX_UDP_LENGTH);
	if (response == NULL) {
		stats_log("failed to allocate send_statistics buffer");
		goto reset_timer;
	}

	buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
				"global.bytes_recv_tcp:%" PRIu64 "|g\n",
				server->bytes_recv_tcp));

	buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
				"global.total_connections:%" PRIu64 "|g\n",
				server->total_connections));

	buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
				"global.bytes_recv_udp:%" PRIu64 "|g\n",
				server->bytes_recv_udp));

	buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
				"global.total_connections:%" PRIu64 "|g\n",
				server->total_connections));

	buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
				"global.last_reload.timestamp:%" PRIu64 "|g\n",
				server->last_reload));

	buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
				"global.malformed_lines:%" PRIu64 "|g\n",
				server->malformed_lines));

	for (int i = 0; i < server->rings->size; i++) {
		stats_backend_group_t* group = (stats_backend_group_t*)server->rings->data[i];
		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"group_%i.filtered_lines:%" PRIu64 "|g\n",
					i, group->filtered_lines));
		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"group_%i.relayed_lines:%" PRIu64 "|g\n",
					i, group->relayed_lines));
	}

	for (size_t i = 0; i < server->num_backends; i++) {
		backend = server->backend_list[i];

		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"backend_%s.bytes_queued:%" PRIu64 "|g\n",
					backend->metrics_key, backend->bytes_queued));

		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"backend_%s.bytes_sent:%" PRIu64 "|g\n",
					backend->metrics_key, backend->bytes_sent));

		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"backend_%s.relayed_lines:%" PRIu64 "|g\n",
					backend->metrics_key, backend->relayed_lines));

		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"backend_%s.dropped_lines:%" PRIu64 "|g\n",
					backend->metrics_key, backend->dropped_lines));

		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"backend_%s.failing.boolean:%i|c\n",
					backend->metrics_key, backend->failing));
	}

	while (buffer_datacount(response) > 0) {
		size_t datasize = buffer_datacount(response);

		if (datasize == 0) {
			break;
		}
		head = (char *)buffer_head(response);
		tail = memchr(head, '\n', datasize);
		if (tail == NULL) {
			break;
		}
		len = tail - head;
		memcpy(line_buffer, head, len);
		memcpy(line_buffer + len, "\n\0", 2);

		if (stats_relay_line(line_buffer, len, server, true) != 0) {
			stats_debug_log("statsrelay: failed to send health metrics");
		}
		buffer_consume(response, len + 1);
	}

	delete_buffer(response);

reset_timer:
	/**
	 * reset timer
	 */
	ev_timer_set(&server->stats_flusher, timeout, 0.);
	ev_timer_start(server->loop, &server->stats_flusher);	
}

stats_server_t *stats_server_create(struct ev_loop *loop,
		struct proto_config *config,
		protocol_parser_t parser,
		validate_line_validator_t validator) {
	stats_server_t *server;
	server = malloc(sizeof(stats_server_t));
	if (server == NULL) {
		stats_log("stats: Unable to allocate memory");
		return NULL;
	}

	server->loop = loop;
	server->num_backends = 0;
	server->num_monitor_backends = 0;
	server->backend_list = NULL;
	server->backend_list_monitor = NULL;
	server->config = config;
	server->rings = statsrelay_list_new();
	server->monitor_ring = statsrelay_list_new();
	{
		/* 
		 * 1. Load the primary shard map from the configuration
		 * 2. Load the duplicate shard map with extra parameters
		 * 3. Load the monitor stats shard map, if present
		 */
		hashring_t ring = hashring_load_from_config(
				config->ring, server, make_backend, nop_kill_backend, RING_DEFAULT);
		if (ring == NULL) {
			stats_error_log("hashring_load_from_config failed");
			goto server_create_err;
		}
		statsrelay_list_expand(server->rings);
		stats_backend_group_t* group = calloc(1, sizeof(stats_backend_group_t));

		group->ring = ring;
		server->rings->data[server->rings->size - 1] = (void*)group;

		for (int dupl_i = 0; dupl_i < config->dupl->size; dupl_i++) {
			struct additional_config *dupl = config->dupl->data[dupl_i];
			ring = hashring_load_from_config(dupl->ring, server, make_backend, nop_kill_backend, RING_DEFAULT);
			if (ring == NULL) {
				stats_error_log("hashring_load_from_config for duplicate ring failed");
				goto server_create_err;
			}
			statsrelay_list_expand(server->rings);
			group = calloc(1, sizeof(stats_backend_group_t));
			server->rings->data[server->rings->size - 1] = (void*)group;

			group->ring = ring;
			group_prefix_create(dupl, group);

			if (dupl->ingress_filter) {
				if (group_filter_create(dupl, group) != 0)
					goto server_create_err;
			}
		}

		if (config->send_self_stats) {
			/**
			 * Only single config for monitor section
			 */
			struct additional_config *stat = config->sstats->data[0];

			ring = hashring_load_from_config(stat->ring, server, make_backend, nop_kill_backend,
					RING_MONITOR);
			if (ring == NULL) {
				stats_error_log("hashring_load_from_config for monitor ring failed");
				goto server_create_err;
			}
			statsrelay_list_expand(server->monitor_ring);
			stats_backend_group_t* monitor_group = calloc(1, sizeof(stats_backend_group_t));
			server->monitor_ring->data[server->monitor_ring->size - 1] = (void*)monitor_group;

			monitor_group->ring = ring;
			group_prefix_create(stat, monitor_group);

			/**
			 * Once initialized, lets kick off the timer
			 */
			ev_timer_init(&server->stats_flusher,
					flush_cluster_stats,
					STATSD_MONITORING_FLUSH_INTERVAL,
					0);

			server->stats_flusher.data = server;
			ev_timer_start(server->loop, &server->stats_flusher);
		}
	}

	server->bytes_recv_udp = 0;
	server->bytes_recv_tcp = 0;
	server->malformed_lines = 0;
	server->total_connections = 0;
	server->last_reload = 0;

	server->parser = parser;
	server->validator = validator;

	for (int i = 0; i < server->rings->size; i++)
		stats_log("initialized server %d (%d total backends in system), hashring size = %d",
				i,
				server->num_backends,
				hashring_size(((stats_backend_group_t*)server->rings->data[i])->ring));


	if (config->send_self_stats) {
		for (int i = 0; i < server->monitor_ring->size; i++)
			stats_log("initialized monitor server %d (%d total backends in system), hashring size = %d",
					i,
					server->num_monitor_backends,
					hashring_size(((stats_backend_group_t*)server->monitor_ring->data[i])->ring));
	}

	return server;

server_create_err:
	if (server != NULL) {
		for (int i = 0; i < server->rings->size; i++) {
			stats_backend_group_t* group = (stats_backend_group_t*)server->rings->data[i];
			group_destroy(group);
		}
		statsrelay_list_destroy(server->rings);

		for (int i = 0; i < server->monitor_ring->size; i++) {
			stats_backend_group_t* group = (stats_backend_group_t*)server->monitor_ring->data[i];
			group_destroy(group);
		}
		statsrelay_list_destroy(server->monitor_ring);
		free(server);
	}
	return NULL;
}

size_t stats_num_backends(stats_server_t *server) {
	return server->num_backends;
}

void *stats_connection(int sd, void *ctx) {
	stats_session_t *session;

	stats_debug_log("stats: accepted client connection on fd %d", sd);
	session = (stats_session_t *) malloc(sizeof(stats_session_t));
	if (session == NULL) {
		stats_log("stats: Unable to allocate memory");
		return NULL;
	}

	if (buffer_init(&session->buffer) != 0) {
		stats_log("stats: Unable to initialize buffer");
		free(session);
		return NULL;
	}

	session->server = (stats_server_t *) ctx;
	session->server->total_connections++;
	session->sd = sd;
	return (void *) session;
}

static int stats_relay_line(const char *line, size_t len, stats_server_t *ss, bool send_to_monitor_cluster) {
	if (ss->config->enable_validation && ss->validator != NULL) {
		if (ss->validator(line, len) != 0) {
			return 1;
		}
	}

	static char key_buffer[KEY_BUFFER];
	size_t key_len = ss->parser(line, len);
	if (key_len == 0) {
		ss->malformed_lines++;
		stats_log("stats: failed to find key: \"%s\"", line);
		return 1;
	}
	memcpy(key_buffer, line, key_len);
	key_buffer[key_len] = '\0';

	hashring_hash_t key_hash = hashring_hash(key_buffer);

	size_t ring_size = send_to_monitor_cluster ? ss->monitor_ring->size : ss->rings->size;
	list_t ring_ptr = send_to_monitor_cluster ? ss->monitor_ring : ss->rings;

	if (ring_size == 0) {
		stats_debug_log("%s ring is empty", send_to_monitor_cluster ? "monitor": "generic");
	}

	for (int i = 0; i < ring_size; i++) {
		stats_backend_group_t* group = (stats_backend_group_t*)ring_ptr->data[i];
		stats_backend_t *backend = hashring_choose_fromhash(group->ring, key_hash, NULL);

		if (backend == NULL) {
			/* No backend? No problem. Just skip doing anything */
			stats_log("statsrelay: Failed to find a backend to send in %s ring", send_to_monitor_cluster ? "monitor": "general");
			continue;
		}

		/* If we have a filter, lets run it */
		if (group->ingress_filter) {
			bool res = filter_exec(group->ingress_filter, key_buffer, key_len);
			if (!res) { /* Filter didn't match, don't process this backend */
				group->filtered_lines++;
				continue;
			}
		}

		/* Allow the line to be modified if needed */
		char* linebuf = (char*)line;
		int send_len = len;

		if (group->prefix != NULL || group->suffix != NULL) {
			static char prefix_line_buffer[MAX_UDP_LENGTH + 1];
			linebuf = prefix_line_buffer;
			linebuf[0] = '\0';

			if (group->prefix) {
				strncpy(linebuf, group->prefix, MAX_UDP_LENGTH);
				prefix_line_buffer[MAX_UDP_LENGTH] = '\0';
				linebuf += group->prefix_len;
			}

			strncpy(linebuf, key_buffer, MAX_UDP_LENGTH - group->prefix_len);
			linebuf += key_len;

			if (group->suffix) {
				strncpy(linebuf, group->suffix, MAX_UDP_LENGTH - group->prefix_len - key_len);
				prefix_line_buffer[MAX_UDP_LENGTH] = '\0';
				linebuf += group->suffix_len;
			}

			/*                                copy \n\0 from line */
			memcpy(linebuf, &line[key_len], (len + 2) - key_len);

			/* Reset position */
			linebuf = prefix_line_buffer;
			send_len += group->prefix_len + group->suffix_len;
		}

		if (tcpclient_sendall(&backend->client, linebuf, send_len + 1) != 0) {
			backend->dropped_lines++;
			if (backend->failing == 0) {
				stats_log("stats: Error sending to backend %s", backend->key);
				backend->failing = 1;
			}
			// We will allow a backend to fail with a full queue
			// and just continue operating. This breaks some backpressure
			// mechanisms and should be fixed.
		} else {
			backend->failing = 0;
		}
		group->relayed_lines++;

		backend->bytes_queued += len + 1;
		backend->relayed_lines++;
	}

	return 0;
}

void stats_send_statistics(stats_session_t *session) {
	stats_backend_t *backend;
	ssize_t bytes_sent;

	// TODO: this only needs to be allocated once, not every time we send
	// statistics
	buffer_t *response = create_buffer(MAX_UDP_LENGTH);
	if (response == NULL) {
		stats_log("failed to allocate send_statistics buffer");
		return;
	}

	buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
				"global bytes_recv_udp gauge %" PRIu64 "\n",
				session->server->bytes_recv_udp));

	buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
				"global bytes_recv_tcp gauge %" PRIu64 "\n",
				session->server->bytes_recv_tcp));

	buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
				"global total_connections gauge %" PRIu64 "\n",
				session->server->total_connections));

	buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
				"global last_reload timestamp %" PRIu64 "\n",
				session->server->last_reload));

	buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response),
				"global malformed_lines gauge %" PRIu64 "\n",
				session->server->malformed_lines));

	for (int i = 0; i < session->server->rings->size; i++) {
		stats_backend_group_t* group = (stats_backend_group_t*)session->server->rings->data[i];
		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"group:%i filtered_lines gauge %" PRIu64 "\n",
					i, group->filtered_lines));
		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"group:%i relayed_lines gauge %" PRIu64 "\n",
					i, group->relayed_lines));
	}

	for (size_t i = 0; i < session->server->num_backends; i++) {
		backend = session->server->backend_list[i];

		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"backend:%s bytes_queued gauge %" PRIu64 "\n",
					backend->key, backend->bytes_queued));

		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"backend:%s bytes_sent gauge %" PRIu64 "\n",
					backend->key, backend->bytes_sent));

		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"backend:%s relayed_lines gauge %" PRIu64 "\n",
					backend->key, backend->relayed_lines));

		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"backend:%s dropped_lines gauge %" PRIu64 "\n",
					backend->key, backend->dropped_lines));

		buffer_produced(response,
				snprintf((char *)buffer_tail(response), buffer_spacecount(response),
					"backend:%s failing boolean %i\n",
					backend->key, backend->failing));
	}

	buffer_produced(response,
			snprintf((char *)buffer_tail(response), buffer_spacecount(response), "\n"));

	while (buffer_datacount(response) > 0) {
		bytes_sent = send(session->sd, buffer_head(response), buffer_datacount(response), 0);
		if (bytes_sent < 0) {
			stats_log("stats: Error sending status response: %s", strerror(errno));
			break;
		}

		if (bytes_sent == 0) {
			stats_log("stats: Error sending status response: Client closed connection");
			break;
		}

		buffer_consume(response, bytes_sent);
	}
	delete_buffer(response);
}

static int stats_process_lines(stats_session_t *session) {
	char *head, *tail;
	size_t len;

	static char line_buffer[MAX_UDP_LENGTH + 2];

	while (1) {
		size_t datasize = buffer_datacount(&session->buffer);
		if (datasize == 0) {
			break;
		}
		head = (char *)buffer_head(&session->buffer);
		tail = memchr(head, '\n', datasize);
		if (tail == NULL) {
			break;
		}
		len = tail - head;
		memcpy(line_buffer, head, len);
		memcpy(line_buffer + len, "\n\0", 2);

		if (len == 6 && strcmp(line_buffer, "status\n") == 0) {
			stats_send_statistics(session);
		} else if (stats_relay_line(line_buffer, len, session->server, false) != 0) {
			return 1;
		}
		buffer_consume(&session->buffer, len + 1);	// Add 1 to include the '\n'
	}

	return 0;
}

void stats_session_destroy(stats_session_t *session) {
	buffer_destroy(&session->buffer);
	free(session);
}

int stats_recv(int sd, void *data, void *ctx) {
	stats_session_t *session = (stats_session_t *)ctx;

	ssize_t bytes_read;
	size_t space;

	// First we try to realign the buffer (memmove so that head
	// and ptr match) If that fails, we double the size of the
	// buffer
	space = buffer_spacecount(&session->buffer);
	if (space == 0) {
		buffer_realign(&session->buffer);
		space = buffer_spacecount(&session->buffer);
		if (space == 0) {
			if (buffer_expand(&session->buffer) != 0) {
				stats_log("stats: Unable to expand buffer, aborting");
				goto stats_recv_err;
			}
			space = buffer_spacecount(&session->buffer);
		}
	}

	bytes_read = recv(sd, buffer_tail(&session->buffer), space, 0);
	if (bytes_read < 0) {
		stats_log("stats: Error receiving from socket: %s", strerror(errno));
		goto stats_recv_err;
	} else if (bytes_read == 0) {
		stats_debug_log("stats: client from fd %d closed connection", sd);
		goto stats_recv_err;
	} else {
		stats_debug_log("stats: received %zd bytes from tcp client fd %d", bytes_read, sd);
	}

	session->server->bytes_recv_tcp += bytes_read;

	if (buffer_produced(&session->buffer, bytes_read) != 0) {
		stats_log("stats: Unable to produce buffer by %i bytes, aborting", bytes_read);
		goto stats_recv_err;
	}

	if (stats_process_lines(session) != 0) {
		stats_log("stats: Invalid line processed, closing connection");
		goto stats_recv_err;
	}

	return 0;

stats_recv_err:
	stats_session_destroy(session);
	return 1;
}

// TODO: refactor this whole method to share more code with the tcp receiver:
//  * this shouldn't have to allocate a new buffer -- it should be on the ss
//  * the line processing stuff should use stats_process_lines()
int stats_udp_recv(int sd, void *data) {
	stats_server_t *ss = (stats_server_t *)data;
	ssize_t bytes_read;
	char *head, *tail;

	static char buffer[MAX_UDP_LENGTH];
	static char line_buffer[MAX_UDP_LENGTH + 2];

	bytes_read = read(sd, buffer, MAX_UDP_LENGTH);

	if (bytes_read == 0) {
		stats_error_log("stats: Unexpectedly received zero-length UDP payload.");
		goto udp_recv_err;
	} else if (bytes_read < 0) {
		if (errno == EAGAIN) {
			stats_error_log("stats: interrupted during recvfrom");
			goto udp_recv_err;
		} else {
			stats_error_log("stats: Error calling recvfrom: %s", strerror(errno));
			goto udp_recv_err;
		}
	} else {
		stats_debug_log("stats: received %zd bytes from udp fd %d", bytes_read, sd);
	}

	ss->bytes_recv_udp += bytes_read;

	size_t line_len;
	size_t offset = 0;
	while (offset < bytes_read) {
		head = (char *) buffer + offset;
		if ((tail = memchr(head, '\n', bytes_read - offset)) == NULL) {
			tail = buffer + bytes_read;
		}

		line_len = tail - head;
		memcpy(line_buffer, head, line_len);
		memcpy(line_buffer + line_len, "\n\0", 2);

		if (stats_relay_line(line_buffer, line_len, ss, false) != 0) {
			goto udp_recv_err;
		}
		offset += line_len + 1;
	}
	return 0;

udp_recv_err:
	return 1;
}


void stats_server_destroy(stats_server_t *server) {
	ev_timer_stop(server->loop, &server->stats_flusher);

	for (int i = 0; i < server->rings->size; i++) {
		stats_backend_group_t* group = (stats_backend_group_t*)server->rings->data[i];
		group_destroy(group);
	}

	for (int i = 0; i < server->monitor_ring->size; i++) {
		stats_backend_group_t* group = (stats_backend_group_t*)server->monitor_ring->data[i];
		group_destroy(group);
	}
	statsrelay_list_destroy(server->rings);
	statsrelay_list_destroy(server->monitor_ring);

	for (size_t i = 0; i < server->num_backends; i++) {
		stats_backend_t *backend = server->backend_list[i];
		if (backend != NULL) {
			kill_backend(backend);
		}
	}

	for (size_t i = 0; i < server->num_monitor_backends; i++) {
		stats_backend_t *backend = server->backend_list_monitor[i];
		kill_backend(backend);
	}

	free(server->backend_list);
	free(server->backend_list_monitor);

	server->num_backends = 0;
	server->num_monitor_backends = 0;

	free(server);
}
