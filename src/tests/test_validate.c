#undef NDEBUG

#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include "../log.h"
#include "../filter.h"
#include "../validate.h"

static void test_line_not_modified(filter_t* filter) {
    const char *lines[] = {
        "a.b.c.__tag1=v1.__tag2=v2:v2:42.000|ms",
        "test.srv.req:2.5|ms|@0.2",
    };
    const int lines_len = sizeof(lines) / sizeof(char *);

    for (int i = 0; i < lines_len; i++) {
        const char *line = lines[i];
        const char *copy = strdup(line);

        validate_parsed_result_t result;
        assert(validate_statsd(line, strlen(line), &result, filter, true) == 0);
        assert(strcmp(line, copy) == 0);
    }
}

static void test_parse_presampling_value(filter_t* filter) {
    validate_parsed_result_t result;

    static const char* exp1 = "test.srv.req:2.5|ms|@0.2";

    assert(0 == validate_statsd(exp1, strlen(exp1), &result, filter, true));
    assert(2.5 == result.value);
    assert(0.2 == result.presampling_value);
    assert(METRIC_TIMER == result.type);
}

typedef struct {
    const char  *stat;
    metric_type expected;
} stat_type_test;

static const stat_type_test stat_type_tests[] = {
    {"c", METRIC_COUNTER},
    {"ms", METRIC_TIMER},
    {"kv", METRIC_KV},
    {"g", METRIC_GAUGE},
    {"h", METRIC_HIST},
    {"s", METRIC_S},
    {"", METRIC_UNKNOWN},
    {"a", METRIC_UNKNOWN},
    {"b", METRIC_UNKNOWN},
    {"abc", METRIC_UNKNOWN},
    {" ", METRIC_UNKNOWN},
    {"ss", METRIC_UNKNOWN},
    {"m", METRIC_UNKNOWN},
    {"cc", METRIC_UNKNOWN},
};
static const int stat_type_tests_len = sizeof(stat_type_test) / sizeof(stat_type_tests[0]);

static void test_parse_stat_type(filter_t* filter) {
    char buf[256];
    char *line = &buf[0];

    validate_parsed_result_t result;
    stat_type_test test;
    int ret;

    for (int i = 0; i < stat_type_tests_len; i++) {
        test = stat_type_tests[i];
        assert(snprintf(line, sizeof(buf), "a.b.c:3|%s", test.stat) > 0);
        ret = validate_statsd(line, strlen(line), &result, filter, true);
        if (test.expected == METRIC_UNKNOWN) {
            assert(ret != 0); // invalid
        } else {
            assert(ret == 0); // valid
        }
        assert(result.type == test.expected);
    }
}

int main(int argc, char **argv) {
    stats_log_verbose(1);

    filter_t* filter;

    validate_parsed_result_t result;
    char *point_tags_regex = "\\.__([a-zA-Z][a-zA-Z0-9_]+)=[^.]+";

    int res = filter_re_create(&filter, point_tags_regex);
    assert(res == 0);

    // valid metric with tag filter enabled
    static const char *exp1 = "a.b.c.__tag1=v1.__tag2=v2.count:42.000|ms";

    assert(0 == validate_statsd(exp1, strlen(exp1), &result, filter, true));
    assert(42.0 == result.value);
    assert(METRIC_TIMER == result.type);

    // metric with reserved tag name usage and filter enabled
    static const char *exp2 = "a.b.c.__asg=v1.__asg=v2.count:42.000|ms";

    assert(1 == validate_statsd(exp2, strlen(exp2), &result, filter, true));

    // metric with reserved tag name and filter disabled
    static const char *exp3 = "a.b.c.__region=iad.__host=xyz.count:45.000|c";

    assert(0 == validate_statsd(exp3, strlen(exp3), &result, filter, false));
    assert(45.0 == result.value);
    assert(METRIC_COUNTER == result.type);

    // metric with no parsable value
    static const char *bad1 = "presto.task_executor.processor_executor.shutdown.__label=presto_beta:False|g";
    assert(1 == validate_statsd(bad1, strlen(bad1), &result, filter, true));

    // metric with no parsable value
    static const char *bad2 = "presto.query_manager.executor.allow_core_thread_time_out.__label=presto_beta:False|g";
    assert(1 == validate_statsd(bad2, strlen(bad2), &result, filter, true));

    // metric with no parsable value
    static const char *bad3 = "streamingspeeds.operator.Map.latency.__tm_id=6b782113819fb527808d388ac5ae308c.__job_id=99f7d76616cc3525f6e9ad6bae8a5163.__task_id=cbc357ccb763df2852fee8c4fc7d55f2.__subtask_index=7.__operator=Map:{}|g";
    assert(1 == validate_statsd(bad3, strlen(bad3), &result, filter, true));

    // metric with valid tags and filter enabled
    static const char *bad4 = "streamingspeeds.operator.Map.latency.__tm_id=6b782113819fb527808d388ac5ae308c.__job_id=99f7d76616cc3525f6e9ad6bae8a5163.__task_id=cbc357ccb763df2852fee8c4fc7d55f2.__subtask_index=7.__operator=Map:34.0|g";
    assert(0 == validate_statsd(bad4, strlen(bad4), &result, filter, true));
    assert(34.0 == result.value);
    assert(METRIC_GAUGE == result.type);

    // metric with valid tags and filter enabled
    static const char *good1 = "a.b.c.__tag1=v1.__tag2=v2:v2:42.000|ms";
    assert(0 == validate_statsd(good1, strlen(good1), &result, filter, true));
    assert(42.0 == result.value);
    assert(METRIC_TIMER == result.type);

    // metric with valid tags and filter enabled
    static const char *bad5 = "a.b.c.__tag1=v1.__tag2=v2:v2:NAN|ms";
    assert(1 == validate_statsd(bad5, strlen(bad5), &result, filter, true));

    // metric set type
    static const char *setType = "a.b.c:12|s";
    assert(0 == validate_statsd(setType, strlen(setType), &result, filter, true));
    assert(METRIC_S == result.type);

    // metric direct gauge type
    static const char *dgType = "a.b.c:13|G";
    assert(0 == validate_statsd(dgType, strlen(dgType), &result, filter, true));
    assert(METRIC_GAUGEDIRECT == result.type);

    // additional tests
    test_line_not_modified(filter);
    test_parse_presampling_value(filter);
    test_parse_stat_type(filter);

    // free the filter
    // to only log warning and not reject the line
    filter_free(filter);
}
