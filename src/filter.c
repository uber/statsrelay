#include <stdbool.h>
#include <pcre.h>

#include "./filter.h"
#include "./log.h"

typedef enum  {
    RE = 1,
} FILTER_TYPE;

typedef int (*filter_f)(filter_t* filter, const char* input, int input_len, int** ovector, int* offset);

typedef struct  {
    FILTER_TYPE type;
    filter_f filter;
} filter_common_t;

typedef struct {
    filter_common_t common;
    pcre_extra* extra;
    pcre* re;
} re_filter_t;

/**
 * The matcher function for PCRE - one or more matches is a filter success.
 * and would allow the action given.
 */
static int re_match(filter_t* filter, const char* input, int input_len, int** ovector, int* offset) {
    int* ov;
    int of, rc;

    re_filter_t* ref = (re_filter_t*)filter;
    ov = *ovector;
    of = *offset;
    rc = pcre_exec(ref->re, ref->extra, input, input_len, of, 0, ov, OVECCOUNT);
    *offset = of;
    if (rc < 0) {
        switch (rc) {
            case PCRE_ERROR_NOMATCH:
                return 0;
            default:
                stats_error_log("pcre internal error code %d", rc);
                return 0;
        }
    }
    if (rc == 0)
        rc = OVECCOUNT/3; /* Output overflow - truncate to max size instead of zero */

    /* At this point we have a match, not much else to do except say success */
    return rc;
}

int filter_re_create(filter_t** filter, const char* re) {
    const char *pcreStudyError, *error;
    int error_offset;

    pcre* pcre = pcre_compile(re, 0, &error, &error_offset, NULL);
    if (pcre == NULL) {
        stats_error_log("Filter compilation error: '%s' at character %d (regexp: '%s')", error, error_offset, re);
        return -1;
    }

    re_filter_t* ref = calloc(1, sizeof(re_filter_t));
    ref->common.type = RE;
    ref->common.filter = re_match;
    ref->re = pcre;

    // Optimize the regex
    ref->extra = pcre_study(pcre, 0, &pcreStudyError);

    if (pcreStudyError != NULL) {
        stats_error_log("Could not study regex '%s': %s", re, pcreStudyError);
        return -1;
    }

    *filter = (filter_t*)ref;
    return 0;
}

int filter_exec(filter_t* filter, const char* input, int input_len, int** ovector, int* offset) {
    int rc;

    filter_common_t* cf = (filter_common_t*)filter;
    rc = cf->filter(filter, input, input_len, ovector, offset);
    return rc;
}

void filter_free(filter_t* filter) {
    filter_common_t* fc = (filter_common_t*)filter;

    switch (fc->type) {
        case RE:
            {
                re_filter_t* r = (re_filter_t*)filter;
                pcre_free(r->re);
                // Free up the EXTRA PCRE value (may be NULL at this point)
                if(r->extra != NULL) {
                    pcre_free(r->extra);
                }
                free(filter);
            }
            break;
    }
}
