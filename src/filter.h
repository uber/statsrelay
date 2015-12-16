#ifndef STATSRELAY_FILTER_H_
#define STATSRELAY_FILTER_H_

#include <stdbool.h>
#include <pcre.h>

typedef struct filter filter_t;

/**
 * Create a PCRE matcher filter. Returns true on match.
 * next_filter is an optional chaining filter, or NULL
 * for no next filter. You need to only retain and filter_free
 * the last filter created in a chain. Do not chain a filter
 * more than once.
 */
int filter_re_create(filter_t** filter, const char* re, filter_t* next_filter);

/**
 * Destroy a filter chain
 */
void filter_free(filter_t* filter);

/**
 * Execute a filter chain, returning the boolean AND
 * of all filter operations.
 */
bool filter_exec(filter_t* filter, const char* input, int input_len);

#endif
