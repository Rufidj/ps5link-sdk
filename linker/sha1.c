/* Minimal, self-contained SHA-1 (FIPS 180-1). Public-domain-style textbook
 * implementation; only used to compute Sony NIDs (11-char import identifiers),
 * not for anything security-sensitive. */
#include "sha1.h"
#include <string.h>

/* NID inputs are always "symbol name + 16-byte salt", well under this. Two
 * 64-byte blocks (128 bytes) covers any realistic C symbol name. */
#define SHA1_MAX_INPUT 512
#define SHA1_MAX_PADDED (SHA1_MAX_INPUT + 64)

static uint32_t rol32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

void sha1(const uint8_t *data, size_t len, uint8_t digest_out[20]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    if (len > SHA1_MAX_INPUT) {
        /* Not expected for NID computation (symbol names are short); refuse
         * rather than silently truncate or overflow. */
        memset(digest_out, 0, 20);
        return;
    }

    uint8_t msg[SHA1_MAX_PADDED];
    size_t padded_len = ((len + 1 + 8 + 63) / 64) * 64;

    memcpy(msg, data, len);
    msg[len] = 0x80;
    memset(msg + len + 1, 0, padded_len - len - 1 - 8);
    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) {
        msg[padded_len - 1 - i] = (uint8_t)(bit_len >> (8 * i));
    }

    for (size_t block = 0; block < padded_len; block += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            const uint8_t *p = msg + block + i * 4;
            w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
        }
        for (int i = 16; i < 80; i++) {
            w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }

            uint32_t temp = rol32(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol32(b, 30); b = a; a = temp;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    for (int i = 0; i < 5; i++) {
        digest_out[i * 4 + 0] = (uint8_t)(h[i] >> 24);
        digest_out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        digest_out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        digest_out[i * 4 + 3] = (uint8_t)(h[i]);
    }
}
