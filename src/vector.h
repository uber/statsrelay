#ifndef VECTOR_H
#define VECTOR_H

#include "./list.h"
#include "./log.h"

#include <stdint.h>
#include <stdbool.h>

// Adds an element to the vector
// (mallocs as per need, hence a vector)
bool vector_add(list_t, void *);

// Fetches an item at given index
// return NULL if the index is out of bounds
void *vector_fetch(list_t, int);

// Delete the item in the vector
bool vector_remove(list_t, int);

// return the size of the list
size_t vector_size(list_t);

// pad items to the left
bool vector_pad(list_t, int );

// debug routine
void vector_dump(list_t);

#endif
