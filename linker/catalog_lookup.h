#ifndef PS5LINK_CATALOG_LOOKUP_H
#define PS5LINK_CATALOG_LOOKUP_H

#include "catalog.h"

/* Looks up which catalog entry (generated or hand-picked extra) publishes
 * plain_name. Returns NULL if nothing does. */
const StubEntry *catalog_find(const char *plain_name);

#endif
