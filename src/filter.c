#include <stdbool.h>
#include <pcre.h>

#include "./filter.h"
#include "./log.h"

typedef enum  {
    RE = 1,
} FILTER_TYPE;

typedef bool (*filter_f)(filter_t* filter, const char* input);

typedef struct  {
    FILTER_TYPE type;
    filter_f filter;
    filter_t* next;
} filter_common_t;

typedef struct {
    filter_common_t common;
    pcre* re;
} re_filter_t;

static bool re_match(filter_t* filter, const char* input) {
    //re_filter_t* ref = (re_filter_t*)filter;
    return false;
}

int filter_re_create(filter_t** filter, const char* re) {
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
    ref->re = pcre;
    *filter = (filter_t*)ref;

    return 0;
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
