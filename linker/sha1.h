#ifndef PS5LINK_SHA1_H
#define PS5LINK_SHA1_H

#include <stddef.h>
#include <stdint.h>

/* Minimal, self-contained SHA-1 (FIPS 180-1). Only used to compute Sony NIDs. */
void sha1(const uint8_t *data, size_t len, uint8_t digest_out[20]);

#endif
