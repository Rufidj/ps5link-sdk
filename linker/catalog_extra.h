#ifndef PS5LINK_CATALOG_EXTRA_H
#define PS5LINK_CATALOG_EXTRA_H

#include "catalog.h"

/*
 * Hand-picked entries for names PS5SDK calls that StubCatalog.cs does not
 * (yet) cover - confirmed gaps, not guesses: cross-checked every sce-/SDL_-
 * prefixed call PS5SDK's sdk/ and examples/ actually make against the generated
 * catalog.
 * Kept separate from catalog.c/.h so re-running gen_catalog.py never
 * clobbers these.
 */
extern const StubEntry ps5link_catalog_extra[];
extern const size_t ps5link_catalog_extra_count;

#endif
