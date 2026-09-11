#include "linker.h"
#include "catalog_lookup.h"
#include "nid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *soname; int id; } ModuleEntry;
typedef struct { char *soname; char *library; int id; } LibraryEntry;

static char *dupstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    memcpy(r, s, n);
    return r;
}

static int is_defined_anywhere(ElfObject **objects, size_t object_count, const char *name) {
    for (size_t i = 0; i < object_count; i++) {
        if (elf_object_find_defined(objects[i], name)) return 1;
    }
    return 0;
}

static int module_index(ModuleEntry *mods, size_t *count, const char *soname) {
    for (size_t i = 0; i < *count; i++) {
        if (strcmp(mods[i].soname, soname) == 0) return (int)i;
    }
    mods[*count].soname = dupstr(soname);
    mods[*count].id = (int)*count;
    return (int)(*count)++;
}

static int library_index(LibraryEntry *libs, size_t *count, const char *soname, const char *library) {
    for (size_t i = 0; i < *count; i++) {
        if (strcmp(libs[i].soname, soname) == 0 && strcmp(libs[i].library, library) == 0) return (int)i;
    }
    libs[*count].soname = dupstr(soname);
    libs[*count].library = dupstr(library);
    libs[*count].id = (int)*count;
    return (int)(*count)++;
}

int linker_resolve(ElfObject **objects, size_t object_count, LinkResolution *out) {
    memset(out, 0, sizeof(*out));

    /* Collect every undefined name referenced across all objects, once each. */
    char **undef_names = NULL;
    size_t undef_count = 0, undef_cap = 0;
    for (size_t i = 0; i < object_count; i++) {
        ElfObject *obj = objects[i];
        for (size_t j = 0; j < obj->symbol_count; j++) {
            if (!obj->symbols[j].is_undefined) continue;
            const char *name = obj->symbols[j].name;
            int already = 0;
            for (size_t k = 0; k < undef_count; k++) {
                if (strcmp(undef_names[k], name) == 0) { already = 1; break; }
            }
            if (already) continue;
            if (undef_count == undef_cap) {
                undef_cap = undef_cap ? undef_cap * 2 : 16;
                undef_names = realloc(undef_names, undef_cap * sizeof(char *));
            }
            undef_names[undef_count++] = (char *)name; /* borrowed, not owned */
        }
    }

    /* Section-boundary symbols the linker itself synthesizes
     * (__start_<section>/__stop_<section>) are not modeled yet (no section
     * merging across objects in this scoped-down port) - if PS5SDK code
     * ever needs those, this is where to add them. For now, every
     * undefined name is resolved either against another object's
     * definitions or against the catalog. */

    ImportSymbol *imports = NULL;
    size_t import_cap = 0, import_count = 0;
    char **unresolved = NULL;
    size_t unresolved_cap = 0, unresolved_count = 0;

    ModuleEntry *mods = calloc(undef_count ? undef_count : 1, sizeof(ModuleEntry));
    size_t mod_count = 0;
    LibraryEntry *libs = calloc(undef_count ? undef_count : 1, sizeof(LibraryEntry));
    size_t lib_count = 0;

    for (size_t i = 0; i < undef_count; i++) {
        const char *name = undef_names[i];
        if (is_defined_anywhere(objects, object_count, name)) continue; /* satisfied by another object */

        const StubEntry *entry = catalog_find(name);
        if (!entry) {
            if (unresolved_count == unresolved_cap) {
                unresolved_cap = unresolved_cap ? unresolved_cap * 2 : 8;
                unresolved = realloc(unresolved, unresolved_cap * sizeof(char *));
            }
            unresolved[unresolved_count++] = dupstr(name);
            continue;
        }

        int mod_id = module_index(mods, &mod_count, entry->soname) + 1; /* modules number from 1 */
        int lib_id = library_index(libs, &lib_count, entry->soname, entry->library);

        if (import_count == import_cap) {
            import_cap = import_cap ? import_cap * 2 : 16;
            imports = realloc(imports, import_cap * sizeof(ImportSymbol));
        }
        ImportSymbol *imp = &imports[import_count++];
        memset(imp, 0, sizeof(*imp));
        imp->plain_name = dupstr(name);
        imp->published_name = dupstr(name);
        imp->soname = dupstr(entry->soname);
        imp->library_name = dupstr(entry->library);
        imp->published_module_name = dupstr(entry->module_name);
        imp->module_version = entry->module_version;
        imp->library_version = entry->library_version;
        imp->module_id = mod_id;
        imp->library_id = lib_id;

        char nid[12], lib_enc[8], mod_enc[8];
        nid_encode(name, nid);
        nid_encode_id(lib_id, lib_enc);
        nid_encode_id(mod_id, mod_enc);
        snprintf(imp->mangled_name, sizeof(imp->mangled_name), "%s#%s#%s", nid, lib_enc, mod_enc);
    }

    free(mods);
    free(libs);
    free(undef_names);

    out->imports = imports;
    out->import_count = import_count;
    out->unresolved = unresolved;
    out->unresolved_count = unresolved_count;
    return 1;
}

void link_resolution_free(LinkResolution *res) {
    for (size_t i = 0; i < res->import_count; i++) {
        ImportSymbol *imp = &res->imports[i];
        free(imp->plain_name); free(imp->published_name); free(imp->soname);
        free(imp->library_name); free(imp->published_module_name);
    }
    free(res->imports);
    for (size_t i = 0; i < res->unresolved_count; i++) free(res->unresolved[i]);
    free(res->unresolved);
    memset(res, 0, sizeof(*res));
}
