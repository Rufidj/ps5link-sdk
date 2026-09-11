#ifndef PS5LINK_LINKER_H
#define PS5LINK_LINKER_H

/*
 * Symbol resolution: given a set of already-read ElfObjects, finds which
 * undefined references resolve against the catalog (becoming NID-encoded
 * imports) and which don't (an error - matches Linker.Resolve() in
 * SharpProspero.Link/Linker.cs, scoped down: no archive (.a) member-pulling
 * yet - that's deferred to the SDL2/Phase-3 work, per the project plan).
 */

#include "elf_object.h"
#include "catalog.h"

typedef struct {
    char *plain_name;
    char *published_name;      /* name the providing module publishes under; == plain_name unless the catalog says otherwise (not currently modeled - always == plain_name) */
    char *soname;               /* module file name, e.g. "libkernel.prx" */
    char *library_name;
    char *published_module_name;
    uint16_t module_version;
    uint16_t library_version;
    int module_id;              /* numbers from 1, by first-seen soname (Linker.cs/DynamicWriter.cs convention) */
    int library_id;             /* numbers from 0, by first-seen (soname, library_name) pair */
    char mangled_name[64];      /* "{nid}#{Encode(library_id)}#{Encode(module_id)}" */
} ImportSymbol;

typedef struct {
    ImportSymbol *imports;
    size_t import_count;
    char **unresolved;          /* names neither defined by any object nor found in the catalog */
    size_t unresolved_count;
} LinkResolution;

/* Resolves undefined references across `objects` against each other and the
 * catalog. Returns 1 always (check resolution->unresolved_count for
 * failure - matches the "report what it cannot resolve" philosophy the
 * catalog docs describe, rather than a boolean success/fail). Caller must
 * link_resolution_free() the result. */
int linker_resolve(ElfObject **objects, size_t object_count, LinkResolution *out);
void link_resolution_free(LinkResolution *res);

#endif
