#ifndef STATSRELAY_VALIDATE_H
#define STATSRELAY_VALIDATE_H

#include <stdlib.h>


/*
 * Define a set of valid statsd types, indexed to validate.c comparisons
 */
typedef enum {
	METRIC_UNKNOWN = -1,
	METRIC_COUNTER = 0,
	METRIC_TIMER = 1,
	METRIC_KV = 2,
	METRIC_GAUGE = 3,
	METRIC_HIST = 4,
	METRIC_S = 5
} metric_type;

typedef struct {
	double value;
	metric_type type;
	double presampling_value;
} validate_parsed_result_t;

typedef int (*validate_line_validator_t)(const char *, size_t, validate_parsed_result_t*);

int validate_statsd(const char *, size_t, validate_parsed_result_t* result);

#endif  // STATSRELAY_VALIDATE_H
