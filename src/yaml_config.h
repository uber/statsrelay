#ifndef STATSRELAY_YAML_CONFIG_H
#define STATSRELAY_YAML_CONFIG_H

#include "./list.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct proto_config {
	bool initialized;
	char *bind;
	bool enable_validation;
	uint64_t max_send_queue;
	list_t ring;
};

struct config {
	struct proto_config statsd_config;
	struct proto_config carbon_config;
};

struct config* parse_config(FILE *input);

// release the memory associated with a config
void destroy_config(struct config *);

#endif  // STATSRELAY_YAML_CONFIG_H
