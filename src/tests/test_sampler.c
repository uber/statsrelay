#undef NDEBUG

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../sampling.h"
#include "../validate.h"


const char* c1 = "foo:1|c";
const char* c1n = "foo";

const char* c2 = "bar:2|c";
const char* c2n = "bar";

static void print_callback(void* data, const char* key, const char* line, int len) {
    char* expect = (char*)data;
    printf(" Expect: %s Got: %s \n", expect, line);
    assert(strcmp(line, expect) == 0);
}

int main(int argc, char** argv) {

    validate_parsed_result_t c1_res, c2_res;
    validate_statsd(c1, strlen(c1), &c1_res);
    assert(c1_res.type == METRIC_COUNTER);
    validate_statsd(c2, strlen(c2), &c2_res);
    assert(c2_res.type == METRIC_COUNTER);

	sampler_t *sampler = NULL;
	sampler_init(&sampler, 10, 10, 0, -1, -1);
	assert(sampler != NULL);

    // the expiry timer watcher must not be initialized
	assert(is_expiry_watcher_active(sampler) == false);
	assert(is_expiry_watcher_pending(sampler) == false);

	int r = sampler_consider_counter(sampler, c1n, &c1_res);
	assert(r == SAMPLER_NOT_SAMPLING);

    /* Load a large number of samples into the sampler */
    for (int i = 0; i < 9; i++) {
        assert(sampler_consider_counter(sampler, c1n, &c1_res) == SAMPLER_NOT_SAMPLING);
    }

    /* This 10th update should be sampled */
    assert(sampler_consider_counter(sampler, c1n, &c1_res) == SAMPLER_SAMPLING);

    /* Trigger the time-based flush of the sampler */
    sampler_update_flags(sampler);

    /* Feed another value, make sure we are now in sampling mode */
    assert(sampler_consider_counter(sampler, c1n, &c1_res) == SAMPLER_SAMPLING);

    /* Feed c2, to check that its not sampled */
    assert(sampler_consider_counter(sampler, c2n, &c2_res) == SAMPLER_NOT_SAMPLING);

    sampler_flush(sampler, print_callback, "foo:1|c@0.5\n");

    /* This update should not sampled */
    assert(sampler_consider_counter(sampler, c1n, &c1_res) == SAMPLER_NOT_SAMPLING);

    sampler_flush(sampler, print_callback, "should not match\n");

    /* Load a large number of samples into the sampler */
    for (int i = 0; i < 10; i++) {
        assert(sampler_consider_counter(sampler, c1n, &c1_res) == SAMPLER_NOT_SAMPLING);
    }

    /* Load a large number of new sampled samples into the sampler */
    for (int i = 0; i < 10000; i++) {
        assert(sampler_consider_counter(sampler, c1n, &c1_res) == SAMPLER_SAMPLING);
    }

    sampler_flush(sampler, print_callback, "foo:1|c@0.0001\n");

    /* foo should still be sampling (it does so across two periods) - lets check */
    assert(sampler_is_sampling(sampler, c1n, METRIC_COUNTER) == SAMPLER_SAMPLING);

    /* Check with a counter thats not just 1 */
    for (int i = 0; i < 10; i++) {
        assert(sampler_consider_counter(sampler, c2n, &c2_res) == SAMPLER_NOT_SAMPLING);
    }

    /* This 10th update should be sampled */
    assert(sampler_consider_counter(sampler, c2n, &c2_res) == SAMPLER_SAMPLING);

    /* This 11th update should be sampled */
    assert(sampler_consider_counter(sampler, c2n, &c2_res) == SAMPLER_SAMPLING);

    sampler_flush(sampler, print_callback, "bar:2|c@0.5\n");

    /* Load a large number of new sampled samples into the sampler */
    for (int i = 0; i < 10000; i++) {
        assert(sampler_consider_counter(sampler, c2n, &c2_res) == SAMPLER_SAMPLING);
    }

    sampler_flush(sampler, print_callback, "bar:2|c@0.0001\n");

    /* foo should now not be sampling */
    assert(sampler_is_sampling(sampler, c1n, METRIC_COUNTER) == SAMPLER_NOT_SAMPLING);

    return 0;
}
