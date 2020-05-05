#include <math.h>
#include <pcre.h>
#include <string.h>
#include "log.h"
#include "validate.h"

static const char * const valid_stat_types[7] = {
    "c",
    "ms",
    "k",
    "g",
    "G",
    "h",
    "s"
};
static const int valid_stat_types_len = 6;

static const char * const reserved_tag_names[10] = {
        "asg",
        "az",
        "backend",
        "canary",
        "host",
        "period",
        "region",
        "shard",
        "source",
        "window"
};
static const int reserved_tag_names_len = 10;

extern void *memrchr(const void*, int, size_t);

static metric_type parse_stat_type(const char *str, size_t len) {
    if (1 <= len && len <= 2) {
        for (int i = 0; i < valid_stat_types_len; i++) {
            const char *stat = valid_stat_types[i];
            // len is bounded, so help the the compiler optimize
            if ((len == 1 ? memcmp(stat, str, 1) : memcmp(stat, str, 2)) == 0) {
                return (metric_type)i;
            }
        }
    }
    return METRIC_UNKNOWN;
}

int validate_statsd(const char* line, size_t len, validate_parsed_result_t* result, filter_t* point_tag_validator,
                    bool validate_point_tags) {

    int* ovector, offset, rc;
    bool reserved_tagname = false;

    const char *start = line;
    size_t plen = len;

    // Search backwards, otherwise might eat up irrelevant data.
    // Example: keyname.__tagname=tag:value:42.0|ms
    //                                      ^^^^--- actual value
    const char *end = memrchr(start, ':', plen);
    if (end == NULL) {
        stats_log("validate: Invalid line \"%.*s\" missing ':'", len, line);
        return 1;
    }
    if ((end - start) < 1) {
        stats_log("validate: Invalid line \"%.*s\" zero length key", len, line);
        return 1;
    }
    start = end + 1;
    plen = len - (start - line);

    char *err;
    result->value = strtod(start, &err);
    if (result->value == 0 && err == start) {
        stats_log("validate: Invalid line \"%.*s\" unable to parse value as double", len, line);
        return 1;
    }
    if (isnan(result->value) || isinf(result->value)) {
        stats_log("validate: Invalid value \"%.*s\" supplied (NaN or INF)", len, line);
        return 1;
    }

    end = memchr(start, '|', plen);
    if (end == NULL) {
        stats_log("validate: Invalid line \"%.*s\" missing '|'", len, line);
        return 1;
    }
    start = end + 1;
    plen = len - (start - line);

    end = memchr(start, '|', plen);
    if (end != NULL) {
        plen = end - start;
    }

    result->type = parse_stat_type(start, plen);
    if (result->type < 0) {
        stats_log("validate: Invalid line \"%.*s\" unknown stat type \"%.*s\"", len, line, plen, start);
        return 1;
    }

    result->presampling_value = 1.0; /* Default pre-sampling to 1.0 */

    if (end != NULL) {
        // end[0] is currently the second | char
        // test if we have at least 1 char following it (@)
        if ((len - (end - line) > 1) && (end[1] == '@')) {
            start = end + 2;
            plen = len - (start - line);
            if (plen == 0) {
                stats_log("validate: Invalid line \"%.*s\" @ sample with no rate", len, line);
                return 1;
            }
            result->presampling_value = strtod(start, &err);
            if ((result->presampling_value == 0.0) && err == start) {
                stats_log("validate: Invalid line \"%.*s\" invalid sample rate", len, line);
                return 1;
            }
        } else {
            stats_log("validate: Invalid line \"%.*s\" no @ sample rate specifier", len, line);
            return 1;
        }
    }

    offset = 0;
    ovector = (int *)malloc(sizeof(int) * OVECCOUNT);
    while (offset < len && (rc = filter_exec(point_tag_validator, line, len, &ovector, &offset)) >= 0) {
        if (rc > 0) {
            for (int i = 0; i < reserved_tag_names_len; i++) {
                if (!strncasecmp(reserved_tag_names[i], line + ovector[2], ovector[3] - ovector[2])) {
                    stats_log("validate: Invalid line \"%.*s\" usage of reserved tag %s", len, line,
                              reserved_tag_names[i]);
                    reserved_tagname = true;
                }
            }
        }
        offset = ovector[1];
    }
    free(ovector);

    if (reserved_tagname && validate_point_tags) {
        return 1;
    }

    return 0;
}
