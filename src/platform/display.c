/* sceDisplay: the frame buffer being shown, vertical blank timing, and the hand-off
 * to the SDL window (psp_display_snapshot) and the screenshot option. */
#include "ge.h"
#include "psp_imports.h"
#include "psp_shim.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#define VBLANK_NS 16683350ull /* 59.94 Hz */

static SDL_SpinLock g_lock;
static void *g_top;
static int g_bufw, g_psm;
static uint32_t g_frames;          /* sceDisplaySetFrameBuf calls with a buffer */
static uint64_t g_start_ns;
static char g_shot_path[512];
static bool g_shot_done;

static void init_once(void)
{
    if (!g_start_ns)
        g_start_ns = SDL_GetTicksNS();
}

static uint32_t vcount(void)
{
    init_once();
    return (uint32_t)((SDL_GetTicksNS() - g_start_ns) / VBLANK_NS);
}

static void convert(uint32_t *rgba, const uint8_t *src, int bufw, int psm)
{
    for (int y = 0; y < PSP_SCREEN_H; y++)
        for (int x = 0; x < PSP_SCREEN_W; x++) {
            uint32_t c;
            if (psm == 3) {
                c = ((const uint32_t *)src)[y * bufw + x];
            } else {
                uint16_t p = ((const uint16_t *)src)[y * bufw + x];
                uint32_t r, g, b;
                if (psm == 0) {
                    r = p & 31; g = (p >> 5) & 63; b = p >> 11;
                    r = r << 3 | r >> 2; g = g << 2 | g >> 4; b = b << 3 | b >> 2;
                } else if (psm == 1) {
                    r = p & 31; g = (p >> 5) & 31; b = (p >> 10) & 31;
                    r = r << 3 | r >> 2; g = g << 3 | g >> 2; b = b << 3 | b >> 2;
                } else {
                    r = (p & 15) * 17; g = ((p >> 4) & 15) * 17; b = ((p >> 8) & 15) * 17;
                }
                c = r | g << 8 | b << 16;
            }
            rgba[y * PSP_SCREEN_W + x] = c | 0xFF000000u; /* the display ignores alpha */
        }
}

bool psp_display_snapshot(uint32_t *rgba, uint32_t *frame_number)
{
    SDL_LockSpinlock(&g_lock);
    void *top = g_top;
    int bufw = g_bufw, psm = g_psm;
    uint32_t n = g_frames;
    SDL_UnlockSpinlock(&g_lock);
    if (frame_number)
        *frame_number = n;
    if (!top || bufw < PSP_SCREEN_W)
        return false;
    convert(rgba, top, bufw, psm);
    return true;
}

uint32_t psp_display_frame_count(void)
{
    SDL_LockSpinlock(&g_lock);
    uint32_t n = g_frames;
    SDL_UnlockSpinlock(&g_lock);
    return n;
}

void psp_display_request_screenshot(const char *path)
{
    snprintf(g_shot_path, sizeof g_shot_path, "%s", path);
    g_shot_done = false;
}

#define MAX_SHOTS 8
static struct { uint32_t frame; char path[512]; } g_shots[MAX_SHOTS];
static int g_shot_count;

void psp_display_request_screenshot_at(uint32_t frame, const char *path)
{
    if (g_shot_count < MAX_SHOTS) {
        g_shots[g_shot_count].frame = frame;
        snprintf(g_shots[g_shot_count].path, sizeof g_shots[0].path, "%s", path);
        g_shot_count++;
    }
}

static void save_png(void *top, int bufw, int psm, const char *path, const char *what)
{
    SDL_Surface *s = SDL_CreateSurface(PSP_SCREEN_W, PSP_SCREEN_H, SDL_PIXELFORMAT_ABGR8888);
    if (!s)
        return;
    convert(s->pixels, top, bufw, psm);
    if (SDL_SavePNG(s, path))
        SDL_Log("[display] %s (#%u) saved to %s", what, (unsigned)g_frames, path);
    else
        SDL_Log("[display] can't save %s: %s", path, SDL_GetError());
    SDL_DestroySurface(s);
}

static void save_screenshot(void *top, int bufw, int psm)
{
    save_png(top, bufw, psm, g_shot_path, "first drawn frame");
}

/* ---- imports ---- */

int32_t sceDisplaySetMode(int mode, int width, int height)
{
    PSP_TRACE(0, "%d, %d, %d", mode, width, height);
    return 0;
}

int32_t sceDisplaySetFrameBuf(void *top, int bufw, int psm, int sync)
{
    SDL_LockSpinlock(&g_lock);
    g_top = top;
    g_bufw = bufw;
    g_psm = psm & 3;
    if (top)
        g_frames++;
    SDL_UnlockSpinlock(&g_lock);
    PSP_TRACE(0, "%p, %d, %d, %d", top, bufw, psm, sync);
    /* The first frame shown that the GE has drawn into. */
    if (top && g_shot_path[0] && !g_shot_done) {
        uint32_t off = (uint32_t)((uint8_t *)top - psp_ge_vram());
        if (off < PSP_EDRAM_SIZE && psp_ge_prims_into(off) > 0) {
            g_shot_done = true;
            save_screenshot(top, bufw, psm & 3);
        }
    }
    for (int i = 0; top && i < g_shot_count; i++)
        if (g_shots[i].frame == g_frames)
            save_png(top, bufw, psm & 3, g_shots[i].path, "frame");
    return 0;
}

int32_t sceDisplayGetFrameBuf(void **top, int *bufw, int *psm, int sync)
{
    SDL_LockSpinlock(&g_lock);
    if (top) *top = g_top;
    if (bufw) *bufw = g_bufw;
    if (psm) *psm = g_psm;
    SDL_UnlockSpinlock(&g_lock);
    PSP_TRACE(0, "%d", sync);
    return 0;
}

int32_t sceDisplayGetVcount(void)
{
    int32_t r = (int32_t)vcount();
    PSP_TRACE(r, NULL);
    return r;
}

int32_t sceDisplayWaitVblankStartCB(void)
{
    init_once();
    uint64_t now = SDL_GetTicksNS() - g_start_ns;
    uint64_t next = (now / VBLANK_NS + 1) * VBLANK_NS;
    psp_kernel_run_callbacks();
    SDL_DelayPrecise(next - now);
    psp_kernel_run_callbacks();
    PSP_TRACE(0, NULL);
    return 0;
}

float sceDisplayGetFramePerSec(void)
{
    PSP_TRACE(59, NULL);
    return 59.94f;
}
