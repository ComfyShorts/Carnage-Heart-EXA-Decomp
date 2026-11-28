/* GE emulation: the sceGe imports, a display-list interpreter and a software
 * rasterizer writing into emulated video memory. See ge.h for the addressing rules.
 *
 * Supported: through-mode and transformed vertices (world/view/projection, viewport,
 * near-plane clipping), points as 1-pixel sprites, lines, triangles, strips, fans and
 * sprites, flat and Gouraud shading, the 565/5551/4444/8888 frame buffers and
 * textures, CLUT4/8/16/32 textures, swizzling, nearest and bilinear filtering,
 * repeat/clamp, the five texture functions, alpha/stencil/depth tests, stencil
 * operations, blending, pixel masks, clear mode, and block transfers.
 *
 * Not yet: lighting, fog, skinning and morphing (the first position is used), Bezier
 * and spline patches, DXT textures, mipmaps (level 0 only), color test, logic ops,
 * dithering, texture projection/environment mapping. Each logs once when met. */
#include "ge.h"
#include "psp_imports.h"
#include "psp_shim.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __SSE2__
#include <emmintrin.h>
#endif

#ifdef PCPORT_M32
_Static_assert(sizeof(void *) == 4, "GE lists carry 32-bit host addresses");
#endif

static _Alignas(64) uint8_t g_vram[PSP_EDRAM_SIZE];

uint8_t *psp_ge_vram(void)
{
    return g_vram;
}

bool psp_ge_reserve_vram_window(void)
{
#ifdef _WIN32
    /* Before the game's big allocations: a 32-bit Windows heap can otherwise grow over it. */
    if (!VirtualAlloc((void *)0x04000000, PSP_EDRAM_SIZE, MEM_RESERVE, PAGE_NOACCESS)) {
        SDL_Log("[ge] warning: can't reserve 0x04000000-0x041FFFFF (in use), so host memory there "
                "would be read as VRAM");
        return false;
    }
#endif
    return true;
}

uint8_t *psp_ge_ptr(uint32_t addr)
{
    /* The PSP's own VRAM addresses. No host memory is ever there: nothing is mapped so low
     * on Linux, and on Windows the window is reserved at start-up (psp_ge_reserve_vram_window).
     * Frame and depth buffers are VRAM offsets and don't come through here; small addresses
     * aren't taken as offsets either, since Windows keeps its stacks and heap below 2 MB. */
    if ((addr & 0xFFE00000u) == 0x04000000u)
        return g_vram + (addr & (PSP_EDRAM_SIZE - 1));
    return (uint8_t *)(uintptr_t)addr;
}

/* ---- state ---- */

enum {
    CMD_NOP = 0x00, CMD_VADDR = 0x01, CMD_IADDR = 0x02, CMD_PRIM = 0x04, CMD_BEZIER = 0x05,
    CMD_SPLINE = 0x06, CMD_BBOX = 0x07, CMD_JUMP = 0x08, CMD_BJUMP = 0x09, CMD_CALL = 0x0A,
    CMD_RET = 0x0B, CMD_END = 0x0C, CMD_SIGNAL = 0x0E, CMD_FINISH = 0x0F, CMD_BASE = 0x10,
    CMD_VTYPE = 0x12, CMD_OFFSETADDR = 0x13, CMD_ORIGIN = 0x14, CMD_LTE = 0x17, CMD_BCE = 0x1D,
    CMD_TME = 0x1E, CMD_FGE = 0x1F, CMD_DTE = 0x20, CMD_ABE = 0x21, CMD_ATE = 0x22, CMD_ZTE = 0x23,
    CMD_STE = 0x24, CMD_CTE = 0x27, CMD_LOE = 0x28, CMD_WMS = 0x3A, CMD_WORLD = 0x3B,
    CMD_VMS = 0x3C, CMD_VIEW = 0x3D, CMD_PMS = 0x3E, CMD_PROJ = 0x3F, CMD_TMS = 0x40,
    CMD_TMATRIX = 0x41, CMD_SX = 0x42, CMD_SY = 0x43, CMD_SZ = 0x44, CMD_TX = 0x45, CMD_TY = 0x46,
    CMD_TZ = 0x47, CMD_SU = 0x48, CMD_SV = 0x49, CMD_TU = 0x4A, CMD_TV = 0x4B, CMD_OFFSETX = 0x4C,
    CMD_OFFSETY = 0x4D, CMD_SHADE = 0x50, CMD_AMC = 0x55, CMD_AMA = 0x58, CMD_FFACE = 0x9B,
    CMD_FBP = 0x9C, CMD_FBW = 0x9D, CMD_ZBP = 0x9E, CMD_ZBW = 0x9F, CMD_TBP0 = 0xA0, CMD_TBW0 = 0xA8,
    CMD_CBP = 0xB0, CMD_CBPH = 0xB1, CMD_TRXSBP = 0xB2, CMD_TRXSBW = 0xB3, CMD_TRXDBP = 0xB4,
    CMD_TRXDBW = 0xB5, CMD_TSIZE0 = 0xB8, CMD_TMAP = 0xC0, CMD_TMODE = 0xC2, CMD_TPF = 0xC3,
    CMD_CLOAD = 0xC4, CMD_CLUT = 0xC5, CMD_TFLT = 0xC6, CMD_TWRAP = 0xC7, CMD_TFUNC = 0xC9,
    CMD_TEC = 0xCA, CMD_FPF = 0xD2, CMD_CMODE = 0xD3, CMD_SCISSOR1 = 0xD4, CMD_SCISSOR2 = 0xD5,
    CMD_MINZ = 0xD6, CMD_MAXZ = 0xD7, CMD_ATST = 0xDB, CMD_STST = 0xDC, CMD_SOP = 0xDD,
    CMD_ZTST = 0xDE, CMD_ALPHA = 0xDF, CMD_SFIX = 0xE0, CMD_DFIX = 0xE1, CMD_ZMSK = 0xE7,
    CMD_PMSKC = 0xE8, CMD_PMSKA = 0xE9, CMD_TRXKICK = 0xEA, CMD_TRXSPOS = 0xEB, CMD_TRXDPOS = 0xEC,
    CMD_TRXSIZE = 0xEE,
};

typedef struct Ge {
    uint32_t reg[256];
    float world[12], view[12], proj[16], tmat[12];
    int world_i, view_i, proj_i, tmat_i;
    uint32_t vaddr, iaddr;
    uint8_t clut[1024];
    uint32_t prims, pixels;
} Ge;

static Ge g_ge;
static SDL_Mutex *g_ge_lock;
static bool g_ge_trace;

/* Primitive counts per frame-buffer offset, for the screenshot's "first frame drawn". */
static struct { uint32_t fbp, prims; } g_fb_prims[4];

void psp_ge_set_trace(bool on)
{
    g_ge_trace = on;
}

uint32_t psp_ge_prims_into(uint32_t fb_offset)
{
    for (int i = 0; i < 4; i++)
        if (g_fb_prims[i].prims && g_fb_prims[i].fbp == fb_offset)
            return g_fb_prims[i].prims;
    return 0;
}

static void count_prim_into(uint32_t fbp)
{
    for (int i = 0; i < 4; i++)
        if (g_fb_prims[i].prims && g_fb_prims[i].fbp == fbp) {
            g_fb_prims[i].prims++;
            return;
        }
    for (int i = 0; i < 4; i++)
        if (!g_fb_prims[i].prims) {
            g_fb_prims[i].fbp = fbp;
            g_fb_prims[i].prims = 1;
            return;
        }
}

static float f24(uint32_t v)
{
    uint32_t bits = v << 8;
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static uint32_t arg(int cmd)
{
    return g_ge.reg[cmd] & 0xFFFFFF;
}

static void unsupported(const char *what)
{
    static const char *seen[32];
    for (int i = 0; i < 32; i++) {
        if (seen[i] == what)
            return;
        if (!seen[i]) {
            seen[i] = what;
            SDL_Log("[ge] not implemented yet: %s", what);
            return;
        }
    }
}

/* ---- colors (PSP order: R in the low bits) ---- */

static uint32_t decode16(uint16_t c, int fmt)
{
    uint32_t r, g, b, a;
    switch (fmt) {
    case 0: /* 565 */
        r = c & 31; g = (c >> 5) & 63; b = c >> 11;
        r = r << 3 | r >> 2; g = g << 2 | g >> 4; b = b << 3 | b >> 2; a = 255;
        break;
    case 1: /* 5551 */
        r = c & 31; g = (c >> 5) & 31; b = (c >> 10) & 31;
        r = r << 3 | r >> 2; g = g << 3 | g >> 2; b = b << 3 | b >> 2; a = (c >> 15) ? 255 : 0;
        break;
    default: /* 4444 */
        r = (c & 15) * 17; g = ((c >> 4) & 15) * 17; b = ((c >> 8) & 15) * 17; a = (c >> 12) * 17;
        break;
    }
    return r | g << 8 | b << 16 | a << 24;
}

static uint16_t encode16(uint32_t c, int fmt)
{
    uint32_t r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255, a = c >> 24;
    switch (fmt) {
    case 0: return (uint16_t)(r >> 3 | (g >> 2) << 5 | (b >> 3) << 11);
    case 1: return (uint16_t)(r >> 3 | (g >> 3) << 5 | (b >> 3) << 10 | (a >= 128) << 15);
    default: return (uint16_t)(r >> 4 | (g >> 4) << 4 | (b >> 4) << 8 | (a >> 4) << 12);
    }
}

/* ---- textures ---- */

/* One primitive's texture state, decoded from the registers once (tex_setup) instead
 * of for every texel. */
typedef struct Tex {
    const uint8_t *base;
    int tpf, bpp, stride;
    bool swizzle;
    uint32_t row_blocks;  /* swizzled: 16-byte blocks per row of the buffer */
    int w, h, cu, cv;     /* size, clamp (1) or wrap (0) */
    int filter_min, filter_mag;
    int clut_shift, clut_mask, clut_offset;
    const uint32_t *clut; /* 512 entries as RGBA, for the CLUT formats */
} Tex;

static uint32_t tex_addr(int level)
{
    return arg(CMD_TBP0 + level) | (g_ge.reg[CMD_TBW0 + level] & 0xFF0000) << 8;
}

static int bits_per_texel(int tpf)
{
    static const int bpp[] = {16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8};
    return tpf <= 10 ? bpp[tpf] : 32;
}

/* The CLUT as RGBA, decoded again only after a CLOAD or when its format changes.
 * A 32-bit CLUT has 256 entries; the table repeats them so that an index masked
 * with 511 works for both. */
static uint32_t g_clut_rgba[512];
static uint32_t g_clut_gen = 1, g_clut_rgba_gen;
static int g_clut_rgba_fmt = -1;

static const uint32_t *clut_table(int fmt)
{
    if (g_clut_rgba_gen != g_clut_gen || g_clut_rgba_fmt != fmt) {
        for (int i = 0; i < 512; i++)
            g_clut_rgba[i] = fmt == 3 ? ((const uint32_t *)g_ge.clut)[i & 255]
                                      : decode16(((const uint16_t *)g_ge.clut)[i], fmt);
        g_clut_rgba_gen = g_clut_gen;
        g_clut_rgba_fmt = fmt;
    }
    return g_clut_rgba;
}

static void tex_setup(Tex *x)
{
    x->tpf = arg(CMD_TPF) & 15;
    x->stride = g_ge.reg[CMD_TBW0] & 0x7FF;
    x->bpp = bits_per_texel(x->tpf);
    x->base = psp_ge_ptr(tex_addr(0));
    x->swizzle = arg(CMD_TMODE) & 1;
    x->row_blocks = (uint32_t)x->stride * (uint32_t)x->bpp / 8 / 16;
    x->w = 1 << (arg(CMD_TSIZE0) & 15);
    x->h = 1 << ((arg(CMD_TSIZE0) >> 8) & 15);
    x->cu = arg(CMD_TWRAP) & 1;
    x->cv = (arg(CMD_TWRAP) >> 8) & 1;
    x->filter_min = arg(CMD_TFLT) & 1;
    x->filter_mag = (arg(CMD_TFLT) >> 8) & 1;
    uint32_t mode = arg(CMD_CLUT);
    x->clut_shift = (mode >> 2) & 31;
    x->clut_mask = (mode >> 8) & 255;
    x->clut_offset = ((mode >> 16) & 31) << 4;
    x->clut = x->tpf >= 4 && x->tpf <= 7 ? clut_table(mode & 3) : NULL;
    if (x->tpf > 7)
        unsupported("DXT textures");
}

static inline uint32_t clut_lookup(const Tex *x, uint32_t index)
{
    return x->clut[(((index >> x->clut_shift) & (uint32_t)x->clut_mask) | (uint32_t)x->clut_offset) & 511];
}

/* One texel of level 0, (u, v) already wrapped into range. */
static inline uint32_t texel(const Tex *x, int u, int v)
{
    uint32_t off_bits;
    if (x->swizzle) {
        /* 16-byte by 8-row blocks laid out row of blocks after row. */
        uint32_t xb = (uint32_t)u * (uint32_t)x->bpp / 8;
        uint32_t block = (uint32_t)(v / 8) * x->row_blocks + xb / 16;
        uint32_t byte = block * 128 + (uint32_t)(v % 8) * 16 + xb % 16;
        off_bits = byte * 8 + (x->bpp == 4 ? (u & 1) * 4 : 0);
    } else {
        off_bits = ((uint32_t)v * (uint32_t)x->stride + (uint32_t)u) * (uint32_t)x->bpp;
    }
    const uint8_t *p = x->base + off_bits / 8;
    switch (x->tpf) {
    case 0: case 1: case 2: return decode16(*(const uint16_t *)p, x->tpf);
    case 3: return *(const uint32_t *)p;
    case 4: return clut_lookup(x, (off_bits & 4) ? *p >> 4 : *p & 15);
    case 5: return clut_lookup(x, *p);
    case 6: return clut_lookup(x, *(const uint16_t *)p);
    case 7: return clut_lookup(x, *(const uint32_t *)p);
    default: return 0xFFFF00FF; /* DXT */
    }
}

static inline int wrap(int c, int size, int clamp)
{
    if (clamp)
        return c < 0 ? 0 : c >= size ? size - 1 : c;
    return c & (size - 1);
}

/* (int)floorf(f) without the library call (SSE2 has no floor instruction). */
static inline int ifloor(float f)
{
    int i = (int)f;
    return f < (float)i && i != INT_MIN ? i - 1 : i;
}

/* The four texels weighted per channel, in SSE lanes: the same float operations in
 * the same order as one channel at a time, so the same result. */
static inline uint32_t bilerp(uint32_t t0, uint32_t t1, uint32_t t2, uint32_t t3, float au, float av)
{
#ifdef __SSE2__
    const __m128i z = _mm_setzero_si128();
#define CH4(t) _mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)(t)), z), z))
    __m128 c0 = CH4(t0), c1 = CH4(t1), c2 = CH4(t2), c3 = CH4(t3);
#undef CH4
    __m128 iu = _mm_set1_ps(1 - au), fu = _mm_set1_ps(au);
    __m128 iv = _mm_set1_ps(1 - av), fv = _mm_set1_ps(av);
    __m128 a = _mm_add_ps(_mm_mul_ps(c0, iu), _mm_mul_ps(c1, fu));
    __m128 b = _mm_add_ps(_mm_mul_ps(c2, iu), _mm_mul_ps(c3, fu));
    __m128 r = _mm_add_ps(_mm_add_ps(_mm_mul_ps(a, iv), _mm_mul_ps(b, fv)), _mm_set1_ps(0.5f));
    __m128i i = _mm_cvttps_epi32(r);
    i = _mm_packs_epi32(i, i);
    i = _mm_packus_epi16(i, i);
    return (uint32_t)_mm_cvtsi128_si32(i);
#else
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        float a = ((t0 >> s) & 255) * (1 - au) + ((t1 >> s) & 255) * au;
        float b = ((t2 >> s) & 255) * (1 - au) + ((t3 >> s) & 255) * au;
        out |= (uint32_t)(a * (1 - av) + b * av + 0.5f) << s;
    }
    return out;
#endif
}

/* Sample at texel coordinates (u, v) (texel centres at +0.5). */
static inline uint32_t sample(const Tex *x, float u, float v, bool magnify)
{
    int filter = magnify ? x->filter_mag : x->filter_min;
    if (!filter)
        return texel(x, wrap(ifloor(u), x->w, x->cu), wrap(ifloor(v), x->h, x->cv));
    float fu = u - 0.5f, fv = v - 0.5f;
    int u0 = ifloor(fu), v0 = ifloor(fv);
    float au = fu - u0, av = fv - v0;
    int ua = wrap(u0, x->w, x->cu), ub = wrap(u0 + 1, x->w, x->cu);
    int va = wrap(v0, x->h, x->cv), vb = wrap(v0 + 1, x->h, x->cv);
    return bilerp(texel(x, ua, va), texel(x, ub, va), texel(x, ua, vb), texel(x, ub, vb), au, av);
}

/* ---- vertices ---- */

typedef struct Vtx {
    float x, y, z, w;   /* screen x/y (pixels in the draw buffer), depth, clip w */
    float u, v;         /* texel units for through mode, normalised otherwise */
    float c[4];         /* r g b a, 0..255 */
} Vtx;

typedef struct VFmt {
    int tc, col, nrm, pos, wgt, idx, wcount, morph;
    bool through;
    int size, tc_off, col_off, pos_off;
} VFmt;

static int align_to(int v, int a)
{
    return (v + a - 1) & -a;
}

static VFmt vfmt(uint32_t t)
{
    static const int tsz[] = {0, 1, 2, 4}, csz[] = {0, 0, 0, 0, 2, 2, 2, 4}, nsz[] = {0, 1, 2, 4};
    VFmt f = {
        .tc = t & 3, .col = (t >> 2) & 7, .nrm = (t >> 5) & 3, .pos = (t >> 7) & 3,
        .wgt = (t >> 9) & 3, .idx = (t >> 11) & 3, .wcount = ((t >> 14) & 7) + 1,
        .morph = ((t >> 18) & 7) + 1, .through = (t >> 23) & 1,
    };
    int off = 0, maxa = 1;
    if (f.wgt) {
        off = align_to(off, tsz[f.wgt]) + tsz[f.wgt] * f.wcount;
        if (tsz[f.wgt] > maxa) maxa = tsz[f.wgt];
    }
    if (f.tc) {
        off = align_to(off, tsz[f.tc]);
        f.tc_off = off;
        off += tsz[f.tc] * 2;
        if (tsz[f.tc] > maxa) maxa = tsz[f.tc];
    }
    if (f.col) {
        off = align_to(off, csz[f.col]);
        f.col_off = off;
        off += csz[f.col];
        if (csz[f.col] > maxa) maxa = csz[f.col];
    }
    if (f.nrm) {
        off = align_to(off, nsz[f.nrm]) + nsz[f.nrm] * 3;
        if (nsz[f.nrm] > maxa) maxa = nsz[f.nrm];
    }
    if (f.pos) {
        off = align_to(off, nsz[f.pos]);
        f.pos_off = off;
        off += nsz[f.pos] * 3;
        if (nsz[f.pos] > maxa) maxa = nsz[f.pos];
    }
    f.size = align_to(off, maxa);
    return f;
}

static float comp(const uint8_t *p, int fmt, int i, bool norm, bool is_signed)
{
    switch (fmt) {
    case 1: return is_signed ? ((const int8_t *)p)[i] / (norm ? 127.0f : 1.0f)
                             : p[i] / (norm ? 128.0f : 1.0f);
    case 2: return is_signed ? ((const int16_t *)p)[i] / (norm ? 32767.0f : 1.0f)
                             : ((const uint16_t *)p)[i] / (norm ? 32768.0f : 1.0f);
    case 3: return ((const float *)p)[i];
    default: return 0;
    }
}

/* Clip-space position before the divide. */
typedef struct Clip {
    float x, y, z, w;
} Clip;

static void mul43(const float *m, const float *in, float *out)
{
    for (int r = 0; r < 3; r++)
        out[r] = m[r] * in[0] + m[3 + r] * in[1] + m[6 + r] * in[2] + m[9 + r];
}

static void decode_vertex(const VFmt *f, const uint8_t *p, Vtx *v, Clip *clip)
{
    memset(v, 0, sizeof *v);
    if (f->tc) {
        v->u = comp(p + f->tc_off, f->tc, 0, !f->through, false);
        v->v = comp(p + f->tc_off, f->tc, 1, !f->through, false);
    }
    uint32_t col;
    if (f->col) {
        const uint8_t *q = p + f->col_off;
        col = f->col == 7 ? *(const uint32_t *)q : decode16(*(const uint16_t *)q, f->col - 4);
    } else {
        col = arg(CMD_AMC) | (g_ge.reg[CMD_AMA] & 255) << 24;
    }
    for (int i = 0; i < 4; i++)
        v->c[i] = (float)((col >> (i * 8)) & 255);
    float pos[3];
    for (int i = 0; i < 3; i++)
        pos[i] = comp(p + f->pos_off, f->pos, i, !f->through, true);
    if (f->through) {
        v->x = pos[0];
        v->y = pos[1];
        v->z = f->pos == 3 ? pos[2] : (float)((const uint16_t *)(p + f->pos_off))[2];
        v->w = 1;
        return;
    }
    float wp[3], vp[3];
    mul43(g_ge.world, pos, wp);
    mul43(g_ge.view, wp, vp);
    const float *m = g_ge.proj;
    clip->x = m[0] * vp[0] + m[4] * vp[1] + m[8] * vp[2] + m[12];
    clip->y = m[1] * vp[0] + m[5] * vp[1] + m[9] * vp[2] + m[13];
    clip->z = m[2] * vp[0] + m[6] * vp[1] + m[10] * vp[2] + m[14];
    clip->w = m[3] * vp[0] + m[7] * vp[1] + m[11] * vp[2] + m[15];
    /* Texture coordinates: scale and offset, still normalised. */
    v->u = v->u * f24(arg(CMD_SU)) + f24(arg(CMD_TU));
    v->v = v->v * f24(arg(CMD_SV)) + f24(arg(CMD_TV));
}

static void project(Vtx *v, const Clip *c)
{
    float iw = 1.0f / c->w;
    v->x = c->x * iw * f24(arg(CMD_SX)) + f24(arg(CMD_TX)) - (arg(CMD_OFFSETX) & 0xFFFF) / 16.0f;
    v->y = c->y * iw * f24(arg(CMD_SY)) + f24(arg(CMD_TY)) - (arg(CMD_OFFSETY) & 0xFFFF) / 16.0f;
    v->z = c->z * iw * f24(arg(CMD_SZ)) + f24(arg(CMD_TZ));
    v->w = c->w;
}

/* ---- pixels ---- */

/* One primitive's drawing state, decoded from the registers once (target()). The
 * per-pixel code below does the same arithmetic as reading the registers each time,
 * in the same order, so it draws the same pixels. */
typedef struct Target {
    uint8_t *fb;
    int fbw, psm;
    uint16_t *zb;
    int zbw;
    int x0, y0, x1, y1;     /* scissor, inclusive */
    int bx0, by0, bx1, by1; /* the scissor within the buffer */
    bool clear;
    uint32_t cmode;
    uint32_t clear_keep;    /* clear mode: the bits kept */
    bool clear_z;
    bool tex, through;
    Tex tx;
    int tfunc;              /* texture function */
    bool trgba, tdbl;
    uint32_t tenv;
    bool ate;               /* alpha test */
    int at_func;
    uint32_t at_ref, at_mask;
    bool ste, ste_test;     /* stencil: enabled, and tested (not in 565) */
    int st_func, sop_fail, sop_zfail, sop_zpass;
    uint32_t st_ref, st_mask;
    bool ztest, zwrite;
    int z_func;
    bool abe, blend_std;    /* blending; the usual src alpha / 1 - src alpha */
    int sf, df, op;
    uint32_t sfix, dfix;
    uint32_t keep;          /* bits a write keeps (pixel masks, stencil) */
} Target;

static bool compare(int func, uint32_t a, uint32_t b)
{
    switch (func) {
    case 0: return false;
    case 1: return true;
    case 2: return a == b;
    case 3: return a != b;
    case 4: return a < b;
    case 5: return a <= b;
    case 6: return a > b;
    default: return a >= b;
    }
}

static inline uint32_t read_fb(const Target *t, int x, int y)
{
    if (t->psm == 3)
        return ((uint32_t *)t->fb)[y * t->fbw + x];
    return decode16(((uint16_t *)t->fb)[y * t->fbw + x], t->psm);
}

static inline void write_fb(const Target *t, int x, int y, uint32_t c, uint32_t keep_mask)
{
    if (t->psm == 3) {
        uint32_t *p = &((uint32_t *)t->fb)[y * t->fbw + x];
        *p = (*p & keep_mask) | (c & ~keep_mask);
    } else {
        uint16_t *p = &((uint16_t *)t->fb)[y * t->fbw + x];
        uint16_t n = encode16(c, t->psm);
        uint16_t keep = encode16(keep_mask, t->psm);
        if (t->psm == 1)
            keep = (uint16_t)((keep & 0x7FFF) | ((keep_mask >> 24) ? 0x8000 : 0));
        *p = (uint16_t)((*p & keep) | (n & ~keep));
    }
}

static uint32_t blend_factor(int sel, uint32_t src, uint32_t dst, uint32_t fix, int ch, bool is_src)
{
    uint32_t sa = src >> 24, da = dst >> 24;
    uint32_t other = is_src ? (dst >> ch) & 255 : (src >> ch) & 255;
    switch (sel) {
    case 0: return other;
    case 1: return 255 - other;
    case 2: return sa;
    case 3: return 255 - sa;
    case 4: return da;
    case 5: return 255 - da;
    case 6: return sa * 2 > 255 ? 255 : sa * 2;
    case 7: return sa * 2 > 255 ? 0 : 255 - sa * 2;
    case 8: return da * 2 > 255 ? 255 : da * 2;
    case 9: return da * 2 > 255 ? 0 : 255 - da * 2;
    default: return (fix >> ch) & 255;
    }
}

#ifdef __SSE2__
/* x / 255 for 0 <= x <= 65025, in 16-bit lanes: (x + 1 + (x >> 8)) >> 8 is exact there. */
static inline __m128i div255_epu16(__m128i x)
{
    return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(x, _mm_set1_epi16(1)), _mm_srli_epi16(x, 8)), 8);
}

static inline __m128i unpack4(uint32_t c)
{
    return _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)c), _mm_setzero_si128());
}

static inline uint32_t pack4(__m128i v)
{
    return (uint32_t)_mm_cvtsi128_si32(_mm_packus_epi16(v, v));
}
#endif

static inline uint32_t blend(const Target *t, uint32_t src, uint32_t dst)
{
    if (t->blend_std) {
        /* (s * sa + d * (255 - sa)) / 255: at most 65025, so 16-bit lanes are exact. */
        uint32_t sa = src >> 24;
#ifdef __SSE2__
        __m128i m = _mm_add_epi16(_mm_mullo_epi16(unpack4(src), _mm_set1_epi16((short)sa)),
                                  _mm_mullo_epi16(unpack4(dst), _mm_set1_epi16((short)(255 - sa))));
        return (pack4(div255_epu16(m)) & 0x00FFFFFFu) | (src & 0xFF000000u);
#else
        uint32_t out = src & 0xFF000000u;
        for (int ch = 0; ch < 24; ch += 8)
            out |= (((src >> ch) & 255) * sa + ((dst >> ch) & 255) * (255 - sa)) / 255 << ch;
        return out;
#endif
    }
    uint32_t out = src & 0xFF000000u;
    for (int ch = 0; ch < 24; ch += 8) {
        int s = (src >> ch) & 255, d = (dst >> ch) & 255;
        int fs = (int)blend_factor(t->sf, src, dst, t->sfix, ch, true);
        int fd = (int)blend_factor(t->df, src, dst, t->dfix, ch, false);
        int r;
        switch (t->op) {
        case 0: r = (s * fs + d * fd) / 255; break;
        case 1: r = (s * fs - d * fd) / 255; break;
        case 2: r = (d * fd - s * fs) / 255; break;
        case 3: r = s < d ? s : d; break;
        case 4: r = s > d ? s : d; break;
        default: r = s > d ? s - d : d - s; break;
        }
        r = r < 0 ? 0 : r > 255 ? 255 : r;
        out |= (uint32_t)r << ch;
    }
    return out;
}

static inline uint32_t tex_function(const Target *tg, uint32_t prim, uint32_t tex)
{
    int func = tg->tfunc;
    bool rgba = tg->trgba, dbl = tg->tdbl;
#ifdef __SSE2__
    if (func == 0 && !dbl) {
        /* Modulate: p * t / 255 in every channel (alpha only with RGBA). */
        uint32_t m = pack4(div255_epu16(_mm_mullo_epi16(unpack4(prim), unpack4(tex))));
        return rgba ? m : (m & 0x00FFFFFFu) | (prim & 0xFF000000u);
    }
#endif
    uint32_t env = tg->tenv;
    uint32_t out = 0;
    for (int ch = 0; ch < 32; ch += 8) {
        int p = (prim >> ch) & 255, t = (tex >> ch) & 255, e = (env >> ch) & 255;
        int r;
        bool alpha = ch == 24;
        if (alpha) {
            if (!rgba) r = p;
            else if (func == 1 || func == 3) r = func == 1 ? p : t;
            else r = p * t / 255;
        } else {
            int ta = tex >> 24;
            switch (func) {
            case 0: r = p * t / 255; break;
            case 1: r = rgba ? (t * ta + p * (255 - ta)) / 255 : t; break;
            case 2: r = (p * (255 - t) + e * t) / 255; break;
            case 3: r = t; break;
            default: r = p + t; break;
            }
            if (dbl) r *= 2;
        }
        out |= (uint32_t)(r > 255 ? 255 : r) << ch;
    }
    return out;
}

static void stencil_op(const Target *t, int x, int y, int op)
{
    if (t->psm == 0)
        return;
    uint32_t dst = read_fb(t, x, y);
    uint32_t s = dst >> 24, n;
    switch (op) {
    case 0: return; /* keep */
    case 1: n = 0; break;
    case 2: n = t->st_ref; break;
    case 3: n = ~s & 255; break;
    case 4: n = s < 255 ? s + 1 : 255; break;
    default: n = s ? s - 1 : 0; break;
    }
    write_fb(t, x, y, (dst & 0xFFFFFF) | n << 24, 0x00FFFFFF);
}

static inline void plot(const Target *t, int x, int y, float z, uint32_t color, float u, float v, bool magnify)
{
    if (x < t->bx0 || x > t->bx1 || y < t->by0 || y > t->by1)
        return;
    g_ge.pixels++;
    if (t->clear) {
        int zi = (int)(z + 0.5f);
        zi = zi < 0 ? 0 : zi > 65535 ? 65535 : zi;
        if (t->clear_keep != 0xFFFFFFFFu)
            write_fb(t, x, y, color, t->clear_keep);
        if (t->clear_z)
            t->zb[y * t->zbw + x] = (uint16_t)zi;
        return;
    }
    if (t->tex)
        color = tex_function(t, color, sample(&t->tx, u, v, magnify));
    if (t->ate && !compare(t->at_func, (color >> 24) & t->at_mask, t->at_ref & t->at_mask))
        return;
    if (t->ste_test) {
        uint32_t s = read_fb(t, x, y) >> 24;
        if (!compare(t->st_func, t->st_ref & t->st_mask, s & t->st_mask)) {
            stencil_op(t, x, y, t->sop_fail);
            return;
        }
    }
    int zi = 0;
    if (t->ztest) {
        zi = (int)(z + 0.5f);
        zi = zi < 0 ? 0 : zi > 65535 ? 65535 : zi;
        if (!compare(t->z_func, (uint32_t)zi, t->zb[y * t->zbw + x])) {
            if (t->ste)
                stencil_op(t, x, y, t->sop_zfail);
            return;
        }
    }
    if (t->ste)
        stencil_op(t, x, y, t->sop_zpass);
    if (t->abe)
        color = blend(t, color, read_fb(t, x, y));
    write_fb(t, x, y, color, t->keep);
    if (t->zwrite)
        t->zb[y * t->zbw + x] = (uint16_t)zi;
}

static uint32_t pack(const float c[4])
{
    uint32_t out = 0;
    for (int i = 0; i < 4; i++) {
        float v = c[i] < 0 ? 0 : c[i] > 255 ? 255 : c[i];
        out |= (uint32_t)(v + 0.5f) << (i * 8);
    }
    return out;
}

static bool target(Target *t, bool through)
{
    memset(t, 0, sizeof *t);
    t->fb = g_vram + (arg(CMD_FBP) & (PSP_EDRAM_SIZE - 1));
    t->fbw = g_ge.reg[CMD_FBW] & 0x7FF;
    t->psm = arg(CMD_FPF) & 3;
    t->zb = (uint16_t *)(g_vram + (arg(CMD_ZBP) & (PSP_EDRAM_SIZE - 1)));
    t->zbw = g_ge.reg[CMD_ZBW] & 0x7FF;
    if (!t->zbw)
        t->zb = NULL;
    t->x0 = arg(CMD_SCISSOR1) & 0x3FF;
    t->y0 = (arg(CMD_SCISSOR1) >> 10) & 0x3FF;
    t->x1 = arg(CMD_SCISSOR2) & 0x3FF;
    t->y1 = (arg(CMD_SCISSOR2) >> 10) & 0x3FF;
    t->bx0 = t->x0 > 0 ? t->x0 : 0;
    t->by0 = t->y0 > 0 ? t->y0 : 0;
    t->bx1 = t->x1 < t->fbw - 1 ? t->x1 : t->fbw - 1;
    t->by1 = t->y1 < 1023 ? t->y1 : 1023;
    t->clear = g_ge.reg[CMD_CMODE] & 1;
    t->cmode = arg(CMD_CMODE);
    t->clear_keep = 0xFFFFFFFFu;
    if (t->cmode & 0x100) t->clear_keep &= 0xFF000000u;
    if (t->cmode & 0x200) t->clear_keep &= 0x00FFFFFFu;
    t->clear_z = (t->cmode & 0x400) && t->zb;
    t->tex = (g_ge.reg[CMD_TME] & 1) && !t->clear;
    t->through = through;
    if (t->tex && (arg(CMD_TMAP) & 3) != 0)
        unsupported("texture projection/environment mapping");
    if (t->tex) {
        tex_setup(&t->tx);
        uint32_t f = arg(CMD_TFUNC);
        t->tfunc = f & 7;
        t->trgba = (f >> 8) & 1;
        t->tdbl = (f >> 16) & 1;
        t->tenv = arg(CMD_TEC);
    }
    uint32_t at = arg(CMD_ATST);
    t->ate = g_ge.reg[CMD_ATE] & 1;
    t->at_func = at & 7;
    t->at_ref = (at >> 8) & 255;
    t->at_mask = (at >> 16) & 255;
    uint32_t st = arg(CMD_STST), sop = arg(CMD_SOP);
    t->ste = g_ge.reg[CMD_STE] & 1;
    t->ste_test = t->ste && t->psm != 0;
    t->st_func = st & 7;
    t->st_ref = (st >> 8) & 255;
    t->st_mask = (st >> 16) & 255;
    t->sop_fail = sop & 7;
    t->sop_zfail = (sop >> 8) & 7;
    t->sop_zpass = (sop >> 16) & 7;
    t->ztest = (g_ge.reg[CMD_ZTE] & 1) && t->zb;
    t->zwrite = t->ztest && !(g_ge.reg[CMD_ZMSK] & 1);
    t->z_func = arg(CMD_ZTST) & 7;
    uint32_t a = arg(CMD_ALPHA);
    t->abe = g_ge.reg[CMD_ABE] & 1;
    t->sf = a & 15;
    t->df = (a >> 4) & 15;
    t->op = (a >> 8) & 15;
    t->sfix = arg(CMD_SFIX);
    t->dfix = arg(CMD_DFIX);
    t->blend_std = t->sf == 2 && t->df == 3 && t->op == 0;
    t->keep = arg(CMD_PMSKC) | (g_ge.reg[CMD_PMSKA] & 255) << 24;
    if (t->ste)
        t->keep |= 0xFF000000u; /* alpha holds the stencil, already written */
    return t->fbw > 0;
}

/* Texel coordinates for a vertex: through mode gives texels already. */
static void texcoords(const Target *t, const Vtx *v, float *u, float *vv)
{
    if (t->through) {
        *u = v->u;
        *vv = v->v;
    } else {
        *u = v->u * (float)(1 << (arg(CMD_TSIZE0) & 15));
        *vv = v->v * (float)(1 << ((arg(CMD_TSIZE0) >> 8) & 15));
    }
}

static void draw_sprite(const Target *t, const Vtx *a, const Vtx *b)
{
    float xa = a->x, xb = b->x, ya = a->y, yb = b->y;
    float ua, va, ub, vb;
    texcoords(t, a, &ua, &va);
    texcoords(t, b, &ub, &vb);
    if (xa > xb) { float s = xa; xa = xb; xb = s; s = ua; ua = ub; ub = s; }
    if (ya > yb) { float s = ya; ya = yb; yb = s; s = va; va = vb; vb = s; }
    int x0 = (int)ceilf(xa - 0.5f), x1 = (int)ceilf(xb - 0.5f);
    int y0 = (int)ceilf(ya - 0.5f), y1 = (int)ceilf(yb - 0.5f);
    uint32_t color = pack(b->c);
    float du = xb > xa ? (ub - ua) / (xb - xa) : 0, dv = yb > ya ? (vb - va) / (yb - ya) : 0;
    bool mag = fabsf(du) < 1.0f && fabsf(dv) < 1.0f;
    /* Only the pixels plot() would keep: the scissor within the buffer. */
    int ys = y0 < t->by0 ? t->by0 : y0, ye = y1 - 1 < t->by1 ? y1 - 1 : t->by1;
    int xs = x0 < t->bx0 ? t->bx0 : x0, xe = x1 - 1 < t->bx1 ? x1 - 1 : t->bx1;
    if (t->clear) {
        /* What plot() does in clear mode, a row at a time. */
        if (xs > xe || ys > ye)
            return;
        int zi = (int)(b->z + 0.5f);
        zi = zi < 0 ? 0 : zi > 65535 ? 65535 : zi;
        uint32_t keep = t->clear_keep;
        uint16_t n16 = 0, keep16 = 0;
        if (t->psm != 3) {
            n16 = encode16(color, t->psm);
            keep16 = encode16(keep, t->psm);
            if (t->psm == 1)
                keep16 = (uint16_t)((keep16 & 0x7FFF) | ((keep >> 24) ? 0x8000 : 0));
        }
        for (int y = ys; y <= ye; y++) {
            if (keep != 0xFFFFFFFFu) {
                if (t->psm == 3) {
                    uint32_t *p = (uint32_t *)t->fb + (size_t)y * t->fbw;
                    if (keep == 0)
                        for (int x = xs; x <= xe; x++) p[x] = color;
                    else
                        for (int x = xs; x <= xe; x++) p[x] = (p[x] & keep) | (color & ~keep);
                } else {
                    uint16_t *p = (uint16_t *)t->fb + (size_t)y * t->fbw;
                    for (int x = xs; x <= xe; x++) p[x] = (uint16_t)((p[x] & keep16) | (n16 & ~keep16));
                }
            }
            if (t->clear_z) {
                uint16_t *z = t->zb + (size_t)y * t->zbw;
                for (int x = xs; x <= xe; x++) z[x] = (uint16_t)zi;
            }
        }
        g_ge.pixels += (uint32_t)((xe - xs + 1) * (ye - ys + 1));
        return;
    }
    for (int y = ys; y <= ye; y++) {
        float v = va + (y + 0.5f - ya) * dv;
        for (int x = xs; x <= xe; x++)
            plot(t, x, y, b->z, color, ua + (x + 0.5f - xa) * du, v, mag);
    }
}

static float edge(const Vtx *a, const Vtx *b, float x, float y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

/* One row of a triangle: the edge functions' parts that only depend on the row. */
typedef struct TriRow {
    float py, a12, a20; /* (v2 - v1).x * (py - v1.y), (v0 - v2).x * (py - v2.y) */
} TriRow;

typedef struct Tri {
    const Vtx *v0, *v1, *v2;
    float area;
} Tri;

/* edge(v1, v2, px, py) and edge(v2, v0, px, py), with the row's parts precomputed:
 * the same operations in the same order. */
static inline float e12(const Tri *r, const TriRow *w, float px)
{
    return w->a12 - (r->v2->y - r->v1->y) * (px - r->v1->x);
}

static inline float e20(const Tri *r, const TriRow *w, float px)
{
    return w->a20 - (r->v0->y - r->v2->y) * (px - r->v2->x);
}

/* The coverage test of draw_triangle for pixel x of the row. */
static inline bool covered(const Tri *r, const TriRow *w, int x, float *pw0, float *pw1, float *pw2)
{
    float px = x + 0.5f;
    float w0 = e12(r, w, px) / r->area, w1 = e20(r, w, px) / r->area;
    float w2 = 1 - w0 - w1;
    if (w0 < 0 || w1 < 0 || w2 < 0)
        return false;
    /* Shared edges: only draw pixels exactly on an edge for one side. */
    if ((w0 == 0 && e12(r, w, px + 1) / r->area < 0) || (w1 == 0 && e20(r, w, px + 1) / r->area < 0) ||
        (w2 == 0 && edge(r->v0, r->v1, px + 1, w->py) / r->area < 0))
        return false;
    *pw0 = w0;
    *pw1 = w1;
    *pw2 = w2;
    return true;
}

/* Where along the row the edge from a to b is on the inside (s: the sign of the
 * area), narrowing [lo, hi]; false when no pixel centre can be. Approximate: the
 * caller widens it and tests every pixel exactly. */
static bool edge_span(const Vtx *a, const Vtx *b, float py, float s, bool exact, float *lo, float *hi)
{
    float k = (b->x - a->x) * (py - a->y) * s, d = (b->y - a->y) * s;
    /* inside: k - d * (px - a.x) >= 0. With d 0 the edge function is exactly k for
     * w0 and w1; w2 is 1 - w0 - w1, which can round the other way, so it never rules
     * a row out. */
    if (d == 0)
        return k >= 0 || !exact;
    float x = a->x + k / d;
    if (!isfinite(x))
        return true;
    if (d > 0) {
        if (x < *hi) *hi = x;
    } else if (x > *lo) {
        *lo = x;
    }
    return true;
}

static void draw_triangle(const Target *t, const Vtx *v0, const Vtx *v1, const Vtx *v2, bool gouraud)
{
    float area = edge(v0, v1, v2->x, v2->y);
    if (area == 0)
        return;
    if ((g_ge.reg[CMD_BCE] & 1) && !t->through && !t->clear) {
        /* Front faces are counter-clockwise on screen when FFACE is 0. */
        bool ccw = area < 0;
        if (ccw == ((g_ge.reg[CMD_FFACE] & 1) != 0))
            return;
    }
    float minx = fminf(v0->x, fminf(v1->x, v2->x)), maxx = fmaxf(v0->x, fmaxf(v1->x, v2->x));
    float miny = fminf(v0->y, fminf(v1->y, v2->y)), maxy = fmaxf(v0->y, fmaxf(v1->y, v2->y));
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < t->x0) x0 = t->x0;
    if (y0 < t->y0) y0 = t->y0;
    if (x1 > t->x1) x1 = t->x1;
    if (y1 > t->y1) y1 = t->y1;
    /* plot() drops pixels outside the buffer, so they needn't be visited. */
    if (x0 < t->bx0) x0 = t->bx0;
    if (y0 < t->by0) y0 = t->by0;
    if (x1 > t->bx1) x1 = t->bx1;
    if (y1 > t->by1) y1 = t->by1;
    float uv[3][2] = {{0}};
    if (t->tex) {
        texcoords(t, v0, &uv[0][0], &uv[0][1]);
        texcoords(t, v1, &uv[1][0], &uv[1][1]);
        texcoords(t, v2, &uv[2][0], &uv[2][1]);
    }
    /* Perspective-correct texture coordinates outside through mode. */
    float iw[3] = {1, 1, 1};
    if (!t->through) {
        iw[0] = 1 / v0->w;
        iw[1] = 1 / v1->w;
        iw[2] = 1 / v2->w;
    }
    bool need_z = t->clear ? t->clear_z : t->ztest;
    uint32_t flat = pack(v2->c);
    Tri r = {v0, v1, v2, area};
    float s = area > 0 ? 1.0f : -1.0f;
    /* Far outside the screen the edge functions lose precision: test the whole row. */
    float big = fmaxf(fmaxf(fabsf(minx), fabsf(maxx)), fmaxf(fabsf(miny), fabsf(maxy)));
    bool estimate = big < 16384.0f;
    for (int y = y0; y <= y1; y++) {
        TriRow w;
        w.py = y + 0.5f;
        w.a12 = (v2->x - v1->x) * (w.py - v1->y);
        w.a20 = (v0->x - v2->x) * (w.py - v2->y);
        /* An estimate of the covered span, widened, then grown while the exact test
         * still passes at either end. */
        float lo = -1e30f, hi = 1e30f;
        if (estimate && (!edge_span(v1, v2, w.py, s, true, &lo, &hi) || !edge_span(v2, v0, w.py, s, true, &lo, &hi) ||
                         !edge_span(v0, v1, w.py, s, false, &lo, &hi)))
            lo = 1e30f, hi = -1e30f;
        int xs = x0, xe = x1;
        if (lo > -1e29f && lo - 2.5f > (float)x0) xs = (int)(lo - 2.5f);
        if (hi < 1e29f && hi + 2.5f < (float)x1) xe = (int)(hi + 2.5f);
        float d0, d1, d2;
        if (lo > hi + 4.0f) {
            xs = x0;
            xe = x0 - 1; /* clearly empty */
        } else {
            while (xs > x0 && covered(&r, &w, xs - 1, &d0, &d1, &d2))
                xs--;
            while (xe < x1 && covered(&r, &w, xe + 1, &d0, &d1, &d2))
                xe++;
        }
        for (int x = xs; x <= xe; x++) {
            float w0, w1, w2;
            if (!covered(&r, &w, x, &w0, &w1, &w2))
                continue;
            float z = need_z ? w0 * v0->z + w1 * v1->z + w2 * v2->z : 0;
            uint32_t color = flat;
            if (gouraud) {
                float c[4];
                for (int i = 0; i < 4; i++)
                    c[i] = w0 * v0->c[i] + w1 * v1->c[i] + w2 * v2->c[i];
                color = pack(c);
            }
            float u = 0, v = 0;
            if (t->tex) {
                float q = w0 * iw[0] + w1 * iw[1] + w2 * iw[2];
                u = (w0 * uv[0][0] * iw[0] + w1 * uv[1][0] * iw[1] + w2 * uv[2][0] * iw[2]) / q;
                v = (w0 * uv[0][1] * iw[0] + w1 * uv[1][1] * iw[1] + w2 * uv[2][1] * iw[2]) / q;
            }
            plot(t, x, y, z, color, u, v, true);
        }
    }
}

/* Narrows [*lo, *hi) to the steps i whose coordinate floorf(p + d * (i / steps)) lies
 * in [c0, c1], that is p + d * (i / steps) >= c0 and < c1 + 1. The coordinate only
 * moves one way as i grows (float division, multiplication and addition are
 * monotonic), so those steps are one run, found by binary search on the same
 * expression. */
static float line_at(float p, float d, int i, int steps)
{
    return p + d * ((float)i / steps);
}

/* The first i in [0, steps] for which (line_at(i) >= edge) == want, given that it
 * only changes from false to true as i grows. */
static int line_first(float p, float d, int steps, float edge, bool want)
{
    int l = 0, h = steps;
    while (l < h) {
        int m = l + (h - l) / 2;
        if ((line_at(p, d, m, steps) >= edge) == want)
            h = m;
        else
            l = m + 1;
    }
    return l;
}

static void line_steps(float p, float d, int steps, int c0, int c1, int *lo, int *hi)
{
    if (!isfinite(p) || !isfinite(d))
        return; /* keep every step */
    int a, b;
    if (d >= 0) {
        a = line_first(p, d, steps, (float)c0, true);      /* >= c0 from here */
        b = line_first(p, d, steps, (float)c1 + 1, true);  /* >= c1 + 1 from here */
    } else {
        a = line_first(p, d, steps, (float)c1 + 1, false); /* < c1 + 1 from here */
        b = line_first(p, d, steps, (float)c0, false);     /* < c0 from here */
    }
    if (a > *lo) *lo = a;
    if (b < *hi) *hi = b;
}

static void draw_line(const Target *t, const Vtx *a, const Vtx *b)
{
    float dx = b->x - a->x, dy = b->y - a->y;
    int steps = (int)fmaxf(fabsf(dx), fabsf(dy));
    if (steps < 1)
        steps = 1;
    float ua, va, ub, vb;
    texcoords(t, a, &ua, &va);
    texcoords(t, b, &ub, &vb);
    /* A line can end far off screen: only step where plot() would draw. */
    int lo = 0, hi = steps;
    line_steps(a->x, dx, steps, t->bx0, t->bx1, &lo, &hi);
    line_steps(a->y, dy, steps, t->by0, t->by1, &lo, &hi);
    for (int i = lo; i < hi; i++) {
        float s = (float)i / steps, c[4];
        for (int k = 0; k < 4; k++)
            c[k] = a->c[k] + (b->c[k] - a->c[k]) * s;
        plot(t, (int)floorf(a->x + dx * s), (int)floorf(a->y + dy * s), a->z + (b->z - a->z) * s,
             pack(c), ua + (ub - ua) * s, va + (vb - va) * s, true);
    }
}

/* Interpolates between two clip-space vertices for near-plane clipping. */
static void lerp_vertex(const Vtx *a, const Clip *ca, const Vtx *b, const Clip *cb, float s, Vtx *o, Clip *co)
{
    co->x = ca->x + (cb->x - ca->x) * s;
    co->y = ca->y + (cb->y - ca->y) * s;
    co->z = ca->z + (cb->z - ca->z) * s;
    co->w = ca->w + (cb->w - ca->w) * s;
    o->u = a->u + (b->u - a->u) * s;
    o->v = a->v + (b->v - a->v) * s;
    for (int i = 0; i < 4; i++)
        o->c[i] = a->c[i] + (b->c[i] - a->c[i]) * s;
}

/* A transformed triangle: clip against z > -w, then project and fan out. */
static void clip_triangle(const Target *t, Vtx *in, Clip *cin, bool gouraud)
{
    Vtx out[4];
    Clip cout[4];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float di = cin[i].z + cin[i].w, dj = cin[j].z + cin[j].w;
        if (di >= 0) {
            out[n] = in[i];
            cout[n++] = cin[i];
        }
        if ((di >= 0) != (dj >= 0) && n < 4) {
            lerp_vertex(&in[i], &cin[i], &in[j], &cin[j], di / (di - dj), &out[n], &cout[n]);
            n++;
        }
    }
    if (n < 3)
        return;
    for (int i = 0; i < n; i++) {
        if (cout[i].w <= 1e-6f)
            return;
        project(&out[i], &cout[i]);
    }
    for (int i = 1; i + 1 < n; i++) {
        /* Keep the last vertex as the flat-shading colour source. */
        Vtx a = out[0], b = out[i], c = out[i + 1];
        if (!gouraud)
            memcpy(a.c, in[2].c, sizeof a.c), memcpy(b.c, in[2].c, sizeof b.c), memcpy(c.c, in[2].c, sizeof c.c);
        draw_triangle(t, &a, &b, &c, gouraud);
    }
}

/* --perf: the rasterizer's share of the GE time. */
#define RASTER(call)                              \
    do {                                          \
        uint64_t perf_t0_ = PERF_BEGIN();         \
        call;                                     \
        psp_perf_end(PERF_RASTER, perf_t0_);      \
    } while (0)

static void do_prim(uint32_t data)
{
    int type = (data >> 16) & 7, count = data & 0xFFFF;
    VFmt f = vfmt(arg(CMD_VTYPE));
    if (f.wgt)
        unsupported("vertex skinning (weights ignored)");
    if (f.morph > 1)
        unsupported("vertex morphing (first set used)");
    if ((g_ge.reg[CMD_LTE] & 1) && !f.through)
        unsupported("lighting");
    if (g_ge.reg[CMD_FGE] & 1)
        unsupported("fog");
    if (g_ge.reg[CMD_LOE] & 1)
        unsupported("logic ops");
    if (g_ge.reg[CMD_CTE] & 1)
        unsupported("color test");
    Target t;
    if (!target(&t, f.through) || count == 0)
        goto advance;
    const uint8_t *vbase = psp_ge_ptr(g_ge.vaddr);
    const uint8_t *ibase = f.idx ? psp_ge_ptr(g_ge.iaddr) : NULL;
    bool gouraud = (g_ge.reg[CMD_SHADE] & 1) || t.clear;
    g_ge.prims++;
    count_prim_into(arg(CMD_FBP) & (PSP_EDRAM_SIZE - 1));

    Vtx win[3];
    Clip cwin[3];
    int have = 0, emitted = 0;
    Vtx first;
    Clip cfirst;
    for (int i = 0; i < count; i++) {
        int index = i;
        if (f.idx == 1) index = ibase[i];
        else if (f.idx == 2) index = ((const uint16_t *)ibase)[i];
        else if (f.idx == 3) index = (int)((const uint32_t *)ibase)[i];
        Vtx v;
        Clip c = {0, 0, 0, 1};
        decode_vertex(&f, vbase + (size_t)index * f.size * f.morph, &v, &c);
        switch (type) {
        case 0: /* points */
            if (!f.through) {
                if (c.w <= 1e-6f) break;
                project(&v, &c);
            }
            RASTER(plot(&t, (int)floorf(v.x), (int)floorf(v.y), v.z, pack(v.c), v.u, v.v, true));
            break;
        case 1: case 2: /* lines, line strip */
            if (!f.through) {
                if (c.w <= 1e-6f) { have = 0; break; }
                project(&v, &c);
            }
            if (have && (type == 2 || (i & 1)))
                RASTER(draw_line(&t, &win[0], &v));
            win[0] = v;
            have = 1;
            break;
        case 6: /* sprites */
            if (!f.through) {
                if (c.w <= 1e-6f) { have = 0; break; }
                project(&v, &c);
            }
            if (i & 1)
                RASTER(draw_sprite(&t, &win[0], &v));
            win[0] = v;
            break;
        default: { /* triangles, strip, fan */
            if (type == 5 && i == 0) {
                first = v;
                cfirst = c;
                break;
            }
            Vtx tri[3];
            Clip ctri[3];
            if (type == 3) {
                win[have] = v;
                cwin[have] = c;
                if (++have < 3) break;
                have = 0;
                memcpy(tri, win, sizeof tri);
                memcpy(ctri, cwin, sizeof ctri);
            } else if (type == 4) {
                win[0] = win[1]; cwin[0] = cwin[1];
                win[1] = win[2]; cwin[1] = cwin[2];
                win[2] = v; cwin[2] = c;
                if (++have < 3) break;
                memcpy(tri, win, sizeof tri);
                memcpy(ctri, cwin, sizeof ctri);
                if (emitted++ & 1) { /* keep the winding of odd strip triangles */
                    Vtx s = tri[0]; tri[0] = tri[1]; tri[1] = s;
                    Clip cs = ctri[0]; ctri[0] = ctri[1]; ctri[1] = cs;
                }
            } else {
                win[0] = win[1]; cwin[0] = cwin[1];
                win[1] = v; cwin[1] = c;
                if (++have < 2) break;
                tri[0] = first; ctri[0] = cfirst;
                tri[1] = win[0]; ctri[1] = cwin[0];
                tri[2] = win[1]; ctri[2] = cwin[1];
            }
            if (f.through)
                RASTER(draw_triangle(&t, &tri[0], &tri[1], &tri[2], gouraud));
            else
                RASTER(clip_triangle(&t, tri, ctri, gouraud));
            break;
        }
        }
    }
advance:
    if (f.idx)
        g_ge.iaddr += (uint32_t)count * (f.idx == 1 ? 1 : f.idx == 2 ? 2 : 4);
    else
        g_ge.vaddr += (uint32_t)count * f.size * f.morph;
}

static void do_transfer(void)
{
    int bpp = (g_ge.reg[CMD_TRXKICK] & 1) ? 4 : 2;
    uint32_t src = arg(CMD_TRXSBP) | (g_ge.reg[CMD_TRXSBW] & 0xFF0000) << 8;
    uint32_t dst = arg(CMD_TRXDBP) | (g_ge.reg[CMD_TRXDBW] & 0xFF0000) << 8;
    int sw = g_ge.reg[CMD_TRXSBW] & 0x7FF, dw = g_ge.reg[CMD_TRXDBW] & 0x7FF;
    int sx = arg(CMD_TRXSPOS) & 0x3FF, sy = (arg(CMD_TRXSPOS) >> 10) & 0x3FF;
    int dx = arg(CMD_TRXDPOS) & 0x3FF, dy = (arg(CMD_TRXDPOS) >> 10) & 0x3FF;
    int w = (arg(CMD_TRXSIZE) & 0x3FF) + 1, h = ((arg(CMD_TRXSIZE) >> 10) & 0x3FF) + 1;
    const uint8_t *s = psp_ge_ptr(src);
    uint8_t *d = psp_ge_ptr(dst);
    for (int y = 0; y < h; y++)
        memmove(d + ((size_t)(dy + y) * dw + dx) * bpp, s + ((size_t)(sy + y) * sw + sx) * bpp, (size_t)w * bpp);
}

/* ---- list execution ---- */

typedef struct GeList {
    int id;
    bool used, done;
    uint32_t start, pc, stall;
    uint32_t stack[32];
    int sp;
    int cbid;
    void *arg;
} GeList;

#define MAX_LISTS 16
static GeList g_lists[MAX_LISTS];
static int g_next_list_id = 1;
static PspGeCallbackData g_callbacks[16];
static bool g_callback_used[16];

static const char *const k_cmd_names[256] = {
    [0x00] = "NOP", [0x01] = "VADDR", [0x02] = "IADDR", [0x04] = "PRIM", [0x05] = "BEZIER",
    [0x06] = "SPLINE", [0x07] = "BBOX", [0x08] = "JUMP", [0x09] = "BJUMP", [0x0A] = "CALL",
    [0x0B] = "RET", [0x0C] = "END", [0x0E] = "SIGNAL", [0x0F] = "FINISH", [0x10] = "BASE",
    [0x12] = "VTYPE", [0x13] = "OFFSETADDR", [0x14] = "ORIGIN", [0x15] = "REGION1", [0x16] = "REGION2",
    [0x17] = "LTE", [0x18] = "LTE0", [0x19] = "LTE1", [0x1A] = "LTE2", [0x1B] = "LTE3", [0x1C] = "CPE",
    [0x1D] = "BCE", [0x1E] = "TME", [0x1F] = "FGE", [0x20] = "DTE", [0x21] = "ABE", [0x22] = "ATE",
    [0x23] = "ZTE", [0x24] = "STE", [0x25] = "AAE", [0x26] = "PCE", [0x27] = "CTE", [0x28] = "LOE",
    [0x36] = "PSUB", [0x38] = "PFACE", [0x3A] = "WMS", [0x3B] = "WORLD", [0x3C] = "VMS", [0x3D] = "VIEW",
    [0x3E] = "PMS", [0x3F] = "PROJ", [0x40] = "TMS", [0x41] = "TMATRIX", [0x42] = "SX", [0x43] = "SY",
    [0x44] = "SZ", [0x45] = "TX", [0x46] = "TY", [0x47] = "TZ", [0x48] = "SU", [0x49] = "SV",
    [0x4A] = "TU", [0x4B] = "TV", [0x4C] = "OFFSETX", [0x4D] = "OFFSETY", [0x50] = "SHADE",
    [0x51] = "RNORM", [0x53] = "CMAT", [0x54] = "EMC", [0x55] = "AMC", [0x56] = "DMC", [0x57] = "SMC",
    [0x58] = "AMA", [0x5B] = "SPOW", [0x5C] = "ALC", [0x5D] = "ALA", [0x5E] = "LMODE", [0x9B] = "FFACE",
    [0x9C] = "FBP", [0x9D] = "FBW", [0x9E] = "ZBP", [0x9F] = "ZBW", [0xA0] = "TBP0", [0xA8] = "TBW0",
    [0xB0] = "CBP", [0xB1] = "CBPH", [0xB2] = "TRXSBP", [0xB3] = "TRXSBW", [0xB4] = "TRXDBP",
    [0xB5] = "TRXDBW", [0xB8] = "TSIZE0", [0xC0] = "TMAP", [0xC1] = "TSHADE", [0xC2] = "TMODE",
    [0xC3] = "TPF", [0xC4] = "CLOAD", [0xC5] = "CLUT", [0xC6] = "TFLT", [0xC7] = "TWRAP",
    [0xC8] = "TLEVEL", [0xC9] = "TFUNC", [0xCA] = "TEC", [0xCB] = "TFLUSH", [0xCC] = "TSYNC",
    [0xCD] = "FOG1", [0xCE] = "FOG2", [0xCF] = "FC", [0xD0] = "TSLOPE", [0xD2] = "FPF", [0xD3] = "CMODE",
    [0xD4] = "SCISSOR1", [0xD5] = "SCISSOR2", [0xD6] = "MINZ", [0xD7] = "MAXZ", [0xD8] = "CTST",
    [0xD9] = "CREF", [0xDA] = "CMSK", [0xDB] = "ATST", [0xDC] = "STST", [0xDD] = "SOP", [0xDE] = "ZTST",
    [0xDF] = "ALPHA", [0xE0] = "SFIX", [0xE1] = "DFIX", [0xE2] = "DITH1", [0xE3] = "DITH2",
    [0xE4] = "DITH3", [0xE5] = "DITH4", [0xE6] = "LOP", [0xE7] = "ZMSK", [0xE8] = "PMSKC",
    [0xE9] = "PMSKA", [0xEA] = "TRXKICK", [0xEB] = "TRXSPOS", [0xEC] = "TRXDPOS", [0xEE] = "TRXSIZE",
};

static uint32_t list_target(uint32_t a)
{
    return ((g_ge.reg[CMD_BASE] & 0xFF0000) << 8 | a) + (arg(CMD_OFFSETADDR) << 8);
}

/* Runs a list until it ends or reaches its stall address. */
static void run_list(GeList *l)
{
    while (!l->done) {
        if (l->stall && l->pc == l->stall)
            return;
        uint32_t op = *(const uint32_t *)psp_ge_ptr(l->pc);
        l->pc += 4;
        int cmd = op >> 24;
        uint32_t a = op & 0xFFFFFF;
        if (g_ge_trace)
            SDL_Log("[ge] %08X %-10s %06X", (unsigned)(l->pc - 4), k_cmd_names[cmd] ? k_cmd_names[cmd] : "?",
                    (unsigned)a);
        switch (cmd) {
        case CMD_VADDR: g_ge.vaddr = list_target(a); break;
        case CMD_IADDR: g_ge.iaddr = list_target(a); break;
        case CMD_PRIM: g_ge.reg[cmd] = op; do_prim(a); break;
        case CMD_BEZIER: case CMD_SPLINE: unsupported("Bezier/spline patches"); break;
        case CMD_JUMP: l->pc = list_target(a) & ~3u; break;
        case CMD_BJUMP: break; /* bounding boxes always pass */
        case CMD_CALL:
            if (l->sp < 32) {
                l->stack[l->sp++] = l->pc;
                l->pc = list_target(a) & ~3u;
            }
            break;
        case CMD_RET:
            if (l->sp > 0)
                l->pc = l->stack[--l->sp];
            break;
        case CMD_END:
            l->done = true;
            break;
        case CMD_SIGNAL:
            if (l->cbid >= 0 && l->cbid < 16 && g_callback_used[l->cbid] && g_callbacks[l->cbid].signal_func)
                g_callbacks[l->cbid].signal_func(a & 0xFFFF, g_callbacks[l->cbid].signal_arg);
            break;
        case CMD_FINISH:
            if (l->cbid >= 0 && l->cbid < 16 && g_callback_used[l->cbid] && g_callbacks[l->cbid].finish_func)
                g_callbacks[l->cbid].finish_func(a & 0xFFFF, g_callbacks[l->cbid].finish_arg);
            break;
        case CMD_WMS: g_ge.world_i = a % 12; break;
        case CMD_WORLD: g_ge.world[g_ge.world_i] = f24(a); g_ge.world_i = (g_ge.world_i + 1) % 12; break;
        case CMD_VMS: g_ge.view_i = a % 12; break;
        case CMD_VIEW: g_ge.view[g_ge.view_i] = f24(a); g_ge.view_i = (g_ge.view_i + 1) % 12; break;
        case CMD_PMS: g_ge.proj_i = a % 16; break;
        case CMD_PROJ: g_ge.proj[g_ge.proj_i] = f24(a); g_ge.proj_i = (g_ge.proj_i + 1) % 16; break;
        case CMD_TMS: g_ge.tmat_i = a % 12; break;
        case CMD_TMATRIX: g_ge.tmat[g_ge.tmat_i] = f24(a); g_ge.tmat_i = (g_ge.tmat_i + 1) % 12; break;
        case CMD_CLOAD: {
            uint32_t addr = arg(CMD_CBP) | (g_ge.reg[CMD_CBPH] & 0xFF0000) << 8;
            uint32_t bytes = (a & 0x3F) * 32;
            memcpy(g_ge.clut, psp_ge_ptr(addr), bytes > sizeof g_ge.clut ? sizeof g_ge.clut : bytes);
            g_clut_gen++; /* the decoded table is stale */
            break;
        }
        case CMD_TRXKICK: g_ge.reg[cmd] = op; do_transfer(); break;
        default: g_ge.reg[cmd] = op; break;
        }
    }
}

static void ge_init_once(void)
{
    static SDL_InitState state;
    if (!SDL_ShouldInit(&state))
        return;
    g_ge_lock = SDL_CreateMutex();
    /* Identity matrices and a full-screen scissor until a list sets them. */
    g_ge.world[0] = g_ge.world[4] = g_ge.world[8] = 1;
    g_ge.view[0] = g_ge.view[4] = g_ge.view[8] = 1;
    g_ge.proj[0] = g_ge.proj[5] = g_ge.proj[10] = g_ge.proj[15] = 1;
    g_ge.reg[CMD_SCISSOR2] = CMD_SCISSOR2 << 24 | 479 | 271 << 10;
    SDL_SetInitialized(&state, true);
}

static int enqueue(const void *list, void *stall, int cbid, void *argp, bool head)
{
    ge_init_once();
    SDL_LockMutex(g_ge_lock);
    GeList *l = NULL;
    for (int i = 0; i < MAX_LISTS && !l; i++)
        if (!g_lists[i].used || g_lists[i].done)
            l = &g_lists[i];
    int r;
    if (!l) {
        r = (int32_t)0x80000021; /* SCE_KERNEL_ERROR_LIST_FULL-ish */
    } else {
        memset(l, 0, sizeof *l);
        l->used = true;
        l->id = g_next_list_id++ & 0x7FFFFFFF;
        l->start = l->pc = (uint32_t)(uintptr_t)list;
        l->stall = (uint32_t)(uintptr_t)stall;
        l->cbid = cbid;
        l->arg = argp;
        uint64_t t0 = PERF_BEGIN();
        run_list(l);
        psp_perf_end(PERF_GE, t0);
        r = l->id;
    }
    SDL_UnlockMutex(g_ge_lock);
    return r;
}

/* ---- imports ---- */

void *sceGeEdramGetAddr(void)
{
    PSP_TRACE_PTR(g_vram, NULL);
    return g_vram;
}

uint32_t sceGeEdramGetSize(void)
{
    PSP_TRACE(PSP_EDRAM_SIZE, NULL);
    return PSP_EDRAM_SIZE;
}

int32_t sceGeListEnQueue(const void *list, void *stall, int cbid, void *argp)
{
    int32_t r = enqueue(list, stall, cbid, argp, false);
    PSP_TRACE(r, "%p, %p, %d, %p", list, stall, cbid, argp);
    return r;
}

int32_t sceGeListEnQueueHead(const void *list, void *stall, int cbid, void *argp)
{
    int32_t r = enqueue(list, stall, cbid, argp, true);
    PSP_TRACE(r, "%p, %p, %d, %p", list, stall, cbid, argp);
    return r;
}

int32_t sceGeListUpdateStallAddr(int qid, void *stall)
{
    ge_init_once();
    SDL_LockMutex(g_ge_lock);
    int32_t r = (int32_t)0x80000100; /* invalid id */
    for (int i = 0; i < MAX_LISTS; i++)
        if (g_lists[i].used && g_lists[i].id == qid) {
            g_lists[i].stall = (uint32_t)(uintptr_t)stall;
            uint64_t t0 = PERF_BEGIN();
            run_list(&g_lists[i]);
            psp_perf_end(PERF_GE, t0);
            r = 0;
            break;
        }
    SDL_UnlockMutex(g_ge_lock);
    PSP_TRACE(r, "%d, %p", qid, stall);
    return r;
}

int32_t sceGeListSync(int qid, int sync_type)
{
    /* Lists run to their stall address when queued, so they are always idle here:
     * 0 = completed, 2 = stalled waiting for more commands. */
    ge_init_once();
    SDL_LockMutex(g_ge_lock);
    int32_t r = 0;
    for (int i = 0; i < MAX_LISTS; i++)
        if (g_lists[i].used && g_lists[i].id == qid && !g_lists[i].done)
            r = sync_type == 1 ? 2 : 0;
    SDL_UnlockMutex(g_ge_lock);
    PSP_TRACE(r, "%d, %d", qid, sync_type);
    return r;
}

int32_t sceGeDrawSync(int sync_type)
{
    PSP_TRACE(0, "%d", sync_type);
    return 0;
}

int32_t sceGeSetCallback(PspGeCallbackData *cb)
{
    ge_init_once();
    int32_t r = (int32_t)0x80000022;
    SDL_LockMutex(g_ge_lock);
    for (int i = 0; i < 16; i++)
        if (!g_callback_used[i]) {
            g_callback_used[i] = true;
            g_callbacks[i] = cb ? *cb : (PspGeCallbackData){0};
            r = i;
            break;
        }
    SDL_UnlockMutex(g_ge_lock);
    PSP_TRACE(r, "%p", (void *)cb);
    return r;
}

int32_t sceGeUnsetCallback(int cbid)
{
    ge_init_once();
    int32_t r = (int32_t)0x80000100;
    SDL_LockMutex(g_ge_lock);
    if (cbid >= 0 && cbid < 16 && g_callback_used[cbid]) {
        g_callback_used[cbid] = false;
        r = 0;
    }
    SDL_UnlockMutex(g_ge_lock);
    PSP_TRACE(r, "%d", cbid);
    return r;
}

int32_t sceGeBreak(int mode, void *param)
{
    PSP_TRACE(0, "%d, %p", mode, param);
    return 0;
}

int32_t sceGeContinue(void)
{
    PSP_TRACE(0, NULL);
    return 0;
}

uint32_t psp_ge_stats_prims(void)
{
    return g_ge.prims;
}

uint32_t psp_ge_stats_pixels(void)
{
    return g_ge.pixels;
}
