#ifndef PS5LINK_ELF_OBJECT_H
#define PS5LINK_ELF_OBJECT_H

/*
 * Reads a plain relocatable (ET_REL) x86-64 ELF64 object, the kind
 * `prospero-clang -c` produces. Ported (scoped-down: no archive/.eh_frame
 * merging yet) from SharpProspero.Link/ElfObjectReader.cs + ElfModel.cs.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_size;
    uint64_t sh_addralign;
    uint32_t sh_link;   /* for SHT_SYMTAB: string table section index; for SHT_RELA: target section index (sh_info instead, see below) */
    uint32_t sh_info;
    uint8_t *data;       /* NULL for SHT_NOBITS (.bss) */
    size_t data_len;
} ElfSection;

typedef enum { SYM_NOTYPE, SYM_OBJECT, SYM_FUNC, SYM_TLS, SYM_SECTION, SYM_OTHER } ElfSymType;
typedef enum { SYM_LOCAL, SYM_GLOBAL, SYM_WEAK } ElfSymBind;

typedef struct {
    char *name;
    ElfSymBind bind;
    ElfSymType type;
    uint16_t shndx;     /* SHN_UNDEF (0) means undefined */
    uint64_t value;
    uint64_t size;
    int is_undefined;
} ElfSymbol;

/* One RELA relocation entry, section-relative. */
typedef struct {
    uint64_t r_offset;
    uint32_t r_type;
    uint32_t sym_index;  /* index into the object's symbol table */
    int64_t  r_addend;
    uint32_t target_section; /* which section this relocation applies to (from the .rela<X> section's sh_info) */
} ElfReloc;

typedef struct {
    char *origin;                 /* file path, for error messages */
    ElfSection *sections;
    size_t section_count;
    ElfSymbol *symbols;
    size_t symbol_count;
    ElfReloc *relocs;
    size_t reloc_count;
} ElfObject;

/* Reads and parses an ET_REL object from a file. Returns 1 on success, 0 on
 * failure (message printed to stderr). Caller must elf_object_free() the
 * result when done, even on partial-success paths this doesn't happen (all
 * paths either fully succeed or free internally before returning 0). */
int elf_object_read(const char *path, ElfObject *out);
void elf_object_free(ElfObject *obj);

/* Finds a defined symbol by name in this object (skips undefined ones).
 * Returns NULL if not found. */
const ElfSymbol *elf_object_find_defined(const ElfObject *obj, const char *name);

#endif
