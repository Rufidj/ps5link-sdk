#include <stdio.h>
#include <stdlib.h>
#include "elf_object.h"
#include "linker.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s file.o [file2.o ...]\n", argv[0]); return 1; }

    size_t n = (size_t)(argc - 1);
    ElfObject *objs = calloc(n, sizeof(ElfObject));
    ElfObject **objp = calloc(n, sizeof(ElfObject *));
    for (size_t i = 0; i < n; i++) {
        if (!elf_object_read(argv[i + 1], &objs[i])) return 1;
        objp[i] = &objs[i];
    }

    LinkResolution res;
    linker_resolve(objp, n, &res);

    printf("Imports (%zu):\n", res.import_count);
    for (size_t i = 0; i < res.import_count; i++) {
        ImportSymbol *imp = &res.imports[i];
        printf("  %-36s module_id=%d library_id=%d soname=%-20s mangled=%s\n",
               imp->plain_name, imp->module_id, imp->library_id, imp->soname, imp->mangled_name);
    }

    printf("\nUnresolved (%zu):\n", res.unresolved_count);
    for (size_t i = 0; i < res.unresolved_count; i++) {
        printf("  %s\n", res.unresolved[i]);
    }

    int had_unresolved = res.unresolved_count > 0;
    link_resolution_free(&res);
    for (size_t i = 0; i < n; i++) elf_object_free(&objs[i]);
    return had_unresolved ? 1 : 0;
}
