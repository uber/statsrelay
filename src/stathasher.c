#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "./hashring.h"
#include "./json_config.h"

static struct option long_options[] = {
	{"config",		required_argument,	NULL, 'c'},
	{"help",		no_argument,		NULL, 'h'},
};

static void* my_strdup(const char *str, void *unused_data, hashring_type_t ring_type) {
	return strdup(str);
}

static void print_help(const char *argv0) {
	printf("Usage: %s [-h] [-c /path/to/config.json]", argv0);
}

int main(int argc, char **argv) {
	char *config_name = (char *) default_config;
	bool process_self_stats = false;
	char c = 0;
	while (c != -1) {
		c = getopt_long(argc, argv, "c:h", long_options, NULL);
		switch (c) {
			case -1:
				break;
			case 0:
			case 'h':
				print_help(argv[0]);
				return 0;
			case 'c':
				config_name = optarg;
				break;
			default:
				printf("%s: Unknown argument %c\n", argv[0], c);
				return 1;
		}
	}
	if (optind != 1 && optind != 3) {
		printf("%s: unexpected command optoins\n", argv[0]);
		return 1;
	}

	FILE *config_file = fopen(config_name, "r");
	if (config_file == NULL) {
		fprintf(stderr, "failed to open %s\n", config_name);
		return 1;
	}
	struct config *app_cfg = parse_json_config(config_file);

	fclose(config_file);
	if (app_cfg == NULL) {
		fprintf(stderr, "failed to parse config %s\n", config_name);
		return 1;
	}

	hashring_t statsd_ring = NULL;

	if (app_cfg->statsd_config.initialized) {
		process_self_stats = app_cfg->statsd_config.send_self_stats;
		statsd_ring = hashring_load_from_config(
				app_cfg->statsd_config.ring, NULL, my_strdup, free, process_self_stats ? RING_MONITOR : RING_DEFAULT);
	}
	destroy_json_config(app_cfg);

	uint32_t shard;
	char *choice = NULL;
	char *line = NULL;
	size_t len;
	ssize_t bytes_read;
	while ((bytes_read = getline(&line, &len, stdin)) != -1) {
		// trim whitespace
		for (ssize_t i = 0; i < bytes_read; i++) {
			if (isspace(line[i])) {
				line[i] = '\0';
				break;
			}
		}
		printf("key=%s", line);
		if (statsd_ring != NULL) {
			choice = hashring_choose(statsd_ring, line, &shard);
			if (choice != NULL) {
				printf(" statsd=%s statsd_shard=%d", choice, shard);
			}
			printf(" process_self_stats=%s", process_self_stats ? "true" : "false");
		}
		putchar('\n');
		fflush(stdout);
	}
	free(line);
	hashring_dealloc(statsd_ring);
	return 0;
}
