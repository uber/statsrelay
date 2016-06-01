#ifndef STATSRELAY_HASHLIB_H
#define STATSRELAY_HASHLIB_H

#include <stdint.h>

// hash a key to get a value in the range [0, output_domain)
uint32_t stats_hash(const char *key,
		uint32_t keylen,
		uint32_t output_domain);

/**
 * Hash a key without a domain limiter
 */
uint32_t stats_hash_key(const char *key,
		uint32_t keylen);

/**
 * Map a hash to an output domain
 */
uint32_t stats_hash_domain(uint32_t hash, uint32_t output_domain);

uint32_t murmur3_32(const char *key, uint32_t len, uint32_t seed);

#endif  // STATSRELAY_HASHLIB_H
