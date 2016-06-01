#ifndef STATSRELAY_JSON_CONFIG_H
#define STATSRELAY_JSON_CONFIG_H

#include "./list.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct additional_config {
	/**
	 * A string to prepend/append to each metric going through a duplicate block.
	 * No dot (.) is added - this is a raw string.
	 */
	char* prefix;
	char* suffix;
	/**
	 * A PCRE compatible regex which will only allow these metrics through this
	 * duplicate endpoint.
	 */
	char* ingress_filter;
	/**
	 * A list of host:port combos where to forward traffic, consistently hashed.
	 */
	list_t ring;
};

struct proto_config {
	bool initialized;
	bool send_self_stats;
	char *bind;
	bool enable_validation;
	bool enable_tcp_cork;
	bool auto_reconnect; /* drop connections to backend and reconnect on full buffer */
	double reconnect_threshold; /* initiate auto reconnect when send buffer hits this threshold */
	uint64_t max_send_queue;
	list_t ring;
	list_t dupl; /* struct additional_config */
	list_t sstats; /* struct additional_config */
};

struct config {
	struct proto_config statsd_config;
};


static const char default_config[] = "/etc/statsrelay.json";

struct config* parse_json_config(FILE *input);

// release the memory associated with a config
void destroy_json_config(struct config *);

#endif  // STATSRELAY_JSON_CONFIG_H
