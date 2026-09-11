/* Generalizes mkelf_test.c's proven ELF shape to real objects + a real
 * LinkResolution. See dynwriter.h for scope notes. */
#include "dynwriter.h"
#include "common_buf.h"
#include "nid.h"
#include "sce_elf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EI_NIDENT 16

typedef struct {
    uint8_t e_ident[EI_NIDENT]; uint16_t e_type; uint16_t e_machine; uint32_t e_version;
    uint64_t e_entry; uint64_t e_phoff; uint64_t e_shoff; uint32_t e_flags;
    uint16_t e_ehsize; uint16_t e_phentsize; uint16_t e_phnum;
    uint16_t e_shentsize; uint16_t e_shnum; uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type; uint32_t p_flags; uint64_t p_offset; uint64_t p_vaddr;
    uint64_t p_paddr; uint64_t p_filesz; uint64_t p_memsz; uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    uint32_t sh_name; uint32_t sh_type; uint64_t sh_flags; uint64_t sh_addr;
    uint64_t sh_offset; uint64_t sh_size; uint32_t sh_link; uint32_t sh_info;
    uint64_t sh_addralign; uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct { uint64_t d_tag; uint64_t d_val; } Elf64_Dyn;

#define SHT_PROGBITS_VAL 1
#define SHT_HASH_VAL     5
#define SHT_DYNAMIC_VAL  6
#define SHT_NOBITS_VAL   8
#define SHT_NOTE_VAL     7
#define SHT_DYNSYM_VAL   11
#define SHT_RELA_VAL     4
#define SHF_WRITE_VAL     1ull
#define SHF_ALLOC_VAL     2ull
#define SHF_EXECINSTR_VAL 4ull

typedef enum { REGION_CODE, REGION_RODATA, REGION_DATA, REGION_BSS, REGION_NONE } RegionKind;

typedef struct { RegionKind region; uint64_t offset; } SectionPlacement;

/* Classifies a section into one of the four merged regions, or NONE to drop
 * it (symtab/strtab/comment/notes/eh_frame/relocation sections themselves -
 * none of those are needed in the final module, and merging .eh_frame
 * correctly is out of scope for this phase). */
static RegionKind classify_section(const ElfSection *s) {
    if (strcmp(s->name, ".eh_frame") == 0) return REGION_NONE;
    if (s->sh_type == (uint32_t)SHT_NOTE_VAL) return REGION_NONE;
    if (!(s->sh_flags & SHF_ALLOC_VAL)) return REGION_NONE; /* symtab, strtab, comment, rela*, shstrtab */
    if (s->sh_type == (uint32_t)SHT_NOBITS_VAL) return REGION_BSS;
    if (s->sh_flags & SHF_EXECINSTR_VAL) return REGION_CODE;
    if (s->sh_flags & SHF_WRITE_VAL) return REGION_DATA;
    return REGION_RODATA;
}

int dynwriter_write(ElfObject **objects, size_t object_count,
                     const LinkResolution *resolution,
                     const char *module_file_name, const char *out_path,
                     const char *entry_symbol) {
    if (resolution->unresolved_count > 0) {
        fprintf(stderr, "dynwriter: %zu unresolved symbol(s), refusing to write an ELF (e.g. %s)\n",
                resolution->unresolved_count, resolution->unresolved[0]);
        return 0;
    }

    /* ---- Pass 1: classify and place every section into its merged region. */
    SectionPlacement **placements = calloc(object_count, sizeof(SectionPlacement *));
    Buf code, rodata, data;
    buf_init(&code); buf_init(&rodata); buf_init(&data);
    uint64_t bss_total = 0;
    uint64_t bss_max_align = 1;

    for (size_t oi = 0; oi < object_count; oi++) {
        ElfObject *obj = objects[oi];
        placements[oi] = calloc(obj->section_count, sizeof(SectionPlacement));
        for (size_t si = 0; si < obj->section_count; si++) {
            ElfSection *s = &obj->sections[si];
            RegionKind k = classify_section(s);
            placements[oi][si].region = k;
            uint64_t align = s->sh_addralign ? s->sh_addralign : 1;
            switch (k) {
                case REGION_CODE:
                    code.len = align_up(code.len, align);
                    placements[oi][si].offset = code.len;
                    buf_push(&code, s->data, s->data_len);
                    break;
                case REGION_RODATA:
                    rodata.len = align_up(rodata.len, align);
                    placements[oi][si].offset = rodata.len;
                    buf_push(&rodata, s->data, s->data_len);
                    break;
                case REGION_DATA:
                    data.len = align_up(data.len, align);
                    placements[oi][si].offset = data.len;
                    buf_push(&data, s->data, s->data_len);
                    break;
                case REGION_BSS:
                    if (align > bss_max_align) bss_max_align = align;
                    bss_total = align_up(bss_total, align);
                    /* .bss lands right after .data's real bytes; final
                     * offset is fixed up below once data.len is final. */
                    placements[oi][si].offset = bss_total;
                    bss_total += s->sh_size;
                    break;
                default: break;
            }
        }
    }
    /* A PT_LOAD segment with BOTH filesz==0 and memsz==0 is invalid - "is
     * mapped but stores nothing" - and SharpProspero's own self-signing
     * tool refuses to wrap a module carrying one ("a module carrying one
     * does not start"), confirmed the hard way: every hardware test so far
     * crashed identically because none of the test programs had any
     * global/static data, so this RW segment was always empty. Matches
     * DynamicWriter.cs's own "if (dataLen == 0) dataLen = 8" - reserve a
     * minimum of 8 bytes even when nothing needs real data storage. */
    if (data.len == 0) buf_pad_to(&data, 8);

    /* .sce_process_param lives INSIDE the RW-data segment's own mapped
     * range (confirmed against a real title's extracted program headers:
     * its PT_SCE_PROC_PARAM vaddr fell inside its RW LOAD segment's
     * address range, not in a separate region of its own) - matches
     * DynamicWriter.cs's RelroOrder, which lists ".sce_process_param"
     * alongside .got/.data.rel.ro as part of the writable group. Giving it
     * its own standalone, never-mapped-by-any-PT_LOAD region (this
     * project's bug for a while) meant the kernel's own proc_param lookup
     * (which reads the mapped image, not the file directly) found nothing
     * there: "[KERNEL] kern_get_sdk_compiled_version: proc_param not
     * found", confirmed via klogsrv on real hardware. */
    uint64_t proc_param_offset_in_data = data.len;
    uint8_t proc_param_bytes[SCE_PROC_PARAM_SIZE];
    sce_build_proc_param(proc_param_bytes);
    buf_push(&data, proc_param_bytes, sizeof(proc_param_bytes));

    /* Three "process parameter" blocks (C library, kernel memory, kernel
     * filesystem) plus three replacement-table stubs and two heap-limit
     * figures, laid end to end right after proc_param - ported from
     * DynamicWriter.cs's BuildParamBlocks()/ParamBlockOffsets (confirmed by
     * reading that code directly, not guessed). proc_param's own
     * LibcParam/KernelMemParam/KernelFsParam fields (offsets 0x38/0x40/0x48)
     * are left zero in the image and must be filled at LOAD TIME by
     * R_X86_64_RELATIVE relocations pointing in here - "the block reads as
     * all zeros in a finished module and still is not [unfilled]" per that
     * file's own comment. Missing this entirely (this port never emitted
     * any RELATIVE record - a comment nearby used to say so explicitly) is
     * exactly what made every hardware test past the rtld stage crash with
     * SIGSEGV *inside libkernel.sprx itself*, writing to address 0x28 -
     * libkernel's own _init_env dereferencing the null KernelMemParam
     * pointer it was handed, confirmed via klogsrv on real hardware. */
#define PB_LIBCPARAM_SZ      0xA8
#define PB_KMEMPARAM_SZ      0x38
#define PB_KFSPARAM_SZ       0x10
#define PB_MALLOCREPL_SZ     0x78
#define PB_NEWREPL_SZ        0xC0
#define PB_TLSMALLOCREPL_SZ  0x38
#define PB_OFF_LIBCPARAM     0
#define PB_OFF_KMEMPARAM     (PB_OFF_LIBCPARAM + PB_LIBCPARAM_SZ)
#define PB_OFF_KFSPARAM      (PB_OFF_KMEMPARAM + PB_KMEMPARAM_SZ)
#define PB_OFF_MALLOCREPL    (PB_OFF_KFSPARAM + PB_KFSPARAM_SZ)
#define PB_OFF_NEWREPL       (PB_OFF_MALLOCREPL + PB_MALLOCREPL_SZ)
#define PB_OFF_TLSMALLOCREPL (PB_OFF_NEWREPL + PB_NEWREPL_SZ)
#define PB_OFF_HEAPSIZE      (PB_OFF_TLSMALLOCREPL + PB_TLSMALLOCREPL_SZ)
#define PB_OFF_HEAPEXT       (PB_OFF_HEAPSIZE + 8)
#define PB_TOTAL_SZ          (PB_OFF_HEAPEXT + 8)
    uint8_t paramblocks[PB_TOTAL_SZ];
    memset(paramblocks, 0, sizeof(paramblocks));
    {
        uint64_t v;
        v = PB_LIBCPARAM_SZ; memcpy(paramblocks + PB_OFF_LIBCPARAM, &v, 8);
        v = 0x000000010000000EULL; memcpy(paramblocks + PB_OFF_LIBCPARAM + 8, &v, 8); /* LibcParamRevision */
        v = PB_KMEMPARAM_SZ; memcpy(paramblocks + PB_OFF_KMEMPARAM, &v, 8);
        v = PB_KFSPARAM_SZ; memcpy(paramblocks + PB_OFF_KFSPARAM, &v, 8);
        v = PB_MALLOCREPL_SZ; memcpy(paramblocks + PB_OFF_MALLOCREPL, &v, 8);
        v = 2; memcpy(paramblocks + PB_OFF_MALLOCREPL + 8, &v, 8);
        v = PB_NEWREPL_SZ; memcpy(paramblocks + PB_OFF_NEWREPL, &v, 8);
        v = 3; memcpy(paramblocks + PB_OFF_NEWREPL + 8, &v, 8);
        v = PB_TLSMALLOCREPL_SZ; memcpy(paramblocks + PB_OFF_TLSMALLOCREPL, &v, 8);
        v = 1; memcpy(paramblocks + PB_OFF_TLSMALLOCREPL + 8, &v, 8);
        v = 0xFFFFFFFFFFFFFFFFULL; memcpy(paramblocks + PB_OFF_HEAPSIZE, &v, 8); /* no heap limit */
        uint32_t ext = 1; memcpy(paramblocks + PB_OFF_HEAPEXT, &ext, 4); /* lift built-in limit too */
    }
    buf_pad_to(&data, align_up(data.len, 8));
    uint64_t paramblocks_offset_in_data = data.len;
    buf_push(&data, paramblocks, sizeof(paramblocks));

    /* .bss sits right after .data in the same RW region - but its base must
     * be aligned to the strictest alignment any .bss section asks for. The
     * sections are laid out relative to each other from zero, so each one's
     * offset is only a multiple of its own alignment if the base they are all
     * shifted by is too. The real bytes of .data end wherever they end
     * (here, after proc_param and the param blocks too), so shifting by
     * data.len as-is moved every .bss object off its alignment - by 8 bytes in
     * SM64, where the compiler, trusting a declared 16-byte alignment, had
     * written `vmovdqa` into one: a general-protection fault in
     * init_sample_dma_buffers on the very first audio initialisation. */
    uint64_t data_real_len = data.len;
    uint64_t bss_base = align_up(data_real_len, bss_max_align);
    for (size_t oi = 0; oi < object_count; oi++) {
        for (size_t si = 0; si < objects[oi]->section_count; si++) {
            if (placements[oi][si].region == REGION_BSS) {
                placements[oi][si].offset += bss_base;
            }
        }
    }
    uint64_t data_region_memsz = bss_base + bss_total;

    /* ---- Identify which imports are actually CALLED (referenced by a
     * PC32/PLT32 relocation against a name no object defines), and give
     * each one a PLT stub + GOT slot - the classic non-lazy "jmp through
     * GOT" pattern, ported from DynamicWriter.cs:819-834 (confirmed by
     * reading that code directly: pltBytes[p]=0xFF, pltBytes[p+1]=0x25,
     * disp32 = GotAddress - (PltAddress+6); relaBytes offset=GotAddress,
     * info=(DynSymIndex<<32)|R_X86_64_JUMP_SLOT(7)). An import declared but
     * never called (e.g. a data import) gets neither - nothing needs one. */
    /* clang/LLVM emits a DIFFERENT relocation for some calls to an unknown
     * (imported) function: R_X86_64_GOTPCRELX(41)/_REX_GOTPCRELX(42) - a
     * load-then-call sequence through a GOT slot holding the address
     * itself, rather than a direct PLT32 call. Unlike PLT32, this needs a
     * plain data GOT slot (filled via R_X86_64_GLOB_DAT) with NO PLT stub -
     * the relocated field points straight at the GOT slot. Confirmed by
     * comparing gcc's output (always PLT32/type 4 for these same calls)
     * against prospero-clang's (type 41) on the identical source. */
    int *import_called = calloc(resolution->import_count ? resolution->import_count : 1, sizeof(int));
    int *import_gotdata = calloc(resolution->import_count ? resolution->import_count : 1, sizeof(int));
    for (size_t oi = 0; oi < object_count; oi++) {
        ElfObject *obj = objects[oi];
        for (size_t ri = 0; ri < obj->reloc_count; ri++) {
            ElfReloc *r = &obj->relocs[ri];
            int is_plt_call = (r->r_type == 2 || r->r_type == 4);
            int is_gotdata = (r->r_type == 9 || r->r_type == 41 || r->r_type == 42);
            if (!is_plt_call && !is_gotdata) continue;
            ElfSymbol *sym = &obj->symbols[r->sym_index];
            if (sym->type == SYM_SECTION || !sym->is_undefined) continue;
            int defined_elsewhere = 0;
            for (size_t oj = 0; oj < object_count && !defined_elsewhere; oj++) {
                if (elf_object_find_defined(objects[oj], sym->name)) defined_elsewhere = 1;
            }
            if (defined_elsewhere) continue;
            for (size_t k = 0; k < resolution->import_count; k++) {
                if (strcmp(resolution->imports[k].plain_name, sym->name) == 0) {
                    if (is_plt_call) import_called[k] = 1;
                    if (is_gotdata) import_gotdata[k] = 1;
                    break;
                }
            }
        }
    }
    size_t bound_count = 0;
    int *bound_index = calloc(resolution->import_count ? resolution->import_count : 1, sizeof(int));
    for (size_t k = 0; k < resolution->import_count; k++) {
        bound_index[k] = import_called[k] ? (int)bound_count++ : -1;
    }
    size_t gotdata_count = 0;
    int *gotdata_index = calloc(resolution->import_count ? resolution->import_count : 1, sizeof(int));
    for (size_t k = 0; k < resolution->import_count; k++) {
        gotdata_index[k] = import_gotdata[k] ? (int)gotdata_count++ : -1;
    }

    /* PLT: a 16-byte reserved header (unused - no lazy resolver, everything
     * binds eagerly at load time) then one 16-byte "jmp *GOT[n](%rip)" stub
     * per bound import. Appended to the merged code buffer - same segment,
     * same PF_X permissions, no separate segment needed. */
    uint64_t plt_base_in_code = align_up(code.len, 16);
    while (code.len < plt_base_in_code) buf_u8(&code, 0);
    buf_pad_to(&code, code.len + 16); /* reserved PLT[0] */
    uint64_t plt_stubs_offset = code.len;
    buf_pad_to(&code, code.len + bound_count * 16);

    /* GOT: a 24-byte reserved header (matching the real convention, unused
     * here) then one 8-byte slot per bound import, in its own small
     * writable segment (kept separate from the .data/.bss segment so
     * neither one's already-validated layout math needs touching). */
    /* GOTPCRELX references to symbols this binary defines are normally relaxed
     * into direct forms further down (mov->lea, indirect call/jmp->direct).
     * Some instruction forms cannot be: `add r64, foo@GOTPCREL(%rip)` or
     * `cmp r64, ...` use the loaded address as an operand, and there is no
     * same-length direct form. SM64 has 129 of them - gMatStack in the scene
     * graph, read every frame, gArctanTable in the maths - so leaving them
     * patched as if relaxed makes them use the symbol's first bytes as an
     * address. Those get what DynamicWriter.cs gives every such reference: a
     * real GOT slot holding the symbol's address, fixed up base-relative at
     * load time, with the instruction left exactly as compiled.
     *
     * Slots are keyed by symbol NAME, and counted here, because the GOT's
     * size has to be known before any address is (the writable segment is
     * placed after it). Names are a safe key for these: every one found is a
     * named global - no section symbols, no same-named statics. */
    const char **local_got_names = calloc(64, sizeof(char *));
    size_t local_got_n = 0, local_got_cap = 64;
    for (size_t oi = 0; oi < object_count; oi++) {
        ElfObject *obj = objects[oi];
        for (size_t ri = 0; ri < obj->reloc_count; ri++) {
            ElfReloc *r = &obj->relocs[ri];
            if (r->r_type != 9 && r->r_type != 41 && r->r_type != 42) continue;
            if (placements[oi][r->target_section].region != REGION_CODE) continue;
            ElfSymbol *sym = &obj->symbols[r->sym_index];
            if (sym->type == SYM_SECTION || !sym->name[0]) continue;
            int is_local = !sym->is_undefined;
            for (size_t oj = 0; oj < object_count && !is_local; oj++)
                if (elf_object_find_defined(objects[oj], sym->name)) is_local = 1;
            if (!is_local) continue;                       /* an import: GLOB_DAT slot as before */
            ElfSection *ts = &obj->sections[r->target_section];
            if (r->r_offset < 2 || r->r_offset + 4 > ts->data_len) continue;
            const uint8_t *op = ts->data + r->r_offset - 2;
            /* Plain GOTPCREL (9), unlike the X variants, carries no promise that
             * the instruction has a relaxable shape - SM64's one instance is a
             * vbroadcastsd - so it always gets a real slot. */
            int relaxable = r->r_type != 9 &&
                            (op[0] == 0x8B || (op[0] == 0xFF && (op[1] == 0x15 || op[1] == 0x25)));
            if (relaxable) continue;
            int seen = 0;
            for (size_t q = 0; q < local_got_n && !seen; q++)
                if (strcmp(local_got_names[q], sym->name) == 0) seen = 1;
            if (seen) continue;
            if (local_got_n == local_got_cap) {
                local_got_cap *= 2;
                local_got_names = realloc(local_got_names, local_got_cap * sizeof(char *));
            }
            local_got_names[local_got_n++] = sym->name;
        }
    }
    int *local_got_emitted = calloc(local_got_n ? local_got_n : 1, sizeof(int));

    uint64_t got_size = 24 + bound_count * 8 + gotdata_count * 8 + local_got_n * 8;

    /* ---- Layout: code, dynlink(dynsym/dynstr/hash/dynamic), rw-data(+bss), procparam, got. */
    uint64_t phoff = sizeof(Elf64_Ehdr);
    uint64_t phnum = 14; /* LOAD(code) LOAD(dynlink) LOAD(rw) DYNAMIC SCE_PROC_PARAM TLS LOAD(rodata) LOAD(got) COMMENT VERSION NOTE NOTE RELRO EH_FRAME */
    uint64_t seg1_off = align_up(phoff + phnum * sizeof(Elf64_Phdr), SCE_SEG_ALIGN);
    /* vaddr is a SEPARATE address space from file position, packed
     * tightly from 0 - confirmed the hard way: an earlier version of this
     * file used vaddr == file_offset throughout (e.g. code_vaddr =
     * seg1_off, a large, file-layout-dependent value), which readelf -l
     * on a real, freshly-built SharpProspero sample (same source code,
     * their real linker) showed is simply wrong - its code segment sits
     * at p_offset=0x4000 but p_vaddr=0x0, rodata at p_offset=0x8000 but
     * p_vaddr=0x4000, etc. This was very likely the actual cause of the
     * real rtld's calcurate_sce_dynlibdata_layout() failing with error 8
     * on every prior build, surviving many other fixes (dynsym/dynstr
     * order, MODULE_INFO packing, proc_param placement, phdr order,
     * RELRO/EH_FRAME position) that were each real but insufficient on
     * their own. */
    uint64_t code_vaddr = 0;
    uint64_t seg1_filesz = code.len;
    uint64_t plt_addr = code_vaddr + plt_stubs_offset;

    /* rodata is appended right after code in the SAME execute segment's
     * address space is wrong (rodata must not be executable) - give it its
     * own placement inside the read-only-if-nonempty group folded into the
     * dynlink segment's read-only neighbor would also be wrong (that
     * segment must carry NO protection at all per the documented shape).
     * Simplest correct choice for this phase: rodata gets its own PT_LOAD,
     * read-only, right after the code segment. */
    uint64_t rodata_off = align_up(seg1_off + seg1_filesz, SCE_SEG_ALIGN);
    uint64_t rodata_vaddr = align_up(code_vaddr + seg1_filesz, SCE_SEG_ALIGN);
    /* Same "mapped but stores nothing" fix as the RW data segment above -
     * this PT_LOAD is always emitted (see ph[6] below), so it must never
     * be 0/0 either. */
    if (rodata.len == 0) buf_pad_to(&rodata, 8);
    int has_rodata = rodata.len > 0;

    /* GOT gets its own small RW segment, placed here (right after rodata,
     * before the dynlink segment) so its address is known before building
     * .rela.plt below - .rela.plt lives inside the dynlink segment and
     * needs to reference these addresses. File position and vaddr are
     * tracked separately now (see code_vaddr's comment above). */
    uint64_t got_off = align_up(rodata_off + (has_rodata ? rodata.len : 0), SCE_SEG_ALIGN);
    uint64_t got_vaddr = align_up(rodata_vaddr + rodata.len, SCE_SEG_ALIGN);
    uint64_t got_addr = got_vaddr + 24; /* past the reserved header - PLT-bound slots */
    uint64_t gotdata_addr = got_addr + bound_count * 8; /* plain data slots, right after */

    uint64_t seg2_off = align_up(got_off + got_size, SCE_SEG_ALIGN);
    /* rw-data's vaddr comes right after GOT's in vaddr space (independent
     * of where either one physically sits in the FILE - rw-data's file
     * position is computed later, after the dynlink segment, since that's
     * this port's chosen FILE layout order; vaddr order does not have to
     * match). The dynlink ("no protection") segment's vaddr is packed
     * TIGHTLY right after rw-data's (16-byte aligned, not a full page) -
     * confirmed against the real reference: its dynlink segment's p_vaddr
     * continues immediately from the writable segment's, not on its own
     * page like every other segment here. */
    uint64_t rw_vaddr = align_up(got_vaddr + got_size, SCE_SEG_ALIGN);
    /* Page-aligned, not just 16-byte aligned as DynlibAlign might suggest:
     * ELF requires p_vaddr === p_offset (mod p_align), and this port's
     * seg2_off (the dynlink segment's FILE position, below) is always a
     * clean SCE_SEG_ALIGN multiple - a 16-byte-aligned vaddr here would
     * violate that congruence against a page-aligned file offset. The
     * real reference actually packs both file offset AND vaddr tightly
     * (non-page-aligned but mutually congruent) for this segment; using a
     * full page here for both instead is less tightly packed but still
     * structurally valid, and simpler to get right. */
    uint64_t dynlink_vaddr = align_up(rw_vaddr + data_region_memsz, SCE_SEG_ALIGN);
#define TOVADDR(fileoff) (dynlink_vaddr + ((fileoff) - seg2_off))

    /* Now that plt_addr and got_addr are both known, fill in each bound
     * import's actual stub bytes: ff 25 <disp32>, disp32 = GOT slot addr -
     * (this stub's own address + 6) - the length of the "jmp *disp32(%rip)"
     * instruction itself (ported from DynamicWriter.cs:830-833). */
    for (size_t k = 0; k < resolution->import_count; k++) {
        if (bound_index[k] < 0) continue;
        /* plt_addr/plt_stubs_offset already point PAST the 16-byte reserved
         * header (see where they're computed) - no extra offset here. */
        uint64_t this_plt = plt_addr + (uint64_t)bound_index[k] * 16;
        uint64_t this_got = got_addr + (uint64_t)bound_index[k] * 8;
        uint64_t stub_file_off = plt_stubs_offset + (uint64_t)bound_index[k] * 16;
        int32_t disp = (int32_t)((int64_t)this_got - (int64_t)(this_plt + 6));
        code.data[stub_file_off] = 0xFF;
        code.data[stub_file_off + 1] = 0x25;
        memcpy(code.data + stub_file_off + 2, &disp, 4);
        /* remaining 10 bytes (already zero from buf_pad_to) are never
         * reached - the jmp above is unconditional. */
    }

    /* ---- Build .dynstr / .dynsym / .hash from resolution->imports. */
    StrTab dynstr; strtab_init(&dynstr);
    Buf dynsym; buf_init(&dynsym);
    buf_pad_to(&dynsym, 24); /* null entry */

    typedef struct { char long_form[160]; } HashName;
    HashName *hash_names = calloc(resolution->import_count + 1, sizeof(HashName));
    size_t hash_name_count = 0;
    strcpy(hash_names[hash_name_count++].long_form, "");

    for (size_t i = 0; i < resolution->import_count; i++) {
        const ImportSymbol *imp = &resolution->imports[i];
        uint32_t off_mangled = strtab_add(&dynstr, imp->mangled_name);
        buf_u32(&dynsym, off_mangled);
        buf_u8(&dynsym, (1 << 4) | 2); /* STB_GLOBAL | STT_FUNC */
        buf_u8(&dynsym, 0);
        { uint16_t shn = 0; buf_push(&dynsym, &shn, 2); }
        buf_u64(&dynsym, 0);
        buf_u64(&dynsym, 0);

        char nid[12];
        nid_encode(imp->plain_name, nid);
        snprintf(hash_names[hash_name_count++].long_form, sizeof(HashName),
                 "%s#%s#%s", nid, imp->library_name, imp->published_module_name);
    }

    size_t sym_count = hash_name_count; /* null + imports */
    int nbucket = (int)sym_count;
    Buf hash; buf_init(&hash);
    buf_u32(&hash, (uint32_t)nbucket);
    buf_u32(&hash, (uint32_t)sym_count);
    uint32_t *buckets = calloc((size_t)nbucket, sizeof(uint32_t));
    uint32_t *chains = calloc(sym_count, sizeof(uint32_t));
    for (int i = (int)sym_count - 1; i >= 1; i--) {
        int bucket = (int)(elf_hash(hash_names[i].long_form) % (uint32_t)nbucket);
        chains[i] = buckets[bucket];
        buckets[bucket] = (uint32_t)i;
    }
    for (int i = 0; i < nbucket; i++) buf_u32(&hash, buckets[i]);
    for (size_t i = 0; i < sym_count; i++) buf_u32(&hash, chains[i]);
    free(buckets); free(chains); free(hash_names);

    /* Needed modules (dedup by module_id) and imported libraries (dedup by
     * library_id), each with the string offsets DT_SCE_NEEDED_MODULE and
     * DT_SCE_IMPORT_LIB actually want: the module's/library's own PUBLISHED
     * name, not necessarily the soname file name (confirmed by reading
     * DynamicWriter.cs:1804-1815 directly - a prior version of this project
     * got this wrong; see mkelf_test.c's fix-forward note). */
    typedef struct { int id; uint32_t soname_off, module_name_off; uint16_t version; const char *soname; } ModRec;
    typedef struct { int id; int module_id; uint32_t lib_name_off; uint16_t version; } LibRec;
    ModRec *mods = calloc(resolution->import_count ? resolution->import_count : 1, sizeof(ModRec));
    LibRec *libs = calloc(resolution->import_count ? resolution->import_count : 1, sizeof(LibRec));
    size_t mod_n = 0, lib_n = 0;
    for (size_t i = 0; i < resolution->import_count; i++) {
        const ImportSymbol *imp = &resolution->imports[i];
        int found = 0;
        for (size_t j = 0; j < mod_n; j++) if (mods[j].id == imp->module_id) { found = 1; break; }
        if (!found) {
            mods[mod_n].id = imp->module_id;
            mods[mod_n].soname_off = strtab_add(&dynstr, imp->soname);
            mods[mod_n].module_name_off = strtab_add(&dynstr, imp->published_module_name);
            mods[mod_n].version = imp->module_version;
            mods[mod_n].soname = imp->soname;
            mod_n++;
        }
        found = 0;
        for (size_t j = 0; j < lib_n; j++) if (libs[j].id == imp->library_id) { found = 1; break; }
        if (!found) {
            libs[lib_n].id = imp->library_id;
            libs[lib_n].module_id = imp->module_id;
            libs[lib_n].lib_name_off = strtab_add(&dynstr, imp->library_name);
            libs[lib_n].version = imp->library_version;
            lib_n++;
        }
    }
    uint32_t off_selfname = strtab_add(&dynstr, module_file_name);

    /* .rela.plt: one JUMP_SLOT record per bound import, telling the loader
     * to fill that GOT slot with the resolved function's address before
     * this module's own code ever runs (no lazy resolver - PS5's loader is
     * known to bind everything eagerly at load time). dynsym index is
     * 1-based position in resolution->imports (index 0 is the null entry
     * built above), NOT the bound-bookkeeping index. */
    Buf relaplt; buf_init(&relaplt);
    for (size_t i = 0; i < resolution->import_count; i++) {
        if (bound_index[i] < 0) continue;
        uint64_t got_slot = got_addr + (uint64_t)bound_index[i] * 8;
        uint64_t dynsym_index = i + 1;
        buf_u64(&relaplt, got_slot);
        buf_u64(&relaplt, (dynsym_index << 32) | R_X86_64_JUMP_SLOT);
        buf_u64(&relaplt, 0); /* r_addend */
    }
    /* Symbol resolution and relocation patching run HERE, before .rela.dyn is
     * built, rather than after the layout of the dynamic segment. They have
     * to: every absolute pointer they patch needs a base-relative record in
     * .rela.dyn, and that table's size fixes the offsets of everything laid
     * out after it. Nothing they use is computed any later than this - only
     * the segment addresses above, the placements, and the import slots. */
    Buf relrel; buf_init(&relrel);
    uint64_t relrel_count = 0;
    int relrel_not_writable = 0;

    /* ---- Pass 2: resolve every defined symbol's final vaddr, by name (for
     * cross-object references) and by (object, shndx) for section symbols. */
    typedef struct { const char *name; uint64_t addr; } NameAddr;
    size_t total_syms = 0;
    for (size_t oi = 0; oi < object_count; oi++) total_syms += objects[oi]->symbol_count;
    NameAddr *name_addrs = calloc(total_syms ? total_syms : 1, sizeof(NameAddr));
    size_t name_addr_n = 0;

    for (size_t oi = 0; oi < object_count; oi++) {
        ElfObject *obj = objects[oi];
        for (size_t si = 0; si < obj->symbol_count; si++) {
            ElfSymbol *sym = &obj->symbols[si];
            if (sym->is_undefined || sym->shndx == 0 || sym->type == SYM_SECTION || !sym->name[0]) continue;
            /* Reserved section indices (SHN_ABS=0xfff1, SHN_COMMON=0xfff2,
             * ...) are not real section table entries - e.g. the STT_FILE
             * symbol every object carries points at SHN_ABS. Skip them;
             * they're never a valid relocation target anyway. */
            if (sym->shndx >= obj->section_count) continue;
            RegionKind k = placements[oi][sym->shndx].region;
            if (k == REGION_NONE) continue;
            uint64_t base = (k == REGION_CODE) ? code_vaddr : (k == REGION_RODATA) ? rodata_vaddr : rw_vaddr;
            name_addrs[name_addr_n].name = sym->name;
            name_addrs[name_addr_n].addr = base + placements[oi][sym->shndx].offset + sym->value;
            name_addr_n++;
        }
    }

    /* ---- Pass 3: apply relocations. Only PC32/PLT32 against a LOCALLY
     * DEFINED symbol (in this object or another one being linked) are
     * patched; relocations against an IMPORTED symbol are deliberately left
     * as-is (no PLT/GOT wiring yet - see dynwriter.h). */
    int skipped_import_relocs = 0;
    int relaxed_gotpcrelx = 0;
    int unrelaxable_gotpcrelx = 0;
    int local_got_slotted = 0;
    for (size_t oi = 0; oi < object_count; oi++) {
        ElfObject *obj = objects[oi];
        for (size_t ri = 0; ri < obj->reloc_count; ri++) {
            ElfReloc *r = &obj->relocs[ri];
            RegionKind target_region = placements[oi][r->target_section].region;
            if (target_region == REGION_NONE) continue; /* e.g. a dropped .eh_frame reloc */
            uint64_t target_base = (target_region == REGION_CODE) ? code_vaddr
                                  : (target_region == REGION_RODATA) ? rodata_vaddr : rw_vaddr;
            uint64_t P = target_base + placements[oi][r->target_section].offset + r->r_offset;

            ElfSymbol *sym = &obj->symbols[r->sym_index];
            uint64_t S = 0;
            int have_S = 0;
            /* Whether S ends up naming a GOT slot (an import) rather than the
             * symbol itself. It decides whether a GOTPCRELX reference gets
             * relaxed below, and getting it backwards breaks the other case. */
            int s_is_got_slot = 0;
            if (sym->type == SYM_SECTION && sym->shndx < obj->section_count) {
                RegionKind k = placements[oi][sym->shndx].region;
                if (k != REGION_NONE) {
                    uint64_t base = (k == REGION_CODE) ? code_vaddr : (k == REGION_RODATA) ? rodata_vaddr : rw_vaddr;
                    S = base + placements[oi][sym->shndx].offset + sym->value;
                    have_S = 1;
                }
            } else if (!sym->is_undefined && sym->shndx != 0 && sym->shndx < obj->section_count) {
                RegionKind k = placements[oi][sym->shndx].region;
                if (k != REGION_NONE) {
                    uint64_t base = (k == REGION_CODE) ? code_vaddr : (k == REGION_RODATA) ? rodata_vaddr : rw_vaddr;
                    S = base + placements[oi][sym->shndx].offset + sym->value;
                    have_S = 1;
                }
            } else {
                for (size_t k = 0; k < name_addr_n; k++) {
                    if (strcmp(name_addrs[k].name, sym->name) == 0) { S = name_addrs[k].addr; have_S = 1; break; }
                }
                /* Not defined by any object - a genuine import call. Target
                 * its PLT stub (which jumps through the GOT slot the
                 * loader fills in at load time), not the real function
                 * address directly - that address isn't known until then. */
                if (!have_S && (r->r_type == 2 || r->r_type == 4)) {
                    for (size_t k = 0; k < resolution->import_count; k++) {
                        if (bound_index[k] >= 0 && strcmp(resolution->imports[k].plain_name, sym->name) == 0) {
                            S = plt_addr + (uint64_t)bound_index[k] * 16;
                            have_S = 1;
                            break;
                        }
                    }
                }
                /* GOTPCRELX/REX_GOTPCRELX (41/42): the relocated field wants
                 * the address of the plain GOT-data slot itself (which the
                 * loader fills with the resolved function's real address
                 * via the GLOB_DAT record above), not a PLT stub. */
                if (!have_S && (r->r_type == 9 || r->r_type == 41 || r->r_type == 42)) {
                    for (size_t k = 0; k < resolution->import_count; k++) {
                        if (gotdata_index[k] >= 0 && strcmp(resolution->imports[k].plain_name, sym->name) == 0) {
                            S = gotdata_addr + (uint64_t)gotdata_index[k] * 8;
                            have_S = 1;
                            s_is_got_slot = 1;
                            break;
                        }
                    }
                }
            }

            if (!have_S) { skipped_import_relocs++; continue; } /* a data import, or some other unhandled case */

            /* Find which merged buffer P actually lives in, to patch bytes
             * in memory (not yet written to disk). */
            Buf *buf = (target_region == REGION_CODE) ? &code : (target_region == REGION_RODATA) ? &rodata : &data;
            uint64_t local_off = P - target_base;
            if (local_off + 4 > buf->len) continue; /* e.g. a relocation into .bss, nothing to patch on disk */

            /* A GOTPCRELX reference to a symbol THIS binary defines has to be
             * relaxed, not just have its displacement filled in.
             *
             * The instruction is `mov foo@GOTPCREL(%rip), %reg`, and it wants
             * the field to name a GOT slot HOLDING foo's address. Writing
             * foo's own address there instead leaves a mov that loads the
             * first eight bytes of foo - its opening instructions - into the
             * register. That is precisely the bug this fixes: SM64 crashed with
             * SYSTEM_XO_VIOLATION reading its own text, because the text
             * segment on this machine is execute-only, so the bogus load
             * faulted before the equally bogus call could.
             *
             * Rewriting the opcode to `lea foo(%rip), %reg` is the standard
             * relaxation these "X" relocation types exist to permit, and the
             * displacement the general case already computes is exactly right
             * for it. An import is left alone: there the mov really is reading
             * a GOT slot, which is what the loader fills in. */
            if ((r->r_type == 9 || r->r_type == 41 || r->r_type == 42) && !s_is_got_slot) {
                /* No opcode for a plain GOTPCREL: it is never relaxed, only slotted. */
                unsigned char *op = (r->r_type != 9 && local_off >= 2) ? (buf->data + local_off - 2) : NULL;
                if (op && op[0] == 0x8B) {
                    op[0] = 0x8D;                 /* mov r64,[rip+d] -> lea r64,[rip+d] */
                    relaxed_gotpcrelx++;
                } else if (op && op[0] == 0xFF && op[1] == 0x15) {
                    op[0] = 0x67; op[1] = 0xE8;   /* call *[rip+d] -> call rel32, padded */
                    relaxed_gotpcrelx++;
                } else if (op && op[0] == 0xFF && op[1] == 0x25) {
                    op[0] = 0x67; op[1] = 0xE9;   /* jmp *[rip+d]  -> jmp rel32, padded  */
                    relaxed_gotpcrelx++;
                } else {
                    int li = -1;
                    for (size_t q = 0; q < local_got_n; q++)
                        if (strcmp(local_got_names[q], sym->name) == 0) { li = (int)q; break; }
                    if (li >= 0) {
                        uint64_t slot = gotdata_addr + gotdata_count * 8 + (uint64_t)li * 8;
                        if (!local_got_emitted[li]) {          /* one fixup per slot, not per use */
                            buf_u64(&relrel, slot);
                            buf_u64(&relrel, (uint64_t)R_X86_64_RELATIVE);
                            buf_u64(&relrel, S);
                            relrel_count++;
                            local_got_emitted[li] = 1;
                        }
                        S = slot;              /* the instruction reads the slot, as compiled */
                        local_got_slotted++;
                    } else {
                        /* Say so rather than patch it and hope: a silently
                         * mispatched relocation of exactly this kind is what
                         * cost a whole debugging session. */
                        unrelaxable_gotpcrelx++;
                    }
                }
            }

            if (r->r_type == R_X86_64_JUMP_SLOT || r->r_type == 2 /* PC32 */ || r->r_type == 4 /* PLT32 */
                || r->r_type == 9 /* GOTPCREL */ || r->r_type == 41 /* GOTPCRELX */ || r->r_type == 42 /* REX_GOTPCRELX */) {
                int32_t patched = (int32_t)((int64_t)S + r->r_addend - (int64_t)P);
                memcpy(buf->data + local_off, &patched, 4);
            } else if (r->r_type == R_X86_64_64) {
                uint64_t patched = S + (uint64_t)r->r_addend;
                if (local_off + 8 <= buf->len) {
                    memcpy(buf->data + local_off, &patched, 8);
                    /* The value just written is an address measured from a
                     * load base of zero, and the module is not loaded at zero
                     * (on this machine it lands at 0x400000). Without a record
                     * telling the loader to add the base, every such pointer -
                     * and a large program is full of them, in its tables of
                     * functions and data - points that far below its target.
                     * SM64 crashed jumping to 0x2700, where 0x402700 held a
                     * perfectly good function. Same record DynamicWriter.cs
                     * emits for an absolute reference to a defined symbol. */
                    buf_u64(&relrel, P);
                    buf_u64(&relrel, (uint64_t)R_X86_64_RELATIVE);
                    buf_u64(&relrel, patched);
                    relrel_count++;
                    if (target_region != REGION_DATA) relrel_not_writable++;
                }
            }
        }
    }
    if (relaxed_gotpcrelx > 0) {
        fprintf(stderr, "dynwriter: relaxed %d GOTPCRELX reference(s) to local symbols\n", relaxed_gotpcrelx);
    }
    if (local_got_slotted > 0) {
        fprintf(stderr, "dynwriter: gave %d GOT-relative reference(s) a real GOT slot (%zu slot(s))\n",
                local_got_slotted, local_got_n);
    }
    if (unrelaxable_gotpcrelx > 0) {
        fprintf(stderr, "dynwriter: WARNING: %d GOTPCRELX reference(s) in an instruction form this "
                "does not know how to relax - they will misbehave at run time\n", unrelaxable_gotpcrelx);
    }
    if (relrel_count > 0) {
        fprintf(stderr, "dynwriter: emitted %llu base-relative relocation(s) for absolute pointers\n",
                (unsigned long long)relrel_count);
    }
    if (relrel_not_writable > 0) {
        fprintf(stderr, "dynwriter: WARNING: %d absolute pointer(s) sit in a segment the loader cannot "
                "write, so their base-relative fixups will fail at load time\n", relrel_not_writable);
    }
    if (skipped_import_relocs > 0) {
        fprintf(stderr, "dynwriter: note: %d relocation(s) against imported functions left unpatched "
                "(no PLT/GOT yet - structural-only phase, see dynwriter.h)\n", skipped_import_relocs);
    }


    /* rela.dyn: RELATIVE records MUST come first - DT_RELACOUNT tells the
     * loader how many leading entries it can apply "the fast way" (base +
     * addend, no symbol lookup at all), confirmed by reading
     * DynamicWriter.cs directly: "Order the table so every base-relative
     * record leads it: the loader treats the first relative-count records
     * as relative and fast-paths them." These eight are exactly the
     * pointers proc_param and the param-blocks buffer above need filled at
     * load time (see the big comment where paramblocks[] is built). */
    uint64_t procparam_vaddr = rw_vaddr + proc_param_offset_in_data;
    uint64_t paramblocks_vaddr = rw_vaddr + paramblocks_offset_in_data;
    Buf reladyn; buf_init(&reladyn);
    uint64_t relative_count = 0;
#define RELDYN_RELATIVE(target, addend) do { \
        buf_u64(&reladyn, (target)); \
        buf_u64(&reladyn, (uint64_t)R_X86_64_RELATIVE); \
        buf_u64(&reladyn, (addend)); \
        relative_count++; \
    } while (0)
    RELDYN_RELATIVE(procparam_vaddr + 0x38, paramblocks_vaddr + PB_OFF_LIBCPARAM);       /* LibcParam */
    RELDYN_RELATIVE(procparam_vaddr + 0x40, paramblocks_vaddr + PB_OFF_KMEMPARAM);       /* KernelMemParam */
    RELDYN_RELATIVE(procparam_vaddr + 0x48, paramblocks_vaddr + PB_OFF_KFSPARAM);        /* KernelFsParam */
    RELDYN_RELATIVE(paramblocks_vaddr + 0x30, paramblocks_vaddr + PB_OFF_MALLOCREPL);    /* malloc replace table */
    RELDYN_RELATIVE(paramblocks_vaddr + 0x38, paramblocks_vaddr + PB_OFF_NEWREPL);       /* new replace table */
    RELDYN_RELATIVE(paramblocks_vaddr + 0x60, paramblocks_vaddr + PB_OFF_TLSMALLOCREPL); /* TLS malloc replace table */
    RELDYN_RELATIVE(paramblocks_vaddr + 0x10, paramblocks_vaddr + PB_OFF_HEAPSIZE);      /* heap size limit */
    RELDYN_RELATIVE(paramblocks_vaddr + 0x20, paramblocks_vaddr + PB_OFF_HEAPEXT);       /* heap extended flag */
#undef RELDYN_RELATIVE
    /* Then one per absolute pointer, still inside the leading run of relative
     * records that DT_RELACOUNT describes - they must come before the
     * GLOB_DAT records below, not after, or the loader's fast path would
     * treat symbol records as base-relative ones. */
    buf_push(&reladyn, relrel.data, relrel.len);
    relative_count += relrel_count;
    /* Plain data GOT slots (GOTPCRELX-style references, no PLT stub) get a
     * GLOB_DAT record in the general relocation table instead of rela.plt -
     * that's the conventional split (JUMP_SLOT/rela.plt for PLT-bound
     * calls, GLOB_DAT/rela.dyn for everything else), kept here rather than
     * mixed into rela.plt so DT_PLTRELSZ stays an accurate PLT-only count.
     * These come AFTER the RELATIVE block above - order matters. */
    for (size_t i = 0; i < resolution->import_count; i++) {
        if (gotdata_index[i] < 0) continue;
        uint64_t got_slot = gotdata_addr + (uint64_t)gotdata_index[i] * 8;
        uint64_t dynsym_index = i + 1;
        buf_u64(&reladyn, got_slot);
        buf_u64(&reladyn, (dynsym_index << 32) | R_X86_64_GLOB_DAT);
        buf_u64(&reladyn, 0);
    }

    /* Order matters and is not arbitrary: confirmed against DynamicWriter.cs's
     * own documented layout ("the string table at the very base of the
     * group, then the symbol table, the two relocation tables ..., the
     * hash, the note, and the dynamic table last") and against the real
     * PS5 rtld's behavior - a build with dynsym/dynstr swapped (this
     * project's bug for a while) passed every individual DT_* presence
     * check but still failed calcurate_sce_dynlibdata_layout() with error
     * 8, confirmed via klogsrv: "[rtld] ERROR calcurate_sce_dynlibdata_
     * layout:8358: preprocess_dt_entries() fails: 8" - the real rtld
     * computes table sizes/bounds from fixed relative positions, so the
     * order has to match exactly, not just "every table present somewhere". */
    uint64_t off_dynstr = seg2_off;
    uint64_t off_dynsym = align_up(off_dynstr + dynstr.bytes.len, 8);
    uint64_t off_relaplt = align_up(off_dynsym + dynsym.len, 8);
    uint64_t off_reladyn = align_up(off_relaplt + relaplt.len, 8);
    uint64_t off_hash = align_up(off_reladyn + reladyn.len, 8);
    /* The GNU build-id note lives HERE, inside the dynlink segment, between
     * hash and dynamic (4-aligned) - confirmed re-reading DynamicWriter.cs
     * lines 803-815 much more carefully than the first pass: "the string
     * table at the very base ... then the symbol table, the two relocation
     * tables ..., the hash, THE NOTE, and the dynamic table last." An
     * earlier version of this file put this note in the unmapped file-tail
     * area alongside COMMENT/VERSION/the OTHER (SIE) note instead, with
     * vaddr=0 - wrong: the real reference's build-id NOTE phdr has a real,
     * non-zero vaddr squarely inside the dynlink segment's range, right
     * before DYNAMIC's. This is a second, distinct note from the SIE one
     * (which does stay in the outer tail, unmapped, per DynamicWriter.cs's
     * own separate `tailNoteFileOff` - confirmed those really are two
     * different notes, not the same one misplaced). */
    uint8_t note_gnu[0x24]; memset(note_gnu, 0, sizeof(note_gnu));
    { uint32_t namesz = 4, descsz = 0x14, type = 3;
      memcpy(note_gnu, &namesz, 4); memcpy(note_gnu + 4, &descsz, 4); memcpy(note_gnu + 8, &type, 4);
      memcpy(note_gnu + 12, "GNU", 4); }
    uint64_t off_note_gnu = align_up(off_hash + hash.len, 4);
    uint64_t off_dynamic = align_up(off_note_gnu + sizeof(note_gnu), 8);

    /* DT_INIT/DT_FINI: every real module measured carries these right
     * after the core tables ("then the setup and teardown routines" per
     * DynamicWriter.cs's own documented tag order) - missed originally
     * since crt1.S's _start calls _init/_fini directly as plain functions
     * rather than relying on the loader to invoke them via these tags.
     * Look their addresses up now (a small local search, mirroring Pass
     * 2's logic below) since dynv is built before that pass runs. */
    uint64_t init_addr = 0, fini_addr = 0;
    for (size_t oi = 0; oi < object_count && (!init_addr || !fini_addr); oi++) {
        ElfObject *obj = objects[oi];
        for (size_t si = 0; si < obj->symbol_count; si++) {
            ElfSymbol *sym = &obj->symbols[si];
            if (sym->is_undefined || sym->shndx == 0 || sym->shndx >= obj->section_count || !sym->name[0]) continue;
            int is_init = !init_addr && strcmp(sym->name, "_init") == 0;
            int is_fini = !fini_addr && strcmp(sym->name, "_fini") == 0;
            if (!is_init && !is_fini) continue;
            /* _init/_fini are ordinary functions, always in the code
             * region in practice - resolved here using only code_vaddr
             * (already known at this point in the function) specifically
             * to avoid a layout-ordering dependency on rw_vaddr, which
             * isn't computed until later. */
            RegionKind k = placements[oi][sym->shndx].region;
            if (k != REGION_CODE) continue;
            uint64_t addr = code_vaddr + placements[oi][sym->shndx].offset + sym->value;
            if (is_init) init_addr = addr; else fini_addr = addr;
        }
    }

    Buf dynv; buf_init(&dynv);
#define DYN(tag, val) do { buf_u64(&dynv, (uint64_t)(tag)); buf_u64(&dynv, (uint64_t)(val)); } while (0)
    for (size_t i = 0; i < mod_n; i++) {
        DYN(DT_NEEDED, mods[i].soname_off);
        DYN(DT_SCE_NEEDED_MODULE, (uint64_t)mods[i].module_name_off | ((uint64_t)mods[i].version << 32) | ((uint64_t)mods[i].id << 48));
        for (size_t j = 0; j < lib_n; j++) {
            if (libs[j].module_id != mods[i].id) continue;
            DYN(DT_SCE_IMPORT_LIB, (uint64_t)libs[j].lib_name_off | ((uint64_t)libs[j].version << 32) | ((uint64_t)libs[j].id << 48));
            DYN(DT_SCE_IMPORT_LIB_ATTR, ((uint64_t)libs[j].id << 48) | 0x09);
        }
    }
    /* Packs the name offset with THIS module's own version in bits 32-47,
     * exactly like DT_SCE_NEEDED_MODULE does for an imported one - missed
     * originally (emitted as a bare offset), confirmed by reading
     * DynamicWriter.cs:1819 directly: `moduleInfoName | (DefaultModuleVersion << 32)`. */
    DYN(DT_SCE_MODULE_INFO, (uint64_t)off_selfname | ((uint64_t)SCE_DEFAULT_MODULE_VERSION << 32));
    DYN(DT_SCE_MODULE_ATTR, 0);
    DYN(DT_SCE_ORIG_FILENAME, off_selfname);
    /* Exact tag order below confirmed against a REAL, correctly-extracted
     * reference title (SharpProspero's own `self --extract` on a known-
     * launchable eboot, not a guess or a paraphrased comment) - it is
     * NOT what an earlier version of this file assumed (that version had
     * HASH/STRTAB/SYMTAB before RELA/JMPREL/PLTGOT, and was missing DEBUG,
     * RELACOUNT, and all six PREINIT_ARRAY/INIT_ARRAY/FINI_ARRAY tags
     * entirely) - confirmed to matter: the real PS5 rtld's
     * calcurate_sce_dynlibdata_layout() failed with error 8 on every
     * build until this exact order and tag set was used, verified via
     * klogsrv on real hardware, several fix attempts at other candidates
     * (dynsym/dynstr order, MODULE_INFO packing, proc_param placement -
     * all real bugs, but not this one) having failed to change the
     * outcome. DEBUG is always 0. RELACOUNT counts the leading
     * R_X86_64_RELATIVE entries in .rela.dyn - the eight param-block
     * pointer fixups built above are always present now, so .rela.dyn is
     * never actually empty and DT_RELA/RELASZ/RELAENT are unconditional.
     * The three ARRAY/ARRAYSZ tag pairs are always 0 - no .init_array/
     * .fini_array walking is implemented (see crt1.S's file header). */
    DYN(DT_DEBUG, 0);
    DYN(DT_RELA, TOVADDR(off_reladyn));
    DYN(DT_RELASZ, reladyn.len);
    DYN(DT_RELAENT, 24);
    DYN(DT_RELACOUNT, relative_count);
    if (bound_count > 0) {
        DYN(DT_JMPREL, TOVADDR(off_relaplt));
        DYN(DT_PLTRELSZ, relaplt.len);
        DYN(DT_PLTGOT, got_vaddr);
        DYN(DT_PLTREL, DT_RELA);
    }
    DYN(DT_SYMTAB, TOVADDR(off_dynsym));
    DYN(DT_SYMENT, 24);
    DYN(DT_STRTAB, TOVADDR(off_dynstr));
    DYN(DT_STRSZ, dynstr.bytes.len);
    DYN(DT_HASH, TOVADDR(off_hash));
    DYN(DT_PREINIT_ARRAY, 0);
    DYN(DT_PREINIT_ARRAYSZ, 0);
    DYN(DT_INIT_ARRAY, 0);
    DYN(DT_INIT_ARRAYSZ, 0);
    DYN(DT_FINI_ARRAY, 0);
    DYN(DT_FINI_ARRAYSZ, 0);
    DYN(DT_INIT, init_addr);
    DYN(DT_FINI, fini_addr);
    DYN(DT_SCE_SYMTABSZ, dynsym.len);
    DYN(DT_SCE_HASHSZ, hash.len);
    DYN(DT_NULL, 0);
#undef DYN
    uint64_t dynamic_size = dynv.len;
    uint64_t seg2_filesz = (off_dynamic + dynamic_size) - seg2_off;

    /* ---- Segment 3: RW data (.data, .sce_process_param, then .bss beyond filesz).
     * rw_vaddr itself was already computed above (needed earlier, to
     * derive dynlink_vaddr); only the FILE position is decided here. */
    uint64_t seg3_off = align_up(seg2_off + seg2_filesz, SCE_SEG_ALIGN);
    uint64_t seg3_filesz = data_real_len;
    uint64_t seg3_memsz = data_region_memsz;
    /* procparam_vaddr already computed earlier (needed by the RELATIVE
     * relocations in .rela.dyn, built before the DYNAMIC table). */

    uint64_t entry_addr = code_vaddr;
    if (entry_symbol) {
        int found = 0;
        for (size_t k = 0; k < name_addr_n; k++) {
            if (strcmp(name_addrs[k].name, entry_symbol) == 0) { entry_addr = name_addrs[k].addr; found = 1; break; }
        }
        if (!found) {
            fprintf(stderr, "dynwriter: entry symbol '%s' not defined by any linked object\n", entry_symbol);
            free(name_addrs);
            return 0;
        }
    }
    free(name_addrs);

    /* ---- Section headers, for readelf/binutils convenience. */
    StrTab shstrtab; strtab_init(&shstrtab);
    uint32_t shn_text = strtab_add(&shstrtab, ".text");
    uint32_t shn_rodata = strtab_add(&shstrtab, ".rodata");
    uint32_t shn_dynsym = strtab_add(&shstrtab, ".dynsym");
    uint32_t shn_dynstr = strtab_add(&shstrtab, ".dynstr");
    uint32_t shn_hash = strtab_add(&shstrtab, ".hash");
    uint32_t shn_dynamic = strtab_add(&shstrtab, ".dynamic");
    uint32_t shn_data = strtab_add(&shstrtab, ".data");
    uint32_t shn_procparam = strtab_add(&shstrtab, ".sce_process_param");
    uint32_t shn_shstrtab = strtab_add(&shstrtab, ".shstrtab");

    /* PT_SCE_COMMENT / PT_SCE_VERSION / two NOTEs - the "file tail" records
     * every real module carries (DynamicWriter.cs:1698-1702, BuildComment/
     * BuildVersion ~2072-2116). Confirmed on real hardware to be required,
     * not cosmetic: a module missing these crashes SceSysCore.elf itself
     * while it tries to read "the Prospero and PS4 SDK version" of the
     * eboot, rather than just failing to launch cleanly (kernel log:
     * "failed to get both Prospero and PS4 SDK version ... processSpawn()
     * error: 0x80aa001a" immediately followed by SceSysCore exiting on
     * SIGSYS) - discovered via the first real hardware test of this linker. */
    Buf comment_blob; buf_init(&comment_blob);
    {
        size_t name_len = strlen(module_file_name) + 1; /* +1 for the NUL */
        size_t blob_len = ((12 + name_len) + 3) & ~(size_t)3;
        buf_pad_to(&comment_blob, blob_len);
        memcpy(comment_blob.data, "PATH", 4);
        uint32_t after_tag = (uint32_t)(blob_len - 8);
        uint32_t text_len = (uint32_t)name_len;
        memcpy(comment_blob.data + 4, &after_tag, 4);
        memcpy(comment_blob.data + 8, &text_len, 4);
        memcpy(comment_blob.data + 12, module_file_name, name_len);
    }

    Buf version_blob; buf_init(&version_blob);
    {
        /* One record per component: the CRT start object first, then every
         * needed module's soname, in first-seen order - matching
         * DynamicWriter.cs:715's `[CrtEmitter.StartComponentName, ..
         * moduleIndex.Keys]`. */
        const char *components[64];
        size_t ncomp = 0;
        components[ncomp++] = "crt1";
        for (size_t i = 0; i < mod_n && ncomp < 64; i++) components[ncomp++] = mods[i].soname;

        for (size_t c = 0; c < ncomp; c++) {
            char name_colon[128];
            snprintf(name_colon, sizeof(name_colon), "%s:", components[c]);
            size_t name_len = strlen(name_colon);
            uint16_t body = (uint16_t)(1 + name_len + 2 * 8);
            buf_u32(&version_blob, 0); /* the leading zero word the header always carries */
            /* overwrite bytes 2-3 of that word with the body length (LE u16) */
            memcpy(version_blob.data + version_blob.len - 2, &body, 2);
            buf_u8(&version_blob, 8); /* VersionWordSize */
            buf_push(&version_blob, name_colon, name_len);
            for (int w = 0; w < 2; w++) {
                uint32_t be_sdk = ((SCE_MODULE_SDK_VERSION & 0xFF) << 24) | ((SCE_MODULE_SDK_VERSION & 0xFF00) << 8)
                                | ((SCE_MODULE_SDK_VERSION & 0xFF0000) >> 8) | ((SCE_MODULE_SDK_VERSION & 0xFF000000) >> 24);
                uint32_t be_one = 0x01000000;
                buf_u32(&version_blob, be_sdk);
                buf_u32(&version_blob, be_one);
            }
        }
    }

    /* The reserved SIE tail note: namesz/descsz/type all left 0, only the
     * name "SIE\0" at +12 is ever written (DynamicWriter.cs:1769) - the
     * remaining 8 bytes stay zero, "outside every segment the container
     * stores" per its own comment. */
    uint8_t tail_note[0x18]; memset(tail_note, 0, sizeof(tail_note));
    memcpy(tail_note + 12, "SIE", 4);

    /* 16-aligned, not just 8: PT_SCE_COMMENT's p_align is 0x10 and its
     * vaddr is 0, so the ELF p_vaddr===p_offset (mod p_align) rule needs
     * this file offset to be a clean multiple of 16, not just 8. */
    uint64_t comment_off = align_up(got_off + got_size, 16);
    uint64_t version_off = align_up(comment_off + comment_blob.len, 8);
    uint64_t tail_note_off = align_up(version_off + version_blob.len, 8);

    /* Minimal well-formed but EMPTY .eh_frame_hdr: version=1, then three
     * DW_EH_PE_omit(0xff) bytes for eh_frame_ptr_enc/fde_count_enc/
     * table_enc - each "omit" byte means the corresponding field (the
     * pointer back to .eh_frame, the FDE count, and the binary-search
     * table) is simply absent. A valid, standard way to say "no unwind
     * info", short of actually merging/relocating real CFI data. */
    static const uint8_t eh_frame_hdr[4] = { 1, 0xff, 0xff, 0xff };
    uint64_t eh_frame_hdr_off = align_up(tail_note_off + sizeof(tail_note), 4);

    uint64_t shstrtab_off = align_up(eh_frame_hdr_off + sizeof(eh_frame_hdr), 8);
    uint64_t shoff = align_up(shstrtab_off + shstrtab.bytes.len, 8);

    enum { SEC_NULL, SEC_TEXT, SEC_RODATA, SEC_DYNSYM, SEC_DYNSTR, SEC_HASH, SEC_DYNAMIC, SEC_DATA, SEC_PROCPARAM, SEC_SHSTRTAB, SEC_COUNT };
    Elf64_Shdr sh[SEC_COUNT];
    memset(sh, 0, sizeof(sh));
    sh[SEC_TEXT] = (Elf64_Shdr){ shn_text, SHT_PROGBITS_VAL, SHF_ALLOC_VAL | SHF_EXECINSTR_VAL, code_vaddr, seg1_off, code.len, 0, 0, 16, 0 };
    if (has_rodata)
        sh[SEC_RODATA] = (Elf64_Shdr){ shn_rodata, SHT_PROGBITS_VAL, SHF_ALLOC_VAL, rodata_vaddr, rodata_off, rodata.len, 0, 0, 8, 0 };
    sh[SEC_DYNSYM] = (Elf64_Shdr){ shn_dynsym, SHT_DYNSYM_VAL, SHF_ALLOC_VAL, off_dynsym, off_dynsym, dynsym.len, SEC_DYNSTR, 1, 8, 24 };
    sh[SEC_DYNSTR] = (Elf64_Shdr){ shn_dynstr, 3 /* SHT_STRTAB */, SHF_ALLOC_VAL, off_dynstr, off_dynstr, dynstr.bytes.len, 0, 0, 1, 0 };
    sh[SEC_HASH] = (Elf64_Shdr){ shn_hash, SHT_HASH_VAL, SHF_ALLOC_VAL, off_hash, off_hash, hash.len, SEC_DYNSYM, 0, 8, 4 };
    sh[SEC_DYNAMIC] = (Elf64_Shdr){ shn_dynamic, SHT_DYNAMIC_VAL, SHF_ALLOC_VAL | SHF_WRITE_VAL, off_dynamic, off_dynamic, dynamic_size, SEC_DYNSTR, 0, 8, 16 };
    sh[SEC_DATA] = (Elf64_Shdr){ shn_data, SHT_PROGBITS_VAL, SHF_ALLOC_VAL | SHF_WRITE_VAL, rw_vaddr, seg3_off, seg3_filesz, 0, 0, 8, 0 };
    sh[SEC_PROCPARAM] = (Elf64_Shdr){ shn_procparam, SHT_PROGBITS_VAL, SHF_ALLOC_VAL | SHF_WRITE_VAL, procparam_vaddr, seg3_off + proc_param_offset_in_data, SCE_PROC_PARAM_SIZE, 0, 0, 8, 0 };
    sh[SEC_SHSTRTAB] = (Elf64_Shdr){ shn_shstrtab, 3, 0, 0, shstrtab_off, shstrtab.bytes.len, 0, 0, 1, 0 };

    uint64_t total_size = align_up(shoff + sizeof(sh), 8);

    /* ---- Write. */
    Elf64_Ehdr eh; memset(&eh, 0, sizeof(eh));
    eh.e_ident[0] = 0x7F; eh.e_ident[1] = 'E'; eh.e_ident[2] = 'L'; eh.e_ident[3] = 'F';
    eh.e_ident[4] = 2; eh.e_ident[5] = 1; eh.e_ident[6] = 1; eh.e_ident[7] = ELFOSABI_SCE;
    /* e_ident[8] (ABI version): confirmed against a real, launchable
     * reference title's extracted ELF header (readelf: "Versión ABI: 2") -
     * ours was always 0 until this fix. Plausibly what SysCore reads as
     * the "PS4 SDK version" half of the "failed to get both Prospero and
     * PS4 SDK version" warning every one of our own builds hit and the
     * real reference never did. */
    eh.e_ident[8] = 2;
    eh.e_type = (uint16_t)ET_SCE_DYNEXEC;
    eh.e_machine = 0x3E;
    eh.e_version = 1;
    eh.e_entry = entry_addr;
    eh.e_phoff = phoff;
    eh.e_shoff = shoff;
    eh.e_ehsize = sizeof(Elf64_Ehdr);
    eh.e_phentsize = sizeof(Elf64_Phdr);
    eh.e_phnum = (uint16_t)phnum;
    eh.e_shentsize = sizeof(Elf64_Shdr);
    eh.e_shnum = SEC_COUNT;
    eh.e_shstrndx = SEC_SHSTRTAB;

    /* Program header ORDER below matches a real, correctly-extracted
     * reference title's own relative order (confirmed via readelf -l on
     * `self --extract` output): code, rodata, rw-data, [RELRO], got(another
     * rw-shaped LOAD), PROC_PARAM, DYNAMIC, TLS, [EH_FRAME], the no-
     * protection "dynlink" LOAD LAST among every PT_LOAD, then the file-
     * tail records - NOT code/dynlink/rw/DYNAMIC/PROC_PARAM/TLS/rodata/got
     * like an earlier version of this file had. Whether phdr order turns
     * out to matter to the real rtld the way DT_* tag order did is not
     * confirmed yet as of this comment - this reorder is itself the next
     * test for that, on top of the already-confirmed .dynamic content/
     * order fix. */
    /* RELRO/EH_FRAME positions fixed here: an earlier version left both at
     * the very end of the array (after the NOTEs), which still matched
     * the real reference's LOAD/DYNAMIC/PROC_PARAM/TLS relative order but
     * not this - confirmed via a real, unsigned SharpProspero sample
     * app's own phdrs (readelf -l) that RELRO comes RIGHT AFTER the LOAD
     * it covers, and EH_FRAME comes RIGHT BEFORE the final ("dynlink")
     * LOAD, not bunched at the tail. GOT is treated as the "small RW"
     * RELRO-covered segment (the reference actually splits its small RW
     * further into several relro-eligible sections alongside a bigger,
     * non-RELRO one for real .data/.bss - not fully replicated here yet,
     * only the position of the RELRO/EH_FRAME entries themselves). */
    Elf64_Phdr ph[14]; memset(ph, 0, sizeof(ph));
    ph[0] = (Elf64_Phdr){ PT_LOAD, PF_X, seg1_off, code_vaddr, code_vaddr, seg1_filesz, seg1_filesz, SCE_SEG_ALIGN };
    /* Always present, sized zero when there's no .rodata to carry - same
     * "declare it even when empty" pattern already validated for PT_TLS. */
    ph[1] = (Elf64_Phdr){ PT_LOAD, PF_R, rodata_off, rodata_vaddr, rodata_vaddr, rodata.len, rodata.len, SCE_SEG_ALIGN };
    /* GOT: writable (the loader writes each bound import's resolved address
     * into it via the .rela.plt JUMP_SLOT records before _start runs). */
    ph[2] = (Elf64_Phdr){ PT_LOAD, PF_R | PF_W, got_off, got_vaddr, got_vaddr, got_size, got_size, SCE_SEG_ALIGN };
    ph[3] = (Elf64_Phdr){ PT_GNU_RELRO, PF_R, got_off, got_vaddr, got_vaddr, got_size, align_up(got_size, SCE_SEG_ALIGN), 1 };
    ph[4] = (Elf64_Phdr){ PT_LOAD, PF_R | PF_W, seg3_off, rw_vaddr, rw_vaddr, seg3_filesz, seg3_memsz, SCE_SEG_ALIGN };
    ph[5] = (Elf64_Phdr){ PT_SCE_PROC_PARAM, PF_R, seg3_off + proc_param_offset_in_data, procparam_vaddr, procparam_vaddr, SCE_PROC_PARAM_SIZE, SCE_PROC_PARAM_SIZE, 8 };
    ph[6] = (Elf64_Phdr){ PT_DYNAMIC, PF_R | PF_W, off_dynamic, TOVADDR(off_dynamic), TOVADDR(off_dynamic), dynamic_size, dynamic_size, 8 };
    ph[7] = (Elf64_Phdr){ PT_TLS, PF_R, 0, got_vaddr, got_vaddr, 0, 0, 1 };
    ph[8] = (Elf64_Phdr){ PT_GNU_EH_FRAME, PF_R, eh_frame_hdr_off, 0, 0, sizeof(eh_frame_hdr), sizeof(eh_frame_hdr), 4 };
    ph[9] = (Elf64_Phdr){ PT_LOAD, 0, seg2_off, dynlink_vaddr, dynlink_vaddr, seg2_filesz, seg2_filesz, SCE_SEG_ALIGN };
    /* File-tail records (DynamicWriter.cs:1698-1702) - see the comment
     * above comment_blob's construction for why these turned out to be
     * required, not cosmetic. vaddr=0 for all four: none of them are
     * mapped memory, the loader reads them directly from the file. */
    ph[10] = (Elf64_Phdr){ PT_SCE_COMMENT, 0, comment_off, 0, 0, comment_blob.len, 0, 0x10 };
    ph[11] = (Elf64_Phdr){ PT_SCE_VERSION, 0, version_off, 0, 0, version_blob.len, version_blob.len, 1 };
    ph[12] = (Elf64_Phdr){ PT_NOTE, 0, off_note_gnu, TOVADDR(off_note_gnu), TOVADDR(off_note_gnu), sizeof(note_gnu), sizeof(note_gnu), 4 };
    ph[13] = (Elf64_Phdr){ PT_NOTE, 0, tail_note_off, 0, 0, sizeof(tail_note), 0, 4 };

    FILE *f = fopen(out_path, "wb");
    if (!f) { perror("fopen"); return 0; }
    fwrite(&eh, sizeof(eh), 1, f);
    fwrite(ph, sizeof(ph), 1, f);

    fseek(f, (long)seg1_off, SEEK_SET); fwrite(code.data, code.len, 1, f);
    if (has_rodata) { fseek(f, (long)rodata_off, SEEK_SET); fwrite(rodata.data, rodata.len, 1, f); }
    fseek(f, (long)off_dynsym, SEEK_SET); fwrite(dynsym.data, dynsym.len, 1, f);
    fseek(f, (long)off_dynstr, SEEK_SET); fwrite(dynstr.bytes.data, dynstr.bytes.len, 1, f);
    if (relaplt.len > 0) { fseek(f, (long)off_relaplt, SEEK_SET); fwrite(relaplt.data, relaplt.len, 1, f); }
    if (reladyn.len > 0) { fseek(f, (long)off_reladyn, SEEK_SET); fwrite(reladyn.data, reladyn.len, 1, f); }
    fseek(f, (long)off_hash, SEEK_SET); fwrite(hash.data, hash.len, 1, f);
    fseek(f, (long)off_dynamic, SEEK_SET); fwrite(dynv.data, dynv.len, 1, f);
    if (seg3_filesz > 0) { fseek(f, (long)seg3_off, SEEK_SET); fwrite(data.data, seg3_filesz, 1, f); }
    if (got_size > 0) {
        /* Zero-initialized - the loader overwrites each bound slot per
         * .rela.plt before _start runs; nothing here needs real content. */
        uint8_t *zeros = calloc(got_size, 1);
        fseek(f, (long)got_off, SEEK_SET); fwrite(zeros, got_size, 1, f);
        free(zeros);
    }
    fseek(f, (long)comment_off, SEEK_SET); fwrite(comment_blob.data, comment_blob.len, 1, f);
    fseek(f, (long)version_off, SEEK_SET); fwrite(version_blob.data, version_blob.len, 1, f);
    fseek(f, (long)off_note_gnu, SEEK_SET); fwrite(note_gnu, sizeof(note_gnu), 1, f);
    fseek(f, (long)tail_note_off, SEEK_SET); fwrite(tail_note, sizeof(tail_note), 1, f);
    fseek(f, (long)eh_frame_hdr_off, SEEK_SET); fwrite(eh_frame_hdr, sizeof(eh_frame_hdr), 1, f);
    fseek(f, (long)shstrtab_off, SEEK_SET); fwrite(shstrtab.bytes.data, shstrtab.bytes.len, 1, f);
    fseek(f, (long)shoff, SEEK_SET); fwrite(sh, sizeof(sh), 1, f);
    fseek(f, (long)total_size - 1, SEEK_SET); fputc(0, f);
    fclose(f);

    printf("dynwriter: wrote %s (%llu bytes), %zu import(s) (%zu bound to a real PLT/GOT call), "
           "%d relocation(s) still unpatched\n",
           out_path, (unsigned long long)total_size, resolution->import_count, bound_count, skipped_import_relocs);

    for (size_t oi = 0; oi < object_count; oi++) free(placements[oi]);
    free(placements);
    buf_free(&code); buf_free(&rodata); buf_free(&data);
    buf_free(&dynsym); buf_free(&dynstr.bytes); buf_free(&hash); buf_free(&dynv); buf_free(&relaplt); buf_free(&reladyn);
    buf_free(&comment_blob); buf_free(&version_blob);
    free(import_called); free(bound_index); free(import_gotdata); free(gotdata_index);
    free(mods); free(libs);
    return 1;
}
