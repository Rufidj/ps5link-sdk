/* Verifies nid_encode() against known-good NIDs confirmed byte-for-byte
 * against a real on-device libc.prx dump (see nid.c / project plan). */
#include <stdio.h>
#include <string.h>
#include "nid.h"

typedef struct { const char *name; const char *expected; } Case;

static const Case CASES[] = {
    {"puts", "YQ0navp+YIc"},
    {"memset", "8zTFvBIAIN8"},
    {"malloc", "gQX+4GDQjpM"},
    {"free", "tIhsqj0qsFE"},
    {"memcpy", "Q3VBxCXhUHs"},
    {"strlen", "j4ViWNHEgww"},
    {"fclose", "uodLYyUip20"},
};

int main(void) {
    int failures = 0;
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        char out[12];
        nid_encode(CASES[i].name, out);
        int ok = strcmp(out, CASES[i].expected) == 0;
        printf("%-8s -> %-12s (expected %-12s) %s\n",
               CASES[i].name, out, CASES[i].expected, ok ? "OK" : "FAIL");
        if (!ok) failures++;
    }

    /* Sanity checks for the integer id encoder. */
    char idbuf[8];
    nid_encode_id(0, idbuf);
    printf("id(0)   -> %-4s (expected A)  %s\n", idbuf, strcmp(idbuf, "A") == 0 ? "OK" : "FAIL");
    if (strcmp(idbuf, "A") != 0) failures++;

    nid_encode_id(1, idbuf);
    printf("id(1)   -> %-4s (expected B)  %s\n", idbuf, strcmp(idbuf, "B") == 0 ? "OK" : "FAIL");
    if (strcmp(idbuf, "B") != 0) failures++;

    nid_encode_id(63, idbuf);
    printf("id(63)  -> %-4s (expected -)  %s\n", idbuf, strcmp(idbuf, "-") == 0 ? "OK" : "FAIL");
    if (strcmp(idbuf, "-") != 0) failures++;

    nid_encode_id(64, idbuf);
    printf("id(64)  -> %-4s (expected BA) %s\n", idbuf, strcmp(idbuf, "BA") == 0 ? "OK" : "FAIL");
    if (strcmp(idbuf, "BA") != 0) failures++;

    if (failures == 0) {
        printf("\nAll %zu cases passed.\n", sizeof(CASES) / sizeof(CASES[0]) + 4);
        return 0;
    }
    printf("\n%d case(s) FAILED.\n", failures);
    return 1;
}
