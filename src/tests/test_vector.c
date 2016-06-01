#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../vector.h"
#include "../log.h"


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

// Test the vector library. We attempt to test
// as close as possible to the exact use case.
int main(int argc, char **argv) {
	stats_log_verbose(1);

	list_t backends;
	session_t* test;
	size_t i, sd;

	backends = statsrelay_list_new();

	assert(backends != NULL);
	assert(backends->allocated_size == 0);
	assert(backends->size == 0);

	sd = 7;
	vector_pad(backends, sd);

	assert(vector_add(backends, sd));
	assert(vector_size(backends) == (sd + 1));

	assert(vector_check_absence(backends, sd) == false);

	assert(vector_remove(backends, sd));

	// file descriptors are returned lowest-integer-first 
	assert(vector_size(backends) == (sd + 1));

	assert(vector_check_absence(backends, sd));

	// Test free function
	statsrelay_list_destroy_full(backends);

	return 0;
}
