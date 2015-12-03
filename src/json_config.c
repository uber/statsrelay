#include "yaml_config.h"
#include "log.h"

#include <stdbool.h>
#include <string.h>
#include <jansson.h>

static void json_error_log(const char* msg, json_error_t* error) {
    stats_error_log("JSON error %s: %s (%s) at line %d", msg, error->text, error->source, error->line);
}

static void init_proto_config(struct proto_config *protoc) {
	protoc->initialized = false;
	protoc->bind = NULL;
	protoc->enable_validation = true;
	protoc->enable_tcp_cork = true;
	protoc->max_send_queue = 134217728;
	protoc->ring = statsrelay_list_new();
	protoc->dupl.ring = statsrelay_list_new();
	protoc->dupl.prefix = NULL;
	protoc->dupl.suffix = NULL;
}

static bool get_bool_orelse(json_t* json, const char* key, bool def) {
    json_t* v = json_object_get(json, key);
    if (v == NULL)
	return def;
    if (json_is_null(v))
	return def;
    return json_is_true(v) ? true : false;
}

static char* get_string(const json_t* json, const char* key) {
    json_t* j = json_object_get(json, key);
    if (key == NULL)
	return NULL;
    if (!json_is_string(j)) {
	stats_error_log("Expected a string value for '%s' - ignoring config value", key);
	return NULL;
    }
    const char* str = json_string_value(j);
    if (str == NULL)
	return NULL;
    else
	return strdup(str);
}

static int get_int_orelse(json_t* json, const char* key, int def) {
    json_t* v = json_object_get(json, key);
    if (v == NULL)
	return def;
    if (!json_is_number(v)) {
	stats_error_log("Expected an integer value for '%s' - using default of '%d'", key, def);
    }
    return (int)json_integer_value(v);
}

static void parse_server_list(const json_t* jshards, list_t ring) {
    if (jshards == NULL) {
	stats_error_log("no servers specified for routing");
	return;
    }

    const json_t* jserver = NULL;
    size_t index;
    json_array_foreach(jshards, index, jserver) {
	statsrelay_list_expand(ring);
	char* serverline = strdup(json_string_value(jserver));
	stats_log("adding server %s", serverline);
	ring->data[ring->size - 1] = serverline;
    }
}

static int parse_proto(json_t* json, struct proto_config* config) {
    config->initialized = true;
    config->enable_validation = get_bool_orelse(json, "validate", true);
    config->enable_tcp_cork = get_bool_orelse(json, "tcp_cork", true);

    char* jbind = get_string(json, "bind");
    if (jbind != NULL) {
	if (config->bind)
	    free(config->bind);
	config->bind = jbind;
    }

    config->max_send_queue = get_int_orelse(json, "max_send_queue", 134217728);

    const json_t* jshards = json_object_get(json, "shard_map");
    parse_server_list(jshards, config->ring);

    const json_t* duplicate = json_object_get(json, "duplicate_to");
    if (duplicate != NULL) {

	config->dupl.prefix = get_string(duplicate, "prefix");
	config->dupl.suffix = get_string(duplicate, "suffix");

	stats_log("adding duplicate cluster with prefix '%s' and suffix '%s'", config->dupl.prefix, config->dupl.suffix);

	const json_t* jdshards = json_object_get(duplicate, "shard_map");
	parse_server_list(jdshards, config->dupl.ring);
	stats_log("added duplicate cluster with %d servers", config->dupl.ring->size);
    }

    return 0;
}

struct config* parse_json_config(FILE* input) {
    	struct config *config = malloc(sizeof(struct config));
	if (config == NULL) {
		stats_error_log("malloc() error");
		return NULL;
	}

	init_proto_config(&config->carbon_config);
	config->carbon_config.bind = strdup("127.0.0.1:2003");

	init_proto_config(&config->statsd_config);
	config->statsd_config.bind = strdup("127.0.0.1:8125");

	json_error_t jerror;
	json_t* json = json_loadf(input, 0, &jerror);
	if (json == NULL) {
	    json_error_log("loading", &jerror);
	    goto parse_error;
	}

	if (!json_is_object(json)) {
	    stats_error_log("Config needs to be a json object");
	    goto parse_error;
	}

	json_t* statsd_json = json_object_get(json, "statsd");
	if (statsd_json) {
	    parse_proto(statsd_json, &config->statsd_config);
	}

	json_t* carbon_json = json_object_get(json, "carbon");
	if (carbon_json) {
	    parse_proto(carbon_json, &config->carbon_config);
	}

	json_decref(json);
	return config;

parse_error:
	if (json != NULL) {
	    json_decref(json);
	}
	destroy_config(config);
	return NULL;
}
