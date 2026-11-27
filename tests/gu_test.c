/* Checks the GE software renderer through the libgu replacement: clears, flat and
 * textured sprites, a Gouraud triangle, a CLUT texture, alpha blending, the depth
 * test, and a transformed (3D) triangle. Pixels are read back from video memory.
 *   chexa-gu-test [--png FILE]   also saves the result as a PNG */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "ge.h"
#include "gu.h"
#include "psp_imports.h"

static int failures;
static _Alignas(64) uint32_t g_list[0x4000];

#define CHECK(cond, ...)                         \
    do {                                         \
        int ok_ = (cond);                        \
        printf(ok_ ? "ok    " : "FAIL  ");       \
        if (!ok_) failures++;                    \
        printf(__VA_ARGS__);                     \
        printf("\n");                            \
    } while (0)

/* RGB of a pixel. A colour+depth clear leaves alpha (the stencil) alone, so alpha is
 * only meaningful where something was drawn; compare colours without it. */
static uint32_t px(int x, int y)
{
    return ((const uint32_t *)psp_ge_vram())[y * 512 + x] & 0xFFFFFF;
}

typedef struct { uint16_t u, v; uint32_t color; int16_t x, y, z; } TexVtx;
typedef struct { uint32_t color; int16_t x, y, z; } ColVtx;
typedef struct { uint32_t color; float x, y, z; } Col3D;

int main(int argc, char **argv)
{
    const char *png = argc == 3 && !strcmp(argv[1], "--png") ? argv[2] : NULL;

    CHECK(sceGuInit() == 0, "sceGuInit");
    sceGuStart(GU_DIRECT, g_list, sizeof g_list);
    sceGuDrawBuffer(GU_PSM_8888, 0, 512);
    sceGuDispBuffer(480, 272, 0x88000, 512);
    sceGuDepthBuffer(0x110000, 512);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuDepthRange(0xFFFF, 0);
    sceGuDisable(GU_SCISSOR_TEST);
    sceGuShadeModel(1);

    /* Clear to dark blue, depth 0. */
    sceGuClearColor(0xFF400000);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT);

    /* A flat red sprite (2D). */
    ColVtx *s = sceGuGetMemory(2 * sizeof *s);
    s[0] = (ColVtx){0xFF0000FF, 10, 10, 0};
    s[1] = (ColVtx){0xFF0000FF, 60, 40, 0};
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL, s);

    /* A Gouraud triangle: red, green, blue corners. */
    ColVtx *t = sceGuGetMemory(3 * sizeof *t);
    t[0] = (ColVtx){0xFF0000FF, 100, 10, 0};
    t[1] = (ColVtx){0xFF00FF00, 180, 90, 0};
    t[2] = (ColVtx){0xFFFF0000, 100, 90, 0};
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 3, NULL, t);

    /* An 8x8 checkerboard texture (8888), drawn 4x magnified. */
    static _Alignas(16) uint32_t tex[8 * 8];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            tex[y * 8 + x] = ((x ^ y) & 1) ? 0xFFFFFFFF : 0xFF000000;
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 8, 8, 8, tex);
    sceGuTexFunc(3, 1); /* replace */
    sceGuTexFilter(0, 0);
    TexVtx *q = sceGuGetMemory(2 * sizeof *q);
    q[0] = (TexVtx){0, 0, 0xFFFFFFFF, 200, 10, 0};
    q[1] = (TexVtx){8, 8, 0xFFFFFFFF, 232, 42, 0};
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL, q);

    /* A 4-bit CLUT texture: index 1 is yellow, everything else magenta. */
    static _Alignas(16) uint8_t tex4[8 * 8 / 2];
    static _Alignas(16) uint32_t clut[16];
    memset(tex4, 0x11, sizeof tex4);
    for (int i = 0; i < 16; i++)
        clut[i] = i == 1 ? 0xFF00FFFF : 0xFFFF00FF;
    sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
    sceGuClutLoad(2, clut);
    sceGuTexMode(GU_PSM_T4, 0, 0, 0);
    sceGuTexImage(0, 8, 8, 8, tex4);
    q = sceGuGetMemory(2 * sizeof *q);
    q[0] = (TexVtx){0, 0, 0xFFFFFFFF, 250, 10, 0};
    q[1] = (TexVtx){8, 8, 0xFFFFFFFF, 266, 26, 0};
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL, q);
    sceGuDisable(GU_TEXTURE_2D);

    /* 50% white blended over the red sprite. */
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(0, 2, 3, 0, 0);
    s = sceGuGetMemory(2 * sizeof *s);
    s[0] = (ColVtx){0x80FFFFFF, 30, 20, 0};
    s[1] = (ColVtx){0x80FFFFFF, 50, 30, 0};
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL, s);
    sceGuDisable(GU_BLEND);

    /* Depth test: a near green sprite, then a farther white one that must be hidden. */
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(7); /* GEQUAL: larger is nearer with this depth range */
    s = sceGuGetMemory(4 * sizeof *s);
    s[0] = (ColVtx){0xFF00FF00, 300, 10, 1000};
    s[1] = (ColVtx){0xFF00FF00, 340, 50, 1000};
    s[2] = (ColVtx){0xFFFFFFFF, 320, 30, 500};
    s[3] = (ColVtx){0xFFFFFFFF, 360, 70, 500};
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 4, NULL, s);
    sceGuDisable(GU_DEPTH_TEST);

    /* A transformed triangle through the matrix stack: orthographic projection of
     * the unit square onto the lower half of the screen. */
    static ScePspFMatrix4 stack[16];
    sceGumSetMatrixStack(stack, 4, 4, 8, 0);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumOrtho(-1, 1, -1, 1, -1, 1);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 move = {0, -0.5f, 0};
    sceGumTranslate(&move);
    sceGumUpdateMatrix();
    Col3D *w = sceGuGetMemory(3 * sizeof *w);
    /* 0xAABBGGRR: cyan */
    w[0] = (Col3D){0xFFFFFF00, -0.2f, -0.2f, 0};
    w[1] = (Col3D){0xFFFFFF00, 0.2f, -0.2f, 0};
    w[2] = (Col3D){0xFFFFFF00, 0.0f, 0.2f, 0};
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF, 3, NULL, w);

    int bytes = sceGuFinish();
    sceGuSync(0, 0);
    CHECK(bytes > 0, "list built and run: %d bytes", bytes);

    CHECK(px(5, 5) == 0x400000, "clear colour (got %08X)", px(5, 5));
    CHECK(px(479, 271) == 0x400000, "clear reaches the bottom-right corner (got %08X)", px(479, 271));
    CHECK(px(15, 15) == 0x0000FF && px(59, 39) == 0x0000FF && px(60, 40) == 0x400000,
          "sprite covers [10,60)x[10,40)");
    uint32_t mid = px(120, 70);
    CHECK((mid & 0xFF) > 20 && ((mid >> 8) & 0xFF) > 20 && ((mid >> 16) & 0xFF) > 20,
          "Gouraud triangle mixes all three colours (got %08X)", mid);
    CHECK(px(170, 20) == 0x400000, "outside the triangle untouched");
    CHECK(px(201, 11) == 0x000000 && px(205, 11) == 0xFFFFFF && px(201, 15) == 0xFFFFFF,
          "checkerboard texels are 4x4 pixels");
    CHECK(px(255, 15) == 0x00FFFF, "CLUT4 texture looks up index 1 (got %08X)", px(255, 15));
    uint32_t b = px(40, 25);
    CHECK((b & 0xFF) == 0xFF && ((b >> 8) & 0xFF) > 0x70 && ((b >> 8) & 0xFF) < 0x90,
          "50%% white over red blends (got %08X)", b);
    CHECK(px(335, 45) == 0x00FF00 && px(350, 60) == 0xFFFFFF, "depth test hides the farther sprite");
    CHECK(px(240, 204) == 0xFFFF00 && px(240, 150) == 0x400000,
          "transformed triangle drawn below the centre (got %08X)", px(240, 204));

    /* The display path: show the buffer and read it back as the window would. */
    static uint32_t shown[480 * 272];
    sceDisplaySetFrameBuf(psp_ge_vram(), 512, GU_PSM_8888, 0);
    CHECK(psp_display_snapshot(shown, NULL) && shown[15 * 480 + 15] == 0xFF0000FF,
          "display snapshot shows the frame buffer (got %08X)", shown[15 * 480 + 15]);

    if (png) {
        SDL_Surface *surf = SDL_CreateSurface(480, 272, SDL_PIXELFORMAT_ABGR8888);
        for (int y = 0; y < 272; y++)
            for (int x = 0; x < 480; x++)
                ((uint32_t *)((uint8_t *)surf->pixels + y * surf->pitch))[x] = ((uint32_t *)psp_ge_vram())[y * 512 + x] | 0xFF000000u;
        printf("%s %s\n", SDL_SavePNG(surf, png) ? "saved" : "couldn't save", png);
        SDL_DestroySurface(surf);
    }
    printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
