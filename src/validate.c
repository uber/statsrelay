#include <math.h>
#include <pcre.h>
#include <string.h>
#include "log.h"
#include "validate.h"

static const char *valid_stat_types[6] = {
    "c",
    "ms",
    "kv",
    "g",
    "h",
    "s"
};

static const char *reserved_tag_names[10] = {
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
static size_t valid_stat_types_len = 6;
static size_t reserved_tag_names_len = 10;

extern void *memrchr(const void*, int, size_t);


int validate_statsd(const char* line, size_t len, validate_parsed_result_t* result, filter_t* point_tag_validator,
                    bool validate_point_tags) {
    size_t plen;
    char c;
    int* ovector, i, valid, offset, rc;
    bool reserved_tagname = false;

    // FIXME: this is dumb, don't do a memory copy
    char *line_copy = strndup(line, len);
    char *start, *end;
    char *err;

    if (line_copy == NULL) {
        stats_log("validate: allocation failure. marking invalid line");
        goto statsd_err;
    }

    start = line_copy;
    plen = len;
    // Example: keyname.__tagname=tag:value:42.0|ms
    //                                      ^^^^--- actual value  
    end = memrchr(start, ':', plen);
    if (end == NULL) {
        stats_log("validate: Invalid line \"%.*s\" missing ':'", len, line);
        goto statsd_err;
    }

    if ((end - start) < 1) {
        stats_log("validate: Invalid line \"%.*s\" zero length key", len, line);
        goto statsd_err;
    }

    start = end + 1;
    plen = len - (start - line_copy);

    c = end[0];
    end[0] = '\0';
    result->presampling_value = 1.0; /* Default pre-sampling to 1.0 */
    result->value = strtod(start, &err);
    if ((result->value == 0.0) && (err == start)) {
        stats_log("validate: Invalid line \"%.*s\" unable to parse value as double", len, line);
        goto statsd_err;
    }
    if (isnan(result->value) || isinf(result->value)) {
        stats_log("validate: Invalid value \"%.*s\" supplied (NaN or INF)", len, line);
        goto statsd_err;
    }
    end[0] = c;

    end = memchr(start, '|', plen);
    if (end == NULL) {
        stats_log("validate: Invalid line \"%.*s\" missing '|'", len, line);
        goto statsd_err;
    }

    start = end + 1;
    plen = len - (start - line_copy);

    end = memchr(start, '|', plen);
    if (end != NULL) {
        c = end[0];
        end[0] = '\0';
        plen = end - start;
    }

    valid = 0;
    for (i = 0; i < valid_stat_types_len; i++) {
        if (strlen(valid_stat_types[i]) != plen) {
            continue;
        }
        if (strncmp(start, valid_stat_types[i], plen) == 0) {
            result->type = (metric_type)i; /* The indexes match the enum values in the comparison list */
            valid = 1;
            break;
        }
    }

    if (valid == 0) {
        stats_log("validate: Invalid line \"%.*s\" unknown stat type \"%.*s\"", len, line, plen, start);
        goto statsd_err;
    }

    if (end != NULL) {
        end[0] = c;
        // end[0] is currently the second | char
        // test if we have at least 1 char following it (@)
        if ((len - (end - line_copy) > 1) && (end[1] == '@')) {
            start = end + 2;
            plen = len - (start - line_copy);
            if (plen == 0) {
                stats_log("validate: Invalid line \"%.*s\" @ sample with no rate", len, line);
                goto statsd_err;
            }
            result->presampling_value = strtod(start, &err);
            if ((result->presampling_value == 0.0) && err == start) {
                stats_log("validate: Invalid line \"%.*s\" invalid sample rate", len, line);
                goto statsd_err;
            }
        } else {
            stats_log("validate: Invalid line \"%.*s\" no @ sample rate specifier", len, line);
            goto statsd_err;
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
        goto statsd_err;
    }

    free(line_copy);
    return 0;

statsd_err:
    free(line_copy);
    return 1;
}
