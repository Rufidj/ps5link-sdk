/* Standard ELF64 ET_REL object reader (nothing SCE-specific here - object
 * files out of `prospero-clang -c` are ordinary ELF64 relocatables). */
#include "elf_object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} RawEhdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} RawShdr;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} RawSym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} RawRela;
#pragma pack(pop)

#define SHT_NOBITS_VAL 8
#define SHT_SYMTAB_VAL 2
#define SHT_RELA_VAL   4
#define SHT_REL_VAL    9

static char *dupstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    memcpy(r, s, n);
    return r;
}

void elf_object_free(ElfObject *obj) {
    if (!obj) return;
    for (size_t i = 0; i < obj->section_count; i++) free(obj->sections[i].name);
    free(obj->sections);
    for (size_t i = 0; i < obj->symbol_count; i++) free(obj->symbols[i].name);
    free(obj->symbols);
    free(obj->relocs);
    free(obj->origin);
    memset(obj, 0, sizeof(*obj));
}

int elf_object_read(const char *path, ElfObject *out) {
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "%s: %s\n", path, "cannot open"); return 0; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < (long)sizeof(RawEhdr)) { fprintf(stderr, "%s: too small to be an ELF object\n", path); fclose(f); return 0; }

    uint8_t *buf = malloc((size_t)fsize);
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        fprintf(stderr, "%s: short read\n", path);
        free(buf); fclose(f); return 0;
    }
    fclose(f);

    RawEhdr *eh = (RawEhdr *)buf;
    if (memcmp(eh->e_ident, "\x7f""ELF", 4) != 0 || eh->e_ident[4] != 2 /* ELFCLASS64 */) {
        fprintf(stderr, "%s: not a 64-bit ELF\n", path);
        free(buf); return 0;
    }
    if (eh->e_type != 1 /* ET_REL */) {
        fprintf(stderr, "%s: not a relocatable (ET_REL) object (e_type=%u) - "
                "ps5link only reads .o files, not final executables\n", path, eh->e_type);
        free(buf); return 0;
    }
    if ((uint64_t)eh->e_shoff + (uint64_t)eh->e_shnum * eh->e_shentsize > (uint64_t)fsize) {
        fprintf(stderr, "%s: section header table runs past end of file\n", path);
        free(buf); return 0;
    }

    RawShdr *sh = (RawShdr *)(buf + eh->e_shoff);
    size_t shnum = eh->e_shnum;

    if (eh->e_shstrndx >= shnum) {
        fprintf(stderr, "%s: invalid e_shstrndx\n", path);
        free(buf); return 0;
    }
    const char *shstrtab = (const char *)(buf + sh[eh->e_shstrndx].sh_offset);

    out->origin = dupstr(path);
    out->sections = calloc(shnum, sizeof(ElfSection));
    out->section_count = shnum;

    int symtab_idx = -1;
    for (size_t i = 0; i < shnum; i++) {
        ElfSection *s = &out->sections[i];
        s->name = dupstr(shstrtab + sh[i].sh_name);
        s->sh_type = sh[i].sh_type;
        s->sh_flags = sh[i].sh_flags;
        s->sh_addr = sh[i].sh_addr;
        s->sh_size = sh[i].sh_size;
        s->sh_addralign = sh[i].sh_addralign;
        s->sh_link = sh[i].sh_link;
        s->sh_info = sh[i].sh_info;
        if (sh[i].sh_type != SHT_NOBITS_VAL && sh[i].sh_size > 0) {
            s->data = malloc(sh[i].sh_size);
            memcpy(s->data, buf + sh[i].sh_offset, sh[i].sh_size);
            s->data_len = sh[i].sh_size;
        }
        if (sh[i].sh_type == (uint32_t)SHT_SYMTAB_VAL) symtab_idx = (int)i;
    }

    if (symtab_idx >= 0) {
        RawShdr *symsh = &sh[symtab_idx];
        size_t nsyms = symsh->sh_size / sizeof(RawSym);
        RawSym *rawsyms = (RawSym *)(buf + symsh->sh_offset);
        const char *strtab = (const char *)(buf + sh[symsh->sh_link].sh_offset);

        out->symbols = calloc(nsyms, sizeof(ElfSymbol));
        out->symbol_count = nsyms;
        for (size_t i = 0; i < nsyms; i++) {
            ElfSymbol *dst = &out->symbols[i];
            const char *nm = strtab + rawsyms[i].st_name;
            dst->name = dupstr(nm[0] ? nm : "");
            int bind = rawsyms[i].st_info >> 4;
            int type = rawsyms[i].st_info & 0xF;
            dst->bind = bind == 0 ? SYM_LOCAL : (bind == 2 ? SYM_WEAK : SYM_GLOBAL);
            switch (type) {
                case 1: dst->type = SYM_OBJECT; break;
                case 2: dst->type = SYM_FUNC; break;
                case 3: dst->type = SYM_SECTION; break;
                case 6: dst->type = SYM_TLS; break;
                default: dst->type = (type == 0) ? SYM_NOTYPE : SYM_OTHER; break;
            }
            dst->shndx = rawsyms[i].st_shndx;
            dst->value = rawsyms[i].st_value;
            dst->size = rawsyms[i].st_size;
            dst->is_undefined = (rawsyms[i].st_shndx == 0) && nm[0] != '\0';
        }
    }

    /* Relocations: only SHT_RELA is expected from prospero-clang/lld on
     * x86-64 (RELA, not REL - x86-64 always carries explicit addends). */
    size_t reloc_cap = 0;
    for (size_t i = 0; i < shnum; i++) {
        if (sh[i].sh_type == SHT_REL_VAL) {
            fprintf(stderr, "%s: section %s is SHT_REL, not SHT_RELA - "
                    "unexpected for x86-64, not handled\n", path, out->sections[i].name);
        }
        if (sh[i].sh_type == (uint32_t)SHT_RELA_VAL) {
            reloc_cap += sh[i].sh_size / sizeof(RawRela);
        }
    }
    out->relocs = calloc(reloc_cap ? reloc_cap : 1, sizeof(ElfReloc));
    size_t reloc_n = 0;
    for (size_t i = 0; i < shnum; i++) {
        if (sh[i].sh_type != (uint32_t)SHT_RELA_VAL) continue;
        size_t n = sh[i].sh_size / sizeof(RawRela);
        RawRela *rr = (RawRela *)(buf + sh[i].sh_offset);
        for (size_t j = 0; j < n; j++) {
            ElfReloc *dst = &out->relocs[reloc_n++];
            dst->r_offset = rr[j].r_offset;
            dst->r_type = (uint32_t)(rr[j].r_info & 0xFFFFFFFFu);
            dst->sym_index = (uint32_t)(rr[j].r_info >> 32);
            dst->r_addend = rr[j].r_addend;
            dst->target_section = sh[i].sh_info; /* the section this .rela<X> applies to */
        }
    }
    out->reloc_count = reloc_n;

    free(buf);
    return 1;
}

const ElfSymbol *elf_object_find_defined(const ElfObject *obj, const char *name) {
    for (size_t i = 0; i < obj->symbol_count; i++) {
        if (!obj->symbols[i].is_undefined && obj->symbols[i].shndx != 0
            && strcmp(obj->symbols[i].name, name) == 0) {
            return &obj->symbols[i];
        }
    }
    return NULL;
}
