#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#include "../vector.h"

typedef struct {
	int sd;
}session_t;


// helper to ensure the vector is in the expected state
static bool vector_check_absence(list_t ring, int sd) {
	session_t *session;

	if (ring->data[sd] != NULL)
		return false;

	return true;
}

static list_t vector_construct(const char *filename) {
	list_t backends;

	session_t* test;
	int sd;
	backends = statsrelay_list_new();

	assert(backends != NULL);
	assert(backends->allocated_size == 0);
	assert(backends->size == 0);

	FILE *fp = fopen(filename, "r");
	assert(fp != NULL);
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	while ((read = getline(&line, &len, fp)) != -1) {
		test = (session_t *)malloc(sizeof(session_t));

		sd = atoi(line);
		test->sd = sd;
		for (ssize_t i = 0; i < read; i++) {
			if (isspace(line[i])) {
				line[i] = '\0';
				break;
			}
		}
		assert(vector_add(backends, sd, test));
		assert(vector_size(backends) == (sd + 1));
		assert(!vector_check_absence(backends, sd));
		// file descriptors are returned lowest-integer-first
		assert(vector_size(backends) == (sd + 1));
	}
	fclose(fp);
	free(line);
	return backends;
}

// Test the vector library. We attempt to test
// as close as possible to the exact use case.
int main(int argc, char **argv) {
	stats_log_verbose(1);

	list_t backends;

	backends = vector_construct("./src/tests/vector.txt");
	stats_debug_log("Backends have been initialized");

	int old_size = vector_size(backends);

	stats_debug_log("removing 11 from the list");
	assert(vector_remove(backends, 11));

	// only set as NULL, dont downsize
	assert(vector_size(backends) == old_size);

	// assert the item is actually gone
	assert(vector_check_absence(backends, 11));

	// Test free function
	statsrelay_list_destroy_full(backends);

	return 0;
}
