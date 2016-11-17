#ifndef STATSRELAY_SAMPLING_H
#define STATSRELAY_SAMPLING_H

#include <stdbool.h>
#include <ev.h>
#include "protocol.h"
#include "hashmap.h"
#include "validate.h"

typedef struct sampler sampler_t;

typedef enum {
	SAMPLER_NOT_SAMPLING = 0,
	SAMPLER_SAMPLING = 1
} sampling_result;

typedef void(sampler_flush_cb)(void* data, const char* key, const char* line, int len);

void expiry_callback_handler(struct ev_loop* , struct ev_timer* , int);

int sampler_init(sampler_t** sampler, int threshold, int window, int reservoir_size,
	int hm_expiry_frequency, int hm_ttl);

/**
 * Registered callback for hashmap stale key expiry.
 */
static int expiry_callback(void* _s, const char* key, void* _value, void * metadata);


/**
 * Consider a statsd counter for sampling - based on its name and validation
 * parsed result which includes its data object.
 */
sampling_result sampler_consider_counter(sampler_t* sampler, const char* name, validate_parsed_result_t*);

/**
 * Consider a statsd timer for sampling - based on its name and validation
 * parsed result with includes its data object. Uses reservoir of size 'k'
 */
sampling_result sampler_consider_timer(sampler_t* sampler, const char* name, validate_parsed_result_t*);

/**
 * Walk through all keys in the sampler and update the sampling or not sampling flag,
 * but do not flush any data.
 */
void sampler_update_flags(sampler_t* sampler);

/**
 * Walk through all keys in the sampler and invoke cb() on them, passing that callback
 * the data pointer.
 */
void sampler_flush(sampler_t* sampler, sampler_flush_cb cb, void* data);

/*
 * Introspect if a key (of any type) is in sampling mode or not.
 * This is mainly used for unit tests
 */
sampling_result sampler_is_sampling(sampler_t* sampler, const char* name, metric_type type);

/*
 * Get the sampling window
 */
int sampler_window(sampler_t* sampler);

/*
 * Get the sampling threshold
 */
int sampler_threshold(sampler_t* sampler);

/**
 * Destroy the sampler
 */
void sampler_destroy(sampler_t* sampler);

/**
 * Return if the hashmap expiry timer is active
 */
bool is_expiry_watcher_active(sampler_t *sampler);

/**
 * Return if the hashmap expiry timer is pending
 */
bool is_expiry_watcher_pending(sampler_t *sampler);

/**
 * Return the frequency of expiration timer (default -1)
 */
int sampler_expiration_timer_frequency(sampler_t  *sampler);

#endif //STATSRELAY_SAMPLING_H
