#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "sampling.h"
#include "hashmap.h"
#include "stats.h"

#define HM_SIZE 32768

typedef struct expiring_entry {
	int hm_expiry_frequency;
	int hm_ttl;
	ev_timer map_expiry_timer; //timer to clear out expired elements from map
} expiring_entry_t;

struct sampler {
	int threshold;
	int window;
	int reservoir_size;
#ifdef __APPLE__
	unsigned short randbuf[3];
#else
	struct drand48_data randbuf;
#endif
	hashmap *map;
	expiring_entry_t base;
};

struct sampler_flush_data {
	sampler_t* sampler;
	void* data;
	sampler_flush_cb *cb;
};

struct sample_bucket {
	bool sampling;
	/**
	 * A record of the number of events received
	 */
	uint64_t last_window_count;

	/**
	 * Unix timestamp of bucket last modification
	 */
	time_t last_modified_at;

	/**
	 * Accumulated sum
	 */
	double sum;

	/**
	 * Accumulated count (which may differ from last_window_count due to sampling)
	 */
	uint64_t count;

	/**
	 * Metric type (COUNTER, TIMER, GAUGE etc.)
	 */
	metric_type type;

	/**
	 * Index of recent item in the reservoir
	 */
	int reservoir_index;

	/**
	 * Upper value of timer seen in the sampling period
	 */
	double upper;

	/**
	 * Lower value of timer seen in the sampling period
	 */
	double lower;

	/**
	 * retain the incoming pre applied sample rate to relay to statsite
	 * for the current_min
	 */
	double lower_sample_rate;

	/**
	 * retain the incoming pre applied sample rate to relay to statsite
	 * for the current_max
	 */
	double upper_sample_rate;

	/**
	 * Maintain a reservoir of 'threshold' timer values
	 */
	double reservoir[];
};


int sampler_init(sampler_t** sampler, int threshold, int window, int reservoir_size,
				 int hm_expiry_frequency, int hm_ttl) {

	struct sampler *sam = calloc(1, sizeof(struct sampler));

	hashmap_init(HM_SIZE, &sam->map);

	sam->threshold = threshold;
	sam->window = window;
	sam->reservoir_size = reservoir_size;
	sam->base.hm_expiry_frequency = hm_expiry_frequency;
	// save this to pass into hashmap_put
	sam->base.hm_ttl = hm_ttl;

    if (hm_ttl != -1) {
        struct ev_loop *loop = ev_default_loop(0);
        ev_timer_init(&sam->base.map_expiry_timer, expiry_callback_handler, sam->base.hm_expiry_frequency, 0);
        sam->base.map_expiry_timer.data = (void*)sam;
        ev_timer_start(loop, &sam->base.map_expiry_timer);
    }

#ifdef __APPLE__
	time_t t = time(NULL);
	sam->randbuf[0] = t & 0xFFFF;
	sam->randbuf[1] = (t >> 16) & 0xFFFF;
	sam->randbuf[2] = (t >> 32) & 0xFFFF;
#else
	srand48_r(time(NULL), &sam->randbuf);
#endif

	*sampler = sam;
	return 0;
}

static time_t timestamp() {
	struct timeval detail_time;
	gettimeofday(&detail_time,NULL);

	return detail_time.tv_sec;
}

static int sampler_update_callback(void* _s, const char* key, void* _value, void *metadata) {
	sampler_t* sampler = (sampler_t*)_s;
	struct sample_bucket* bucket = (struct sample_bucket*)_value;

	if (bucket->last_window_count > sampler->threshold) {
		bucket->sampling = true;
	} else if (bucket->sampling && bucket->last_window_count <= sampler->threshold) {
		bucket->sampling = false;
		bucket->reservoir_index = 0;
		stats_debug_log("stopped %s sampling '%s'",
				  bucket->type == METRIC_COUNTER ? "counter": "timer",
				  key);
	}

	bucket->last_window_count = 0;
	return 0;
}

static int expiry_callback(void* _s, const char* key, void* _value, void *metadata) {
	sampler_t* sampler = (sampler_t*)_s;
	struct sample_bucket* bucket = (struct sample_bucket*)_value;
	int* ttl = (int*)metadata;

	// simply return if the bucket is being sampled!
	if (bucket->sampling) return 0;

	time_t now = timestamp();

	if ((now - bucket->last_modified_at) > (*ttl)) {
		stats_debug_log("deleting key %s", key);
		hashmap_delete(sampler->map, key);
	}

	return 0;
}

static int sampler_flush_callback(void* _s, const char* key, void* _value, void* metadata) {
	struct sampler_flush_data* flush_data = (struct sampler_flush_data*)_s;
	struct sample_bucket* bucket = (struct sample_bucket*)_value;

	if (!bucket->sampling || bucket->count == 0) goto exit;
	char line_buffer[MAX_UDP_LENGTH];
	int len;
	line_buffer[0] = '\0';

	if (bucket->type == METRIC_COUNTER) {
		len = sprintf(line_buffer, "%s:%g|c@%g\n", key, bucket->sum / bucket->count, 1.0 / bucket->count);
		len -= 1; /* \n is not part of the length */
		flush_data->cb(flush_data->data, key, line_buffer, len);
	} else if (bucket->type == METRIC_TIMER) {
		int num_samples = 0;
		for (int j = 0; j < flush_data->sampler->threshold; j++) {
			if (!isnan(bucket->reservoir[j])) {
				num_samples++;
			}
		}

		// Flush the max and min for the well-being of timer.upper and timer.lower respectively
		if (bucket->upper > DBL_MIN) {
			len = sprintf(line_buffer, "%s:%g|ms@%g\n", key, bucket->upper, bucket->upper_sample_rate);
			len -= 1;
			flush_data->cb(flush_data->data, key, line_buffer, len);
			bucket->upper = DBL_MIN;
		}

		if (bucket->lower < DBL_MAX) {
			len = sprintf(line_buffer, "%s:%g|ms@%g\n", key, bucket->lower, bucket->lower_sample_rate);
			len -= 1;
			flush_data->cb(flush_data->data, key, line_buffer, len);
			bucket->lower = DBL_MAX;
		}

		double sample_rate = (double)(1.0 * num_samples) / bucket->count;
		for (int j = 0; j < flush_data->sampler->threshold; j++) {
			if (!isnan(bucket->reservoir[j])) {
				len = sprintf(line_buffer, "%s:%g|ms@%g\n", key, bucket->reservoir[j], sample_rate);
				len -= 1;
				flush_data->cb(flush_data->data, key, line_buffer, len);
				bucket->reservoir[j] = NAN;
			}
		}
	}
	bucket->count = 0;
	bucket->sum = 0;

	exit:
	/* Also call update */
	sampler_update_callback(flush_data->sampler, key, _value, metadata);
	return 0;
}

void expiry_callback_handler(struct ev_loop *loop, struct ev_timer *timer, int events) {
	sampler_t* sampler = (sampler_t*)timer->data;

	// Iterate over items and callback.
	hashmap_iter(sampler->map, expiry_callback, (void *)timer->data);
	ev_timer_set(&sampler->base.map_expiry_timer, sampler->base.hm_expiry_frequency, 0.0);
	ev_timer_start(loop, &sampler->base.map_expiry_timer);
}

void sampler_flush(sampler_t* sampler, sampler_flush_cb cb, void* data) {
	struct sampler_flush_data fd = {
		.data = data,
		.sampler = sampler,
		.cb = cb
	};
	hashmap_iter(sampler->map, sampler_flush_callback, (void*)&fd);
}

sampling_result sampler_is_sampling(sampler_t* sampler, const char* name, metric_type type) {
	struct sample_bucket* bucket = NULL;

	hashmap_get(sampler->map, name, (void**)&bucket);

	if (bucket == NULL) {
		return SAMPLER_NOT_SAMPLING;
	} else {
		if (bucket->sampling)
			return SAMPLER_SAMPLING;
		else
			return SAMPLER_NOT_SAMPLING;
	}
}

void sampler_update_flags(sampler_t* sampler) {
	hashmap_iter(sampler->map, sampler_update_callback, (void*)sampler);
}

sampling_result sampler_consider_timer(sampler_t* sampler, const char* name, validate_parsed_result_t* parsed) {
	// safety check, also checked for in stats.c
	if (parsed->type != METRIC_TIMER) {
		return SAMPLER_NOT_SAMPLING;
	}

	struct sample_bucket* bucket = NULL;
	hashmap_get(sampler->map, name, (void**)&bucket);
	if (bucket == NULL) {
		/* Intialize a new bucket */
		bucket = malloc(sizeof(struct sample_bucket) + (sizeof(double) * sampler->reservoir_size));
		bucket->sampling = false;
		bucket->reservoir_index = 0;
		bucket->last_window_count = 0;
		bucket->type = parsed->type;
		bucket->upper = DBL_MIN;
		bucket->lower = DBL_MAX;
		bucket->sum = 0;
		bucket->count = 0;
		bucket->last_modified_at = timestamp();

		for (int k = 0; k < sampler_threshold(sampler); k++) {
			bucket->reservoir[k] = NAN;
		}
		bucket->last_window_count += 1;
		hashmap_put(sampler->map, name, (void*)bucket, (void*)&sampler->base.hm_ttl);
	} else {
		bucket->last_window_count++;

		/* Circuit break and enable sampling mode */
		if (!bucket->sampling && bucket->last_window_count > sampler->threshold) {
			stats_debug_log("started timer sampling '%s'", name);
			bucket->sampling = true;
		}

		if (bucket->sampling) {
			double value = parsed->value;
			bucket->last_modified_at = timestamp();

			/**
			 * update the upper and lower
			 * timer values.
			 */
			if (value > bucket->upper) {
				// keep the sampling rate in sync with the value
				bucket->upper_sample_rate = parsed->presampling_value;

				if (bucket->upper != DBL_MIN) {
					// add previous_max to reservoir
					// update current_max
					double old_max = bucket->upper;
					bucket->upper = value;
					value = old_max;
				} else {
					// dont include it in the reservoir
					bucket->upper = value;
					return SAMPLER_SAMPLING;
				}
			}

			if (value < bucket->lower) {
				// keep the sampling rate in sync with the value
				bucket->lower_sample_rate = parsed->presampling_value;

				if (bucket->lower != DBL_MAX) {
					// add previous_min to reservoir
					// update current_min
					double old_min = bucket->lower;
					bucket->lower = value;
					value = old_min;
				} else {
					// dont include it in the reservoir
					bucket->lower = value;
					return SAMPLER_SAMPLING;
				}
			}

			if (bucket->reservoir_index < sampler_threshold(sampler)) {
				bucket->reservoir[bucket->reservoir_index++] = value;
			} else {
				long int i, k;
#ifdef __APPLE__
				i = nrand48(sampler->randbuf);
#else
				lrand48_r(&sampler->randbuf, &i);
#endif
				k = i % (bucket->last_window_count);

				if (k < sampler_threshold(sampler)) {
					bucket->reservoir[k] = value;
				}
			}

			double count = 1.0;
			if (parsed->presampling_value > 0.0 && parsed->presampling_value < 1.0) {
				count = 1 * (1.0 / parsed->presampling_value);
			}

			bucket->sum += value;
			bucket->count += count;
			return SAMPLER_SAMPLING;
		}
	}

	return SAMPLER_NOT_SAMPLING;
}

sampling_result sampler_consider_counter(sampler_t* sampler, const char* name, validate_parsed_result_t* parsed) {
	// safety check, also checked for in stats.c
	if (parsed->type != METRIC_COUNTER) {
		return SAMPLER_NOT_SAMPLING;
	}

	struct sample_bucket* bucket = NULL;
	hashmap_get(sampler->map, name, (void**)&bucket);
	if (bucket == NULL) {
		/* Intialize a new bucket */
		bucket = malloc(sizeof(struct sample_bucket));
		bucket->sampling = false;
		bucket->last_window_count = 1;
		bucket->type = parsed->type;
		bucket->sum = 0;
		bucket->count = 0;
		bucket->last_modified_at = timestamp();
		hashmap_put(sampler->map, name, (void*)bucket, NULL);
	} else {
		bucket->last_window_count++;

		/* Circuit break and enable sampling mode */
		if (!bucket->sampling && bucket->last_window_count > sampler->threshold) {
			stats_debug_log("started counter sampling '%s'", name);
			bucket->sampling = true;
		}

		if (bucket->sampling) {
			double value = parsed->value;
			double count = 1.0;
			if (parsed->presampling_value > 0.0 && parsed->presampling_value < 1.0) {
				value = value * (1.0 / parsed->presampling_value);
				count = 1 * (1.0 / parsed->presampling_value);
			}
			bucket->sum += value;
			bucket->count += count;

			bucket->last_modified_at = timestamp();
			return SAMPLER_SAMPLING;
		}
	}
	return SAMPLER_NOT_SAMPLING;
}

int sampler_window(sampler_t* sampler) {
	return sampler->window;
}

int sampler_threshold(sampler_t* sampler) {
	return sampler->threshold;
}

void sampler_destroy(sampler_t* sampler) {
	if (sampler->base.hm_ttl != -1) {
        stats_debug_log("Stopping passive hashmap expiry timer.");
        struct ev_loop* loop = ev_default_loop(0);
		ev_timer_stop(loop, &sampler->base.map_expiry_timer);
	}
	hashmap_destroy(sampler->map);
}

int sampler_expiration_timer_frequency(sampler_t* sampler) {
	return sampler->base.hm_expiry_frequency;
}

bool is_expiry_watcher_active(sampler_t* sampler) {
	return sampler->base.hm_expiry_frequency != -1 ? ev_is_active(&sampler->base.map_expiry_timer) : 0;
}

bool is_expiry_watcher_pending(sampler_t* sampler) {
	return sampler->base.hm_expiry_frequency != -1 ? ev_is_pending(&sampler->base.map_expiry_timer) : 0;
}
