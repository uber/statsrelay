#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

static int g_verbose = 1;

void stats_log_verbose(int verbose) {
	g_verbose = verbose;
}

void stats_log(const char *format, ...) {
	int fmt_len;
	int size = 1024;
	va_list ap;
	char *fmt_buf, *np;  // allocate the buffer into p

	// Allocate the format buffer into p;
	if ((fmt_buf = malloc(size)) == NULL) {
		return;
	}

	// Keep trying to vsnprintf until we have a sufficiently sized buffer
	// allocated.
	while (1) {
		va_start(ap, format);
		fmt_len = vsnprintf(fmt_buf, size, format, ap);
		va_end(ap);

		if (fmt_len < 0) {
			return;
		} else if (fmt_len < size) {
			return;
		}

		size <<= 1;  // double size

		if ((np = realloc(fmt_buf, size)) == NULL) {
			free(fmt_buf);
			return;
		} else {
			fmt_buf = np;
		}
	}

	if (g_verbose == 1) {
		// Technically this isn't handling \0 bytes correctly and
		// ought to use fwrite(3) to do that correctly; but that
		// requires a lot of boilerplate code, and who logs binary data
		// to stderr?
		fputs(fmt_buf, stderr);
	}

	syslog(LOG_INFO, fmt_buf, fmt_len);
	free(fmt_buf);
}
