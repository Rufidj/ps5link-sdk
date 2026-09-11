#ifndef PS5LINK_DYNWRITER_H
#define PS5LINK_DYNWRITER_H

/*
 * Generalizes mkelf_test.c's proven-correct ELF shape (SCE segments, NID-
 * mangled .dynsym, SysV .hash over the long form, DT_SCE_* tags) to real
 * objects and a real LinkResolution, instead of one hardcoded import.
 *
 * Scoped for Phase 0-1 (structural verification, no hardware yet): merges
 * .text/.rodata/.data/.bss across objects and patches R_X86_64_PC32/PLT32
 * relocations against LOCALLY DEFINED symbols (same-link function/global
 * references). Relocations against IMPORTED symbols are deliberately left
 * unpatched - wiring an actual PLT/GOT call to an imported function is
 * Phase 2 (hardware-verified) work; this phase only needs the import's
 * .dynsym/.dynstr/.hash/DT_SCE_* metadata to be correct, which is what
 * `readelf` checks statically.
 */
#include "elf_object.h"
#include "linker.h"

/* entry_symbol: name of the defined symbol to use as e_entry (e.g.
 * "_start"). NULL keeps the previous default (start of merged .text) -
 * only useful before a real crt1.o is linked in.
 * Returns 1 on success, 0 on failure (message on stderr). */
int dynwriter_write(ElfObject **objects, size_t object_count,
                     const LinkResolution *resolution,
                     const char *module_file_name, const char *out_path,
                     const char *entry_symbol);

#endif
