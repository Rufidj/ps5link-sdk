/* Driver tying elf_object.c + linker.c + dynwriter.c together: reads real
 * .o files, resolves symbols against the catalog, writes a real SCE ELF. */
#include <stdio.h>
#include <stdlib.h>
#include "elf_object.h"
#include "linker.h"
#include "dynwriter.h"

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s out.elf file1.o [file2.o ...]\n", argv[0]); return 1; }
    const char *out_path = argv[1];
    size_t n = (size_t)(argc - 2);

    ElfObject *objs = calloc(n, sizeof(ElfObject));
    ElfObject **objp = calloc(n, sizeof(ElfObject *));
    for (size_t i = 0; i < n; i++) {
        if (!elf_object_read(argv[i + 2], &objs[i])) return 1;
        objp[i] = &objs[i];
    }

    LinkResolution res;
    linker_resolve(objp, n, &res);
    if (res.unresolved_count > 0) {
        fprintf(stderr, "link_real: %zu unresolved symbol(s):\n", res.unresolved_count);
        for (size_t i = 0; i < res.unresolved_count; i++) fprintf(stderr, "  %s\n", res.unresolved[i]);
        return 1;
    }

    int ok = dynwriter_write(objp, n, &res, out_path, out_path, "_start");

    link_resolution_free(&res);
    for (size_t i = 0; i < n; i++) elf_object_free(&objs[i]);
    return ok ? 0 : 1;
}
