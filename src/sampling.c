#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "sampling.h"
#include "hashmap.h"
#include "validate.h"
#include "stats.h"

#define HM_SIZE 32768

struct sampler {
	int threshold;
	int window;
	int reservoir_size;
	struct drand48_data randbuf;
	hashmap *map;
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
	 * Maintain a reservoir of 'threshold' timer values
	 */
	double reservoir[];
};

int sampler_init(sampler_t** sampler, int threshold, int window, int reservoir_size) {
	struct sampler *sam = calloc(1, sizeof(struct sampler));

	hashmap_init(HM_SIZE, &sam->map);

	sam->threshold = threshold;
	sam->window = window;
	sam->reservoir_size = reservoir_size;
	srand48_r(time(NULL), &sam->randbuf);

	*sampler = sam;
	return 0;
}

static int sampler_update_callback(void* _s, const char* key, void* _value) {
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

static int sampler_flush_callback(void* _s, const char* key, void* _value) {
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
	sampler_update_callback(flush_data->sampler, key, _value);
	return 0;
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
		bucket->sum = 0;
		bucket->count = 0;

		for (int k = 0; k < sampler_threshold(sampler); k++) {
			bucket->reservoir[k] = NAN;
		}
		bucket->last_window_count += 1;
		hashmap_put(sampler->map, name, (void*)bucket);
	} else {
		bucket->last_window_count++;

		/* Circuit break and enable sampling mode */
		if (!bucket->sampling && bucket->last_window_count > sampler->threshold) {
			stats_debug_log("started timer sampling '%s'", name);
			bucket->sampling = true;
		}

		if (bucket->sampling) {
			double value = parsed->value;

			if (bucket->reservoir_index < sampler_threshold(sampler)) {
				bucket->reservoir[bucket->reservoir_index++] = value;
			} else {
				long int i, k;
				lrand48_r(&sampler->randbuf, &i);

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

sampling_result sampler_consider_metric(sampler_t* sampler, const char* name, validate_parsed_result_t* parsed) {
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
		hashmap_put(sampler->map, name, (void*)bucket);
	} else {
		bucket->last_window_count++;

		/* Circuit break and enable sampling mode */
		if (!bucket->sampling && bucket->last_window_count > sampler->threshold) {
			stats_debug_log("started sampling '%s'", name);
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
	hashmap_destroy(sampler->map);
}
