#ifndef STATSRELAY_HASHRING_H
#define STATSRELAY_HASHRING_H

#include <stddef.h>

typedef void* (*hashring_alloc_func)(const char *);
typedef void (*hashring_dealloc_func)(void *);

// Initialize the hashring with the list of backends.
int hashring_init(const char *hashfile,
		  size_t expected_size,
		  hashring_alloc_func alloc_func);

// Choose a backend
void *hashring_choose(const char *key, size_t len);

// Release allocated memory
void hashring_dealloc(hashring_dealloc_func dealloc_func);

#endif // STATSRELAY_HASHRING_H
