#include "vector.h"
#include "tcpserver.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void vector_dump(list_t list) {
	tcpsession_t *session;
	for (int i = 0; i < list->size; i++) {
		session = (tcpsession_t *)list->data[i];
		if (session != NULL)
			stats_debug_log("Index: %d, The session sd %d", i, session->sd);
		else
			stats_debug_log("Index: %d, NULL", i);
	}
}

bool vector_add(list_t clients, void *obj) {
	if (obj == NULL) {
		stats_error_log("malformed object passed in, NULL pointer");
		goto add_err;
	}

	// grow the list
	if (statsrelay_list_expand(clients) == NULL) {
		stats_error_log("vector: failed to expand list");
		goto add_err;
	}

	clients->data[clients->size - 1] = obj;
	return true;

add_err:
	return false;
}

void *vector_fetch(list_t clients, int index) {
	if (index >= 0 && index <= clients->size) {
		return clients->data[index];
	}
	return NULL;
}

bool vector_remove(list_t clients, int index) {
	if (index < 0 || index > clients->size)
		return false;

	stats_debug_log("vector_remove: removing element %d", index);
	clients->data[index] = NULL;

	stats_debug_log("vector_remove: total items %d", clients->size);
	return true;
}

size_t vector_size(list_t list) {
	return list->size;
}

bool vector_pad(list_t list, int pad_left) {
	if (!list->allocated_size) {
		stats_debug_log("vector: padding to the left %d", pad_left);
		// add some padding to facilitate
		// constant time lookups
		for (int p = 0; p < pad_left; p++) {
			if (statsrelay_list_expand(list) == NULL) {
				stats_error_log("tcplistener_accept_callback: failed to expand list");
				goto add_err;
			}
			list->data[list->size - 1] = NULL;
		}
		return true;
	}

add_err:
	return false;
}
