#include "vector.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


bool vector_add(list_t clients, int at_index, void *obj) {
    if (obj == NULL) {
        stats_error_log("malformed object passed in, NULL pointer");
        goto add_err;
    }

    if (at_index > clients->size) {
        vector_pad(clients, clients->size, at_index);
    }
    // allocate memory to save the actual object
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

bool vector_pad(list_t list, int start_index, int pad_left) {
    int i = start_index;
    if (!list->allocated_size) {
        // add some padding to facilitate
        // constant time lookups
        i = 0;
    }
    for (int p = start_index; p < pad_left; p++) {
        if (statsrelay_list_expand(list) == NULL) {
            stats_error_log("tcplistener_accept_callback: failed to expand list");
            goto add_err;
        }
        list->data[list->size - 1] = NULL;
    }
    return true;

add_err:
    return false;
}

void vector_dump(list_t list, void (*cb)(int, void *)) {
    void *obj;
    for (int i = 0; i < list->size; i++) {
        obj = list->data[i];
        cb(i, obj);
    }
}