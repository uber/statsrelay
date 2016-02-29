#ifndef VECTOR_H
#define VECTOR_H

#include "log.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct vector {
	int capacity;
	int size_factor;
	int count;
	void **items;
} vector_t;

// Initialize the vector of given size
vector_t* vector_init(vector_t *, int);

// Returns the total number
// of elements in vector
int vector_count(vector_t *);

// Returns the current capacity
// of the vector
int vector_capacity(vector_t *);

// Adds an element to the vector
// (mallocs as per need, hence a vector)
void vector_insert(vector_t *, void *);

// Fetches an item at given index
// return NULL if the index is out of bounds
void *vector_fetch(vector_t *, int);

// Delete the item in the vector
// at the given index.
void vector_delete_at(vector_t *, int);

// Free
void vector_free(vector_t *);

#endif
