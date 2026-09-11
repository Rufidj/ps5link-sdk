/*
 * Ported byte-for-byte from SharpProspero.Link/NidEncoder.cs (Encode) and
 * DynamicWriter.cs (private Encode(int)).
 */
#include "nid.h"
#include "sha1.h"
#include <string.h>

static const char ALPHABET[64] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";

/* The sixteen bytes the real SDK's own crt1.o stores at .rodata.cst16 + 0x10;
 * confirmed byte-for-byte against a real on-device libc.prx dump. */
static const unsigned char SALT[16] = {
    0x51, 0x8D, 0x64, 0xA6, 0x35, 0xDE, 0xD8, 0xC1,
    0xE6, 0xB0, 0x39, 0xB1, 0xC3, 0xE5, 0x52, 0x30,
};

void nid_encode(const char *symbol_name, char out[12]) {
    unsigned char input[256 + sizeof(SALT)];
    size_t name_len = strlen(symbol_name);
    if (name_len > sizeof(input) - sizeof(SALT)) {
        name_len = sizeof(input) - sizeof(SALT); /* not expected; avoid overflow */
    }

    memcpy(input, symbol_name, name_len);
    memcpy(input + name_len, SALT, sizeof(SALT));

    unsigned char digest[20];
    sha1(input, name_len + sizeof(SALT), digest);

    /* Reverse the first 8 bytes in place (read little-endian, re-emit
     * big-endian). Skipping this swap is a documented, previously-made
     * mistake: it matches the SDK stub library's internal .scenid bytes
     * instead of what the real on-device loader resolver looks for. */
    unsigned char tmp;
    tmp = digest[0]; digest[0] = digest[7]; digest[7] = tmp;
    tmp = digest[1]; digest[1] = digest[6]; digest[6] = tmp;
    tmp = digest[2]; digest[2] = digest[5]; digest[5] = tmp;
    tmp = digest[3]; digest[3] = digest[4]; digest[4] = tmp;

    /* Zero bytes [8..16) so the third/fourth base64 triplets read clean
     * trailing zeros. */
    memset(digest + 8, 0, 8);

    char result[16];
    int pos = 0;
    for (int i = 0; i < 12; i += 3) {
        int abc = (digest[i] << 16) | (digest[i + 1] << 8) | digest[i + 2];
        result[pos++] = ALPHABET[(abc >> 18) & 0x3F];
        result[pos++] = ALPHABET[(abc >> 12) & 0x3F];
        result[pos++] = ALPHABET[(abc >> 6) & 0x3F];
        result[pos++] = ALPHABET[abc & 0x3F];
    }

    memcpy(out, result, 11);
    out[11] = '\0';
}

void nid_encode_id(int id, char out[8]) {
    if (id == 0) {
        out[0] = 'A';
        out[1] = '\0';
        return;
    }

    char digits[8];
    int n = 0;
    unsigned int v = (unsigned int)id;
    while (v > 0 && n < (int)sizeof(digits)) {
        digits[n++] = ALPHABET[v % 64];
        v /= 64;
    }

    /* digits[] was filled least-significant-first; reverse into out. */
    for (int i = 0; i < n; i++) {
        out[i] = digits[n - 1 - i];
    }
    out[n] = '\0';
}
