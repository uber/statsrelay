#include <stdio.h>
#include <stdlib.h>

#include "vector.h"

vector_t* vector_init(vector_t *v, unsigned int init_size) {
	v->capacity = init_size;
	v->size_factor = init_size;
	v->count = 0;
	v->items = malloc(sizeof(void *) * v->capacity);
	return v;
}

int vector_count(vector_t *v) {
	return v->count;
}

int vector_capacity(vector_t *v) {
	return v->capacity;
}

static void vector_resize(vector_t *v, unsigned int capacity) {
	stats_debug_log("vector_resize: %d to %d", v->capacity, capacity);

	void **items = realloc(v->items, sizeof(void *) * capacity);
	if (items) {
		v->items = items;
		v->capacity = capacity;
	} else {
        perror("realloc() failure");
	}
}

void vector_insert(vector_t *v, void *item) {
	if (v->capacity == v->count) {
		vector_resize(v, v->capacity * 2);
	}

	v->items[v->count++] = item;
}

void *vector_fetch(vector_t *v, unsigned int index) {
	if (index >= 0 && index < v->count) {
		return v->items[index];
	}
	return NULL;
}

void vector_delete_at(vector_t *v, unsigned int index) {
	if (index < 0 || index >= v->count)
		return;

	v->items[index] = NULL;

	for (int i = index; i < v->count - 1; i++) {
		v->items[i] = v->items[i + 1];
		v->items[i + 1] = NULL;
	}

	v->count--;
	stats_debug_log("vector_delete: total items %d", v->count);

	if (v->count > 0 && v->count == v->capacity / v->size_factor) {
		vector_resize(v, v->capacity / 2);
	}
}

void vector_free(vector_t *v) {
	free(v->items);
	free(v);
}

void vector_free_all(vector_t *v) {
	for (unsigned int i = 0; i < v->count; i++) {
		free(v->items[i]);
	}
}
