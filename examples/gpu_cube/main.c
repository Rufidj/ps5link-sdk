/* A lit, animated 3D cube drawn by the GPU: video out, a tiled render target,
 * a command buffer, two shaders and a flip, with nothing in between. The
 * register-level details that matter are in the comments as they come up -
 * tiled surface sizing, 16KB direct-memory alignment, the kUNorm channel type,
 * and the user-data register base of each stage.
 *
 * Per-vertex data and matrices are the heart of it. The
 * shaders are SharpProspero's built-in mesh pair, already proven on hardware,
 * whose PSSL source states the contract exactly:
 *   Vertex  { float3 pos@0; float3 normal@12; float2 uv@24; uint color@32; }  stride 36
 *   Constants { row_major float4x4 mvp; row_major float4x4 model; }           128 bytes
 * The vertex program reads the vertex by index from a RegularBuffer (t0) and
 * its matrices from a ConstantBuffer (b0) - both bound to the VERTEX stage.
 * The pixel program takes no resources; it lights the interpolated normal.
 *
 * No SDL, no stdio, no libc math: same minimal-surface philosophy as
 * hw_test.c/pad_test.c, so the trigonometry and matrix maths are here too.
 */
/* Built by shaders/build.sh from SharpProspero's containers. */
#include "mesh_vs_sb.h"
#include "mesh_ps_sb.h"

/* ---- kernel / notify ---- */
extern int sceKernelSendNotificationRequest(int device, void *request, int size, int flags);
extern int sceKernelUsleep(unsigned int microseconds);
extern unsigned long sceKernelGetDirectMemorySize(void);
extern int sceKernelAllocateDirectMemory(long searchStart, long searchEnd, unsigned long len,
                                          unsigned long alignment, int memoryType, long *physOut);
extern int sceKernelMapDirectMemory(void **addrInOut, unsigned long len, int prot, int mapFlags,
                                     long physAddr, unsigned long alignment);
extern int sceKernelReleaseDirectMemory(long start, unsigned long len);

#define MEM_TYPE_CACHED_SHARED 12
#define PROT_CPU_RW   0x03
#define PROT_GPU_ALL  0x30
#define PROT_ALL      (PROT_CPU_RW | PROT_GPU_ALL)

/* ---- VideoOut ---- */
/* SceVideoOutBuffers: {void* Data; void* Metadata; void* Reserved0; void* Reserved1;} - 4 pointers. */
typedef struct { void *data; void *metadata; void *reserved0; void *reserved1; } SceVideoOutBuffers;
#define SCE_USER_SYSTEM 0xFF
#define VIDEOOUT_BUS_MAIN 0
#define VIDEOOUT_TILING_TILED 0   /* VideoOutTilingMode.Tiled = 0 */
#define VIDEOOUT_TILING_LINEAR 1
#define VIDEOOUT_PIXELFORMAT_BGRA8_SRGB 0x8000000000000000ULL
extern int sceVideoOutOpen(int userId, int busType, int index, const void *param);
extern int sceVideoOutClose(int handle);
extern int sceVideoOutSetFlipRate(int handle, int rate);
extern void sceVideoOutSetBufferAttribute2(void *attr, unsigned long long pixelFormat, unsigned int tilingMode,
                                            unsigned int width, unsigned int height, unsigned long long option,
                                            unsigned int dccControl, unsigned long long dccCbRegisterClearColor);
extern int sceVideoOutRegisterBuffers2(int handle, int startIndex, int set, const SceVideoOutBuffers *addresses,
                                        int bufferNum, const void *attr, int category, const void *unk);
extern int sceVideoOutSubmitFlip(int handle, int bufferIndex, unsigned int flipMode, long long flipArg);
extern int sceVideoOutWaitVblank(int handle);

/* ---- AGC ---- */
extern int sceAgcInit(void *state, unsigned int defaultsRevision);
extern void *sceAgcGetRegisterDefaults(void);
extern int sceAgcCreateShader(void **outHandle, void *header, void *gpuCode);
extern int sceAgcLinkShaders(void *linkageOut, void *primitiveStateOut, void *unused,
                              void *vsHandle, void *psHandle, unsigned int primitiveType);
extern void *sceAgcDcbSetCxRegisterDirect(void *st, unsigned long long packed);
extern void *sceAgcDcbSetShRegisterDirect(void *st, unsigned long long packed);
extern void sceAgcDcbSetCxRegistersIndirect(void *dcb, void *regs, unsigned int count);
extern void sceAgcDcbSetShRegistersIndirect(void *dcb, void *regs, unsigned int count);
extern void sceAgcDcbSetUcRegistersIndirect(void *dcb, void *regs, unsigned int count);
extern void sceAgcCbSetShRegisterRangeDirect(void *dcb, unsigned int baseOffset, unsigned int *words, unsigned int count);
extern void *sceAgcDcbDrawIndexAuto(void *st, unsigned int indexCount, unsigned long long modifier);
extern void *sceAgcDcbSetIndexBuffer(void *st, void *indexAddr);
extern void *sceAgcDcbSetIndexCount(void *st, unsigned int indexCount);
extern void *sceAgcDcbSetIndexSize(void *st, unsigned char indexSize, unsigned char cachePolicy);
extern void *sceAgcDcbDrawIndex(void *st, unsigned int indexCount, void *indexAddr, unsigned long long modifier);
extern void *sceAgcDcbSetFlip(void *st, unsigned int videoOutHandle, int bufferIndex, unsigned int flipMode, long long flipArg);
extern int sceAgcDriverSubmitDcb(void *submitDescription);
extern int sceAgcSuspendPoint(void);

/* SharpProspero's CxRegister is {ushort Offset; uint Value;} sequential, which C#
 * lays out as offset(2) + pad(2) + value(4) = 8 bytes (uint needs 4-byte alignment).
 * Mirrored exactly here so the shader's embedded register arrays (read straight out
 * of GPU-visible memory the driver wrote) can be indexed as this same struct. */
typedef struct { unsigned short offset; unsigned short pad; unsigned int value; } CxRegister;

/* ---- command buffer state (matches DrawCommandBuffer's private State struct byte-for-byte) ---- */
typedef struct {
    unsigned int *bottom;
    unsigned int *top;
    unsigned int *up_cursor;
    unsigned int *down_cursor;
    long callback; /* out-of-space callback; must be a real function pointer, not null */
    void *user_data;
    unsigned int reserved_dwords;
    unsigned int _pad;
} DcbState;

static unsigned char out_of_space(DcbState *st, unsigned int size, void *userData) {
    (void)st; (void)size; (void)userData;
    return 0;
}

static void dcb_reset(DcbState *st, unsigned int *buffer, unsigned int capacity_dwords) {
    st->bottom = buffer;
    st->top = buffer + capacity_dwords;
    st->up_cursor = buffer;
    st->down_cursor = buffer + capacity_dwords;
    st->callback = (long)(void *)&out_of_space;
    st->user_data = 0;
    st->reserved_dwords = 0;
    st->_pad = 0;
}

/* ---- tiny direct-memory allocator ---- */
typedef struct { void *ptr; long phys; unsigned long size; } DirectMem;

/* Direct memory is only ever granted in whole pages: KernelMemory.PageSize
 * (16384) is "the alignment a direct mapping is made on" per SharpProspero's
 * own KernelMemory.cs. A caller asking for a smaller alignment (8, 256...)
 * isn't rejected loudly - it just makes sceKernelAllocateDirectMemory or
 * sceKernelMapDirectMemory fail, and this function used to return -1 in that
 * case *without every caller checking it*, leaving the DirectMem's .ptr as
 * uninitialized stack garbage (often 0 on a fresh frame) - a silent NULL that
 * only crashed later, on the first real write through it. Clamping here
 * closes off that whole class of mistake at the source. */
#define DIRECT_MEM_MIN_ALIGN 16384u

static int direct_alloc(DirectMem *out, unsigned long bytes, unsigned long align) {
    out->ptr = 0; out->phys = 0; out->size = 0;
    if (align < DIRECT_MEM_MIN_ALIGN) align = DIRECT_MEM_MIN_ALIGN;
    unsigned long size = (bytes + align - 1) / align * align;
    long phys = 0;
    unsigned long pool = sceKernelGetDirectMemorySize();
    if (sceKernelAllocateDirectMemory(0, (long)pool, size, align, MEM_TYPE_CACHED_SHARED, &phys) < 0)
        return -1;
    void *addr = 0;
    if (sceKernelMapDirectMemory(&addr, size, PROT_ALL, 0, phys, align) < 0) {
        sceKernelReleaseDirectMemory(phys, size);
        return -1;
    }
    out->ptr = addr;
    out->phys = phys;
    out->size = size;
    return 0;
}

static void notify(const char *msg) {
    char req[3120];
    for (int i = 0; i < 3120; i++) req[i] = 0;
    for (int i = 0; msg[i] && i < 3074; i++) req[45 + i] = msg[i];
    sceKernelSendNotificationRequest(0, req, sizeof(req), 0);
}

/* Writes msg followed by " rc=" and rc in hex (e.g. "80290005") into a small
 * stack buffer and notifies it - so a failure says which error, not just where. */
static void notify_rc(const char *msg, int rc) {
    char buf[96];
    int len = 0;
    for (; msg[len] && len < 64; len++) buf[len] = msg[len];
    const char *tag = " rc=0x";
    for (int i = 0; tag[i]; i++) buf[len++] = tag[i];
    unsigned int u = (unsigned int)rc;
    char hex[8];
    for (int i = 7; i >= 0; i--) { unsigned int nib = u & 0xF; hex[i] = (char)(nib < 10 ? ('0'+nib) : ('A'+nib-10)); u >>= 4; }
    for (int i = 0; i < 8; i++) buf[len++] = hex[i];
    buf[len] = 0;
    notify(buf);
}

static int hex_append(char *buf, int len, unsigned long long v, int digits) {
    for (int i = digits - 1; i >= 0; i--) {
        unsigned int nib = (unsigned int)((v >> (i * 4)) & 0xF);
        buf[len++] = (char)(nib < 10 ? ('0' + nib) : ('A' + nib - 10));
    }
    return len;
}

/* Dumps up to 4 labelled hex values in one notification, e.g.
 * notify_vals("chk1", "fb0", fb0, "dwo", dword_offset, "cx", cx, "sh", sh)
 * -> "chk1 fb0=00AB1000 dwo=0000008C cx=00000051 sh=00000010" */
static void notify_vals(const char *tag,
                         const char *n1, unsigned long long v1,
                         const char *n2, unsigned long long v2,
                         const char *n3, unsigned long long v3,
                         const char *n4, unsigned long long v4) {
    char buf[200];
    int len = 0;
    for (; tag[len] && len < 20; len++) buf[len] = tag[len];
    const char *names[4] = {n1, n2, n3, n4};
    unsigned long long vals[4] = {v1, v2, v3, v4};
    for (int k = 0; k < 4; k++) {
        buf[len++] = ' ';
        for (int i = 0; names[k][i] && len < 190; i++) buf[len++] = names[k][i];
        buf[len++] = '=';
        len = hex_append(buf, len, vals[k], 8);
    }
    buf[len] = 0;
    notify(buf);
}

/* ---- shader-binary ELF container parsing (mirrors ShaderBinary.cs Load()) ---- */
typedef struct { const unsigned char *header; unsigned int header_len; const unsigned char *code; unsigned int code_len; } ShaderParts;

static unsigned int rd32(const unsigned char *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((unsigned int)p[3]<<24); }
static unsigned long long rdu64(const unsigned char *p) {
    unsigned long long lo = rd32(p);
    unsigned long long hi = rd32(p+4);
    return lo | (hi << 32);
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static int shader_parts_from_elf(const unsigned char *elf, unsigned int len, ShaderParts *out) {
    if (len < 64 || elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') return -1;
    unsigned long long shoff = rdu64(elf + 40);
    unsigned short shentsize = (unsigned short)(elf[58] | (elf[59] << 8));
    unsigned short shnum = (unsigned short)(elf[60] | (elf[61] << 8));
    unsigned short shstrndx = (unsigned short)(elf[62] | (elf[63] << 8));
    if (shoff == 0 || shnum == 0 || shstrndx >= shnum) return -1;

    unsigned int strrec = (unsigned int)shoff + shstrndx * shentsize;
    unsigned long long stroff = rdu64(elf + strrec + 24);

    out->header = 0; out->code = 0;
    for (int i = 0; i < shnum; i++) {
        unsigned int rec = (unsigned int)shoff + i * shentsize;
        unsigned int nameIndex = rd32(elf + rec);
        unsigned long long off = rdu64(elf + rec + 24);
        unsigned long long size = rdu64(elf + rec + 32);
        const char *name = (const char *)(elf + stroff + nameIndex);
        if (str_eq(name, ".shader_header")) { out->header = elf + off; out->header_len = (unsigned int)size; }
        else if (str_eq(name, ".shader_text")) { out->code = elf + off; out->code_len = (unsigned int)size; }
    }
    if (!out->header || !out->code) return -1;
    return 0;
}

/* ---- shader user-data layout (AgcShader.cs TryGetResourceSlot, byte-exact) ---- */
#define SHDR_USERDATA(h)   (*(void**)((unsigned char*)(h) + 8))
#define SHDR_CXREGS(h)     (*(CxRegister**)((unsigned char*)(h) + 24))
#define SHDR_SHREGS(h)     (*(CxRegister**)((unsigned char*)(h) + 32))
#define SHDR_CXCOUNT(h)    (((unsigned char*)(h))[91])
#define SHDR_SHCOUNT(h)    (((unsigned char*)(h))[92])

/* ShaderResourceKind */
#define KIND_READONLY       0
#define KIND_READWRITE      1
#define KIND_SAMPLER        2
#define KIND_CONSTANTBUFFER 3

/* User-data register bases, per shader stage: SPI_SHADER_USER_DATA_<stage>_0
 * minus the 0x2C00 base of the SH register space. */
#define GS_USER_DATA_BASE 0x008C
#define PS_USER_DATA_BASE 0x000C

static int resource_dword_offset(void *handle, unsigned short kind, int slot) {
    void *ud = SHDR_USERDATA(handle);
    if (!ud) return -1;
    unsigned short *counts = (unsigned short*)((unsigned char*)ud + 46);
    if ((unsigned int)slot >= counts[kind]) return -1;
    unsigned short *sharps = *(unsigned short**)((unsigned char*)ud + 8 + kind * sizeof(void*));
    return sharps[slot] & 0x7FFF;
}

/* ---- buffer descriptors (AgcBufferDescriptor.cs) ---- */
static void desc_structured(unsigned int *w, unsigned long long addr, unsigned int stride, unsigned int count) {
    w[0] = (unsigned int)(addr & 0xFFFFFFFFu);
    w[1] = (unsigned int)((addr >> 32) & 0xFFFFu) | ((stride & 0x3FFFu) << 16);
    w[2] = count;
    w[3] = 0x204u | (5u << 12);       /* RegularSwizzle | RegularFormat<<12 */
}
static void desc_constant(unsigned int *w, unsigned long long addr, unsigned int size_bytes) {
    unsigned int record = 16;
    w[0] = (unsigned int)(addr & 0xFFFFFFFFu);
    w[1] = (unsigned int)((addr >> 32) & 0xFFFFu) | (record << 16);
    w[2] = (size_bytes + record - 1) / record;
    w[3] = 0xfacu | (77u << 12);      /* ConstantSwizzle | ConstantFormat<<12 */
}

/* ---- minimal maths: no libc, so sine comes from a 7th-order Taylor series
 * after wrapping into [-PI, PI], which is accurate to about 1e-4 there - far
 * finer than a 1080p pixel. Matrices are row-major and act on row vectors
 * (v' = v * M), matching the PSSL's row_major float4x4 and its mul(v, M). ---- */
#define M_PI_F 3.14159265358979f
#define M_TAU_F 6.28318530717959f

static float m_sin(float x) {
    while (x >  M_PI_F) x -= M_TAU_F;
    while (x < -M_PI_F) x += M_TAU_F;
    float x2 = x * x;
    return x * (1.0f - x2 * (1.0f/6.0f - x2 * (1.0f/120.0f - x2 * (1.0f/5040.0f))));
}
static float m_cos(float x) { return m_sin(x + M_PI_F * 0.5f); }

static void mat_identity(float *m) {
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}
static void mat_mul(float *out, const float *a, const float *b) {
    float t[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += a[r*4+k] * b[k*4+c];
            t[r*4+c] = s;
        }
    for (int i = 0; i < 16; i++) out[i] = t[i];
}
static void mat_rot_y(float *m, float a) {
    mat_identity(m);
    float c = m_cos(a), s = m_sin(a);
    m[0] = c; m[2] = -s; m[8] = s; m[10] = c;
}
static void mat_rot_x(float *m, float a) {
    mat_identity(m);
    float c = m_cos(a), s = m_sin(a);
    m[5] = c; m[6] = s; m[9] = -s; m[10] = c;
}
static void mat_translate(float *m, float x, float y, float z) {
    mat_identity(m);
    m[12] = x; m[13] = y; m[14] = z;
}
/* Left-handed perspective with depth in [0,1], for row vectors. */
static void mat_perspective(float *m, float fov_y, float aspect, float zn, float zf) {
    float f = m_cos(fov_y * 0.5f) / m_sin(fov_y * 0.5f);   /* cot(fov/2) */
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = zf / (zf - zn);
    m[11] = 1.0f;
    m[14] = -zn * zf / (zf - zn);
}

/* ---- the cube: 6 faces x 4 corners, so every face carries its own flat
 * normal and colour rather than sharing averaged ones at the corners. ---- */
#define VERTEX_STRIDE 36
#define CUBE_VERTS 24
#define CUBE_INDICES 36

static void put_vertex(unsigned char *p, int i,
                       float px, float py, float pz,
                       float nx, float ny, float nz,
                       unsigned int color) {
    float *f = (float*)(p + (unsigned int)i * VERTEX_STRIDE);
    f[0] = px; f[1] = py; f[2] = pz;
    f[3] = nx; f[4] = ny; f[5] = nz;
    f[6] = 0.0f; f[7] = 0.0f;                    /* uv, unused by these shaders */
    *(unsigned int*)(p + (unsigned int)i * VERTEX_STRIDE + 32) = color;
}

static void build_cube(unsigned char *vp, unsigned int *ip) {
    /* Each face: normal, its four corners in an order that winds the same way
     * seen from outside, and a colour so the faces are told apart at a glance. */
    static const float N[6][3] = {
        { 0, 0,-1}, { 0, 0, 1}, {-1, 0, 0}, { 1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}
    };
    static const float C[6][4][3] = {
        {{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1},{ 1,-1,-1}},   /* -Z */
        {{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1},{-1,-1, 1}},   /* +Z */
        {{-1,-1, 1},{-1, 1, 1},{-1, 1,-1},{-1,-1,-1}},   /* -X */
        {{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1},{ 1,-1, 1}},   /* +X */
        {{-1, 1,-1},{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1}},   /* +Y */
        {{-1,-1, 1},{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1}},   /* -Y */
    };
    /* A<<24 | R<<16 | G<<8 | B, as the vertex program unpacks it. */
    static const unsigned int COL[6] = {
        0xFFE05050u, 0xFF50E050u, 0xFF5050E0u,
        0xFFE0E050u, 0xFFE050E0u, 0xFF50E0E0u
    };
    for (int f = 0; f < 6; f++) {
        for (int c = 0; c < 4; c++)
            put_vertex(vp, f*4 + c, C[f][c][0], C[f][c][1], C[f][c][2],
                       N[f][0], N[f][1], N[f][2], COL[f]);
        unsigned int b = (unsigned int)(f * 4);
        unsigned int *q = ip + f * 6;
        q[0] = b + 0; q[1] = b + 1; q[2] = b + 2;
        q[3] = b + 0; q[4] = b + 2; q[5] = b + 3;
    }
}

int main(void) {
    notify("mesh_test: arrancando...");

    static unsigned long long agc_state;
    { int r = sceAgcInit(&agc_state, 8); if (r < 0) { notify_rc("mesh_test: sceAgcInit fallo", r); return 1; } }

    int vhandle = sceVideoOutOpen(SCE_USER_SYSTEM, VIDEOOUT_BUS_MAIN, 0, 0);
    if (vhandle < 0) { notify_rc("mesh_test: sceVideoOutOpen fallo", vhandle); return 1; }
    sceVideoOutSetFlipRate(vhandle, 0);

    const unsigned int W = 1920, H = 1080;
    const int BUF_COUNT = 2;
    unsigned int padded_w = (W + 127u) & ~127u;      /* 64KB tiles are 128x128 elements at 32bpp */
    unsigned int padded_h = (H + 127u) & ~127u;
    unsigned long frame_bytes = (unsigned long)padded_w * padded_h * 4;
    frame_bytes = (frame_bytes + (2*1024*1024 - 1)) & ~(unsigned long)(2*1024*1024 - 1);

    #define ALLOC_OR_FAIL(region, bytes, align, what) \
        do { if (direct_alloc(&(region), (bytes), (align)) < 0) { notify("mesh_test: alloc " what " fallo"); return 1; } } while (0)

    DirectMem fb[2];
    for (int i = 0; i < BUF_COUNT; i++)
        ALLOC_OR_FAIL(fb[i], frame_bytes, 2*1024*1024, "framebuffer");

    SceVideoOutBuffers addresses[2];
    for (int i = 0; i < BUF_COUNT; i++) {
        addresses[i].data = fb[i].ptr;
        addresses[i].metadata = 0; addresses[i].reserved0 = 0; addresses[i].reserved1 = 0;
    }
    unsigned char attr[64]; for (int i = 0; i < 64; i++) attr[i] = 0;
    sceVideoOutSetBufferAttribute2(attr, VIDEOOUT_PIXELFORMAT_BGRA8_SRGB, VIDEOOUT_TILING_TILED, W, H, 0ULL, 0u, 0ULL);
    { int r = sceVideoOutRegisterBuffers2(vhandle, 0, 0, addresses, BUF_COUNT, attr, 0, 0);
      if (r < 0) { notify_rc("mesh_test: RegisterBuffers2 fallo", r); return 1; } }

    /* Shaders */
    ShaderParts vs_parts, ps_parts;
    if (shader_parts_from_elf(mesh_vs_sb, mesh_vs_sb_len, &vs_parts) < 0 ||
        shader_parts_from_elf(mesh_ps_sb, mesh_ps_sb_len, &ps_parts) < 0) {
        notify("mesh_test: parseo de shader fallo"); return 1;
    }
    DirectMem vs_hdr, vs_code, ps_hdr, ps_code;
    ALLOC_OR_FAIL(vs_hdr,  vs_parts.header_len, DIRECT_MEM_MIN_ALIGN, "vs_hdr");
    ALLOC_OR_FAIL(vs_code, vs_parts.code_len,   DIRECT_MEM_MIN_ALIGN, "vs_code");
    ALLOC_OR_FAIL(ps_hdr,  ps_parts.header_len, DIRECT_MEM_MIN_ALIGN, "ps_hdr");
    ALLOC_OR_FAIL(ps_code, ps_parts.code_len,   DIRECT_MEM_MIN_ALIGN, "ps_code");
    { unsigned char *d = vs_hdr.ptr;  for (unsigned int i=0;i<vs_parts.header_len;i++) d[i]=vs_parts.header[i]; }
    { unsigned char *d = vs_code.ptr; for (unsigned int i=0;i<vs_parts.code_len;i++)   d[i]=vs_parts.code[i]; }
    { unsigned char *d = ps_hdr.ptr;  for (unsigned int i=0;i<ps_parts.header_len;i++) d[i]=ps_parts.header[i]; }
    { unsigned char *d = ps_code.ptr; for (unsigned int i=0;i<ps_parts.code_len;i++)   d[i]=ps_parts.code[i]; }

    void *vs_handle = 0, *ps_handle = 0;
    { int r = sceAgcCreateShader(&vs_handle, vs_hdr.ptr, vs_code.ptr); if (r < 0) { notify_rc("mesh_test: CreateShader VS fallo", r); return 1; } }
    { int r = sceAgcCreateShader(&ps_handle, ps_hdr.ptr, ps_code.ptr); if (r < 0) { notify_rc("mesh_test: CreateShader PS fallo", r); return 1; } }

    /* Geometry */
    DirectMem vbuf, ibuf;
    ALLOC_OR_FAIL(vbuf, CUBE_VERTS * VERTEX_STRIDE, DIRECT_MEM_MIN_ALIGN, "vbuf");
    ALLOC_OR_FAIL(ibuf, CUBE_INDICES * 4, DIRECT_MEM_MIN_ALIGN, "ibuf");
    build_cube((unsigned char*)vbuf.ptr, (unsigned int*)ibuf.ptr);

    /* Where the vertex program wants its two resources. */
    int cb_dwo = resource_dword_offset(vs_handle, KIND_CONSTANTBUFFER, 0);
    int vb_dwo = resource_dword_offset(vs_handle, KIND_READONLY, 0);
    notify_vals("chk1", "cbdwo", (unsigned long long)(int)cb_dwo, "vbdwo", (unsigned long long)(int)vb_dwo,
                "vscx", SHDR_CXCOUNT(vs_handle), "pscx", SHDR_CXCOUNT(ps_handle));
    if (cb_dwo < 0 || vb_dwo < 0) { notify("mesh_test: el VS no declara sus recursos"); return 1; }

    unsigned int vb_words[4];
    desc_structured(vb_words, (unsigned long long)(unsigned long)vbuf.ptr, VERTEX_STRIDE, CUBE_VERTS);

    /* Per-frame-in-flight state, so recording one frame never overwrites what
     * an earlier frame's draw is still reading. */
    DirectMem dcb_m[2], ctx_m[2], sh_m[2], prim_m[2], cons_m[2];
    DcbState dcb_state[2];
    for (int i = 0; i < BUF_COUNT; i++) {
        ALLOC_OR_FAIL(dcb_m[i],  64*1024, DIRECT_MEM_MIN_ALIGN, "dcb");
        ALLOC_OR_FAIL(ctx_m[i],  8192,    DIRECT_MEM_MIN_ALIGN, "ctx");
        ALLOC_OR_FAIL(sh_m[i],   4096,    DIRECT_MEM_MIN_ALIGN, "sh");
        ALLOC_OR_FAIL(prim_m[i], 4096,    DIRECT_MEM_MIN_ALIGN, "prim");
        ALLOC_OR_FAIL(cons_m[i], 256,     DIRECT_MEM_MIN_ALIGN, "cons");
    }

    void *defaults = sceAgcGetRegisterDefaults();
    CxRegister **cx_blocks = *(CxRegister***)((unsigned char*)defaults + 0x00);
    unsigned int cx_record_count = *(unsigned int*)((unsigned char*)defaults + 0x20);
    CxRegister *cx_records = cx_blocks ? cx_blocks[0] : 0;
    static const unsigned short kRenderTargetOffsets[16] = {
        0x0318,0x031B,0x031C,0x031D,0x031E,0x031F,0x0321,0x0323,
        0x0324,0x0325,0x0390,0x0398,0x03A0,0x03A8,0x03B0,0x03B8,
    };

    float proj[16], view[16];
    mat_perspective(proj, 60.0f * (M_PI_F / 180.0f), (float)W / (float)H, 0.1f, 100.0f);
    mat_translate(view, 0.0f, 0.0f, 4.0f);   /* camera 4 units back along -Z */

    notify("mesh_test: dibujando cubo 600 frames...");

    const int FRAMES = 600;
    for (int frame = 0; frame < FRAMES; frame++) {
        int slot = frame & 1;
        DirectMem *ctx = &ctx_m[slot], *shm = &sh_m[slot], *prm = &prim_m[slot], *cns = &cons_m[slot];
        DcbState *dcb = &dcb_state[slot];

        /* Matrices for this frame. mul order is model, then view, then
         * projection, because these are row vectors. */
        float angle = (float)frame * 0.02f;
        float rot_y[16], rot_x[16], model[16], mv[16], mvp[16];
        mat_rot_y(rot_y, angle);
        mat_rot_x(rot_x, angle * 0.6f);
        mat_mul(model, rot_x, rot_y);
        mat_mul(mv, model, view);
        mat_mul(mvp, mv, proj);
        { float *c = (float*)cns->ptr;
          for (int i = 0; i < 16; i++) c[i] = mvp[i];
          for (int i = 0; i < 16; i++) c[16 + i] = model[i]; }
        unsigned int cb_words[4];
        desc_constant(cb_words, (unsigned long long)(unsigned long)cns->ptr, 128);

        /* Clear this frame's target to a dark background from the CPU: these
         * shaders draw a cube, not a background, and without a depth buffer
         * there is nothing else to wipe last frame's cube away. */
        { unsigned int *p = (unsigned int*)fb[slot].ptr;
          unsigned long n = fb[slot].size / 4;
          for (unsigned long i = 0; i < n; i++) p[i] = 0xFF201828u; }

        dcb_reset(dcb, (unsigned int*)dcb_m[slot].ptr, (unsigned int)(dcb_m[slot].size / 4));

        /* Render target -> this frame's framebuffer. */
        CxRegister rt[16];
        for (int i = 0; i < 16; i++) {
            rt[i].offset = kRenderTargetOffsets[i]; rt[i].pad = 0; rt[i].value = 0;
            for (unsigned int r = 0; r < cx_record_count; r++)
                if (cx_records[r].offset == kRenderTargetOffsets[i]) { rt[i].value = cx_records[r].value; break; }
        }
        #define RT_SET(idx, mask, val) rt[idx].value = (rt[idx].value & ~(unsigned int)(mask)) | (unsigned int)(val)
        RT_SET(1, 0x03ffe000u, 0u);
        RT_SET(2, 0x0000007cu, 0x00000028u); /* Format = k8_8_8_8 */
        RT_SET(2, 0x00000700u, 0x00000000u); /* ChannelType = kUNorm */
        RT_SET(2, 0x00001800u, 0x00000800u); /* ChannelOrder = kAlt */
        RT_SET(2, 0x00010000u, 0u);          /* BlendBypass off */
        RT_SET(2, 0x00008000u, 0x00008000u); /* BlendClamp on */
        RT_SET(2, 0x00040000u, 0u);          /* RoundMode = by half */
        RT_SET(2, 0x10000000u, 0u);          /* DccCompression off */
        RT_SET(2, 0x00004000u, 0u);          /* FmaskCompression off */
        RT_SET(3, 0x00007000u, 0u);          /* 1 sample */
        RT_SET(3, 0x00018000u, 0u);          /* 1 fragment */
        RT_SET(4, 0x00000008u, 0x00000008u);
        RT_SET(4, 0x00000040u, 0x00000040u);
        RT_SET(4, 0x00100200u, 0u);
        RT_SET(4, 0x00080000u, 0u);
        rt[14].value = (rt[14].value & 0xffffc000u) | ((H - 1) & 0x00003fffu);
        rt[14].value = (rt[14].value & 0xf0003fffu) | (((W - 1) << 14) & 0x0fffc000u);
        rt[14].value = (rt[14].value & 0x0fffffffu) | 0u;
        rt[15].value = (rt[15].value & 0xffffe000u) | 0u;
        RT_SET(15, 0x0007c000u, 0x0006c000u); /* TileMode = render target */
        RT_SET(15, 0x03000000u, 0x01000000u); /* 2D */
        RT_SET(15, 0x44000000u, 0x44000000u); /* metadata pipe alignment */
        { unsigned long long a = (unsigned long long)(unsigned long)fb[slot].ptr;
          rt[0].value = (unsigned int)((a >> 8) & 0xffffffffu);
          rt[10].value = (rt[10].value & 0xffffff00u) | (unsigned int)((a >> 40) & 0xffu);
          rt[5].value = 0; rt[11].value &= 0xffffff00u;
          rt[6].value = 0; rt[12].value &= 0xffffff00u;
          rt[9].value = 0; rt[13].value &= 0xffffff00u; }

        /* Viewport */
        CxRegister vp[14];
        { float xs = W * 0.5f, xo = xs, ys = -(float)H * 0.5f, yo = H * 0.5f;
          union { float f; unsigned int u; } cv;
          int i = 0;
          #define VPF(off, fval) { cv.f = (fval); vp[i].offset=(off); vp[i].pad=0; vp[i].value=cv.u; i++; }
          VPF(0x10F, xs); VPF(0x110, xo); VPF(0x111, ys); VPF(0x112, yo);
          VPF(0x113, 1.0f); VPF(0x114, 0.0f); VPF(0x0B4, 0.0f); VPF(0x0B5, 1.0f);
          VPF(0x2FA, 8.0f); VPF(0x2FB, 8.0f); VPF(0x2FC, 8.0f); VPF(0x2FD, 8.0f);
          #undef VPF
          vp[i].offset=0x090; vp[i].pad=0; vp[i].value=0x80000000u; i++;
          vp[i].offset=0x091; vp[i].pad=0; vp[i].value=(0x4000u) | (0x4000u << 16); i++; }

        CxRegister linkage[34], primitive_state[3];
        { int r = sceAgcLinkShaders(linkage, primitive_state, 0, vs_handle, ps_handle, 4);
          if (r < 0) { notify_rc("mesh_test: LinkShaders fallo", r); return 1; } }

        CxRegister context[16 + 14 + 1 + 34 + 32 + 32];
        int cx = 0;
        for (int i = 0; i < 16; i++) context[cx++] = rt[i];
        for (int i = 0; i < 14; i++) context[cx++] = vp[i];
        context[cx].offset = 0x008E; context[cx].pad = 0; context[cx].value = 0xF; cx++;  /* CB_TARGET_MASK */

        /* Back-face culling. Without a depth buffer the faces land in index
         * order, so the far side of the cube paints over the near side; for a
         * convex shape, dropping back faces is enough on its own to fix that.
         *
         * PA_SU_SC_MODE_CNTL is 0xA205, so 0x205 in the context register space
         * (the same mapping that makes PA_CL_VPORT_XSCALE 0xA10F the 0x10F the
         * viewport block already uses). Bits: [0] cull front, [1] cull back,
         * [2] which winding counts as front (0 = CCW, 1 = CW).
         *
         * This cube's front faces come out CLOCKWISE in window coordinates:
         * the viewport's yScale is negative, so clip-space +Y maps to smaller
         * row numbers, which flips the handedness of the winding the
         * rasterizer measures. Hence FACE = 1 alongside CULL_BACK. Start from
         * the driver's own default so the fields this doesn't touch (polygon
         * mode, the offset enables) keep their reset values. */
        {
            unsigned int mode = 0;
            for (unsigned int r = 0; r < cx_record_count; r++)
                if (cx_records[r].offset == 0x205) { mode = cx_records[r].value; break; }
            mode = (mode & ~0x7u) | 0x2u /* CULL_BACK */ | 0x4u /* front = CW */;
            context[cx].offset = 0x205; context[cx].pad = 0; context[cx].value = mode; cx++;
        }
        for (int i = 0; i < 34; i++) context[cx++] = linkage[i];
        for (int i = 0; i < SHDR_CXCOUNT(vs_handle); i++) context[cx++] = SHDR_CXREGS(vs_handle)[i];
        for (int i = 0; i < SHDR_CXCOUNT(ps_handle); i++) context[cx++] = SHDR_CXREGS(ps_handle)[i];

        CxRegister shr[32];
        int sh = 0;
        for (int i = 0; i < SHDR_SHCOUNT(vs_handle); i++) shr[sh++] = SHDR_SHREGS(vs_handle)[i];
        for (int i = 0; i < SHDR_SHCOUNT(ps_handle); i++) shr[sh++] = SHDR_SHREGS(ps_handle)[i];

        for (int i = 0; i < cx; i++) ((CxRegister*)ctx->ptr)[i] = context[i];
        for (int i = 0; i < sh; i++) ((CxRegister*)shm->ptr)[i] = shr[i];
        for (int i = 0; i < 3;  i++) ((CxRegister*)prm->ptr)[i] = primitive_state[i];

        sceAgcDcbSetCxRegistersIndirect(dcb, ctx->ptr, (unsigned int)cx);
        sceAgcDcbSetShRegistersIndirect(dcb, shm->ptr, (unsigned int)sh);
        sceAgcDcbSetUcRegistersIndirect(dcb, prm->ptr, 3);

        /* Both resources belong to the vertex stage. */
        sceAgcCbSetShRegisterRangeDirect(dcb, GS_USER_DATA_BASE + (unsigned int)cb_dwo, cb_words, 4);
        sceAgcCbSetShRegisterRangeDirect(dcb, GS_USER_DATA_BASE + (unsigned int)vb_dwo, vb_words, 4);

        sceAgcDcbSetIndexSize(dcb, 1, 0);
        sceAgcDcbSetIndexBuffer(dcb, ibuf.ptr);
        sceAgcDcbSetIndexCount(dcb, CUBE_INDICES);
        sceAgcDcbDrawIndex(dcb, CUBE_INDICES, ibuf.ptr, 0);
        sceAgcDcbSetFlip(dcb, (unsigned int)vhandle, slot, 1 /* VSync */, (long long)frame);

        struct { void *words; unsigned int wordCount; unsigned char flag; } submit;
        submit.words = dcb->bottom;
        submit.wordCount = (unsigned int)(dcb->up_cursor - dcb->bottom);
        submit.flag = 0;
        { int r = sceAgcDriverSubmitDcb(&submit);
          if (r < 0) { notify_rc("mesh_test: SubmitDcb fallo", r); return 1; } }
        sceAgcSuspendPoint();

        sceVideoOutWaitVblank(vhandle);
    }

    notify("mesh_test: fin");
    return 0;
}
