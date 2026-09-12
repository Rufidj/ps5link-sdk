#!/usr/bin/env python3
"""agcpack - put a program of any length into an AGC shader container.

A shader container (.sb, .ags) is an ELF whose .shader_text section holds more
than the program. Read from SharpProspero's mesh_vs.sb and mesh_ps.sb, the
section is laid out as:

    program                     its length is recorded in the trailer
    zero padding to 16
    "sl00" block                its length is recorded in the trailer too
    zero padding to 16
    trailer, 0x30 bytes         ends the section

and .shader_header records the section's size at +0x44. Both containers agree:
mesh_vs has a 0x198 program, sl00 at 0x1a0, the trailer at 0x210 and a 0x240
section.

So a longer program is a matter of moving the sl00 block and the trailer along,
correcting the two lengths, and rewriting the ELF around the bigger section.
The trailer and header also carry what looks like a hash of the program; the
driver was found not to check it, so it is left as it is.

    agcpack.py pack  <container> <program.bin> <out container> [--vgprs N] [--or-reg OFF=BITS]
    agcpack.py embed <header in> <header out> <array name> <container>
    agcpack.py array <header out> <array name> <container>
    agcpack.py texture-container <mesh_ps.sb> <out container>

pack refuses to run unless repacking the container's own program reproduces
the container byte for byte, which is what shows the layout above is the whole
story for that file.
"""
import struct
import sys

TRAILER_SIZE = 0x30
TRAILER_PROGRAM_LENGTH = 0x14
TRAILER_SL00_LENGTH = 0x1C
HEADER_TEXT_SIZE = 0x44
SHT_NOBITS = 8


def align(value, to):
    return (value + to - 1) & ~(to - 1)


def u32(data, at):
    return struct.unpack_from("<I", data, at)[0]


def read_sections(elf):
    shoff = struct.unpack_from("<Q", elf, 0x28)[0]
    entsize, count, strndx = struct.unpack_from("<HHH", elf, 0x3A)
    sections = []
    for i in range(count):
        fields = struct.unpack_from("<IIQQQQIIQQ", elf, shoff + i * entsize)
        keys = ("name_at", "type", "flags", "addr", "offset", "size", "link", "info", "align", "entsize")
        s = dict(zip(keys, fields))
        s["data"] = b"" if s["type"] == SHT_NOBITS else elf[s["offset"]:s["offset"] + s["size"]]
        sections.append(s)
    names = sections[strndx]["data"]
    for s in sections:
        s["name"] = names[s["name_at"]:names.index(b"\0", s["name_at"])].decode()
    return sections, entsize, strndx


def write_elf(original, sections, entsize, strndx):
    """Lays the sections out in their original order, each at its alignment,
    after whatever preceded the first one, then the section headers."""
    ordered = sorted((s for s in sections if s["size"] or s["offset"]), key=lambda s: s["offset"])
    first = min(s["offset"] for s in ordered if s["data"]) if ordered else 0x40
    out = bytearray(original[:first])
    for s in ordered:
        if not s["data"]:
            continue
        at = align(len(out), max(s["align"], 1))
        out += b"\0" * (at - len(out))
        s["offset"] = at
        s["size"] = len(s["data"])
        out += s["data"]
    shoff = align(len(out), 8)
    out += b"\0" * (shoff - len(out))
    for s in sections:
        out += struct.pack("<IIQQQQIIQQ", s["name_at"], s["type"], s["flags"], s["addr"], s["offset"],
                           s["size"], s["link"], s["info"], s["align"], s["entsize"])
    struct.pack_into("<Q", out, 0x28, shoff)
    struct.pack_into("<HHH", out, 0x3A, entsize, len(sections), strndx)
    return bytes(out)


def repack_text(text, program):
    trailer_at = len(text) - TRAILER_SIZE
    program_length = u32(text, trailer_at + TRAILER_PROGRAM_LENGTH)
    sl00_length = u32(text, trailer_at + TRAILER_SL00_LENGTH)
    sl00_at = align(program_length, 16)
    if text[sl00_at:sl00_at + 4] != b"sl00" or align(sl00_at + sl00_length, 16) != trailer_at:
        raise SystemExit("the .shader_text layout is not the one this tool knows")

    sl00 = text[sl00_at:sl00_at + sl00_length]
    trailer = bytearray(text[trailer_at:])
    struct.pack_into("<I", trailer, TRAILER_PROGRAM_LENGTH, len(program))

    out = bytearray(program)
    out += b"\0" * (align(len(out), 16) - len(out))
    out += sl00
    out += b"\0" * (align(len(out), 16) - len(out))
    out += trailer
    return bytes(out), program_length


def pack(container, program):
    sections, entsize, strndx = read_sections(container)
    by_name = {s["name"]: s for s in sections}
    text, header = by_name[".shader_text"], by_name[".shader_header"]
    new_text, _ = repack_text(text["data"], program)
    new_header = bytearray(header["data"])
    struct.pack_into("<I", new_header, HEADER_TEXT_SIZE, len(new_text))
    text["data"], header["data"] = new_text, bytes(new_header)
    return write_elf(container, sections, entsize, strndx)


def own_program(container):
    sections, _, _ = read_sections(container)
    text = next(s for s in sections if s["name"] == ".shader_text")["data"]
    return text[:u32(text, len(text) - TRAILER_SIZE + TRAILER_PROGRAM_LENGTH)]


RSRC1_OFFSETS = (0x00A, 0x08A)          # SPI_SHADER_PGM_RSRC1_PS, SPI_SHADER_PGM_RSRC1_VS
RSRC1_VGPRS_MASK = 0x3F                  # bits 0-5, per Mesa's gfx10.json


def set_vgprs(container, vgprs):
    """Rewrites the VGPRS field - how many vector registers the program may
    use - in the RSRC1 register the header asks to be written. The header
    holds two register lists, at the pointers in +24 and +32 with counts in
    bytes 91 and 92, each entry an offset, padding and value."""
    sections, entsize, strndx = read_sections(container)
    header = next(s for s in sections if s["name"] == ".shader_header")
    h = bytearray(header["data"])
    patched = 0
    for pointer_at, count_at in ((24, 91), (32, 92)):
        base = struct.unpack_from("<Q", h, pointer_at)[0]
        for i in range(h[count_at]):
            at = base + 8 * i
            offset, _, value = struct.unpack_from("<HHI", h, at)
            if offset in RSRC1_OFFSETS:
                struct.pack_into("<I", h, at + 4, (value & ~RSRC1_VGPRS_MASK) | (vgprs & RSRC1_VGPRS_MASK))
                patched += 1
    if patched != 1:
        raise SystemExit(f"expected one RSRC1 register in the header, found {patched}")
    header["data"] = bytes(h)
    return write_elf(container, sections, entsize, strndx)


def texture_container(container):
    """Turns SharpProspero's mesh pixel program container - two interpolants,
    no resources - into one that also declares a texture and a sampler, the
    resources every textured pixel program here reads: the texture's
    descriptor in user-data dwords 0-7 (s[0:7]) and the sampler's in 8-11
    (s[8:11]). The program itself is replaced afterwards by `pack`.

    Only header fields that describe resources change. Worked out by setting
    the headers of pixel programs that do declare these two resources beside
    this one: every other difference between them followed their programs
    (sizes, checksums) or their interpolants, which match here.

      +0x40  header size: 0x160 -> 0x170, for the two slot tables appended
      +0x4C  0x06 -> 0x0B
      +0x76  user-data dwords the resources take: 12
      +0xAC  flags beside the text's trailer offset: high byte 0x01 -> 0x21
      +0x120..+0x130  self-relative offsets of the per-kind slot tables
      +0x13E, +0x142  the texture and sampler counts
      +0x160  slot table: the texture at dword 0
      +0x168  slot table: the sampler at dword 8 (0x8000: a small resource)
      register 0x00A (SPI_SHADER_PGM_RSRC1_PS): SGPRS, bits 6-9, = 1
      register 0x00B (SPI_SHADER_PGM_RSRC2_PS): 0x18
    """
    sections, entsize, strndx = read_sections(container)
    header = next(s for s in sections if s["name"] == ".shader_header")
    h = bytearray(header["data"])
    expected = {0x40: 0x160, 0x120: 0x40, 0x128: 0x38, 0x130: 0x30, 0x138: 0x0000000B00000000, 0x140: 0}
    for at, value in expected.items():
        got = struct.unpack_from("<I" if at == 0x40 else "<Q", h, at)[0]
        if len(h) != 0x160 or got != value:
            raise SystemExit("this is not the resource-less pixel program header this tool knows "
                             "(SharpProspero's mesh_ps.sb)")
    h += bytes(16)
    struct.pack_into("<I", h, 0x40, len(h))
    struct.pack_into("<I", h, 0x4C, 0x0B)
    h[0x76] = 12
    flags = struct.unpack_from("<I", h, 0xAC)[0]
    struct.pack_into("<I", h, 0xAC, (flags & 0x00FFFFFF) | 0x21000000)
    struct.pack_into("<QQQ", h, 0x120, 0x48, 0x40, 0x40)
    struct.pack_into("<Q", h, 0x138, 0x0001000B00000000)
    struct.pack_into("<Q", h, 0x140, 0x0000000000010000)
    struct.pack_into("<Q", h, 0x160, 0x0000)
    struct.pack_into("<Q", h, 0x168, 0x8008)
    patched = 0
    for pointer_at, count_at in ((24, 91), (32, 92)):
        base = struct.unpack_from("<Q", h, pointer_at)[0]
        for i in range(h[count_at]):
            at = base + 8 * i
            reg, _, value = struct.unpack_from("<HHI", h, at)
            if reg == 0x00A:
                struct.pack_into("<I", h, at + 4, (value & ~0x3C0) | (1 << 6))
                patched += 1
            elif reg == 0x00B:
                struct.pack_into("<I", h, at + 4, 0x18)
                patched += 1
    if patched != 2:
        raise SystemExit(f"expected the RSRC1 and RSRC2 registers in the header, found {patched}")
    header["data"] = bytes(h)
    return write_elf(container, sections, entsize, strndx)


def texture_container_n(container, n_textures):
    """Generalization of texture_container() to N textures (and N samplers,
    one per texture, same UV set). Confident vs. guessed parts are marked
    below - only the fields that follow a mechanical, obviously-scaling
    pattern from the N=1 case are filled in with confidence; texture_container
    for N=1 is kept as the source of truth and this must reproduce it exactly
    when n_textures == 1 (checked at the end).

    Each per-kind resource table holds tightly-packed 16-bit "Sharp" entries
    (bit15 small-resource flag, bits0-14 dword offset - matches AgcShader.cs's
    TryGetResourceSlot exactly), and the WHOLE table is padded up to an 8-byte
    boundary regardless of how many entries fit in that space - confirmed by
    N=0 and N=1 both needing an 8-byte-aligned region even though N=1's single
    entry only needs 2 bytes. region_size(N) = ((2*N + 7)//8)*8 - for N=1..4
    this is a constant 8 bytes (which is why an earlier version of this
    function, treating each entry as its own 8-byte slot, happened to work at
    N=1 but silently wrote N=2's second entry into a location the runtime
    never reads: confirmed on hardware via ps5gpu_debug_program_slots - the
    second texture's dword offset came back 0 instead of 12, same as the
    first, and this stride mismatch is why).

    +0x76 user-data dword count is 12*N; +0x13E/+0x142 texture/sampler counts
    are N (these two were already right - the debug output showed the runtime
    correctly parsing count=2, only the per-entry values beyond index 0 were
    wrong).

    The three self-relative pointer qwords at +0x120/+0x128/+0x130 (in the
    resource-less base header: 0x40, 0x38, 0x30, each resolving - field
    address plus value - to the same address, the header's end) point into a
    fixed sequence of four per-kind resource tables right after the header:
    ReadOnly (textures - fixed at +0x160, never patched, hence never mentioned
    above), ReadWrite (unused here, always zero-length), Sampler,
    ConstantBuffer (unused, always zero-length):

      target(ReadWrite)      = 0x160 + region_size(N)
      target(Sampler)        = 0x160 + region_size(N)    (ReadWrite is empty)
      target(ConstantBuffer) = 0x160 + 2*region_size(N)

    which for N=0 gives 0x160/0x160/0x160 and for N=1 gives 0x168/0x168/0x170
    - both match texture_container(), already proven on hardware.
    """
    if n_textures < 1:
        raise SystemExit("need at least one texture")
    sections, entsize, strndx = read_sections(container)
    header = next(s for s in sections if s["name"] == ".shader_header")
    h = bytearray(header["data"])
    expected = {0x40: 0x160, 0x120: 0x40, 0x128: 0x38, 0x130: 0x30, 0x138: 0x0000000B00000000, 0x140: 0}
    for at, value in expected.items():
        got = struct.unpack_from("<I" if at == 0x40 else "<Q", h, at)[0]
        if len(h) != 0x160 or got != value:
            raise SystemExit("this is not the resource-less pixel program header this tool knows "
                             "(SharpProspero's mesh_ps.sb)")
    region_size = ((2 * n_textures + 7) // 8) * 8   # each per-kind table, 8-byte aligned as a whole
    h += bytes(2 * region_size)                     # one region for textures, one for samplers
    struct.pack_into("<I", h, 0x40, len(h))
    struct.pack_into("<I", h, 0x4C, 0x0B)
    h[0x76] = 12 * n_textures
    flags = struct.unpack_from("<I", h, 0xAC)[0]
    struct.pack_into("<I", h, 0xAC, (flags & 0x00FFFFFF) | 0x21000000)
    struct.pack_into("<QQQ", h, 0x120,
                      (0x160 + region_size) - 0x120,
                      (0x160 + region_size) - 0x128,
                      (0x160 + 2 * region_size) - 0x130)
    struct.pack_into("<Q", h, 0x138, 0x0001000B00000000)
    h[0x13E] = n_textures
    h[0x142] = n_textures
    tex_table_at = 0x160
    samp_table_at = 0x160 + region_size
    for i in range(n_textures):
        struct.pack_into("<H", h, tex_table_at + 2 * i, 12 * i)
        struct.pack_into("<H", h, samp_table_at + 2 * i, 0x8000 | (12 * i + 8))
    # SPI_SHADER_PGM_RSRC1_PS.SGPRS: NUM_SGPR = (SGPRS+1)*8, so the wave has
    # room for the 12*N dwords of user data this shader now needs. N=1 needs
    # SGPRS=1 (16 SGPRs for 12 dwords) - texture_container() hardcodes exactly
    # that value, which is where this formula comes from. Left fixed at N=1's
    # value for any N, the console does not reject the shader, but reads
    # garbage into the resource descriptors it never actually had room to
    # load - confirmed on hardware: garbage/static showed up for BOTH texture
    # units at N=2 with unit 0 in isolation, not only the new unit 1.
    # +1 step (8 extra SGPRs) of margin beyond the exact fit: at N=2 (24
    # dwords, landing exactly on a granularity boundary with no slack) unit 1
    # - the last 12 dwords, s[12:23] - came back readable in the resource
    # metadata but still sampled wrong on hardware, while unit 0 (s[0:11],
    # comfortably inside the allocation) was fine. The exact-fit granularity
    # boundary leaves no room for whatever few SGPRs the hardware reserves
    # beyond straight user-data forwarding, and the tail of the range - unit
    # 1 here - is what gets clipped first.
    sgprs_field = (12 * n_textures + 7) // 8 - 1
    if n_textures > 1:
        sgprs_field = 0xF   # deliberately maxed out (128 SGPRs) - a decisive test to
                             # confirm or rule out SGPR budget as the remaining cause;
                             # N=1 must stay exactly at texture_container()'s proven value
    if sgprs_field > 0xF:
        raise SystemExit(f"N={n_textures} needs more SGPRs than this 4-bit field can encode")
    patched = 0
    for pointer_at, count_at in ((24, 91), (32, 92)):
        base = struct.unpack_from("<Q", h, pointer_at)[0]
        for i in range(h[count_at]):
            at = base + 8 * i
            reg, _, value = struct.unpack_from("<HHI", h, at)
            if reg == 0x00A:
                struct.pack_into("<I", h, at + 4, (value & ~0x3C0) | (sgprs_field << 6))
                patched += 1
            elif reg == 0x00B:
                struct.pack_into("<I", h, at + 4, 0x18)
                patched += 1
    if patched != 2:
        raise SystemExit(f"expected the RSRC1 and RSRC2 registers in the header, found {patched}")
    header["data"] = bytes(h)
    return write_elf(container, sections, entsize, strndx)


def or_register(container, offset, bits):
    """Sets `bits` in the value of every entry for register `offset` in the
    header's two register lists - e.g. 0x1B3 and 0x1B4, SPI_PS_INPUT_ENA and its
    companion, to hand the program the fragment's position."""
    sections, entsize, strndx = read_sections(container)
    header = next(s for s in sections if s["name"] == ".shader_header")
    h = bytearray(header["data"])
    patched = 0
    for pointer_at, count_at in ((24, 91), (32, 92)):
        base = struct.unpack_from("<Q", h, pointer_at)[0]
        for i in range(h[count_at]):
            at = base + 8 * i
            reg, _, value = struct.unpack_from("<HHI", h, at)
            if reg == offset:
                struct.pack_into("<I", h, at + 4, value | bits)
                patched += 1
    if patched == 0:
        raise SystemExit(f"register {offset:#x} is not in the header")
    header["data"] = bytes(h)
    return write_elf(container, sections, entsize, strndx)


def main(argv):
    vgprs = None
    if "--vgprs" in argv:
        at = argv.index("--vgprs")
        vgprs = int(argv[at + 1], 0)
        argv = argv[:at] + argv[at + 2:]
    or_regs = []
    while "--or-reg" in argv:
        at = argv.index("--or-reg")
        offset, bits = argv[at + 1].split("=")
        or_regs.append((int(offset, 0), int(bits, 0)))
        argv = argv[:at] + argv[at + 2:]
    if len(argv) == 5 and argv[1] == "pack":
        container = open(argv[2], "rb").read()
        if pack(container, own_program(container)) != container:
            raise SystemExit("repacking the container's own program does not reproduce it; refusing")
        program = open(argv[3], "rb").read()
        out = pack(container, program)
        if vgprs is not None:
            out = set_vgprs(out, vgprs)
        for offset, bits in or_regs:
            out = or_register(out, offset, bits)
        open(argv[4], "wb").write(out)
        print(f"{argv[4]}: program {len(program):#x} bytes, container {len(out)} bytes (self-check passed)")
    elif len(argv) == 4 and argv[1] == "texture-container":
        out = texture_container(open(argv[2], "rb").read())
        open(argv[3], "wb").write(out)
        print(f"{argv[3]}: texture and sampler declared, container {len(out)} bytes")
    elif len(argv) == 6 and argv[1] == "embed":
        header_in, header_out, name, container = argv[2:6]
        data = open(container, "rb").read()
        text = open(header_in).read()
        start = text.index(f"static const unsigned char {name}[")
        end = text.index("};", start) + 2
        body = ",\n".join("    " + ",".join(str(b) for b in data[i:i + 16]) for i in range(0, len(data), 16))
        text = text[:start] + f"static const unsigned char {name}[{len(data)}] = {{\n{body},\n}};" + text[end:]
        length_decl = f"static const unsigned int {name}_len = "
        if length_decl in text:
            at = text.index(length_decl) + len(length_decl)
            text = text[:at] + str(len(data)) + text[text.index(";", at):]
        open(header_out, "w").write(text)
        print(f"{header_out}: {name} is now {len(data)} bytes")
    elif len(argv) == 5 and argv[1] == "array":
        header_out, name, container = argv[2:5]
        data = open(container, "rb").read()
        body = ",\n".join("    " + ",".join(str(b) for b in data[i:i + 16]) for i in range(0, len(data), 16))
        open(header_out, "w").write(
            f"/* Generated by shader_tools/agcpack.py from {container.split('/')[-1]}. Do not edit. */\n"
            f"static const unsigned char {name}[{len(data)}] = {{\n{body},\n}};\n"
            f"static const unsigned int {name}_len = {len(data)};\n")
        print(f"{header_out}: {name}, {len(data)} bytes")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
