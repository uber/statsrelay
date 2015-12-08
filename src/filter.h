#ifndef STATSRELAY_FILTER_H_
#define STATSRELAY_FILTER_H_

#include <pcre.h>

typedef struct filter filter_t;

int filter_re_create(filter_t** filter, const char* re);

void filter_free(filter_t* filter);

#endif
