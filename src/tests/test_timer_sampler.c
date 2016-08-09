#undef NDEBUG

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../sampling.h"
#include "../log.h"
#include "../validate.h"

const char* t1 = "differing_geohash_query:77923.200000|ms";
const char* t1n = "differing_geohash_query";

const char* t2 = "envoy.downstream_cx_length_ms:72|ms";
const char* t2n = "envoy.downstream_cx_length_ms";

static void print_callback(void* data, const char* key, const char* line, int len) {
	char* expect = (char*)data;
	stats_log(" Expect: %s Got: %s \n", expect, line);
	assert(strcmp(line, expect) == 0);
}

int main(int argc, char** argv) {
	validate_parsed_result_t t1_res, t2_res;
	validate_statsd(t1, strlen(t1), &t1_res);
	assert(t1_res.type == METRIC_TIMER);
	validate_statsd(t2, strlen(t2), &t2_res);
	assert(t2_res.type == METRIC_TIMER);

	sampler_t *sampler = NULL;
	sampler_init(&sampler, 10, 10);
	assert(sampler != NULL);

	int r = sampler_consider_timer(sampler, t1n, &t1_res);
	assert(r == SAMPLER_NOT_SAMPLING);

	/* Load a large number of samples into the sampler */
	for (int i = 0; i < 9; i++) {
		assert(sampler_consider_timer(sampler, t1n, &t1_res) == SAMPLER_NOT_SAMPLING);
	}

	/* This 10th update should be sampled */
	assert(sampler_consider_timer(sampler, t1n, &t1_res) == SAMPLER_SAMPLING);

	/* Trigger the time-based flush of the sampler */
	sampler_update_flags(sampler);

	/* Feed another value, make sure we are now in sampling mode */
	assert(sampler_consider_timer(sampler, t1n, &t1_res) == SAMPLER_SAMPLING);

	/* Feed t2, to check that its not sampled */
	assert(sampler_consider_metric(sampler, t2n, &t2_res) == SAMPLER_NOT_SAMPLING);

	/* Feed another value, make sure we are now in sampling mode */
	assert(sampler_consider_timer(sampler, t1n, &t1_res) == SAMPLER_SAMPLING);

	sampler_flush(sampler, print_callback, "differing_geohash_query:77923.2|ms@1\n");

	/* This update should not sampled */
	assert(sampler_consider_timer(sampler, t1n, &t1_res) == SAMPLER_NOT_SAMPLING);

	sampler_flush(sampler, print_callback, "should not match\n");

	//  Load a large number of samples into the sampler 
	for (int i = 0; i < 10; i++) {
		assert(sampler_consider_timer(sampler, t1n, &t1_res) == SAMPLER_NOT_SAMPLING);
	}

	/* Load a large number of new sampled samples into the sampler */
	for (int i = 0; i < 10000; i++) {
		assert(sampler_consider_timer(sampler, t1n, &t1_res) == SAMPLER_SAMPLING);
	}

	sampler_flush(sampler, print_callback, "differing_geohash_query:77923.2|ms@0.001\n");

	/* foo should still be sampling (it does so across two periods) - lets check */
	assert(sampler_is_sampling(sampler, t1n, METRIC_TIMER) == SAMPLER_SAMPLING);

	for (int i = 0; i < 10; i++) {
		assert(sampler_consider_timer(sampler, t2n, &t2_res) == SAMPLER_NOT_SAMPLING);
	}

	/* Load a large number of new sampled samples into the sampler */
	for (int i = 0; i < 10000; i++) {
		assert(sampler_consider_timer(sampler, t2n, &t2_res) == SAMPLER_SAMPLING);
	}

	sampler_flush(sampler, print_callback, "envoy.downstream_cx_length_ms:72|ms@0.001\n");

	/* foo should now not be sampling */
	assert(sampler_is_sampling(sampler, t1n, METRIC_TIMER) == SAMPLER_NOT_SAMPLING);

	return 0;
}
