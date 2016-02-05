#include "./server.h"

#include "./log.h"

#include <ev.h>
#include <string.h>

static void init_server(struct server *server) {
	server->enabled = false;
	server->send_self_stats = false;
	server->server = NULL;
	server->ts = NULL;
	server->us = NULL;
}

static bool connect_server(struct server *server,
		struct proto_config *config,
		protocol_parser_t parser,
		validate_line_validator_t validator,
		const char *name) {
	if (config->ring->size == 0) {
		stats_log("%s has no backends, skipping", name);
		return false;
	}

	if (config->send_self_stats) {
		/**
		 * Deduce if we should relay self stats over
		 */
		server->send_self_stats = true;
	}

	struct ev_loop *loop = ev_default_loop(0);

	server->server = stats_server_create(
			loop, config, parser, validator);

	server->enabled = true;

	if (server->server == NULL) {
		stats_error_log("main: Unable to create stats_server");
		return false;
	}
	server->ts = tcpserver_create(loop, server->server);
	if (server->ts == NULL) {
		stats_error_log("failed to create tcpserver");
		return false;
	}

	server->us = udpserver_create(loop, server->server);
	if (server->us == NULL) {
		stats_error_log("failed to create udpserver");
		return false;
	}

	if (tcpserver_bind(server->ts, config->bind, stats_connection, stats_recv) != 0) {
		stats_error_log("unable to bind tcp %s", config->bind);
		return false;
	}
	if (udpserver_bind(server->us, config->bind, stats_udp_recv) != 0) {
		stats_error_log("unable to bind udp %s", config->bind);
		return false;
	}
	return true;
}

static void destroy_server(struct server *server) {
	if (!server->enabled) {
		return;
	}
	if (server->ts != NULL) {
		tcpserver_destroy(server->ts);
	}
	if (server->us != NULL) {
		udpserver_destroy(server->us);
	}
	if (server->server != NULL) {
		stats_server_destroy(server->server);
	}
}

void init_server_collection(struct server_collection *server_collection,
		const char *filename) {
	server_collection->initialized = true;
	server_collection->config_file = strdup(filename);
	init_server(&server_collection->statsd_server);
}

bool connect_server_collection(struct server_collection *server_collection,
		struct config *config) {
	bool enabled_any = false;
	enabled_any |= connect_server(&server_collection->statsd_server,
			&config->statsd_config,
			protocol_parser_statsd,
			validate_statsd,
			"statsd");
	if (!enabled_any) {
		stats_error_log("failed to enable any backends");
	}
	return enabled_any;
}

void destroy_server_collection(struct server_collection *server_collection) {
	if (server_collection->initialized) {
		free(server_collection->config_file);
		destroy_server(&server_collection->statsd_server);
		server_collection->initialized = false;
	}
}
