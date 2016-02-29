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
}tcpsession_t;

#define INITIAL_VECTOR_SIZE 4
#define VECTOR_INSERT_BATCH 32

// searches and removes all the items with session
// structure socket descriptor in the vector we maintain
// also resizes the vector
static void vector_remove(vector_t *v, int sd) {
	tcpsession_t *session;
	int vector_sz = vector_count(v);
	for (int v_index = 0; v_index < vector_sz; v_index++) {
		session = (tcpsession_t *)v->items[v_index];
		if (session != NULL && session->sd == sd) {
			stats_debug_log("test_vector: removing %d, from vector", session->sd);
			vector_delete_at(v, v_index);
		}
	}
	// Resizing worked
	assert(vector_fetch(v, vector_sz -1) == NULL);
	return;
}

// helper to ensure the vector is in the expected state
static void vector_check_absence(vector_t *v, int sd) {
	tcpsession_t *session;
	int vector_sz = vector_count(v);
	for (int v_index = 0; v_index < vector_sz; v_index++) {
		session = (tcpsession_t *)v->items[v_index];
		// Test if the vector is getting resized
		assert(session != NULL);
		if (session->sd == sd) {			
			assert(session->sd != sd);
		}
	}
}

// Test the vector library. We attempt to test
// as close as possible to the exact use case.
int main(int argc, char **argv) {
	stats_log_verbose(1);

	vector_t vector;
	vector_t *vectorRef;

	vectorRef = vector_init(&vector, INITIAL_VECTOR_SIZE);

	assert(vectorRef != NULL);
	assert(vector_count(vectorRef) == 0);
	assert(vector_capacity(vectorRef) == INITIAL_VECTOR_SIZE);

	uint32_t i;
	tcpsession_t *t_session;
	for (i = 0; i < VECTOR_INSERT_BATCH; i++) {
		t_session = (tcpsession_t *)malloc(sizeof(tcpsession_t));
		t_session->sd = i + 1;
		vector_insert(vectorRef, (void *)t_session);
		assert(vector_count(vectorRef) == (i +1));
	}
	// we expect 32 items in the vector
	assert(vector_count(vectorRef) == VECTOR_INSERT_BATCH);

	// initial size set to 4
	// vector capacity increases in the factor of 4
	assert(vector_capacity(vectorRef) == VECTOR_INSERT_BATCH);

	// Remove item with sd matching 4
	vector_remove(vectorRef, 4);

	// Ensure that item is actually gone
	assert(vector_count(vectorRef) == 31);
	// Capacity shouldn't change
	assert(vector_capacity(vectorRef) == 32);

	// Check if the item is actually deleted.
	vector_check_absence(vectorRef, 4);

	// Check if the item is actually deleted.
	// Remove item with sd matching 4
	vector_remove(vectorRef, 1);
	vector_check_absence(vectorRef, 1);

	// Test Get function
	t_session = (tcpsession_t *)vector_fetch(vectorRef, 5);
	assert(t_session != NULL);

	t_session = (tcpsession_t *)vector_fetch(vectorRef, vector_count(vectorRef));
	assert(t_session == NULL);

	// Test free function
	vector_free(vectorRef);
	return 0;
}
