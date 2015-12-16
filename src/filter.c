#include <stdbool.h>
#include <pcre.h>

#include "./filter.h"
#include "./log.h"

/* PCRE output vector buffer */
#define OVECCOUNT 30    /* should be a multiple of 3 */

typedef enum  {
    RE = 1,
} FILTER_TYPE;

typedef bool (*filter_f)(filter_t* filter, const char* input, int input_len);

typedef struct  {
    FILTER_TYPE type;
    filter_f filter;
    filter_t* next;
} filter_common_t;

typedef struct {
    filter_common_t common;
    pcre* re;
} re_filter_t;

/**
 * The matcher function for PCRE - one or more matches is a filter success.
 * and would allow the action given.
 */
static bool re_match(filter_t* filter, const char* input, int input_len) {
    re_filter_t* ref = (re_filter_t*)filter;
    int ovector[OVECCOUNT];

    int rc = pcre_exec(ref->re, NULL, input, input_len, 0, 0, ovector, OVECCOUNT);
    if (rc < 0) {
        switch (rc) {
        case PCRE_ERROR_NOMATCH:
            return false;
        default:
            stats_error_log("pcre internal error code %d", rc);
            return false;
        }
    }
    if (rc == 0)
        rc = OVECCOUNT/3; /* Output overflow - truncate to max size instead of zero */

    /* At this point we have a match, not much else to do except say success */
    return true;
}

int filter_re_create(filter_t** filter, const char* re, filter_t* next_filter) {
    const char* error;
    int error_offset;

    pcre* pcre = pcre_compile(re, 0, &error, &error_offset, NULL);
    if (pcre == NULL) {
	stats_error_log("Filter compilation error: '%s' at character %d (regexp: '%s')", error, error_offset, re);
	return -1;
    }

    re_filter_t* ref = calloc(1, sizeof(re_filter_t));
    ref->common.type = RE;
    ref->common.filter = re_match;
    ref->common.next = next_filter;
    ref->re = pcre;
    *filter = (filter_t*)ref;

    return 0;
}

bool filter_exec(filter_t* filter, const char* input, int input_len) {
    filter_common_t* cf = (filter_common_t*)filter;
    bool ret = true;
    while (cf != NULL) {
        ret &= cf->filter(filter, input, input_len);
        cf = (filter_common_t*)cf->next;
    }
    return ret;
}

void filter_free(filter_t* filter) {
    filter_common_t* fc = (filter_common_t*)filter;
    if (fc->next)
	filter_free(fc->next);

    switch (fc->type) {
    case RE:
    {
	re_filter_t* r = (re_filter_t*)filter;
	pcre_free(r->re);
	free(filter);
    }
    break;
    }
}
