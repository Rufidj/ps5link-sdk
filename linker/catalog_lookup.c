#include "catalog_lookup.h"
#include "catalog_extra.h"
#include <string.h>

static const StubEntry *find_in(const StubEntry *table, size_t count, const char *plain_name) {
    for (size_t i = 0; i < count; i++) {
        const StubEntry *e = &table[i];
        for (size_t j = 0; j < e->export_count; j++) {
            if (strcmp(e->exports[j].plain_name, plain_name) == 0) {
                return e;
            }
        }
    }
    return NULL;
}

const StubEntry *catalog_find(const char *plain_name) {
    const StubEntry *e = find_in(ps5link_catalog, ps5link_catalog_count, plain_name);
    if (e) return e;
    return find_in(ps5link_catalog_extra, ps5link_catalog_extra_count, plain_name);
}
