#include <stdio.h>
#include "elf_object.h"

static const char *bind_name(ElfSymBind b) {
    switch (b) { case SYM_LOCAL: return "LOCAL"; case SYM_GLOBAL: return "GLOBAL"; default: return "WEAK"; }
}
static const char *type_name(ElfSymType t) {
    switch (t) {
        case SYM_OBJECT: return "OBJECT"; case SYM_FUNC: return "FUNC";
        case SYM_TLS: return "TLS"; case SYM_SECTION: return "SECTION";
        case SYM_NOTYPE: return "NOTYPE"; default: return "OTHER";
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s file.o\n", argv[0]); return 1; }

    ElfObject obj;
    if (!elf_object_read(argv[1], &obj)) return 1;

    printf("Sections (%zu):\n", obj.section_count);
    for (size_t i = 0; i < obj.section_count; i++) {
        ElfSection *s = &obj.sections[i];
        printf("  [%2zu] %-20s type=%-2u size=%-6llu align=%llu\n",
               i, s->name, s->sh_type, (unsigned long long)s->sh_size, (unsigned long long)s->sh_addralign);
    }

    printf("\nSymbols (%zu):\n", obj.symbol_count);
    for (size_t i = 0; i < obj.symbol_count; i++) {
        ElfSymbol *sym = &obj.symbols[i];
        if (!sym->name[0]) continue;
        printf("  %-30s bind=%-6s type=%-7s shndx=%-3u %s\n",
               sym->name, bind_name(sym->bind), type_name(sym->type), sym->shndx,
               sym->is_undefined ? "UNDEFINED" : "defined");
    }

    printf("\nRelocations (%zu):\n", obj.reloc_count);
    for (size_t i = 0; i < obj.reloc_count && i < 20; i++) {
        ElfReloc *r = &obj.relocs[i];
        printf("  target_sec=%-3u off=0x%-6llx type=%-3u sym_idx=%u addend=%lld\n",
               r->target_section, (unsigned long long)r->r_offset, r->r_type, r->sym_index, (long long)r->r_addend);
    }

    elf_object_free(&obj);
    return 0;
}
