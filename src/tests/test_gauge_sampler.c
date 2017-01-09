#undef NDEBUG

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../sampling.h"
#include "../validate.h"


const char* g1 = "foo:1|g";
const char* g1n = "foo";

const char* g2 = "bar:2|g";
const char* g2n = "bar";

static void print_callback(void* data, const char* key, const char* line, int len) {
    char* expect = (char*)data;
    printf(" Expect: %s Got: %s \n", expect, line);
    assert(strcmp(line, expect) == 0);
}

int main(int argc, char** argv) {

    validate_parsed_result_t g1_res, g2_res;
    validate_statsd(g1, strlen(g1), &g1_res);
    assert(g1_res.type == METRIC_GAUGE);
    validate_statsd(g2, strlen(g2), &g2_res);
    assert(g2_res.type == METRIC_GAUGE);

    sampler_t *sampler = NULL;
    sampler_init(&sampler, 10, 10, 10, 10, false, -1, -1);
    assert(sampler != NULL);

    // the expiry timer watcher must not be initialized
    assert(is_expiry_watcher_active(sampler) == false);
    assert(is_expiry_watcher_pending(sampler) == false);

    int r = sampler_consider_gauge(sampler, g1n, &g1_res);
    assert(r == SAMPLER_NOT_SAMPLING);

    /* Load a large number of samples into the sampler */
    for (int i = 0; i < 9; i++) {
        assert(sampler_consider_gauge(sampler, g1n, &g1_res) == SAMPLER_NOT_SAMPLING);
    }

    /* This 10th update should be sampled */
    assert(sampler_consider_gauge(sampler, g1n, &g1_res) == SAMPLER_SAMPLING);

    /* Trigger the time-based flush of the sampler */
    sampler_update_flags(sampler);

    /* Feed another value, make sure we are now in sampling mode */
    assert(sampler_consider_gauge(sampler, g1n, &g1_res) == SAMPLER_SAMPLING);

    /* Feed g2, to check that its not sampled */
    assert(sampler_consider_gauge(sampler, g2n, &g2_res) == SAMPLER_NOT_SAMPLING);

    sampler_flush(sampler, print_callback, "foo:1|g\n");

    /* This update should not sampled */
    assert(sampler_consider_gauge(sampler, g1n, &g1_res) == SAMPLER_NOT_SAMPLING);

    sampler_flush(sampler, print_callback, "should not match\n");

    /* Load a large number of samples into the sampler */
    for (int i = 0; i < 10; i++) {
        assert(sampler_consider_gauge(sampler, g1n, &g1_res) == SAMPLER_NOT_SAMPLING);
    }

    /* Load a large number of new sampled samples into the sampler */
    for (int i = 0; i < 10000; i++) {
        assert(sampler_consider_gauge(sampler, g1n, &g1_res) == SAMPLER_SAMPLING);
    }

    sampler_flush(sampler, print_callback, "foo:1|g\n");

    /* foo should still be sampling (it does so across two periods) - lets check */
    assert(sampler_is_sampling(sampler, g1n, METRIC_GAUGE) == SAMPLER_SAMPLING);

    /* Check with a gauge thats not just 1 */
    for (int i = 0; i < 10; i++) {
        assert(sampler_consider_gauge(sampler, g2n, &g2_res) == SAMPLER_NOT_SAMPLING);
    }

    /* This 10th update should be sampled */
    assert(sampler_consider_gauge(sampler, g2n, &g2_res) == SAMPLER_SAMPLING);

    /* This 11th update should be sampled */
    assert(sampler_consider_gauge(sampler, g2n, &g2_res) == SAMPLER_SAMPLING);

    sampler_flush(sampler, print_callback, "bar:2|g\n");

    /* Load a large number of new sampled samples into the sampler */
    for (int i = 0; i < 10000; i++) {
        assert(sampler_consider_gauge(sampler, g2n, &g2_res) == SAMPLER_SAMPLING);
    }

    sampler_flush(sampler, print_callback, "bar:2|g\n");

    /* foo should now not be sampling */
    assert(sampler_is_sampling(sampler, g1n, METRIC_GAUGE) == SAMPLER_NOT_SAMPLING);

    return 0;
}
