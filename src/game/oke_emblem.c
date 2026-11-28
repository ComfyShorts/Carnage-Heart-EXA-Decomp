/* See oke_emblem.h. Hand-written from CEmblemEditor (0x088c4000 .. 0x088d3000):
 * opened by FUN_088ccb10, the mode stack FUN_088c5624 (push) / FUN_088c5540 (pop),
 * the per-mode updates FUN_088c6594, the controls shared by the drawing modes
 * (FUN_088c82d8: L / R pick the palette entry, FUN_088ca2f8; square held erases,
 * FUN_088cb3bc), the draw FUN_088d0004.
 *
 * The emblem is the card's hardware + 0: 32 x 32 pixels of 4 bits (the even pixel in
 * the low nibble, FUN_088c9848), then a palette of 16 ABGR words (FUN_089ed0d4 /
 * FUN_089ed0e4). The editor works on a copy and writes it back when it closes
 * (FUN_088cb970); placement is the bit mask at hardware + 0x254 (FUN_089ed5b0 /
 * FUN_089ed580), toggled at once.
 *
 * Mode 1, the tool column (FUN_088cd654): eleven buttons, the table at 0x08be6ee4
 * at 0x08be6edc (y, button picture, icon), help CHardString 0x9E + icon:
 *   Undo (a swap with the copy taken before the last change, FUN_088c9630),
 *   Pencil (2), Line (3), Rectangle (4), Oval (5), Paint (6), Palette (7),
 *   Grid (toggles +0x74), Delete Image (9), Placement (10), Samples (0xB);
 * cross writes the emblem back and closes.
 * Not ported: Samples (mode 0xB, the preset emblems) and selecting a range with
 * triangle (modes 0xD-0xF: move, copy, flip, rotate); choosing them is logged. */
#include "oke_emblem.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "font.h"
#include "gamedata.h"
#include "gu.h"
#include "oke_custom.h"
#include "oke_parts.h"
#include "pad.h"
#include "picture.h"
#include "psp_shim.h"
#include "savedata.h"
#include "sound.h"
#include "ui.h"

#define EMBLEM_W 32
#define PIXELS 0x200
#define PALETTE 16
#define HW_EMBLEM 0x000
#define HW_EMBLEM_PALETTE 0x200
#define HW_EMBLEM_PLACE 0x254
#define PLACES 3

/* The hard parts list (FUN_088ff920: names at 0x08c7ec50, "parts/hard/%s.gim"). */
#define T_HARD_PARTS 0x0047AC50u
#define T_TOOLS 0x003E2EDCu /* 0x08be6edc: 11 x (y, button picture, icon) */
enum {
    HP_EM_ICDS = 0x69, HP_EM_PLT = 0x6A, HP_EM_BRK_BL = 0x6B, HP_EM_BRK_WB = 0x6E, HP_EM_BRK_ST = 0x6F,
    HP_EM_ICON = 0x70, HP_EM_BGWND = 0x71, HP_COUNT = 0x72,
};

/* The canvas (FUN_088ca9a0 / FUN_088ca9b0): pixel (x, y) at (0x33 + 5x, 0x1E + 5y). */
#define CANVAS_X 0x33
#define CANVAS_Y 0x1E
#define CELL 5

enum { TOOL_UNDO, TOOL_PENCIL, TOOL_LINE, TOOL_RECT, TOOL_OVAL, TOOL_PAINT, TOOL_PALETTE, TOOL_GRID,
       TOOL_DELETE, TOOL_PLACE, TOOL_SAMPLE, TOOL_COUNT };

typedef enum { E_MENU, E_PENCIL, E_LINE, E_RECT, E_OVAL, E_PAINT, E_PALETTE, E_DELETE, E_PLACE } EMode;

static struct {
    int card;
    EMode mode;
    bool closed;
    uint8_t pix[PIXELS], undo_pix[PIXELS];
    uint8_t pal[PALETTE * 4], undo_pal[PALETTE * 4];
    bool has_undo;            /* +0x80 */
    int tool;                 /* +0x44 */
    int color;                /* +0x68 */
    int cx, cy;               /* +0x4C / +0x54 */
    int sx, sy, stage;        /* the first point (+0x50 / +0x58) and +0x60 */
    bool armed, stroke;       /* the pencil's +0x8C / +0x90 */
    bool erase_stroke;        /* +0x94 */
    bool grid;                /* +0x74 */
    int place;                /* +0x88 */
    ColorEdit palette;
    UiAsk ask;
    const char *message;
    Picture *hp[HP_COUNT];
} g;

static const char *nz(const char *s)
{
    return s ? s : "";
}

static Picture *hard_part(int i)
{
    if (i < 0 || i >= HP_COUNT)
        return NULL;
    if (!g.hp[i]) {
        uint32_t p = gamedata_word(T_HARD_PARTS + 4u * (uint32_t)i, 0);
        if (p >= 0x08804000u)
            p -= 0x08804000u;
        const char *name = p ? gamedata_string(p) : NULL;
        if (!name)
            return NULL;
        char path[64];
        snprintf(path, sizeof path, "parts/hard/%s.gim", name);
        g.hp[i] = picture_load(path);
    }
    return g.hp[i];
}

static int tool_word(int tool, int field)
{
    return (int)gamedata_word(T_TOOLS + 12u * (uint32_t)tool + 4u * (uint32_t)field, 0);
}

static const char *yes_no(int id) /* CBattleTestString 0x1F / 0x20 */
{
    uint32_t p = gamedata_word(0x00466D50u + 4u * (uint32_t)id, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

void emblem_open(int card)
{
    Picture *keep[HP_COUNT];
    memcpy(keep, g.hp, sizeof keep);
    memset(&g, 0, sizeof g);
    memcpy(g.hp, keep, sizeof keep);
    g.card = card;
    const OkeDataBase *c = savedata_card(card);
    memcpy(g.pix, c->hardware + HW_EMBLEM, PIXELS);
    memcpy(g.pal, c->hardware + HW_EMBLEM_PALETTE, sizeof g.pal);
    g.grid = true; /* +0x74 = 1 */
    g.mode = E_MENU;
    SDL_Log("[emblem] card %d", card + 1);
}

/* ---- the picture ---- */

static int pixel(int x, int y)
{
    int i = x + y * EMBLEM_W;
    return i & 1 ? g.pix[i / 2] >> 4 : g.pix[i / 2] & 0xF;
}

static void snapshot(void) /* FUN_088c95e0 */
{
    memcpy(g.undo_pix, g.pix, PIXELS);
    memcpy(g.undo_pal, g.pal, sizeof g.pal);
    g.has_undo = true;
}

static void plot(int c, int x, int y, bool new_change) /* FUN_088c9848 */
{
    if (x < 0 || y < 0 || x >= EMBLEM_W || y >= EMBLEM_W)
        return;
    if (new_change)
        snapshot();
    int i = x + y * EMBLEM_W;
    if (i & 1)
        g.pix[i / 2] = (uint8_t)((g.pix[i / 2] & 0x0F) | c << 4);
    else
        g.pix[i / 2] = (uint8_t)((g.pix[i / 2] & 0xF0) | c);
}

static void undo(void) /* FUN_088c9630 */
{
    if (!g.has_undo)
        return;
    uint8_t t[PIXELS], tp[PALETTE * 4];
    memcpy(t, g.pix, PIXELS);
    memcpy(g.pix, g.undo_pix, PIXELS);
    memcpy(g.undo_pix, t, PIXELS);
    memcpy(tp, g.pal, sizeof tp);
    memcpy(g.pal, g.undo_pal, sizeof tp);
    memcpy(g.undo_pal, tp, sizeof tp);
}

/* The shapes from the first point to the cursor. The game builds a point list at
 * +0x510 (FUN_088c8c48); these are the usual rasterizations: Bresenham's line, the
 * rectangle's outline and the midpoint ellipse in the bounding box. */
typedef void (*PlotFn)(int x, int y, void *ctx);

static void shape_line(int x0, int y0, int x1, int y1, PlotFn fn, void *ctx)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        fn(x0, y0, ctx);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void shape_rect(int x0, int y0, int x1, int y1, PlotFn fn, void *ctx)
{
    shape_line(x0, y0, x1, y0, fn, ctx);
    shape_line(x1, y0, x1, y1, fn, ctx);
    shape_line(x1, y1, x0, y1, fn, ctx);
    shape_line(x0, y1, x0, y0, fn, ctx);
}

static void shape_oval(int x0, int y0, int x1, int y1, PlotFn fn, void *ctx)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    long a = x1 - x0, b = y1 - y0, b1 = b & 1;
    long dx = 4 * (1 - a) * b * b, dy = 4 * (b1 + 1) * a * a, err = dx + dy + b1 * a * a, e2;
    y0 += (int)((b + 1) / 2);
    y1 = y0 - (int)b1;
    a *= 8 * a;
    b1 = 8 * b * b;
    do {
        fn(x1, y0, ctx);
        fn(x0, y0, ctx);
        fn(x0, y1, ctx);
        fn(x1, y1, ctx);
        e2 = 2 * err;
        if (e2 <= dy) {
            y0++;
            y1--;
            err += dy += a;
        }
        if (e2 >= dx || 2 * err > dy) {
            x0++;
            x1--;
            err += dx += b1;
        }
    } while (x0 <= x1);
    while (y0 - y1 <= b) {
        fn(x0 - 1, y0, ctx);
        fn(x1 + 1, y0++, ctx);
        fn(x0 - 1, y1, ctx);
        fn(x1 + 1, y1--, ctx);
    }
}

static void shape(EMode m, PlotFn fn, void *ctx)
{
    if (m == E_LINE)
        shape_line(g.sx, g.sy, g.cx, g.cy, fn, ctx);
    else if (m == E_RECT)
        shape_rect(g.sx, g.sy, g.cx, g.cy, fn, ctx);
    else
        shape_oval(g.sx, g.sy, g.cx, g.cy, fn, ctx);
}

static void plot_cb(int x, int y, void *ctx)
{
    (void)ctx;
    plot(g.color, x, y, false);
}

/* FUN_088cab28: paints the area of the colour under the cursor, edge to edge. */
static void paint(int x, int y, int c)
{
    int from = pixel(x, y);
    if (from == c)
        return;
    snapshot();
    static int16_t stack[EMBLEM_W * EMBLEM_W * 4][2];
    int n = 0;
    stack[n][0] = (int16_t)x, stack[n][1] = (int16_t)y, n++;
    while (n) {
        n--;
        int px = stack[n][0], py = stack[n][1];
        if (px < 0 || py < 0 || px >= EMBLEM_W || py >= EMBLEM_W || pixel(px, py) != from)
            continue;
        plot(c, px, py, false);
        static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int k = 0; k < 4; k++) {
            stack[n][0] = (int16_t)(px + d[k][0]);
            stack[n][1] = (int16_t)(py + d[k][1]);
            n++;
        }
    }
}

/* ---- input ---- */

static void push(EMode m)
{
    g.mode = m;
    g.message = NULL;
}

static void pop(void)
{
    sound_se(SE_CANCEL, 1.0f);
    g.mode = E_MENU;
}

/* FUN_088c9a28: the cursor over the 32 x 32 pixels; held keys stop at the edges, a
 * fresh press wraps. */
static void move_cursor(void)
{
    struct { uint32_t pad; int *v, d; } k[4] = {
        {PSP_CTRL_UP, &g.cy, -1}, {PSP_CTRL_DOWN, &g.cy, 1}, {PSP_CTRL_LEFT, &g.cx, -1}, {PSP_CTRL_RIGHT, &g.cx, 1}};
    for (int i = 0; i < 4; i++) {
        if (!pad_repeat(k[i].pad))
            continue;
        int to = *k[i].v + k[i].d;
        if (to >= 0 && to < EMBLEM_W)
            *k[i].v = to;
        else if (pad_trigger(k[i].pad))
            *k[i].v = to < 0 ? EMBLEM_W - 1 : 0;
    }
}

/* FUN_088ca2f8: L / R step through the palette, wrapping on a fresh press. */
static void pick_color(void)
{
    int old = g.color;
    if (pad_repeat(PSP_CTRL_RTRIGGER)) {
        if (g.color < PALETTE - 1)
            g.color++;
        else if (pad_trigger(PSP_CTRL_RTRIGGER))
            g.color = 0;
    } else if (pad_repeat(PSP_CTRL_LTRIGGER)) {
        if (g.color > 0)
            g.color--;
        else if (pad_trigger(PSP_CTRL_LTRIGGER))
            g.color = PALETTE - 1;
    }
    if (g.color != old)
        sound_se(SE_CURSOR, 1.0f);
}

/* FUN_088cb3bc: square held erases under the cursor (colour 0), one change a press. */
static void erase(void)
{
    if (pad_held(PSP_CTRL_SQUARE)) {
        if (g.erase_stroke)
            sound_se(0xE, 1.0f);
        plot(0, g.cx, g.cy, g.erase_stroke);
        g.erase_stroke = false;
    } else {
        g.erase_stroke = true;
    }
}

static void select_area(void)
{
    SDL_Log("[emblem] selecting a range (modes 0xD-0xF) isn't ported");
    g.message = hard_string(0xAA);
    sound_se(SE_ERROR, 1.0f);
}

static void open_tool(int tool)
{
    switch (tool) {
    case TOOL_UNDO: undo(); break;
    case TOOL_PENCIL: g.armed = g.stroke = false; push(E_PENCIL); break; /* FUN_088c59d8 */
    case TOOL_LINE: g.stage = 0; push(E_LINE); break;
    case TOOL_RECT: g.stage = 0; push(E_RECT); break;
    case TOOL_OVAL: g.stage = 0; push(E_OVAL); break;
    case TOOL_PAINT: push(E_PAINT); break;
    case TOOL_PALETTE: { /* FUN_088c5d78: four rows, no putting back */
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) {
            int b = g.pal[g.color * 4 + i];
            /* FUN_088c96fc: R, G, B as (b - 0x1C) / 2, A as b * 100 / 255 rounded */
            int s = i < 3 ? ((b - 0x1C) / 2 < 0 ? 0 : (b - 0x1C) / 2) : (b * 100 + 127) / 255;
            v |= (uint32_t)(s & 0xFF) << (8 * i);
        }
        color_edit_open(&g.palette, v, 4, false);
        snapshot();
        push(E_PALETTE);
        break;
    }
    case TOOL_GRID: g.grid = !g.grid; break;
    case TOOL_DELETE: /* mode 9: "Delete image?" */
        ui_ask_open(&g.ask, hard_string(0xBA), yes_no(0x1F), yes_no(0x20), 1);
        push(E_DELETE);
        break;
    case TOOL_PLACE: push(E_PLACE); break;
    default:
        SDL_Log("[emblem] \"%s\" isn't ported", nz(hard_string(0x9E + tool)));
        g.message = hard_string(0x9E + tool);
        sound_se(SE_ERROR, 1.0f);
        break;
    }
}

static void write_back(void) /* FUN_088cb970 */
{
    OkeDataBase *c = savedata_card_edit(g.card);
    memcpy(c->hardware + HW_EMBLEM, g.pix, PIXELS);
    memcpy(c->hardware + HW_EMBLEM_PALETTE, g.pal, sizeof g.pal);
    SDL_Log("[emblem] card %d: emblem written back", g.card + 1);
}

static void update_menu(void)
{
    int old = g.tool;
    if (pad_repeat(PSP_CTRL_UP)) {
        if (g.tool > 0)
            g.tool--;
        else if (pad_trigger(PSP_CTRL_UP))
            g.tool = TOOL_COUNT - 1;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        if (g.tool < TOOL_COUNT - 1)
            g.tool++;
        else if (pad_trigger(PSP_CTRL_DOWN))
            g.tool = 0;
    }
    if (g.tool != old) {
        sound_se(SE_CURSOR, 1.0f);
        g.message = NULL;
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        open_tool(g.tool);
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        write_back();
        g.closed = true;
    }
}

/* Mode 2 (FUN_088c6718): circle arms the pencil; then cross held draws, a stroke
 * being one change; unarmed, cross goes back to the tools; triangle selects a range. */
static void update_pencil(void)
{
    move_cursor();
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        g.armed = true;
        sound_se(SE_DECIDE, 1.0f);
    }
    if (!pad_held(PSP_CTRL_CROSS))
        g.stroke = true;
    if (!pad_held(PSP_CTRL_CROSS) || !g.armed) {
        if (pad_pressed(PSP_CTRL_CROSS))
            pop();
        else if (pad_pressed(PSP_CTRL_TRIANGLE))
            select_area();
    } else {
        plot(g.color, g.cx, g.cy, g.stroke);
        g.stroke = false;
    }
}

/* Modes 3-5 (FUN_088c6a40 .. with FUN_088c9cc4): circle sets the first point, then
 * draws the shape to the cursor and goes back to the first point; cross drops the
 * first point, or with none goes back to the tools. */
static void update_shape(void)
{
    move_cursor();
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        if (g.stage == 0) {
            g.sx = g.cx, g.sy = g.cy;
            g.stage = 1;
        } else {
            snapshot();
            shape(g.mode, plot_cb, NULL);
            g.cx = g.sx, g.cy = g.sy; /* FUN_088ca050(.., 1) */
            g.stage = 0;
        }
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        if (g.stage == 1) {
            g.cx = g.sx, g.cy = g.sy;
            g.stage = 0;
            sound_se(0xE, 1.0f);
        } else {
            pop();
        }
    } else if (pad_pressed(PSP_CTRL_TRIANGLE) && g.stage == 0) {
        select_area();
    }
}

static void update_paint(void) /* mode 6, FUN_088c6fec */
{
    move_cursor();
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        paint(g.cx, g.cy, g.color);
    } else if (pad_pressed(PSP_CTRL_TRIANGLE)) {
        select_area();
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        pop();
    }
}

/* Mode 7 (FUN_088c72ac): the window's values back into the palette entry each frame
 * (FUN_088c96fc the other way: v * 2 + 0x1C, A v * 255 / 100 rounded). */
static void update_palette(void)
{
    int keep = g.color;
    pick_color();
    if (g.color != keep) { /* FUN_088ca2f8 loads the new entry into the window */
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) {
            int b = g.pal[g.color * 4 + i];
            int s = i < 3 ? ((b - 0x1C) / 2 < 0 ? 0 : (b - 0x1C) / 2) : (b * 100 + 127) / 255;
            v |= (uint32_t)(s & 0xFF) << (8 * i);
        }
        int row = g.palette.row;
        color_edit_open(&g.palette, v, 4, false);
        g.palette.row = row;
    }
    int d = color_edit_update(&g.palette);
    for (int i = 0; i < 4; i++) {
        int s = g.palette.v[i];
        g.pal[g.color * 4 + i] = (uint8_t)(i < 3 ? s * 2 + 0x1C : (s * 255 + 50) / 100);
    }
    if (d >= 0)
        g.mode = E_MENU;
}

static void update_place(void) /* mode 10, FUN_088cdbec */
{
    int old = g.place;
    if (pad_repeat(PSP_CTRL_LEFT)) {
        if (g.place > 0)
            g.place--;
        else if (pad_trigger(PSP_CTRL_LEFT))
            g.place = PLACES - 1;
    } else if (pad_repeat(PSP_CTRL_RIGHT)) {
        if (g.place < PLACES - 1)
            g.place++;
        else if (pad_trigger(PSP_CTRL_RIGHT))
            g.place = 0;
    }
    if (g.place != old)
        sound_se(SE_CURSOR, 1.0f);
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        int32_t m = hw_get(savedata_card(g.card), HW_EMBLEM_PLACE) ^ (1 << g.place);
        hw_set(savedata_card_edit(g.card), HW_EMBLEM_PLACE, m);
        SDL_Log("[emblem] card %d: place %d %s", g.card + 1, g.place + 1, m & (1 << g.place) ? "on" : "off");
        sound_se(SE_DECIDE, 1.0f);
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        pop();
    }
}

int emblem_update(float dt)
{
    switch (g.mode) {
    case E_MENU: pick_color(); update_menu(); break; /* FUN_088c82d8 case 1 */
    case E_PENCIL: pick_color(); erase(); update_pencil(); break;
    case E_LINE:
    case E_RECT:
    case E_OVAL:
        pick_color();
        if (g.stage == 0)
            erase();
        update_shape();
        break;
    case E_PAINT: pick_color(); erase(); update_paint(); break;
    case E_PALETTE: update_palette(); break;
    case E_DELETE: {
        int r = ui_ask_update(&g.ask, dt);
        if (r == 1) { /* FUN_088c96c0 */
            snapshot();
            memset(g.pix, 0, PIXELS);
            SDL_Log("[emblem] image deleted");
        }
        if (r >= 0)
            g.mode = E_MENU;
        break;
    }
    case E_PLACE: update_place(); break;
    }
    return g.closed ? 0 : 1;
}

/* ---- drawing ---- */

static uint32_t pal_color(int i)
{
    const uint8_t *p = g.pal + 4 * i;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void draw_preview_cb(int x, int y, void *ctx)
{
    (void)ctx;
    if (x >= 0 && y >= 0 && x < EMBLEM_W && y < EMBLEM_W)
        ui_fill(CANVAS_X + x * CELL, CANVAS_Y + y * CELL, CELL, CELL, pal_color(g.color) | 0xFF000000u);
}

static void draw_tools(void) /* FUN_088d02cc, the column at rest */
{
    float p = ui_pulse(SDL_GetTicks() / 1000.0f);
    for (int i = 0; i < TOOL_COUNT; i++) {
        int y = tool_word(i, 0), button = tool_word(i, 1), icon = tool_word(i, 2);
        bool sel = i == g.tool && g.mode == E_MENU;
        Picture *icds = hard_part(HP_EM_ICDS), *b = hard_part(sel ? HP_EM_BRK_WB : button);
        if (icds)
            picture_draw(icds, 7, y - 1, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
        if (b)
            picture_draw(b, 8, y, 0, BLEND_ALPHA, sel ? (uint32_t)(0x80 + p * 0x7F) << 24 | 0xFFFFFF : 0xFFFFFFFF, 0, 0);
        Picture *ic = hard_part(HP_EM_ICON);
        if (ic)
            picture_draw_rect(ic, 8, y, 0, (icon & 3) * 32, (icon >> 2) * 16, 32, 16, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
        if (i == g.tool)
            ui_frame(ui_part(PART_BF_CUR0), 6, y - 2, 36, 20, 6, sel ? p : 0.5f);
    }
}

static void draw_places(void) /* FUN_088cbc54 */
{
    int32_t m = hw_get(savedata_card(g.card), HW_EMBLEM_PLACE);
    for (int i = 0; i < PLACES; i++) {
        int x = 0x130 + i * 0x30;
        Picture *icds = hard_part(HP_EM_ICDS), *b = hard_part(m & (1 << i) ? HP_EM_BRK_ST : HP_EM_BRK_BL);
        Picture *ic = hard_part(HP_EM_ICON);
        if (icds)
            picture_draw(icds, x - 1, 0xA0, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
        if (b)
            picture_draw(b, x, 0xA1, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
        int icon = 0x10 + i;
        if (ic)
            picture_draw_rect(ic, x, 0xA1, 0, (icon & 3) * 32, (icon >> 2) * 16, 32, 16, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    }
    ui_frame(ui_part(PART_BF_CUR0), 0x12E + g.place * 0x30, 0x9F, 0x24, 0x13, 6, ui_pulse(SDL_GetTicks() / 1000.0f));
}

void emblem_draw(void)
{
    /* the windows (FUN_088ccb10): the canvas at (0x31, 0x1C) 0xA7 square and the
     * picture at its size at (0xFB, 0x1C), both on em_bgwnd */
    Picture *bg = hard_part(HP_EM_BGWND);
    ui_frame(bg, 0x31, 0x1C, 0xA7, 0xA7, 8, 1.0f);
    ui_frame(bg, 0xFB, 0x1C, 0x27, 0x27, 8, 1.0f);
    for (int y = 0; y < EMBLEM_W; y++)
        for (int x = 0; x < EMBLEM_W; x++) {
            uint32_t c = pal_color(pixel(x, y));
            ui_fill(CANVAS_X + x * CELL, CANVAS_Y + y * CELL, CELL, CELL, c);
            ui_fill(0xFD + x, 0x1E + y, 1, 1, c);
        }
    if (g.grid) /* FUN_088c8380: 33 lines each way, 5 apart */
        for (int i = 0; i <= EMBLEM_W; i++) {
            ui_fill(0x32 + i * CELL, 0x1D, 1, 0xA1, 0x60FFFFFF);
            ui_fill(0x32, 0x1D + i * CELL, 0xA1, 1, 0x60FFFFFF);
        }
    /* the palette (FUN_088c8a04): em_plt at (0x31, 0xBE), 16 swatches 9 apart */
    Picture *plt = hard_part(HP_EM_PLT);
    if (plt)
        picture_draw(plt, 0x31, 0xBE, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    for (int i = 0; i < PALETTE; i++)
        ui_fill(0x3C + i * 9, 0xC1, 7, 6, pal_color(i));
    ui_fill(0x3B + g.color * 9, 0xC0, 9, 1, 0xFFFFFFFF);
    ui_fill(0x3B + g.color * 9, 0xC7, 9, 1, 0xFFFFFFFF);
    ui_fill(0x3B + g.color * 9, 0xC0, 1, 8, 0xFFFFFFFF);
    ui_fill(0x3B + g.color * 9 + 8, 0xC0, 1, 8, 0xFFFFFFFF);
    draw_tools();
    float p = ui_pulse(SDL_GetTicks() / 1000.0f);
    if (g.mode >= E_PENCIL && g.mode <= E_PAINT) { /* FUN_088c8b24: the colour under a pulsing box */
        if ((g.mode == E_LINE || g.mode == E_RECT || g.mode == E_OVAL) && g.stage == 1)
            shape(g.mode, draw_preview_cb, NULL);
        int x = CANVAS_X + g.cx * CELL, y = CANVAS_Y + g.cy * CELL;
        ui_fill(x, y, CELL, CELL, pal_color(g.color) | 0xFF000000u);
        uint32_t w = (uint32_t)(p * 255) << 24 | 0xFFFFFF;
        ui_fill(x - 1, y - 1, 7, 1, w);
        ui_fill(x - 1, y + 5, 7, 1, w);
        ui_fill(x - 1, y - 1, 1, 7, w);
        ui_fill(x + 5, y - 1, 1, 7, w);
    }
    if (g.mode == E_PALETTE)
        color_edit_draw(&g.palette, 0xEC, 0x84); /* FUN_088c5d78 */
    if (g.mode == E_PLACE)
        draw_places();
    if (g.mode == E_DELETE)
        ui_ask_draw(&g.ask);
}

const char *emblem_help(void)
{
    if (g.message)
        return g.message;
    switch (g.mode) {
    case E_MENU: return hard_string(0x9E + g.tool);
    case E_PENCIL: return hard_string(0xAE);
    case E_LINE: return hard_string(g.stage ? 0xB0 : 0xAF);
    case E_RECT: return hard_string(g.stage ? 0xB2 : 0xB1);
    case E_OVAL: return hard_string(g.stage ? 0xB4 : 0xB3);
    case E_PAINT: return hard_string(0xB5);
    case E_PALETTE: return hard_string(0xB6);
    case E_PLACE: return hard_string(0xA7);
    default: return NULL;
    }
}
