#ifndef STATSRELAY_FILTER_H_
#define STATSRELAY_FILTER_H_

#include <stdbool.h>
#include <pcre.h>

/* PCRE output vector buffer */
#define OVECCOUNT 30    /* allows up to 9 capture groups */

typedef struct filter filter_t;

/**
 * Create a PCRE matcher filter. Returns true on match.
 * You need to only retain and filter_free
 */
int filter_re_create(filter_t** filter, const char* re);

/**
 * Destroy a filter chain
 */
void filter_free(filter_t* filter);

/**
 * Execute a filter chain, returning the boolean AND
 * of all filter operations.
 */
int filter_exec(filter_t* filter, const char* input, int input_len, int** ovector, int* offset);

#endif
