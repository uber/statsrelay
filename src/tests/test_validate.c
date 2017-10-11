#undef NDEBUG

#include <assert.h>
#include <string.h>

#include "../log.h"
#include "../validate.h"


int main(int argc, char **argv) {
    stats_log_verbose(1);

    validate_parsed_result_t result;

    static const char* exp1 = "a.b.c.__tag1=v1.__tag2=v2:v2:42.000|ms";

    assert(0 == validate_statsd(exp1, strlen(exp1), &result));
    assert(42.0 == result.value);
    assert(METRIC_TIMER == result.type);

    return 0;
}
