#ifndef STATSRELAY_STATS_H
#define STATSRELAY_STATS_H

#include <ev.h>
#include <stdint.h>

#include "protocol.h"
#include "validate.h"
#include "json_config.h"

#include "./filter.h"
#include "./hashring.h"
#include "./buffer.h"
#include "./log.h"
#include "./stats.h"
#include "./tcpclient.h"
#include "./validate.h"


#define MAX_UDP_LENGTH 65536

#define STATSD_MONITORING_FLUSH_INTERVAL 1

/**
 * Static stack space for a single statsd key value
 */
#define KEY_BUFFER 8192

typedef struct {
	tcpclient_t client;
	char *key; /** for ex: 127.0.0.1:8125:tcp */
	char *metrics_key; /** for ex: 127_0_0_1_8125.tcp */
	uint64_t bytes_queued;
	uint64_t bytes_sent;
	uint64_t relayed_lines;
	uint64_t dropped_lines;
	int failing;
} stats_backend_t;

typedef struct {
	const char* prefix;
	size_t prefix_len;
	const char* suffix;
	size_t suffix_len;

	filter_t* ingress_filter;
	hashring_t ring;

	/* Stats */
	uint64_t relayed_lines;
	uint64_t filtered_lines;
} stats_backend_group_t;

struct stats_server_t {
	struct ev_loop *loop;

	uint64_t bytes_recv_udp;
	uint64_t bytes_recv_tcp;
	uint64_t total_connections;
	uint64_t malformed_lines;
	time_t last_reload;

	struct proto_config *config;
	size_t num_backends;
	stats_backend_t **backend_list;

	list_t rings;
	protocol_parser_t parser;
	validate_line_validator_t validator;

	/** Maintain unique ring for monitoring stats */
	list_t monitor_ring;
	size_t num_monitor_backends;
	stats_backend_t **backend_list_monitor;

	/** timer to flush health stats to central cluster **/
	ev_timer stats_flusher;
};

typedef struct {
	struct stats_server_t *server;
	buffer_t buffer;
	int sd;
} stats_session_t;

typedef struct stats_server_t stats_server_t;

stats_server_t *stats_server_create(
		struct ev_loop *loop,
		struct proto_config *config,
		protocol_parser_t parser,
		validate_line_validator_t validator);
stats_server_t *server;

static int stats_relay_line(const char *line, size_t len, stats_server_t *ss, bool is_monitor_ring);

size_t stats_num_backends(stats_server_t *server);

void stats_server_destroy(stats_server_t *server);

// ctx is a (void *) cast of the stats_server_t instance.
void *stats_connection(int sd, void *ctx);

int stats_recv(int sd, void *data, void *ctx);

int stats_udp_recv(int sd, void *data);

#endif  // STATSRELAY_STATS_H
