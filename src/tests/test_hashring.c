#include "../hashring.h"
#include "../log.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


int main(int argc, char **argv) {
	// Test the hashring.  Note that when the hash space is
	// expanded in hashring1 -> hashring2, we are checking
	// explicitly that apple and orange do not move to new nodes.
	stats_log_verbose(1);
	assert(hashring_init("tests/hashring1.txt", 4, (hashring_alloc_func) strdup) == 0);
	assert(strcmp(hashring_choose("apple", strlen("apple")), "bbb") == 0);
	assert(strcmp(hashring_choose("banana", strlen("banana")), "bbb") == 0);
	assert(strcmp(hashring_choose("orange", strlen("orange")), "aaa") == 0);
	assert(strcmp(hashring_choose("lemon", strlen("lemon")), "aaa") == 0);
	hashring_dealloc((hashring_dealloc_func) free);

	assert(hashring_init("tests/hashring2.txt", 4, (hashring_alloc_func) strdup) == 0);
	assert(strcmp(hashring_choose("apple", strlen("apple")), "bbb") == 0);
	assert(strcmp(hashring_choose("banana", strlen("banana")), "ddd") == 0);
	assert(strcmp(hashring_choose("orange", strlen("orange")), "aaa") == 0);
	assert(strcmp(hashring_choose("lemon", strlen("lemon")), "ccc") == 0);
	hashring_dealloc((hashring_dealloc_func) free);

	return 0;
}
