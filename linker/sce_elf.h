#ifndef PS5LINK_SCE_ELF_H
#define PS5LINK_SCE_ELF_H

/*
 * SCE-specific ELF constants, copied verbatim (not re-derived) from
 * SharpProspero.Link/DynamicWriter.cs (lines ~30-150), which validated this
 * shape byte-for-byte against real on-device modules.
 */

#include <stdint.h>

/* e_type values (DynamicWriter.cs:31-32) */
#define ET_SCE_DYNEXEC  0xFE10u  /* executable */
#define ET_SCE_DYNAMIC  0xFE18u  /* library/prx */

/* e_ident[EI_OSABI] (DynamicWriter.cs:1634, observed value) */
#define ELFOSABI_SCE    9

/* Segment alignment and program-header flags (DynamicWriter.cs:30,33) */
#define SCE_SEG_ALIGN   0x4000ull
#define PF_X            1u
#define PF_W            2u
#define PF_R            4u

/* Program header p_type values, standard + SCE (DynamicWriter.cs:137-138) */
#define PT_LOAD             1u
#define PT_DYNAMIC          2u
#define PT_NOTE             4u
#define PT_TLS              7u
#define PT_SCE_PROC_PARAM   0x61000001u
#define PT_SCE_MODULE_PARAM 0x61000002u
#define PT_GNU_EH_FRAME     0x6474E550u
#define PT_GNU_RELRO        0x6474E552u
#define PT_SCE_COMMENT      0x6FFFFF00u
#define PT_SCE_VERSION      0x6FFFFF01u

/* Dynamic tag values, standard + SCE (DynamicWriter.cs:140-147) */
#define DT_NEEDED             1L
#define DT_PLTRELSZ           2L
#define DT_PLTGOT             3L
#define DT_HASH               4L
#define DT_STRTAB             5L
#define DT_SYMTAB             6L
#define DT_RELA               7L
#define DT_RELASZ             8L
#define DT_RELAENT            9L
#define DT_STRSZ              10L
#define DT_SYMENT             11L
#define DT_INIT               12L
#define DT_FINI               13L
#define DT_PLTREL             20L
#define DT_DEBUG              21L
#define DT_JMPREL             23L
#define DT_INIT_ARRAY         25L
#define DT_FINI_ARRAY         26L
#define DT_INIT_ARRAYSZ       27L
#define DT_FINI_ARRAYSZ       28L
#define DT_PREINIT_ARRAY      32L
#define DT_PREINIT_ARRAYSZ    33L
#define DT_RELACOUNT          0x6ffffff9L
#define DT_SCE_HASHSZ         0x6100003dL
#define DT_SCE_SYMTABSZ       0x6100003fL
#define DT_SCE_ORIG_FILENAME  0x61000041L
#define DT_SCE_MODULE_INFO    0x61000043L
#define DT_SCE_NEEDED_MODULE  0x61000045L
#define DT_SCE_EXPORT_LIB     0x61000047L
#define DT_SCE_IMPORT_LIB     0x61000049L
#define DT_SCE_MODULE_ATTR    0x61000011L
#define DT_SCE_EXPORT_LIB_ATTR 0x61000017L
#define DT_SCE_IMPORT_LIB_ATTR 0x61000019L
#define DT_SCE_STUB_MODULE_NAME    0x6100001dL /* only needed if parsing real .stub files */
#define DT_SCE_STUB_MODULE_VERSION 0x6100001fL
#define DT_SCE_STUB_LIBRARY_NAME   0x61000021L
#define DT_SCE_STUB_LIBRARY_VERSION 0x61000023L
#define DT_NULL               0L

/* Relocation types (DynamicWriter.cs:148,150) */
#define R_X86_64_JUMP_SLOT  7u
#define R_X86_64_GLOB_DAT   6u
#define R_X86_64_RELATIVE   8u
#define R_X86_64_64         1u
#define R_X86_64_DTPMOD64   16u

/* Module/library default versions (StubLibrary.cs) */
#define SCE_DEFAULT_MODULE_VERSION  0x0101u
#define SCE_DEFAULT_LIBRARY_VERSION 0x0001u

/* SDK version pair (DynamicWriter.cs:130-133) - the two travel together,
 * every measured module carries both or neither. */
#define SCE_MODULE_SDK_VERSION    0x02000009u
#define SCE_COMPANION_SDK_VERSION 0x08050001u

/*
 * Process-parameter block for an EXECUTABLE (PT_SCE_PROC_PARAM), 0x60 bytes,
 * ported from DynamicWriter.cs BuildProcParam() (~line 2049-2065):
 *   0x00 u64 = 0x60 (declared size, matches the block's own length)
 *   0x08 4 bytes "ORBI"
 *   0x0C u32 = 5
 *   0x10 u32 = SCE_COMPANION_SDK_VERSION
 *   0x14 u32 = SCE_MODULE_SDK_VERSION
 *   0x58 u32 = 1
 *   everything else zero (pointers the loader fills in at load time)
 */
#define SCE_PROC_PARAM_SIZE 0x60

/*
 * Process-parameter block for a LIBRARY/PRX (PT_SCE_MODULE_PARAM), 0x20
 * bytes, ported from DynamicWriter.cs BuildModuleParam() (~line 2037-2047):
 *   0x00 u64 = 0x20 (declared size)
 *   0x08 u32 = SCE_MODULE_PARAM_MAGIC
 *   0x0C u32 = 3
 *   0x10 u32 = SCE_COMPANION_SDK_VERSION
 *   0x14 u32 = SCE_MODULE_SDK_VERSION
 *   0x18 u32 = 1
 */
#define SCE_MODULE_PARAM_SIZE  0x20
#define SCE_MODULE_PARAM_MAGIC 0x3C13F4BFu

static inline void sce_build_proc_param(uint8_t out[SCE_PROC_PARAM_SIZE]) {
    for (int i = 0; i < SCE_PROC_PARAM_SIZE; i++) out[i] = 0;
    out[0] = 0x60; /* u64 LE, only low byte non-zero */
    out[8] = 'O'; out[9] = 'R'; out[10] = 'B'; out[11] = 'I';
    out[0x0C] = 5;
    out[0x10] = (uint8_t)(SCE_COMPANION_SDK_VERSION);
    out[0x11] = (uint8_t)(SCE_COMPANION_SDK_VERSION >> 8);
    out[0x12] = (uint8_t)(SCE_COMPANION_SDK_VERSION >> 16);
    out[0x13] = (uint8_t)(SCE_COMPANION_SDK_VERSION >> 24);
    out[0x14] = (uint8_t)(SCE_MODULE_SDK_VERSION);
    out[0x15] = (uint8_t)(SCE_MODULE_SDK_VERSION >> 8);
    out[0x16] = (uint8_t)(SCE_MODULE_SDK_VERSION >> 16);
    out[0x17] = (uint8_t)(SCE_MODULE_SDK_VERSION >> 24);
    out[0x58] = 1;
}

#endif
