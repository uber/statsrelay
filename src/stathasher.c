#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "./hashring.h"


int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s /path/to/hash.txt\n", argv[0]);
		return 1;
	}
	if (hashring_init(argv[1], 0, (hashring_alloc_func) strdup)) {
		fprintf(stderr, "failed to init with config file \"%s\"\n", argv[1]);
		return 1;
	}

	char *line = NULL;
	size_t len;
	ssize_t bytes_read;

	while ((bytes_read = getline(&line, &len, stdin)) != -1) {
		for (ssize_t i = len; i >= 0; i--) {
			if (isspace(line[i])) {
				line[i] = '\0';
			}
		}
		printf("%s %s\n", line, (const char *) hashring_choose(line, bytes_read));
	}
	free(line);
	return 0;
}
