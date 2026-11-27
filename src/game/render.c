/* CRender / CGraphics, written from the disassembly. Ghidra addresses in comments.
 * The game calls its statically linked libgu; the port calls the replacement in
 * src/platform/gu.c with the same arguments. */
#include "render.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "gu.h"
#include "heap.h"
#include "psp_imports.h"
#include "psp_shim.h"

#define PACKET_SIZE 0x80000

static Render *g_render; /* DAT_08c8d338 */

/* DAT_08c49290: the dither matrix ResetState sends (the standard PSPSDK one). */
static const int k_dither[16] = {-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2};

/* CClear::Init (FUN_08b08004) and its setters. */
static void clear_init(RenderClear *c)
{
    c->color = 0;
    c->flags = GU_STENCIL_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT;
    c->depth = 0;
    c->stencil = 0;
}

/* FUN_08b08020. */
static void clear_apply(const RenderClear *c)
{
    sceGuClearColor(c->color);
    sceGuClearDepth(c->depth);
    sceGuClearStencil(c->stencil);
    sceGuClear(c->flags);
}

void render_set_clear_color(Render *r, uint32_t color)
{
    r->clear.color = color;
    r->clear.flags = color ? 0x17 : 0x16;
}

/* Constructor (FUN_08b209dc). The render list and its node pool (+0x00..+0xCF) have
 * no users in the port yet. */
static void render_ctor(Render *r)
{
    SDL_zerop(r);
    r->interval = 1;
    r->vblanks = 1;
    r->step = 1;
    r->max_step = 2;
    clear_init(&r->clear);
    r->kick_list = heap_alloc(&g_default_heap, 0x40, 0x40, "CRender kick list", 0);
    for (int i = 0; i < 2; i++)
        r->packet[i] = heap_alloc(&g_default_heap, PACKET_SIZE, 0x40, "CRender packet", 0);
    r->matrix_stack = heap_alloc(&g_default_heap, 16 * sizeof(ScePspFMatrix4), 16, "CRender matrices", 0);
    render_set_clear_color(r, 0xFF000000);
    r->clear.flags = 0x17;
    r->clear.depth = 0;
    r->clear.stencil = 0;
}

Render *render_instance(void)
{
    if (!g_render) {
        g_render = game_new(sizeof *g_render);
        if (g_render)
            render_ctor(g_render);
    }
    return g_render;
}

void render_gu_init(Render *r)
{
    sceGuInit();
}

void render_setup_display(Render *r, int on)
{
    sceGuStart(GU_DIRECT, r->packet[0], PACKET_SIZE);
    sceGuDrawBuffer(GU_PSM_5551, 0, 512);
    sceGuDispBuffer(480, 272, 0x44000, 512);
    sceGuDepthBuffer(0x88000, 512);
    sceGuOffset(2048 - 480 / 2, 2048 - 272 / 2); /* 0x710, 0x778 */
    sceGuFinish();
    sceGuSync(0, 0);
    for (int i = 0; i < 2; i++) {
        sceGuStart(GU_CALL, r->packet[i], PACKET_SIZE);
        sceGuFinish();
    }
    sceGumSetMatrixStack(r->matrix_stack, 4, 4, 8, 0);
    for (int mode = GU_MODEL; mode >= GU_PROJECTION; mode--) {
        sceGumMatrixMode(mode);
        sceGumLoadIdentity();
    }
    if (on)
        sceGuDisplay(1);
}

/* FUN_08b20770: the render state every packet starts from. */
static void reset_state(void)
{
    static const int off1[] = {GU_FACE_NORMAL_REVERSE, GU_FRAGMENT_2X, GU_COLOR_TEST,
                               GU_LINE_SMOOTH, GU_COLOR_LOGIC_OP, GU_STENCIL_TEST};
    static const int on[] = {GU_PATCH_FACE, GU_DEPTH_TEST, GU_ALPHA_TEST, GU_BLEND,
                             GU_DITHER, GU_CLIP_PLANES, GU_CULL_FACE, GU_PATCH_CULL_FACE};
    static const int off2[] = {GU_SCISSOR_TEST, GU_FOG, GU_LIGHTING, GU_LIGHT0,
                               GU_LIGHT1, GU_LIGHT2, GU_LIGHT3, GU_TEXTURE_2D};
    for (size_t i = 0; i < SDL_arraysize(off1); i++)
        sceGuDisable(off1[i]);
    for (size_t i = 0; i < SDL_arraysize(on); i++)
        sceGuEnable(on[i]);
    sceGuShadeModel(1);
    sceGuFrontFace(1);
    sceGuSetDither(k_dither);
    sceGuDepthOffset(0);
    sceGuDepthRange(0xFFFF, 0);
    sceGuColorMaterial(3);
    sceGuAlphaFunc(6, 0, 0xFF);   /* GREATER than 0 */
    sceGuDepthFunc(7);            /* GEQUAL */
    sceGuStencilFunc(7, 0, 0xFF);
    sceGuColorFunc(2, 0xFFFFFF, 0xFFFFFF);
    sceGuBlendFunc(0, 2, 3, 0, 0); /* add, src alpha, 1 - src alpha */
    sceGuStencilOp(0, 0, 0);
    sceGuLogicalOp(15);
    for (size_t i = 0; i < SDL_arraysize(off2); i++)
        sceGuDisable(off2[i]);
    sceGuTexMode(GU_PSM_5551, 0, 0, 0);
    sceGuTexFilter(7, 1);
    sceGuTexFunc(0, 1);
    sceGuTexWrap(0, 0);
    /* The game passes the bit patterns 0x00800000 (the smallest normal float) and
     * 0x7F7FFFFF (FLT_MAX): fog never reaches anything. */
    sceGuFog(1.17549435e-38f, 3.40282347e+38f, 0xFFFFFFFF);
    sceGuLightMode(0);
    sceGuAmbient(0xFFFFFFFF);
    sceGuAmbientColor(0xFFFFFFFF);
    sceGuMaterial(1, 0xFFFFFFFF);
    sceGuMaterial(2, 0xFFFFFFFF);
    sceGuMaterial(4, 0xFFFFFFFF);
    sceGuSpecular(1.0f);
    sceGuSpriteSize(16, 16, 32, 32);
    sceGuColor(0xFFFFFFFF);
    sceGuPatchDivision(1, 1);
    for (int mode = GU_MODEL; mode >= GU_PROJECTION; mode--) {
        sceGumMatrixMode(mode);
        sceGumLoadIdentity();
    }
    sceGumUpdateMatrix();
}

/* FUN_08b20678: build the packet for this frame. */
#define MAX_NODES 16
static struct {
    int priority;
    RenderDrawFn draw;
    void *ctx;
} g_nodes[MAX_NODES];
static int g_node_count;

void render_add_node(Render *r, int priority, RenderDrawFn draw, void *ctx)
{
    (void)r;
    if (g_node_count >= MAX_NODES) {
        SDL_Log("[render] render list full");
        return;
    }
    int i = g_node_count++;
    while (i > 0 && g_nodes[i - 1].priority > priority) {
        g_nodes[i] = g_nodes[i - 1];
        i--;
    }
    g_nodes[i].priority = priority;
    g_nodes[i].draw = draw;
    g_nodes[i].ctx = ctx;
}

void render_remove_node(Render *r, void *ctx)
{
    (void)r;
    for (int i = 0; i < g_node_count; i++)
        if (g_nodes[i].ctx == ctx) {
            memmove(&g_nodes[i], &g_nodes[i + 1], (size_t)(g_node_count - i - 1) * sizeof g_nodes[0]);
            g_node_count--;
            return;
        }
}

static void begin_packet(Render *r, int frame)
{
    sceGuStart(GU_CALL2, r->packet[frame], PACKET_SIZE);
    reset_state();
    clear_apply(&r->clear);
    /* Every object on the render list (vtable +0x13C, +0xC); FUN_08b209d4 after it is
     * empty. */
    for (int i = 0; i < g_node_count; i++)
        g_nodes[i].draw(g_nodes[i].ctx);
    r->packet_size = sceGuFinish();
    if (r->packet_size >= PACKET_SIZE)
        SDL_Log("[render] assertion failed: this->ms_iPacketBufsizeMax > this->m_iPacketSize");
    sceKernelDcacheWritebackRange(r->packet[frame], (uint32_t)r->packet_size);
}

/* FUN_08b20624: a direct list that calls the packet. */
static void kick(Render *r, int frame)
{
    sceGuStart(GU_DIRECT, r->kick_list, 0x40);
    sceGuCallList(r->packet[frame]);
    sceGuFinish();
}

void render_end_frame(Render *r)
{
    uint64_t t0 = PERF_BEGIN();
    begin_packet(r, r->frame);
    psp_perf_end(PERF_BUILD, t0);
    /* FUN_08b1f75c / FUN_08b1f784: CPU/GPU timers around the sync (not ported). */
    sceGuSync(0, 0);
    /* FUN_08b25a98: utility dialogs (save data etc.), not ported. */
    if (!r->paused) {
        /* FUN_08b37dd8 / 37d5c / 37bcc: movie playback hooks, not ported. */
        int vc;
        t0 = PERF_BEGIN();
        do {
            sceDisplayWaitVblankStartCB();
            vc = sceDisplayGetVcount();
        } while (vc < r->last_vcount + r->interval);
        psp_perf_end(PERF_WAIT, t0);
        r->vblanks = sceDisplayGetVcount() - r->last_vcount;
        r->step = r->vblanks <= r->max_step ? r->vblanks : r->max_step;
        r->last_vcount = sceDisplayGetVcount();
        sceGuSwapBuffers();
        if (r->display_delay > 0 && --r->display_delay < 1) {
            sceGuDisplay(1);
            r->display_delay = 0;
        }
    } else {
        sceKernelDelayThreadCB(100);
        r->vblanks = sceDisplayGetVcount() - r->last_vcount;
        r->last_vcount = sceDisplayGetVcount();
    }
    if (!r->paused) {
        kick(r, r->frame);
        r->frame ^= 1;
    }
    r->frame_count++;
    psp_perf_frame(r->vblanks, r->interval);
}

int render_interval(const Render *r)
{
    return r->interval;
}
