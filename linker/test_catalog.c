#include <stdio.h>
#include "catalog_lookup.h"
#include "catalog_extra.h"

int main(void) {
    const char *names[] = {
        "sceKernelGetCompiledSdkVersion", "sceUserServiceGetForegroundUser",
        "sceVideoOutOpen", "scePadInit", "sceAgcDcbDrawIndex", "sceAgcDriverSubmitDcb",
        "does_not_exist_anywhere",
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const StubEntry *e = catalog_find(names[i]);
        if (e) {
            printf("%-32s -> %-24s (module=%s soname=%s)\n", names[i], e->library, e->module_name, e->soname);
        } else {
            printf("%-32s -> NOT FOUND\n", names[i]);
            if (i != sizeof(names) / sizeof(names[0]) - 1) failures++; /* last one is expected to miss */
        }
    }
    printf("\nTotal catalog entries: %zu + %zu extra\n", ps5link_catalog_count, ps5link_catalog_extra_count);
    return failures;
}
