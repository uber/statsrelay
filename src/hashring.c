#include "./hashring.h"

#include "./hashlib.h"
#include "./list.h"
#include "./log.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


hashring_t hashring_init(void *alloc_data,
		hashring_alloc_func alloc,
		hashring_dealloc_func dealloc,
		hashring_type_t r_type) {
	struct hashring *ring = malloc(sizeof(struct hashring));
	if (ring == NULL) {
		stats_error_log("failure to malloc() in hashring_init");
		return NULL;
	}
	ring->backends = statsrelay_list_new();
	ring->alloc_data = alloc_data;
	ring->alloc = alloc;
	ring->dealloc = dealloc;
	ring->ring_type = r_type;
	return ring;
}

hashring_t hashring_load_from_config(list_t config_ring,
		void *alloc_data,
		hashring_alloc_func alloc_func,
		hashring_dealloc_func dealloc_func,
		hashring_type_t r_type) {
	hashring_t ring = hashring_init(alloc_data, alloc_func, dealloc_func, r_type);
	if (ring == NULL) {
		stats_error_log("failed to hashring_init");
		return NULL;
	}
	for (size_t i = 0; i < config_ring->size; i++) {
		if (!hashring_add(ring, config_ring->data[i])) {
			hashring_dealloc(ring);
			return NULL;
		}
	}
	return ring;
}

bool hashring_add(hashring_t ring, const char *line) {
	if (line == NULL) {
		stats_error_log("cowardly refusing to alloc NULL pointer");
		goto add_err;
	}
	// allocate an object
	void *obj = ring->alloc(line, ring->alloc_data, ring->ring_type);
	if (obj == NULL) {
		stats_error_log("hashring: failed to alloc line \"%s\"", line);
		goto add_err;
	}

	// grow the list
	if (statsrelay_list_expand(ring->backends) == NULL) {
		stats_error_log("hashring: failed to expand list");
		ring->dealloc(obj);
		goto add_err;
	}

	ring->backends->data[ring->backends->size - 1] = obj;
	return true;

add_err:
	return false;
}

size_t hashring_size(hashring_t ring) {
	if (ring == NULL) {
		return 0;
	}
	return ring->backends->size;
}

hashring_hash_t hashring_hash(const char* key) {
	return stats_hash_key(key, strlen(key));
}

void* hashring_choose_fromhash(struct hashring* ring,
		hashring_hash_t hash,
		uint32_t* shard_num) {
	if (ring == NULL || ring->backends == NULL) {
		return NULL;
	}
	const size_t ring_size = ring->backends->size;
	if (ring_size == 0) {
		return NULL;
	}
	if (ring_size == 1) {
		return ring->backends->data[0];
	}
	const uint32_t index = stats_hash_domain(hash, ring_size);
	if (shard_num != NULL) {
		*shard_num = index;
	}
	return ring->backends->data[index];

}

void* hashring_choose(struct hashring *ring,
		const char *key,
		uint32_t *shard_num) {
	hashring_hash_t hash = hashring_hash(key);
	return hashring_choose_fromhash(ring, hash, shard_num);
}

void hashring_dealloc(struct hashring *ring) {
	if (ring == NULL) {
		return;
	}
	if (ring->backends == NULL) {
		return;
	}
	const size_t ring_size = ring->backends->size;
	for (size_t i = 0; i < ring_size; i++) {
		bool need_dealloc = true;
		for (size_t j = 0; j < i; j++) {
			if (ring->backends->data[i] == ring->backends->data[j]) {
				need_dealloc = false;
				break;
			}
		}
		if (need_dealloc) {
			ring->dealloc(ring->backends->data[i]);
		}
	}
	statsrelay_list_destroy(ring->backends);
	free(ring);
}
