#include "./hashring.h"

#include "./hashlib.h"
#include "./log.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>


static size_t g_hashring_size = 0;
static void **g_hashring_backends = NULL;


int hashring_init(const char *hashfile,
		  size_t expected_size,
		  hashring_alloc_func alloc) {
	FILE *hash_file;
	if ((hash_file = fopen(hashfile, "r")) == NULL) {
		return 1;
	}

	char *line = NULL;
	size_t len = 0;
	ssize_t read;

	if ((g_hashring_backends = malloc(sizeof(void *) * expected_size)) == NULL) {
		goto init_err;
	}
	while ((read = getline(&line, &len, hash_file)) != -1) {
		// strip the trailing newline
		if (line[read] == '\n') {
			line[read] = '\0';
		} else if (read && line[read - 1] == '\n') {
			line[read - 1] = '\0';
		}

		// expand the hashing
		void *obj = alloc(line);
		if (obj == NULL) {
			stats_log("hashring: failed to alloc after reading line \"%s\"", line);
			goto init_err;
		}
		g_hashring_backends[g_hashring_size++] = obj;
	}

	if (g_hashring_size != expected_size) {
		stats_log("hashring: fatal error in init, expected %d lines but actually saw %d lines",
			  expected_size, g_hashring_size);
		goto init_err;
	}

	free(line);
	fclose(hash_file);
	return 0;

init_err:
	free(line);
	fclose(hash_file);
	return 1;
}

void* hashring_choose(const char *key, size_t len) {
	const uint32_t index = stats_hash(key, (uint32_t) len, g_hashring_size);
	return g_hashring_backends[index];
}


void hashring_dealloc(hashring_dealloc_func dealloc) {
	if (g_hashring_backends == NULL) {
		return;
	}
	for (size_t i = 0; i < g_hashring_size; i++) {
		dealloc(g_hashring_backends[i]);
	}
	free(g_hashring_backends);
	g_hashring_backends = NULL;
	g_hashring_size = 0;
}
