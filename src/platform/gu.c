/* libgu replacement. See gu.h. Behaviour follows the game's copy of the library as read
 * from the disassembly (list contexts, what sceGuStart/Finish emit, the clear sprites,
 * the depth-range arithmetic); the code is written from scratch. */
#include "gu.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "ge.h"
#include "psp_imports.h"

/* ---- state ---- */

typedef struct GuContext {
    uint32_t *start, *cur;
    int size;
    int prev; /* the context that was current before this one, or -1 */
    bool active;
} GuContext;

typedef struct GuState {
    bool inited, defaults_sent, display_on;
    int cur;            /* current context, -1 for none */
    GuContext ctx[5];
    int qid, send_qid;
    int ge_cbid;
    SceUID signal_flag;
    /* frame buffers (VRAM offsets) */
    int draw_psm, draw_fbw, disp_fbw;
    uint32_t draw_fbp, disp_fbp, zbp;
    int disp_w, disp_h;
    /* scissor */
    int sc_x0, sc_y0, sc_x1, sc_y1;
    bool scissor_on;
    /* depth */
    int depth_near, depth_far, depth_offset;
    /* texture function */
    int tfx, tcc;
    bool frag2x;
    /* clear */
    uint32_t clear_color;
    unsigned clear_depth, clear_stencil;
    int sprite_srcw, sprite_srch, sprite_dstw, sprite_dsth;
} GuState;

static GuState g;
static bool g_trace;

void psp_gu_set_trace(bool on)
{
    g_trace = on;
}

static void trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void trace(const char *fmt, ...)
{
    if (!g_trace)
        return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    SDL_Log("[gu] %s", buf);
}

static const char *const k_states[GU_STATE_COUNT] = {
    "ALPHA_TEST", "DEPTH_TEST", "SCISSOR_TEST", "STENCIL_TEST", "BLEND", "CULL_FACE", "DITHER",
    "FOG", "CLIP_PLANES", "TEXTURE_2D", "LIGHTING", "LIGHT0", "LIGHT1", "LIGHT2", "LIGHT3",
    "LINE_SMOOTH", "PATCH_CULL_FACE", "COLOR_TEST", "COLOR_LOGIC_OP", "FACE_NORMAL_REVERSE",
    "PATCH_FACE", "FRAGMENT_2X",
};

static const char *state_name(int s)
{
    return s >= 0 && s < GU_STATE_COUNT ? k_states[s] : "?";
}

static GuContext *ctx(void)
{
    return g.cur >= 0 ? &g.ctx[g.cur] : NULL;
}

static void emit(int cmd, uint32_t arg)
{
    GuContext *c = ctx();
    if (!c)
        return;
    if ((uint8_t *)(c->cur + 1) > (uint8_t *)c->start + c->size) {
        SDL_Log("[gu] display list %p overflowed (%d bytes)", (void *)c->start, c->size);
        return;
    }
    *c->cur++ = (uint32_t)cmd << 24 | (arg & 0xFFFFFF);
}

static uint32_t f24(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return bits >> 8;
}

static uint32_t addr_of(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}

/* BASE with the address's high byte (see ge.h), then cmd with its low 24 bits. */
static void emit_addr(int cmd, const void *p)
{
    uint32_t a = addr_of(p);
    emit(0x10, (a >> 8) & 0xFF0000);
    emit(cmd, a & 0xFFFFFF);
}

static void update_stall(void)
{
    if (g.cur == GU_DIRECT && g.ctx[GU_DIRECT].active)
        sceGeListUpdateStallAddr(g.qid, g.ctx[GU_DIRECT].cur);
}

/* ---- GE callbacks ---- */

static void on_signal(int id, void *arg)
{
    sceKernelSetEventFlag(g.signal_flag, 1);
}

static void on_finish(int id, void *arg)
{
}

/* ---- setup ---- */

/* Register defaults run once by sceGuInit (this port's own choice of values). */
static uint32_t g_init_list[64];

int sceGuInit(void)
{
    trace("sceGuInit()");
    if (g.inited)
        return (int32_t)0x80000020;
    memset(&g, 0, sizeof g);
    g.cur = -1;
    for (int i = 0; i < 5; i++)
        g.ctx[i].prev = -1;
    g.disp_w = 480;
    g.disp_h = 272;
    g.draw_fbw = g.disp_fbw = 480;
    g.draw_psm = GU_PSM_5551;
    g.sc_x1 = 479;
    g.sc_y1 = 271;
    g.depth_near = 0xFFFF;
    sceGeEdramGetAddr();
    g.signal_flag = sceKernelCreateEventFlag("SceGuSignal", 0x200, 1, NULL);
    if (g.signal_flag < 0)
        return g.signal_flag;
    PspGeCallbackData cb = {on_signal, NULL, on_finish, NULL};
    g.ge_cbid = sceGeSetCallback(&cb);
    if (g.ge_cbid < 0) {
        sceKernelDeleteEventFlag(g.signal_flag);
        return g.ge_cbid;
    }
    int n = 0;
    static const int enables[] = {0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
                                  0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x51};
    for (size_t i = 0; i < SDL_arraysize(enables); i++)
        g_init_list[n++] = (uint32_t)enables[i] << 24;
    g_init_list[n++] = 0x12u << 24;                     /* VTYPE 0 */
    g_init_list[n++] = 0x13u << 24;                     /* OFFSETADDR 0 */
    g_init_list[n++] = 0xD3u << 24;                     /* clear mode off */
    g_init_list[n++] = 0xE7u << 24;                     /* depth writes on */
    g_init_list[n++] = 0xE8u << 24;                     /* no pixel mask */
    g_init_list[n++] = 0xE9u << 24;
    g_init_list[n++] = 0xC9u << 24 | 0x100;            /* modulate, RGBA */
    g_init_list[n++] = 0xD5u << 24 | 479 | 271 << 10;   /* scissor */
    g_init_list[n++] = 0x0Fu << 24;                     /* FINISH */
    g_init_list[n++] = 0x0Cu << 24;                     /* END */
    int id = sceGeListEnQueue(g_init_list, NULL, g.ge_cbid, NULL);
    if (id < 0) {
        sceKernelDeleteEventFlag(g.signal_flag);
        sceGeUnsetCallback(g.ge_cbid);
        return id;
    }
    g.qid = id;
    sceGeListSync(id, 0);
    sceGeDrawSync(0);
    g.inited = true;
    return 0;
}

void sceGuTerm(void)
{
    trace("sceGuTerm()");
    if (!g.inited)
        return;
    sceKernelDeleteEventFlag(g.signal_flag);
    sceGeUnsetCallback(g.ge_cbid);
    g.inited = false;
}

static const int k_default_dither[16] = {-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2};

static void draw_buffer_cmds(void)
{
    emit(0xD2, (uint32_t)g.draw_psm);
    emit(0x9D, (uint32_t)g.draw_fbw | (g.draw_fbp & 0xFF000000) >> 8);
    emit(0x9C, g.draw_fbp & 0xFFFFFF);
}

int sceGuStart(int cid, void *list, int size)
{
    trace("sceGuStart(%d, %p, 0x%X)", cid, list, size);
    if (!g.inited)
        return (int32_t)0x80000001;
    if (cid < 0 || cid > 4 || g.ctx[cid].active)
        return (int32_t)0x80000021;
    GuContext *c = &g.ctx[cid];
    c->start = c->cur = list;
    c->size = size;
    c->prev = g.cur;
    c->active = true;
    g.cur = cid;
    if (cid == GU_DIRECT) {
        int id = sceGeListEnQueue(list, list, g.ge_cbid, NULL);
        if (id < 0)
            return id;
        g.qid = id;
    }
    if (!g.defaults_sent) {
        bool tr = g_trace;
        g_trace = false;
        sceGuSetDither(k_default_dither);
        sceGuPatchDivision(16, 16);
        sceGuColorMaterial(7);
        sceGuSpecular(1.0f);
        sceGuTexScale(1.0f, 1.0f);
        g_trace = tr;
        g.defaults_sent = true;
    }
    if (cid == GU_DIRECT)
        draw_buffer_cmds();
    return 0;
}

int sceGuFinishId(unsigned int id)
{
    GuContext *c = ctx();
    if (!c)
        return (int32_t)0x80000021;
    switch (g.cur) {
    case GU_DIRECT:
        emit(0x0F, id & 0xFFFF);
        emit(0x0C, 0);
        update_stall();
        break;
    case GU_CALL:
    case GU_CALL2:
        emit(0x0B, 0);
        break;
    default:
        emit(0x0F, id & 0xFFFF);
        emit(0x0C, 0);
        break;
    }
    int bytes = (int)((uint8_t *)c->cur - (uint8_t *)c->start);
    c->active = false;
    g.cur = c->prev;
    c->prev = -1;
    return bytes;
}

int sceGuFinish(void)
{
    int r = sceGuFinishId(0);
    trace("sceGuFinish() -> %d bytes", r);
    return r;
}

int sceGuSync(int mode, int what)
{
    trace("sceGuSync(%d, %d)", mode, what);
    switch (mode) {
    case 0: return sceGeDrawSync(what);
    case 3: return sceGeListSync(g.qid, what);
    case 4: return sceGeListSync(g.send_qid, what);
    default: return 0;
    }
}

void *sceGuSwapBuffers(void)
{
    uint32_t t = g.disp_fbp;
    g.disp_fbp = g.draw_fbp;
    g.draw_fbp = t;
    if (g.display_on)
        sceDisplaySetFrameBuf(psp_ge_vram() + g.disp_fbp, g.disp_fbw, g.draw_psm, 0);
    trace("sceGuSwapBuffers() -> draw 0x%X, display 0x%X", (unsigned)g.draw_fbp, (unsigned)g.disp_fbp);
    return (void *)(uintptr_t)g.draw_fbp;
}

int sceGuDisplay(int on)
{
    trace("sceGuDisplay(%d)", on);
    if (on)
        sceDisplaySetFrameBuf(psp_ge_vram() + g.disp_fbp, g.disp_fbw, g.draw_psm, 1);
    else
        sceDisplaySetFrameBuf(NULL, 0, g.draw_psm, 1);
    int was = g.display_on;
    g.display_on = on != 0;
    return was;
}

void sceGuCallList(const void *list)
{
    trace("sceGuCallList(%p)", list);
    emit_addr(0x0A, list);
    update_stall();
}

void *sceGuGetMemory(int size)
{
    GuContext *c = ctx();
    if (!c)
        return NULL;
    size = (size + 3) & ~3;
    uint32_t *jump = c->cur;
    uint32_t *mem = jump + 2;
    uint32_t *after = mem + size / 4;
    emit_addr(0x08, after);
    c->cur = after;
    return mem;
}

void sceGuSendCommandi(int cmd, int argument)
{
    trace("sceGuSendCommandi(0x%02X, 0x%06X)", cmd, argument & 0xFFFFFF);
    emit(cmd, (uint32_t)argument);
}

/* ---- buffers and viewport ---- */

void sceGuDrawBuffer(int psm, uint32_t fbp, int fbw)
{
    trace("sceGuDrawBuffer(%d, 0x%X, %d)", psm, (unsigned)fbp, fbw);
    g.draw_psm = psm;
    g.draw_fbp = fbp;
    g.draw_fbw = fbw;
    draw_buffer_cmds();
}

void sceGuDispBuffer(int width, int height, uint32_t dispbp, int dispbw)
{
    trace("sceGuDispBuffer(%d, %d, 0x%X, %d)", width, height, (unsigned)dispbp, dispbw);
    g.disp_w = width;
    g.disp_h = height;
    g.disp_fbp = dispbp;
    g.disp_fbw = dispbw;
    sceDisplaySetMode(0, width, height);
    if (g.display_on)
        sceDisplaySetFrameBuf(psp_ge_vram() + dispbp, dispbw, g.draw_psm, 1);
}

void sceGuDepthBuffer(uint32_t zbp, int zbw)
{
    trace("sceGuDepthBuffer(0x%X, %d)", (unsigned)zbp, zbw);
    g.zbp = zbp;
    emit(0x9F, (uint32_t)zbw | (zbp & 0xFF000000) >> 8);
    emit(0x9E, zbp & 0xFFFFFF);
}

void sceGuOffset(unsigned int x, unsigned int y)
{
    trace("sceGuOffset(%u, %u)", x, y);
    emit(0x4C, x << 4);
    emit(0x4D, y << 4);
}

void sceGuViewport(int cx, int cy, int width, int height)
{
    trace("sceGuViewport(%d, %d, %d, %d)", cx, cy, width, height);
    emit(0x42, f24(width * 0.5f));
    emit(0x43, f24(height * -0.5f));
    emit(0x45, f24((float)cx));
    emit(0x46, f24((float)cy));
}

static void scissor_cmds(void)
{
    if (g.scissor_on) {
        emit(0xD4, (uint32_t)(g.sc_x0 | g.sc_y0 << 10));
        emit(0xD5, (uint32_t)(g.sc_x1 | g.sc_y1 << 10));
    } else {
        emit(0xD4, 0);
        emit(0xD5, (uint32_t)((g.disp_w - 1) | (g.disp_h - 1) << 10));
    }
    emit(0x15, 0);
    emit(0x16, (uint32_t)((g.disp_w - 1) | (g.disp_h - 1) << 10));
}

void sceGuScissor(int x, int y, int width, int height)
{
    trace("sceGuScissor(%d, %d, %d, %d)", x, y, width, height);
    g.sc_x0 = x;
    g.sc_y0 = y;
    g.sc_x1 = x + width - 1;
    g.sc_y1 = y + height - 1;
    if (g.scissor_on)
        scissor_cmds();
}

static void texfunc_cmd(void)
{
    emit(0xC9, (uint32_t)(g.tfx | g.tcc << 8 | g.frag2x << 16));
}

static void set_state(int state, bool on)
{
    static const uint8_t cmds[GU_STATE_COUNT] = {
        0x22, 0x23, 0, 0x24, 0x21, 0x1D, 0x20, 0x1F, 0x1C, 0x1E, 0x17, 0x18, 0x19,
        0x1A, 0x1B, 0x25, 0x26, 0x27, 0x28, 0x51, 0x38, 0,
    };
    if (state == GU_SCISSOR_TEST) {
        g.scissor_on = on;
        scissor_cmds();
    } else if (state == GU_FRAGMENT_2X) {
        g.frag2x = on;
        texfunc_cmd();
    } else if (state >= 0 && state < GU_STATE_COUNT) {
        emit(cmds[state], on);
    }
}

void sceGuEnable(int state)
{
    trace("sceGuEnable(GU_%s)", state_name(state));
    set_state(state, true);
}

void sceGuDisable(int state)
{
    trace("sceGuDisable(GU_%s)", state_name(state));
    set_state(state, false);
}

/* ---- render state ---- */

void sceGuSetDither(const int *m)
{
    trace("sceGuSetDither(...)");
    for (int row = 0; row < 4; row++)
        emit(0xE2 + row, (uint32_t)((m[row * 4] & 15) | (m[row * 4 + 1] & 15) << 4 |
                                    (m[row * 4 + 2] & 15) << 8 | (m[row * 4 + 3] & 15) << 12));
}

void sceGuPatchDivision(int s, int t)
{
    trace("sceGuPatchDivision(%d, %d)", s, t);
    emit(0x36, (uint32_t)(s | t << 8));
}

void sceGuColorMaterial(int components)
{
    trace("sceGuColorMaterial(%d)", components);
    emit(0x53, (uint32_t)components & 7);
}

void sceGuSpecular(float power)
{
    trace("sceGuSpecular(%g)", power);
    emit(0x5B, f24(power));
}

void sceGuShadeModel(int smooth)
{
    trace("sceGuShadeModel(%d)", smooth);
    emit(0x50, smooth ? 1 : 0);
}

void sceGuFrontFace(int order)
{
    trace("sceGuFrontFace(%d)", order);
    emit(0x9B, order ? 0 : 1);
}

static void depth_cmds(void)
{
    float centre = (g.depth_near + g.depth_far) * 0.5f;
    emit(0x44, f24(centre - g.depth_near));
    emit(0x47, f24(centre + g.depth_offset));
    int lo = g.depth_near < g.depth_far ? g.depth_near : g.depth_far;
    int hi = g.depth_near < g.depth_far ? g.depth_far : g.depth_near;
    emit(0xD6, (uint32_t)lo);
    emit(0xD7, (uint32_t)hi);
}

void sceGuDepthOffset(int offset)
{
    trace("sceGuDepthOffset(%d)", offset);
    g.depth_offset = offset;
    depth_cmds();
}

void sceGuDepthRange(int near, int far)
{
    trace("sceGuDepthRange(0x%X, 0x%X)", near, far);
    g.depth_near = near;
    g.depth_far = far;
    depth_cmds();
}

void sceGuDepthFunc(int func)
{
    trace("sceGuDepthFunc(%d)", func);
    emit(0xDE, (uint32_t)func);
}

void sceGuDepthMask(int mask)
{
    trace("sceGuDepthMask(%d)", mask);
    emit(0xE7, (uint32_t)mask);
}

void sceGuAlphaFunc(int func, int ref, int mask)
{
    trace("sceGuAlphaFunc(%d, 0x%X, 0x%X)", func, ref, mask);
    emit(0xDB, (uint32_t)(func | (ref & 255) << 8 | (mask & 255) << 16));
}

void sceGuStencilFunc(int func, int ref, int mask)
{
    trace("sceGuStencilFunc(%d, 0x%X, 0x%X)", func, ref, mask);
    emit(0xDC, (uint32_t)(func | (ref & 255) << 8 | (mask & 255) << 16));
}

void sceGuStencilOp(int fail, int zfail, int zpass)
{
    trace("sceGuStencilOp(%d, %d, %d)", fail, zfail, zpass);
    emit(0xDD, (uint32_t)(fail | zfail << 8 | zpass << 16));
}

void sceGuColorFunc(int func, uint32_t color, uint32_t mask)
{
    trace("sceGuColorFunc(%d, 0x%06X, 0x%06X)", func, (unsigned)(color & 0xFFFFFF), (unsigned)(mask & 0xFFFFFF));
    emit(0xD8, (uint32_t)func);
    emit(0xD9, color);
    emit(0xDA, mask);
}

void sceGuBlendFunc(int op, int src, int dst, uint32_t fixa, uint32_t fixb)
{
    trace("sceGuBlendFunc(%d, %d, %d, 0x%06X, 0x%06X)", op, src, dst, (unsigned)(fixa & 0xFFFFFF),
          (unsigned)(fixb & 0xFFFFFF));
    emit(0xDF, (uint32_t)(src | dst << 4 | op << 8));
    emit(0xE0, fixa);
    emit(0xE1, fixb);
}

void sceGuLogicalOp(int op)
{
    trace("sceGuLogicalOp(%d)", op);
    emit(0xE6, (uint32_t)op & 15);
}

void sceGuPixelMask(uint32_t mask)
{
    trace("sceGuPixelMask(0x%08X)", (unsigned)mask);
    emit(0xE8, mask);
    emit(0xE9, mask >> 24);
}

void sceGuFog(float near, float far, uint32_t color)
{
    trace("sceGuFog(%g, %g, 0x%06X)", near, far, (unsigned)(color & 0xFFFFFF));
    float d = far - near;
    emit(0xCF, color);
    emit(0xCD, f24(far));
    emit(0xCE, f24(d != 0 && isfinite(d) ? 1.0f / d : 0.0f));
}

void sceGuLightMode(int mode)
{
    trace("sceGuLightMode(%d)", mode);
    emit(0x5E, (uint32_t)mode & 1);
}

void sceGuAmbient(uint32_t color)
{
    trace("sceGuAmbient(0x%08X)", (unsigned)color);
    emit(0x5C, color);
    emit(0x5D, color >> 24);
}

void sceGuAmbientColor(uint32_t color)
{
    trace("sceGuAmbientColor(0x%08X)", (unsigned)color);
    emit(0x55, color);
    emit(0x58, color >> 24);
}

static void material(int mode, uint32_t color)
{
    if (mode & 1) {
        emit(0x55, color);
        emit(0x58, color >> 24);
    }
    if (mode & 2)
        emit(0x56, color);
    if (mode & 4)
        emit(0x57, color);
}

void sceGuMaterial(int mode, uint32_t color)
{
    trace("sceGuMaterial(%d, 0x%08X)", mode, (unsigned)color);
    material(mode, color);
}

void sceGuColor(uint32_t color)
{
    trace("sceGuColor(0x%08X)", (unsigned)color);
    material(7, color);
}

/* ---- textures ---- */

void sceGuTexMode(int psm, int maxmips, int a2, int swizzle)
{
    trace("sceGuTexMode(%d, %d, %d, %d)", psm, maxmips, a2, swizzle);
    emit(0xC2, (uint32_t)(maxmips << 16 | a2 << 8 | swizzle));
    emit(0xC3, (uint32_t)psm);
    emit(0xCB, 0);
}

static int log2i(int v)
{
    int r = 0;
    while ((1 << r) < v && r < 15)
        r++;
    return r;
}

void sceGuTexImage(int mipmap, int width, int height, int tbw, const void *tbp)
{
    trace("sceGuTexImage(%d, %d, %d, %d, %p)", mipmap, width, height, tbw, tbp);
    uint32_t a = addr_of(tbp);
    emit(0xA0 + mipmap, a & 0xFFFFFF);
    emit(0xA8 + mipmap, (uint32_t)tbw | (a >> 8 & 0xFF0000));
    emit(0xB8 + mipmap, (uint32_t)(log2i(width) | log2i(height) << 8));
    emit(0xCB, 0);
}

void sceGuTexFilter(int min, int mag)
{
    trace("sceGuTexFilter(%d, %d)", min, mag);
    emit(0xC6, (uint32_t)(min | mag << 8));
}

void sceGuTexFunc(int tfx, int tcc)
{
    trace("sceGuTexFunc(%d, %d)", tfx, tcc);
    g.tfx = tfx;
    g.tcc = tcc;
    texfunc_cmd();
}

void sceGuTexWrap(int u, int v)
{
    trace("sceGuTexWrap(%d, %d)", u, v);
    emit(0xC7, (uint32_t)(u | v << 8));
}

void sceGuTexScale(float u, float v)
{
    trace("sceGuTexScale(%g, %g)", u, v);
    emit(0x48, f24(u));
    emit(0x49, f24(v));
}

void sceGuTexOffset(float u, float v)
{
    trace("sceGuTexOffset(%g, %g)", u, v);
    emit(0x4A, f24(u));
    emit(0x4B, f24(v));
}

void sceGuTexEnvColor(uint32_t color)
{
    trace("sceGuTexEnvColor(0x%06X)", (unsigned)(color & 0xFFFFFF));
    emit(0xCA, color);
}

void sceGuTexFlush(void)
{
    trace("sceGuTexFlush()");
    emit(0xCB, 0);
}

void sceGuTexSync(void)
{
    trace("sceGuTexSync()");
    emit(0xCC, 0);
}

void sceGuClutMode(int cpsm, int shift, int mask, int a3)
{
    trace("sceGuClutMode(%d, %d, 0x%X, %d)", cpsm, shift, mask, a3);
    emit(0xC5, (uint32_t)(cpsm | shift << 2 | (mask & 255) << 8 | a3 << 16));
}

void sceGuClutLoad(int num_blocks, const void *cbp)
{
    trace("sceGuClutLoad(%d, %p)", num_blocks, cbp);
    uint32_t a = addr_of(cbp);
    emit(0xB0, a & 0xFFFFFF);
    emit(0xB1, (a >> 8) & 0xFF0000);
    emit(0xC4, (uint32_t)num_blocks);
}

/* ---- clearing ---- */

void sceGuClearColor(uint32_t color)
{
    trace("sceGuClearColor(0x%08X)", (unsigned)color);
    g.clear_color = color;
}

void sceGuClearDepth(unsigned int depth)
{
    trace("sceGuClearDepth(0x%X)", depth);
    g.clear_depth = depth;
}

void sceGuClearStencil(unsigned int stencil)
{
    trace("sceGuClearStencil(%u)", stencil);
    g.clear_stencil = stencil;
}

typedef struct ClearVertex {
    uint32_t color;
    int16_t x, y, z;
    int16_t pad;
} ClearVertex;

void sceGuClear(int flags)
{
    trace("sceGuClear(0x%X)", flags);
    uint32_t color = g.clear_color;
    if (g.draw_psm == GU_PSM_8888)
        color = (color & 0xFFFFFF) | g.clear_stencil << 24;
    else if (g.draw_psm == GU_PSM_5551)
        color = (color & 0xFFFFFF) | g.clear_stencil << 31;
    else if (g.draw_psm == GU_PSM_4444)
        color = (color & 0xFFFFFF) | g.clear_stencil << 28;
    int count;
    ClearVertex *v;
    if (flags & GU_FAST_CLEAR_BIT) {
        /* Vertical strips 64 pixels wide (32 for 32-bit buffers), as the game does. */
        int step = g.draw_psm == GU_PSM_8888 ? 32 : 64;
        count = 512 / step * 2;
        v = sceGuGetMemory(count * (int)sizeof *v);
        if (!v)
            return;
        for (int i = 0; i < count; i++)
            v[i] = (ClearVertex){color, (int16_t)((i / 2 + i % 2) * step), (int16_t)((i % 2) * g.disp_h),
                                 (int16_t)g.clear_depth, 0};
    } else {
        count = 2;
        v = sceGuGetMemory(count * (int)sizeof *v);
        if (!v)
            return;
        v[0] = (ClearVertex){color, (int16_t)g.sc_x0, (int16_t)g.sc_y0, (int16_t)g.clear_depth, 0};
        v[1] = (ClearVertex){color, (int16_t)(g.sc_x1 + 1), (int16_t)(g.sc_y1 + 1), (int16_t)g.clear_depth, 0};
    }
    emit(0xD3, (uint32_t)((flags & 1) << 8 | ((flags >> 1) & 1) << 9 | ((flags >> 2) & 1) << 10 | 1));
    emit(0x12, GU_TRANSFORM_2D | GU_VERTEX_16BIT | GU_COLOR_8888);
    emit_addr(0x01, v);
    emit(0x04, (uint32_t)(GU_SPRITES << 16 | count));
    emit(0xD3, 0);
}

/* ---- drawing ---- */

void sceGuDrawArray(int prim, int vtype, int count, const void *indices, const void *vertices)
{
    trace("sceGuDrawArray(%d, 0x%06X, %d, %p, %p)", prim, vtype, count, indices, vertices);
    if (vtype)
        emit(0x12, (uint32_t)vtype);
    if (indices)
        emit_addr(0x02, indices);
    if (vertices)
        emit_addr(0x01, vertices);
    emit(0x04, (uint32_t)(prim << 16 | count));
}

void sceGuSetMatrix(int type, const ScePspFMatrix4 *m)
{
    trace("sceGuSetMatrix(%d, ...)", type);
    const float *f = m->m;
    if (type == GU_PROJECTION) {
        emit(0x3E, 0);
        for (int i = 0; i < 16; i++)
            emit(0x3F, f24(f[i]));
        return;
    }
    static const int start[] = {0, 0x3C, 0x3A, 0x40};
    if (type < 1 || type > 3)
        return;
    emit(start[type], 0);
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 3; row++)
            emit(start[type] + 1, f24(f[col * 4 + row]));
}

void sceGuSpriteSize(int srcw, int srch, int dstw, int dsth)
{
    trace("sceGuSpriteSize(%d, %d, %d, %d)", srcw, srch, dstw, dsth);
    g.sprite_srcw = srcw;
    g.sprite_srch = srch;
    g.sprite_dstw = dstw;
    g.sprite_dsth = dsth;
}

typedef struct SpriteVertex {
    int16_t u, v, x, y, z;
} SpriteVertex;

void sceGuSprite(int x, int y, int z, int u, int v, int flip, int corner)
{
    trace("sceGuSprite(%d, %d, %d, %d, %d, %d, %d)", x, y, z, u, v, flip, corner);
    SpriteVertex *p = sceGuGetMemory(2 * (int)sizeof *p);
    if (!p)
        return;
    p[0] = (SpriteVertex){(int16_t)u, (int16_t)v, (int16_t)x, (int16_t)y, (int16_t)z};
    p[1] = (SpriteVertex){(int16_t)(u + g.sprite_srcw), (int16_t)(v + g.sprite_srch),
                          (int16_t)(x + g.sprite_dstw), (int16_t)(y + g.sprite_dsth), (int16_t)z};
    int16_t t;
    if (flip & 1) { /* mirror horizontally: swap the u coordinates */
        t = p[0].u; p[0].u = p[1].u; p[1].u = t;
    }
    if (flip & 2) { /* mirror vertically */
        t = p[0].v; p[0].v = p[1].v; p[1].v = t;
    }
    switch (corner) { /* which screen corner the first texel lands on */
    case 1:
        t = p[0].y; p[0].y = p[1].y; p[1].y = t;
        break;
    case 2:
        t = p[0].x; p[0].x = p[1].x; p[1].x = t;
        t = p[0].y; p[0].y = p[1].y; p[1].y = t;
        break;
    case 3:
        t = p[0].x; p[0].x = p[1].x; p[1].x = t;
        break;
    default:
        break;
    }
    emit(0x12, GU_TRANSFORM_2D | GU_VERTEX_16BIT | GU_TEXTURE_16BIT);
    emit_addr(0x01, p);
    emit(0x04, (uint32_t)(GU_SPRITES << 16 | 2));
    update_stall();
}

/* ---- libgum ---- */

static struct {
    ScePspFMatrix4 *base[4];
    int depth[4];
    ScePspFMatrix4 *cur[4];
    bool dirty[4];
    int mode;
} gm;

static const ScePspFMatrix4 k_identity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};

void sceGumSetMatrixStack(ScePspFMatrix4 *mem, int proj_depth, int view_depth, int model_depth, int tex_depth)
{
    trace("sceGumSetMatrixStack(%p, %d, %d, %d, %d)", (void *)mem, proj_depth, view_depth, model_depth, tex_depth);
    int depths[4] = {proj_depth, view_depth, model_depth, tex_depth};
    ScePspFMatrix4 *p = mem;
    for (int i = 0; i < 4; i++) {
        gm.depth[i] = depths[i];
        gm.base[i] = depths[i] ? p : NULL;
        gm.cur[i] = gm.base[i];
        if (gm.cur[i])
            *gm.cur[i] = k_identity;
        gm.dirty[i] = false;
        p += depths[i];
    }
    gm.mode = GU_PROJECTION;
}

void sceGumMatrixMode(int mode)
{
    trace("sceGumMatrixMode(%d)", mode);
    if (mode >= 0 && mode < 4 && gm.cur[mode])
        gm.mode = mode;
}

static ScePspFMatrix4 *cur_matrix(void)
{
    return gm.cur[gm.mode];
}

void sceGumLoadIdentity(void)
{
    trace("sceGumLoadIdentity()");
    ScePspFMatrix4 *m = cur_matrix();
    if (m) {
        *m = k_identity;
        gm.dirty[gm.mode] = true;
    }
}

void sceGumLoadMatrix(const ScePspFMatrix4 *src)
{
    trace("sceGumLoadMatrix(...)");
    ScePspFMatrix4 *m = cur_matrix();
    if (m) {
        *m = *src;
        gm.dirty[gm.mode] = true;
    }
}

static void mul(ScePspFMatrix4 *out, const ScePspFMatrix4 *a, const ScePspFMatrix4 *b)
{
    ScePspFMatrix4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += a->m[k * 4 + row] * b->m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    *out = r;
}

static void post_multiply(const ScePspFMatrix4 *b)
{
    ScePspFMatrix4 *m = cur_matrix();
    if (m) {
        mul(m, m, b);
        gm.dirty[gm.mode] = true;
    }
}

void sceGumMultMatrix(const ScePspFMatrix4 *m)
{
    trace("sceGumMultMatrix(...)");
    post_multiply(m);
}

void sceGumPushMatrix(void)
{
    trace("sceGumPushMatrix()");
    int i = gm.mode;
    if (gm.cur[i] && gm.cur[i] + 1 < gm.base[i] + gm.depth[i]) {
        gm.cur[i][1] = gm.cur[i][0];
        gm.cur[i]++;
    }
}

void sceGumPopMatrix(void)
{
    trace("sceGumPopMatrix()");
    int i = gm.mode;
    if (gm.cur[i] && gm.cur[i] > gm.base[i]) {
        gm.cur[i]--;
        gm.dirty[i] = true;
    }
}

void sceGumOrtho(float l, float r, float b, float t, float n, float f)
{
    trace("sceGumOrtho(%g, %g, %g, %g, %g, %g)", l, r, b, t, n, f);
    ScePspFMatrix4 m = k_identity;
    m.m[0] = 2 / (r - l);
    m.m[5] = 2 / (t - b);
    m.m[10] = -2 / (f - n);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(f + n) / (f - n);
    post_multiply(&m);
}

void sceGumPerspective(float fovy, float aspect, float n, float f)
{
    trace("sceGumPerspective(%g, %g, %g, %g)", fovy, aspect, n, f);
    float c = 1.0f / tanf(fovy * 0.5f * 3.14159265f / 180.0f);
    ScePspFMatrix4 m = {{0}};
    m.m[0] = c / aspect;
    m.m[5] = c;
    m.m[10] = (f + n) / (n - f);
    m.m[11] = -1;
    m.m[14] = 2 * f * n / (n - f);
    post_multiply(&m);
}

static ScePspFVector3 norm3(ScePspFVector3 v)
{
    float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l > 0)
        v.x /= l, v.y /= l, v.z /= l;
    return v;
}

static ScePspFVector3 cross3(ScePspFVector3 a, ScePspFVector3 b)
{
    return (ScePspFVector3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

void sceGumLookAt(const ScePspFVector3 *eye, const ScePspFVector3 *center, const ScePspFVector3 *up)
{
    trace("sceGumLookAt(...)");
    ScePspFVector3 fw = norm3((ScePspFVector3){center->x - eye->x, center->y - eye->y, center->z - eye->z});
    ScePspFVector3 side = norm3(cross3(fw, *up));
    ScePspFVector3 u = cross3(side, fw);
    ScePspFMatrix4 m = k_identity;
    m.m[0] = side.x; m.m[4] = side.y; m.m[8] = side.z;
    m.m[1] = u.x; m.m[5] = u.y; m.m[9] = u.z;
    m.m[2] = -fw.x; m.m[6] = -fw.y; m.m[10] = -fw.z;
    post_multiply(&m);
    ScePspFVector3 t = {-eye->x, -eye->y, -eye->z};
    bool tr = g_trace;
    g_trace = false;
    sceGumTranslate(&t);
    g_trace = tr;
}

void sceGumTranslate(const ScePspFVector3 *v)
{
    trace("sceGumTranslate(%g, %g, %g)", v->x, v->y, v->z);
    ScePspFMatrix4 m = k_identity;
    m.m[12] = v->x;
    m.m[13] = v->y;
    m.m[14] = v->z;
    post_multiply(&m);
}

void sceGumScale(const ScePspFVector3 *v)
{
    trace("sceGumScale(%g, %g, %g)", v->x, v->y, v->z);
    ScePspFMatrix4 m = k_identity;
    m.m[0] = v->x;
    m.m[5] = v->y;
    m.m[10] = v->z;
    post_multiply(&m);
}

static void rotate(int axis, float a)
{
    float c = cosf(a), s = sinf(a);
    ScePspFMatrix4 m = k_identity;
    int i = axis == 0 ? 1 : 0, j = axis == 2 ? 1 : 2;
    m.m[i * 4 + i] = c;
    m.m[j * 4 + j] = c;
    m.m[j * 4 + i] = -s;
    m.m[i * 4 + j] = s;
    if (axis == 1) { /* keep the right-handed sense for Y */
        m.m[j * 4 + i] = s;
        m.m[i * 4 + j] = -s;
    }
    post_multiply(&m);
}

void sceGumRotateX(float angle) { trace("sceGumRotateX(%g)", angle); rotate(0, angle); }
void sceGumRotateY(float angle) { trace("sceGumRotateY(%g)", angle); rotate(1, angle); }
void sceGumRotateZ(float angle) { trace("sceGumRotateZ(%g)", angle); rotate(2, angle); }

void sceGumUpdateMatrix(void)
{
    trace("sceGumUpdateMatrix()");
    bool tr = g_trace;
    g_trace = false;
    for (int i = 0; i < 4; i++)
        if (gm.dirty[i] && gm.cur[i]) {
            sceGuSetMatrix(i, gm.cur[i]);
            gm.dirty[i] = false;
        }
    g_trace = tr;
}
