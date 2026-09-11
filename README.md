# ps5link SDK

**ps5link** is a linker, written in C, that turns object files built with
`prospero-clang` into a **real PS5 application**, not a payload. The result has
the console's own dynamic linking: imports by NID from the system modules, PLT
and GOT, and the SCE dynamic tables. You install it as a folder, register it
with ShadowMountPlus, and launch it from the home screen like any other title.
It has full access to the system libraries, GPU (AGC) included.

It is a C port of the linker in [SharpProspero](https://github.com/SvenGDK/SharpProspero)
(`SharpProspero.Link/DynamicWriter.cs` and `CrtEmitter.cs`). So you can build
native C titles without the .NET toolchain; .NET is only needed to sign.

Around it, this repository holds what a homebrew project needs:
- **`linker/`**: ps5link, and the startup object a title begins with.
- **`sdk/`**: small C headers over the console's services - video out, a
  framebuffer with text, the DualSense, notifications, files.
- **`shaders/`**: GPU programs. An assembler script turns AMD GPU assembly into
  the shader containers AGC accepts, so a title can draw with programs of its
  own.
- **`examples/`**: titles built with ps5link, and payloads built the other way,
  with SDL2 and the ELF loader.

The [Super Mario 64 PS5 port](https://github.com/Rufidj/sm64-ps5) is built with it. It renders on the
GPU with custom shaders, 60 fps, real-time shadows and reflections.

> Tested on a jailbroken **PS5 on firmware 9.00**. Other firmwares may work but
> are untested.

---

## What you need on the console

| Tool | Notes |
|---|---|
| A jailbroken PS5 | Tested on FW 9.00 with etaHEN. That gives you the ELF loader (port 9021) and FTP (port 1337). |
| [kstuff](https://github.com/EchoStretch/kstuff) (EchoStretch fork) | Required. Send it with the ELF loader **after the console has fully booted**. `patching app.db` confirms it worked. **Do not put it in an autoload script**: loading it during boot caused kernel panics in a loop. The upstream kstuff gave no confirmation, and ShadowMountPlus did not detect it. |
| [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) **v1.6beta16** | Registers folder titles so they appear on the home screen. **Do not use v1.7alpha13fix1**: it fails to parse `param.json` for every title. |
| [klogsrv](https://github.com/ps5-payload-dev/klogsrv) (optional) | Streams the kernel log on TCP port 3232. It is the only reliable way to see why a title refuses to start. |

### ShadowMountPlus scan path

ShadowMountPlus scans `/data/homebrew` (internal) and `homebrew/` on USB drives.
Check `/data/shadowmount/config.ini`: if it has any `scanpath=` line, **only
the listed paths are scanned**. In that case add the line below yourself:

```
scanpath=/data/homebrew
```

---

## What you need on the PC

Tested on Linux Mint 22.1 (Ubuntu 24.04 based).

- A host C compiler (`cc`/`gcc`) and `make`.
- [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk), for `prospero-clang`
  (tested with the pacbrew package). Set `PS5_PAYLOAD_SDK` to its install
  folder.
- For signing: the [.NET SDK](https://dotnet.microsoft.com/) 10 and a checkout
  of [SharpProspero](https://github.com/SvenGDK/SharpProspero). Its
  `self --sign` command wraps the ELF in a container the console accepts.
- Only if you write GPU shader programs: LLVM 18 `llvm-mc` and `llvm-objcopy`
  built with the **amdgcn** target. Ubuntu's `llvm-18` package has it, under
  `/usr/lib/llvm-18/bin`.

---

## Building

```sh
make PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk      # linker/link_real and linker/crt1_ps5.o
make test                                      # host-side unit tests
make examples PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk SHARPPROSPERO=$HOME/SharpProspero
```

`make examples` builds each `examples/*/main.c` into a signed `eboot.bin`.

---

## The two ways to run code

| | A real title (ps5link) | A payload |
|---|---|---|
| How it starts | Installed as a folder, launched from the home screen | Sent to the ELF loader (port 9021) while something else runs |
| Entry point | `int main(void)` with `linker/crt1_ps5.o` | `int SDL_main(int, char **)` |
| Built by | `linker/link_real` + `self --sign` | ps5-payload-sdk's `prospero.mk` |
| Good for | Finished programs, GPU rendering, anything to keep | Trying things out quickly |

`examples/hello_notify`, `examples/pad_input` and `examples/gpu_cube` are
titles.
`examples/payload/` holds the SDL2 ones, built with their own Makefile:

```sh
cd examples/payload
make EXAMPLE=hello_pad PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
```

The headers in `sdk/` are for those: `ps5sdk.h` brings up SDL and the console's
services before `SDL_main()`, and gives you a framebuffer, text, the pad and
notifications.

## GPU programs

`shaders/` builds pixel programs for AGC:

```sh
cd shaders
LLVM_MC=/usr/lib/llvm-18/bin/llvm-mc LLVM_OBJCOPY=/usr/lib/llvm-18/bin/llvm-objcopy ./build.sh
```

Each `src/*.s` (AMD GPU assembly, gfx1030) is assembled and packed into a
shader container, and written out as a C header holding its bytes, ready for
`sceAgcCreateShader`. `src/textured_p.s` is a complete example: a texture
sampled at the interpolated coordinates, times the vertex colour.

The container itself is made from SharpProspero's `mesh_ps.sb`, with a texture
and a sampler declared in it by `tools/agcpack.py texture-container`. The
vertex program is SharpProspero's `mesh_vs.sb`. `agcpack.py` explains the
container layout it works from, and checks its own work by repacking the
original byte for byte before it packs anything new.

`examples/gpu_cube` draws with them: a lit, animated cube, straight from video
out to the flip, with no engine in between. For a whole renderer built this
way - render targets, batching, water, shadows, reflections - see the SM64
port's `ps5/` folder.

## Writing an application

A ps5link application is plain C with a normal `int main(void)`.
Each object goes through three steps:

```sh
# 1. compile
$PS5_PAYLOAD_SDK/bin/prospero-clang -c -O2 main.c -o main.o

# 2. link: crt1_ps5.o always comes first
./linker/link_real app.elf linker/crt1_ps5.o main.o [more.o ...]

# 3. sign
cd $SHARPPROSPERO/tools/SharpProspero.Bindings.Generator
dotnet run -c Release -- self --sign --in /path/to/app.elf --out /path/to/eboot.bin
```

`link_real` lists every symbol it cannot resolve and stops. On success it
prints a `dynwriter:` summary line. **If you do not see that line, the ELF was
not written.** Never sign an old `app.elf` by mistake.

### Calling the system

- System functions are declared by hand as `extern` (see `examples/`). The
  linker imports them by NID from the module that exports them.
- `linker/catalog.c` lists the names the linker knows and the module each comes
  from. It is generated from SharpProspero's `StubCatalog.cs` by
  `linker/gen_catalog.py`. `linker/catalog_extra.c` holds hand-added entries.
  To call something missing, add it there with its module and library, then
  rebuild `link_real`.
- libc comes from the console's `libc.prx`: `malloc`, `memcpy`, `snprintf`,
  `fopen`/`fread`/`fwrite`, math and so on. Only what the catalog lists can be
  imported.

### Things that do not work (yet)

- **Thread-local storage** (`_Thread_local`, `__thread`) compiles to
  `__emutls_get_address`, which the console does not provide. Avoid it. For
  example, build stb_image with `STBI_NO_THREAD_LOCALS`.
- Functions missing from the console's libc, such as `strnlen` and `sprintf`,
  need a small implementation of your own. The SM64 port's
  `ps5/glue/libc_shims.c` is an example.
- If a shim built on `sinf` and `cosf` is compiled with builtins enabled, the
  compiler can fold it back into a call to itself. Compile such shims with
  `-fno-builtin`.
- C++ is untested.

## Packaging and installing

A title is a folder:

```
PPSA12345/
├── eboot.bin              the signed program
├── sce_module/
│   └── libc.prx           REQUIRED, see below
└── sce_sys/
    ├── param.json         titleId, conceptId, contentId, name
    └── icon0.png          512x512 PNG
```

- **`param.json`**: `titleId` and `conceptId` are 4 capital letters and 5
  digits, and must be unique on the console. `contentId` looks like
  `IV0000-PPSA12345_00-ANYSIXTEENCHARS0`. The SM64 port's
  `ps5/package/sce_sys/param.json` is a template.
- **`sce_module/libc.prx`**: the system does not provide `libc.prx` to
  applications; each title ships its own copy.
  - It must be a **signed** module that matches your firmware. Take it from
    the `sce_module/` folder of a game dump made for your firmware or an older
    one. A module from a newer firmware is rejected.
  - The decrypted ELF you get by copying the file off a running title is also
    rejected, with `sceSblAuthMgrAuthHeader:readHeader ... invalid state`.
    Sign such a file with `self --sign` first.
  - This file belongs to Sony and **cannot be distributed**: this repository
    does not include it.

To install:
1. Copy the folder with FTP to `/data/homebrew/<TITLE_ID>/`, or to
   `homebrew/<TITLE_ID>/` on a USB drive.
2. Start ShadowMountPlus. The log shows `[REG] Installed NEW!`, and the title
   appears on the home screen.

**Tip:** give each new build its own title id, and delete old ones. The console
caches titles by id, and reusing one can keep an old build around.

---

## Debugging

- **Kernel log**: send klogsrv with the ELF loader, then run
  `nc <ps5-ip> 3232` before launching. The kernel's messages are literal and
  worth reading in full, for example:
  `Lack of a .prx file in /app0/sce_module is detected!!!`
- **Notifications**: `sceKernelSendNotificationRequest`, as in
  `examples/hello_notify`. It is the quickest way to trace a title that has no
  display yet.
- Crashes show up in the kernel log with the faulting address. The text segment
  is **execute-only**, so reading data from inside it crashes with
  `SYSTEM_XO_VIOLATION`.

---

## Layout

```
linker/            the linker (host C) and the startup object (crt1.S)
  link_real.c      driver: read objects, resolve, write the SCE ELF
  elf_object.c     object file reader
  linker.c         symbol resolution against the objects and the catalog
  dynwriter.c      SCE dynamic ELF writer (port of DynamicWriter.cs)
  catalog*.c       importable names and their modules
  nid.c, sha1.c    NID computation
  test_*.c         unit tests
sdk/               C headers over the console's services, for payloads
shaders/           GPU programs: the packer, SharpProspero's containers, an example
examples/
  hello_notify/    one notification from a real title
  pad_input/       reads the DualSense
  gpu_cube/        a lit 3D cube drawn through AGC
  payload/         the SDL2 examples and their Makefile
```

---

## Credits

- **[SharpProspero](https://github.com/SvenGDK/SharpProspero)** by SvenGDK: the
  linker this is ported from, the NID catalog, the signing tool and the shader
  containers. GPL-3.0.
- **[ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk)** by John Törnblom
  and contributors: `prospero-clang` and the PS5 headers.
- **[kstuff](https://github.com/EchoStretch/kstuff)** (EchoStretch fork),
  **[ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus)** by drakmor,
  **[klogsrv](https://github.com/ps5-payload-dev/klogsrv)**, and etaHEN: what
  makes running a title possible at all.

## License

GPL-3.0, like SharpProspero, which this is derived from. See `LICENSE`.

This project contains no Sony code, firmware files or keys.
