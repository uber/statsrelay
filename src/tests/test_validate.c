#undef NDEBUG

#include <assert.h>
#include <string.h>

#include "../log.h"
#include "../filter.h"
#include "../validate.h"


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

    // free the filter
    // to only log warning and not reject the line
    filter_free(filter);

    return 0;
}
