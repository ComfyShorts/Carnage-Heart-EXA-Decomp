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
 *
 * Samples (mode 0xB, FUN_088ce050; entry FUN_088cd4dc): a strip of five 32 x 32
 * pictures at (0x10E + 0x26 i, 0xA7), chosen with left / right; circle copies the
 * picture into the editor (FUN_088c9304: palette R, G, B kept within 0x1C-0xE4).
 * The list is made when the editor opens (SetupSampleEmblem FUN_088d1984): the hard
 * parts EMBLEM02-75 (0xD-0x52), then the emblems of the OKE cards (not an installed
 * private one) and of the teams, when not blank (FUN_088cbbd4). Triangle opens mode
 * 0xC (FUN_088ce900): the user's own pictures, 32 x 32 16-colour BMP files in
 * ms0:/PICTURE/CHE/, read one directory entry a frame (FUN_088cc18c, up to 50) while
 * the samples are open, and turned into pictures (CEmblemParser FUN_088d2af8 /
 * FUN_088d2c80: rows flipped, nibbles swapped, BGR to RGB).
 *
 * A range (triangle in Pencil, a shape tool with no first point, or Paint): mode 0xD
 * (FUN_088c7664 with FUN_088c9cc4) sizes it from the cursor, circle opens mode 0xE
 * (FUN_088cf3f0), the column of Move / Copy / Delete (table 0x08be6fd8, help
 * CHardString 0xAB + item). Move and Copy lift the range (FUN_088cad88, Move clearing
 * it) and open mode 0xF (FUN_088cf954): the d-pad moves it inside the picture
 * (FUN_088ca08c), triangle flips it, R / L turn it a quarter (FUN_088cb0e0, the range
 * re-centred FUN_088cb028), circle pastes it (FUN_088caa40), cross takes the change
 * back with Undo. */
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
#include "psp_imports.h"
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
#define T_TEAM_TOOLS 0x003E2F60u /* 0x08be6f60: the team variant's 10 (FUN_088c9118 ..) */
enum {
    HP_EM_ICDS = 0x69, HP_EM_PLT = 0x6A, HP_EM_BRK_BL = 0x6B, HP_EM_BRK_WB = 0x6E, HP_EM_BRK_ST = 0x6F,
    HP_EM_ICON = 0x70, HP_EM_BGWND = 0x71, HP_SD_SARO0 = 0x75, HP_SD_SARO1 = 0x76, HP_COUNT = 0x7E,
    HP_SAMPLE_FIRST = 0x0D, HP_SAMPLE_LAST = 0x52, /* EMBLEM02 .. EMBLEM75 */
};
#define T_RANGE_TOOLS 0x003E2FD8u /* 0x08be6fd8: 3 x (y, button picture, icon) */
#define PART_LC_CUR 0xB6          /* the samples' cursor (FUN_088cd4dc) */
#define SAMPLES_MAX 0x7A
#define USER_MAX 0x32
#define USER_DIR "ms0:/PICTURE/CHE/"
#define STRIP_ROWS 5

/* The canvas (FUN_088ca9a0 / FUN_088ca9b0): pixel (x, y) at (0x33 + 5x, 0x1E + 5y). */
#define CANVAS_X 0x33
#define CANVAS_Y 0x1E
#define CELL 5

enum { TOOL_UNDO, TOOL_PENCIL, TOOL_LINE, TOOL_RECT, TOOL_OVAL, TOOL_PAINT, TOOL_PALETTE, TOOL_GRID,
       TOOL_DELETE, TOOL_PLACE, TOOL_SAMPLE, TOOL_COUNT };

typedef enum { E_MENU, E_PENCIL, E_LINE, E_RECT, E_OVAL, E_PAINT, E_PALETTE, E_DELETE, E_PLACE, E_SAMPLES, E_USER,
               E_RANGE, E_RANGE_MENU, E_RANGE_MOVE } EMode;

enum { RANGE_MOVE, RANGE_COPY, RANGE_DELETE, RANGE_ITEMS };

typedef struct Sample {
    uint8_t pix[PIXELS], pal[PALETTE * 4];
    Picture *pic;
} Sample;

/* A strip of pictures (+0x6C / +0x70 for the samples, +0xB20 / +0xB24 for the user's):
 * the cursor's place 0-4 and the scroll, 0 or less; item = row - scroll. */
typedef struct Strip {
    int row, scroll;
} Strip;

static struct {
    int card;
    int team;                 /* +0xC8, with +0xC4 set; -1 for an OKE card */
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
    EMode below;              /* the drawing mode a range was started from */
    int range_item;           /* +0x48 */
    uint8_t clip[EMBLEM_W * EMBLEM_W]; /* +0x518, one pixel a byte here */
    int bx, by, bw, bh, rot, rot0;     /* +0xB0..+0xBC, +0xAC, +0xC0 */
    Sample samples[SAMPLES_MAX];
    int nsamples;             /* +0x78 */
    Strip strip;
    Sample user[USER_MAX];
    int nuser;                /* +0x878 */
    Strip ustrip;
    SceUID dir;               /* +0x874, -1 closed */
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
        snprintf(path, sizeof path, i < 0x72 ? "parts/hard/%s.gim" : "parts/soft/%s.gim", name);
        g.hp[i] = picture_load(path);
    }
    return g.hp[i];
}

static int tool_word(uint32_t table, int tool, int field)
{
    return (int)gamedata_word(table + 12u * (uint32_t)tool + 4u * (uint32_t)field, 0);
}

/* FUN_088c9100: the column's length; FUN_088c9118 / 9158 / 9198: its table. */
static int tool_count(void)
{
    return g.team >= 0 ? TOOL_COUNT - 1 : TOOL_COUNT;
}

static uint32_t tool_table(void)
{
    return g.team >= 0 ? T_TEAM_TOOLS : T_TOOLS;
}

/* The tool on row i: the row's icon, which is the tool's number (FUN_088c91d8). */
static int tool_at(int i)
{
    return tool_word(tool_table(), i, 2);
}

static const char *yes_no(int id) /* CBattleTestString 0x1F / 0x20 */
{
    uint32_t p = gamedata_word(0x00466D50u + 4u * (uint32_t)id, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

static void setup_samples(void);
static void close_user(void);

static void open_on(int card, int team, const uint8_t *pix, const uint8_t *pal);

void emblem_open(int card)
{
    const OkeDataBase *c = savedata_card(card);
    open_on(card, -1, c->hardware + HW_EMBLEM, c->hardware + HW_EMBLEM_PALETTE);
}

void emblem_open_team(int team)
{
    const uint8_t *e = savedata_team_emblem(team);
    if (e)
        open_on(-1, team, e, e + PIXELS);
}

static void open_on(int card, int team, const uint8_t *pix, const uint8_t *pal)
{
    Picture *keep[HP_COUNT];
    memcpy(keep, g.hp, sizeof keep);
    for (int i = 0; i < g.nsamples; i++)
        picture_release(g.samples[i].pic);
    if (g.dir > 0 || g.nuser)
        close_user();
    memset(&g, 0, sizeof g);
    memcpy(g.hp, keep, sizeof keep);
    g.dir = -1;
    g.card = card;
    g.team = team;
    memcpy(g.pix, pix, PIXELS);
    memcpy(g.pal, pal, sizeof g.pal);
    g.grid = true; /* +0x74 = 1 */
    g.mode = E_MENU;
    setup_samples();
    SDL_Log("[emblem] %s %d, %d samples", team >= 0 ? "team" : "card", (team >= 0 ? team : card) + 1, g.nsamples);
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

/* Mode 0xD's entry (FUN_088c62a4): the range starts as the cursor's pixel, the
 * cursor moving its second corner. */
static void select_area(void)
{
    g.below = g.mode;
    g.sx = g.cx, g.sy = g.cy;
    g.stage = 1;
    push(E_RANGE);
}


/* ---- samples (modes 0xB / 0xC) ---- */

static void clamp_palette(uint8_t *pal) /* R, G, B within 0x1C-0xE4 */
{
    for (int i = 0; i < PALETTE; i++)
        for (int c = 0; c < 3; c++) {
            uint8_t *v = &pal[4 * i + c];
            *v = *v < 0x1C ? 0x1C : *v > 0xE4 ? 0xE4 : *v;
        }
}

/* FUN_088cbbd4: blank when every colour is darker than 0x1D in R, G and B, or when
 * every pixel is 0. */
static bool emblem_blank(const uint8_t *pix, const uint8_t *pal)
{
    bool lit = false;
    for (int i = 0; i < PALETTE && !lit; i++)
        lit = pal[4 * i] >= 0x1D || pal[4 * i + 1] >= 0x1D || pal[4 * i + 2] >= 0x1D;
    if (!lit)
        return true;
    for (int i = 0; i < PIXELS; i++)
        if (pix[i])
            return false;
    return true;
}

static void add_sample(Sample *list, int *n, int max, const uint8_t *pix, const uint8_t *pal)
{
    if (*n >= max)
        return;
    Sample *e = &list[*n];
    memcpy(e->pix, pix, PIXELS);
    memcpy(e->pal, pal, sizeof e->pal);
    e->pic = picture_from_pixels("emblem sample", 4, EMBLEM_W, EMBLEM_W, e->pix, e->pal);
    if (e->pic)
        (*n)++;
}

/* The palette words of a GIM as 8888 (the shipped samples are 8888 already). */
static bool palette_8888(const uint8_t *src, int format, uint8_t *out)
{
    for (int i = 0; i < PALETTE; i++) {
        uint32_t c;
        if (format == 3) {
            memcpy(&out[4 * i], &src[4 * i], 4);
            continue;
        }
        uint16_t v = (uint16_t)(src[2 * i] | src[2 * i + 1] << 8);
        if (format == 0)
            c = (uint32_t)(v & 0x1F) * 255 / 31 | (uint32_t)(v >> 5 & 0x3F) * 255 / 63 << 8 |
                (uint32_t)(v >> 11) * 255 / 31 << 16 | 0xFF000000u;
        else if (format == 1)
            c = (uint32_t)(v & 0x1F) * 255 / 31 | (uint32_t)(v >> 5 & 0x1F) * 255 / 31 << 8 |
                (uint32_t)(v >> 10 & 0x1F) * 255 / 31 << 16 | (v & 0x8000 ? 0xFF000000u : 0);
        else if (format == 2)
            c = (uint32_t)(v & 0xF) * 17 | (uint32_t)(v >> 4 & 0xF) * 17 << 8 | (uint32_t)(v >> 8 & 0xF) * 17 << 16 |
                (uint32_t)(v >> 12) * 17 << 24;
        else
            return false;
        memcpy(&out[4 * i], &c, 4);
    }
    return true;
}

/* SetupSampleEmblem (FUN_088d1984). */
static void setup_samples(void)
{
    for (int i = HP_SAMPLE_FIRST; i <= HP_SAMPLE_LAST; i++) {
        const uint8_t *pix, *src;
        int fmt;
        uint8_t pal[PALETTE * 4];
        if (!picture_index4_data(hard_part(i), EMBLEM_W, EMBLEM_W, &pix, &src, &fmt) ||
            !palette_8888(src, fmt, pal)) {
            SDL_Log("[emblem] sample %d isn't a 32 x 32 16-colour picture", i);
            continue;
        }
        clamp_palette(pal);
        add_sample(g.samples, &g.nsamples, SAMPLES_MAX, pix, pal);
    }
    for (int c = 0; c < SAVE_CARDS; c++) { /* the OKE cards', not an installed private one */
        const OkeDataBase *card = savedata_card(c);
        int st = savedata_card_status(c);
        int32_t privacy;
        memcpy(&privacy, card->header + 0x20, 4);
        if (st == 0 || ((st == 2 || st == 3) && privacy == 1))
            continue;
        if (!emblem_blank(card->hardware + HW_EMBLEM, card->hardware + HW_EMBLEM_PALETTE))
            add_sample(g.samples, &g.nsamples, SAMPLES_MAX, card->hardware + HW_EMBLEM,
                       card->hardware + HW_EMBLEM_PALETTE);
    }
    for (int t = 0; t < SAVE_TEAMS; t++) { /* the teams' */
        const uint8_t *e = savedata_team_emblem(t);
        if (savedata_team_status(t) != 0 && e && !emblem_blank(e, e + PIXELS))
            add_sample(g.samples, &g.nsamples, SAMPLES_MAX, e, e + PIXELS);
    }
}

/* FUN_088cbff8 (and FUN_088cc13c): the folder closed, the user's pictures let go. */
static void close_user(void)
{
    if (g.dir >= 0)
        sceIoDclose(g.dir);
    g.dir = -1;
    for (int i = 0; i < g.nuser; i++)
        picture_release(g.user[i].pic);
    g.nuser = 0;
    g.ustrip = (Strip){0, 0};
}

/* CEmblemParser: a 32 x 32, 4-bit, uncompressed BMP (FUN_088d2a50), its rows bottom
 * up (flipped, FUN_088d2af8), the left pixel in the high nibble (swapped), the colours
 * B, G, R from 0x36 (to R, G, B with alpha 0xFF, FUN_088d2c80). */
static bool parse_bmp(const uint8_t *f, int32_t size, uint8_t *pix, uint8_t *pal)
{
#define U16(o) ((uint32_t)f[o] | (uint32_t)f[(o) + 1] << 8)
#define U32(o) (U16(o) | U16((o) + 2) << 16)
    if (size < 0x36 + PALETTE * 4 || f[0] != 'B' || f[1] != 'M')
        return false;
    uint32_t bits = U32(10);
    if ((int32_t)U32(18) != EMBLEM_W || (int32_t)U32(22) != EMBLEM_W || U16(28) != 4 || U32(30) != 0 ||
        bits + PIXELS > (uint32_t)size)
        return false;
    for (int r = 0; r < EMBLEM_W; r++)
        for (int b = 0; b < EMBLEM_W / 2; b++) {
            uint8_t v = f[bits + (EMBLEM_W - 1 - r) * 16 + b];
            pix[r * 16 + b] = (uint8_t)(v >> 4 | v << 4);
        }
    for (int i = 0; i < PALETTE; i++) {
        pal[4 * i] = f[0x36 + 4 * i + 2];
        pal[4 * i + 1] = f[0x36 + 4 * i + 1];
        pal[4 * i + 2] = f[0x36 + 4 * i];
        pal[4 * i + 3] = 0xFF;
    }
    return true;
#undef U16
#undef U32
}

/* FUN_088cc18c, every frame: one entry of the folder; a ".bmp" (any case) is read. */
static void scan_user(void)
{
    if (g.dir < 0)
        return;
    SceIoDirent e;
    memset(&e, 0, sizeof e);
    int r = sceIoDread(g.dir, &e);
    if (r <= 0) {
        sceIoDclose(g.dir);
        g.dir = -1;
        SDL_Log("[emblem] %s: %d picture(s)", USER_DIR, g.nuser);
        return;
    }
    size_t len = strlen(e.d_name);
    if (len < 5 || SDL_strcasecmp(e.d_name + len - 4, ".bmp") != 0)
        return;
    char path[300];
    snprintf(path, sizeof path, "%s%s", USER_DIR, e.d_name);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return;
    int32_t size = (int32_t)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    uint8_t *buf = size > 0 && size < 0x100000 ? SDL_malloc((size_t)size) : NULL;
    uint8_t pix[PIXELS], pal[PALETTE * 4];
    if (buf && sceIoRead(fd, buf, (SceSize)size) == size && parse_bmp(buf, size, pix, pal))
        add_sample(g.user, &g.nuser, USER_MAX, pix, pal);
    else
        SDL_Log("[emblem] %s isn't a 32 x 32 16-colour BMP", path);
    SDL_free(buf);
    sceIoClose(fd);
    if (g.nuser >= USER_MAX) {
        sceIoDclose(g.dir);
        g.dir = -1;
    }
}

static void open_samples(void) /* FUN_088cd4dc: the strip, help 0xB7, the folder opened */
{
    close_user();
    g.dir = sceIoDopen(USER_DIR);
    if (g.dir < 0)
        g.dir = -1;
    push(E_SAMPLES);
}

/* The strip's left / right (FUN_088ce050 / FUN_088ce900): the cursor moves over five
 * places, then the strip scrolls; a fresh press past an end wraps. The user's strip
 * stops at its last picture when it has fewer than five. */
static void move_strip(Strip *st, int n, bool short_list)
{
    if (n < 1)
        return;
    Strip old = *st;
    if (pad_repeat(PSP_CTRL_LEFT)) {
        if (st->row > 0)
            st->row--;
        else if (st->scroll < 0)
            st->scroll++;
        else if (pad_trigger(PSP_CTRL_LEFT)) {
            st->row = STRIP_ROWS - 1;
            st->scroll = STRIP_ROWS - n;
            if (short_list && n < STRIP_ROWS)
                st->row = n - 1, st->scroll = 0;
        }
    } else if (pad_repeat(PSP_CTRL_RIGHT)) {
        if (short_list && n < STRIP_ROWS) {
            if (st->row < n - 1)
                st->row++;
            else if (pad_trigger(PSP_CTRL_RIGHT))
                st->row = 0;
        } else if (st->row < STRIP_ROWS - 1) {
            st->row++;
        } else if (st->scroll > 1 - (n - st->row)) {
            st->scroll--;
        } else if (pad_trigger(PSP_CTRL_RIGHT)) {
            *st = (Strip){0, 0};
        }
    }
    if (st->row != old.row || st->scroll != old.scroll)
        sound_se(SE_CURSOR, 1.0f);
}

static void use_sample(const Sample *e) /* FUN_088c9304 */
{
    snapshot();
    memcpy(g.pix, e->pix, PIXELS);
    memcpy(g.pal, e->pal, sizeof g.pal);
    clamp_palette(g.pal);
}

static void update_samples(void)
{
    move_strip(&g.strip, g.nsamples, false);
    int i = g.strip.row - g.strip.scroll;
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        if (i >= 0 && i < g.nsamples)
            use_sample(&g.samples[i]);
        close_user();
        g.mode = E_MENU;
        sound_se(SE_DECIDE, 1.0f);
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        close_user();
        g.mode = E_MENU;
        sound_se(SE_CANCEL, 1.0f);
    } else if (pad_pressed(PSP_CTRL_TRIANGLE)) {
        sound_se(SE_DECIDE, 1.0f);
        push(E_USER); /* FUN_088c6174 */
    }
}

static void update_user(void)
{
    move_strip(&g.ustrip, g.nuser, true);
    int i = g.ustrip.row - g.ustrip.scroll;
    if (pad_pressed(PSP_CTRL_CIRCLE) && g.nuser > 0) {
        if (i >= 0 && i < g.nuser)
            use_sample(&g.user[i]);
        close_user();
        g.mode = E_MENU;
        sound_se(SE_DECIDE, 1.0f);
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        close_user();
        g.mode = E_MENU;
        sound_se(SE_CANCEL, 1.0f);
    } else if (pad_pressed(PSP_CTRL_TRIANGLE)) {
        sound_se(SE_DECIDE, 1.0f);
        g.mode = E_SAMPLES;
    }
}

/* ---- ranges (modes 0xD-0xF) ---- */

static int rx0(void) { return g.sx < g.cx ? g.sx : g.cx; }
static int ry0(void) { return g.sy < g.cy ? g.sy : g.cy; }
static int rw(void) { return (g.sx < g.cx ? g.cx - g.sx : g.sx - g.cx) + 1; }
static int rh(void) { return (g.sy < g.cy ? g.cy - g.sy : g.sy - g.cy) + 1; }

static void range_done(void) /* back to the drawing mode the range came from */
{
    g.stage = 0;
    g.mode = g.below;
    g.message = NULL;
}

/* Mode 0xD: the cursor sizes the range; circle opens the menu, cross drops it. */
static void update_range(void)
{
    move_cursor();
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        push(E_RANGE_MENU); /* FUN_088c63a4 */
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(0xE, 1.0f);
        range_done(); /* FUN_088ca050(.., 1): the cursor stays */
    }
}

/* FUN_088cb0e0 on the lifted range (w x h): flip mirrors it left to right; turn -1
 * (R) gives (x, y) -> (y, w - 1 - x), +1 (L) (x, y) -> (h - 1 - y, x), h wide. */
static void transform_clip(int w, int h, bool flip, int turn)
{
    uint8_t t[EMBLEM_W * EMBLEM_W];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t v = g.clip[x + y * w];
            if (flip)
                t[(w - 1 - x) + y * w] = v;
            else if (turn < 0)
                t[y + (w - 1 - x) * h] = v;
            else
                t[(h - 1 - y) + x * h] = v;
        }
    memcpy(g.clip, t, (size_t)(w * h));
}

/* Mode 0xE: Move / Copy / Delete, up / down wrapping on a fresh press. */
static void update_range_menu(void)
{
    int old = g.range_item;
    if (pad_repeat(PSP_CTRL_UP)) {
        if (g.range_item > 0)
            g.range_item--;
        else if (pad_trigger(PSP_CTRL_UP))
            g.range_item = RANGE_ITEMS - 1;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        if (g.range_item < RANGE_ITEMS - 1)
            g.range_item++;
        else if (pad_trigger(PSP_CTRL_DOWN))
            g.range_item = 0;
    }
    if (g.range_item != old)
        sound_se(SE_CURSOR, 1.0f);
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        /* FUN_088cafec: the corners in order, the cursor on the far one */
        int x = rx0(), y = ry0(), w = rw(), h = rh();
        g.sx = x, g.sy = y, g.cx = x + w - 1, g.cy = y + h - 1;
        snapshot();
        for (int j = 0; j < h; j++) /* FUN_088cad88 */
            for (int i = 0; i < w; i++) {
                if (g.range_item != RANGE_DELETE)
                    g.clip[i + j * w] = (uint8_t)pixel(x + i, y + j);
                if (g.range_item != RANGE_COPY)
                    plot(0, x + i, y + j, false);
            }
        if (g.range_item == RANGE_DELETE) {
            range_done();
            return;
        }
        g.bx = x, g.by = y, g.bw = w, g.bh = h, g.rot = g.rot0 = 0; /* FUN_088c64a4 */
        g.mode = E_RANGE_MOVE;
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        range_done();
    }
}

/* FUN_088cb028: after a turn, the range at its size before the moves, or turned
 * about that range's centre (and kept inside the picture) for an odd quarter. */
static void place_turned(void)
{
    int d = g.rot - g.rot0, x = g.bx, y = g.by, w = g.bw, h = g.bh;
    if (d < 0)
        d = -d;
    if (d == 1 || d == 3) {
        x = x + w / 2 - h / 2;
        y = y + h / 2 - w / 2;
        x = x < 0 ? 0 : x;
        y = y < 0 ? 0 : y;
        if (x + h > EMBLEM_W)
            x = EMBLEM_W - h;
        if (y + w > EMBLEM_W)
            y = EMBLEM_W - w;
        int t = w;
        w = h, h = t;
    }
    g.sx = x, g.sy = y, g.cx = x + w - 1, g.cy = y + h - 1;
}

/* Mode 0xF. */
static void update_range_move(void)
{
    /* FUN_088ca08c: the whole range a pixel at a time while it stays inside */
    int dx = 0, dy = 0;
    if (pad_repeat(PSP_CTRL_UP))
        dy = g.sy > 0 ? -1 : 0;
    else if (pad_repeat(PSP_CTRL_DOWN))
        dy = g.cy < EMBLEM_W - 1 ? 1 : 0;
    else if (pad_repeat(PSP_CTRL_LEFT))
        dx = g.sx > 0 ? -1 : 0;
    else if (pad_repeat(PSP_CTRL_RIGHT))
        dx = g.cx < EMBLEM_W - 1 ? 1 : 0;
    if (dx || dy) {
        g.sx += dx, g.cx += dx, g.sy += dy, g.cy += dy;
        g.bx = g.sx, g.by = g.sy, g.bw = rw(), g.bh = rh(), g.rot0 = g.rot;
        sound_se(SE_CURSOR, 1.0f);
    }
    int w = rw(), h = rh();
    if (pad_pressed(PSP_CTRL_TRIANGLE)) {
        sound_se(SE_DECIDE, 1.0f);
        transform_clip(w, h, true, 0);
    } else if (pad_pressed(PSP_CTRL_RTRIGGER) || pad_pressed(PSP_CTRL_LTRIGGER)) {
        sound_se(SE_DECIDE, 1.0f);
        int turn = pad_pressed(PSP_CTRL_RTRIGGER) ? -1 : 1;
        g.rot = (g.rot + (turn < 0 ? 1 : 3)) & 3;
        transform_clip(w, h, false, turn);
        place_turned();
    } else if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        for (int j = 0; j < h; j++) /* FUN_088caa40: every pixel, 0 too */
            for (int i = 0; i < w; i++)
                plot(g.clip[i + j * w], g.sx + i, g.sy + j, false);
        g.armed = false; /* +0x8C */
        range_done();    /* the cursor on the far corner */
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(0xE, 1.0f);
        undo();          /* back to before the range was lifted */
        g.cx = g.sx, g.cy = g.sy; /* FUN_088ca050(.., 0) */
        range_done();
    }
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
    case TOOL_SAMPLE: open_samples(); break;
    default:
        SDL_Log("[emblem] \"%s\" isn't ported", nz(hard_string(0x9E + tool)));
        g.message = hard_string(0x9E + tool);
        sound_se(SE_ERROR, 1.0f);
        break;
    }
}

static void write_back(void) /* FUN_088cb970 */
{
    if (g.team >= 0) {
        uint8_t *e = savedata_team_emblem_edit(g.team);
        if (e) {
            memcpy(e, g.pix, PIXELS);
            memcpy(e + PIXELS, g.pal, sizeof g.pal);
        }
        SDL_Log("[emblem] team %d: emblem written back", g.team + 1);
        return;
    }
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
            g.tool = tool_count() - 1;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        if (g.tool < tool_count() - 1)
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
        open_tool(tool_at(g.tool));
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
 * draws the shape to the cursor, which stays where it is; cross drops the first point,
 * or with none goes back to the tools. */
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
            g.stage = 0; /* FUN_088ca050(.., 1): the first point to the cursor */
        }
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        if (g.stage == 1) {
            g.stage = 0; /* the cursor stays (FUN_088ca050(.., 1)) */
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
    case E_SAMPLES: update_samples(); break;
    case E_USER: update_user(); break;
    case E_RANGE: update_range(); break;
    case E_RANGE_MENU: update_range_menu(); break;
    case E_RANGE_MOVE: update_range_move(); break;
    }
    scan_user(); /* FUN_088c778c */
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

/* FUN_088d02cc, the tool column; FUN_088d0da0, the range column of modes 0xE / 0xF
 * (Move, Copy, Delete) in its place. */
static void draw_tools(void)
{
    float p = ui_pulse(SDL_GetTicks() / 1000.0f);
    bool range = g.mode == E_RANGE_MENU || g.mode == E_RANGE_MOVE;
    uint32_t table = range ? T_RANGE_TOOLS : tool_table();
    int count = range ? RANGE_ITEMS : tool_count(), cur = range ? g.range_item : g.tool;
    for (int i = 0; i < count; i++) {
        int y = tool_word(table, i, 0), button = tool_word(table, i, 1), icon = tool_word(table, i, 2);
        bool sel = i == cur && (g.mode == E_MENU || g.mode == E_RANGE_MENU);
        Picture *icds = hard_part(HP_EM_ICDS), *b = hard_part(sel ? HP_EM_BRK_WB : button);
        if (icds)
            picture_draw(icds, 7, y - 1, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
        if (b)
            picture_draw(b, 8, y, 0, BLEND_ALPHA, sel ? (uint32_t)(0x80 + p * 0x7F) << 24 | 0xFFFFFF : 0xFFFFFFFF, 0, 0);
        Picture *ic = hard_part(HP_EM_ICON);
        if (ic)
            picture_draw_rect(ic, 8, y, 0, (icon & 3) * 32, (icon >> 2) * 16, 32, 16, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
        if (i == cur)
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

/* FUN_088c8ee0: the range's outline, a pixel outside it. */
static void draw_range_box(int x0, int y0, int w, int h, float alpha)
{
    int x = CANVAS_X + x0 * CELL - 1, y = CANVAS_Y + y0 * CELL - 1, W = w * CELL + 2, H = h * CELL + 2;
    uint32_t c = (uint32_t)(alpha * 255) << 24 | 0xFFFFFF;
    ui_fill(x, y, W, 1, c);
    ui_fill(x, y + H - 1, W, 1, c);
    ui_fill(x, y, 1, H, c);
    ui_fill(x + W - 1, y, 1, H, c);
}

/* FUN_088c8854: the lifted range over the canvas, on black where it is clear. */
static void draw_clip(void)
{
    int w = rw(), h = rh();
    ui_fill(CANVAS_X + g.sx * CELL, CANVAS_Y + g.sy * CELL, w * CELL, h * CELL, 0xFF000000u);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            ui_fill(CANVAS_X + (g.sx + i) * CELL, CANVAS_Y + (g.sy + j) * CELL, CELL, CELL, pal_color(g.clip[i + j * w]));
}

/* FUN_088c85a0 / FUN_088cc418: the strip clipped to (0x10B, 0xA4) 0xBE x 0x26, the
 * pictures 0x26 apart from 0x10E, all but the chosen one dimmed (0x60), the arrows
 * sd_saro0 / sd_saro1 while there is more that way, the cursor lc_cur. */
static void draw_strip(const Sample *list, int n, const Strip *st)
{
    sceGuScissor(0x10B, 0xA4, 0xBE, 0x26);
    for (int i = 0; i < n; i++) {
        int x = 0x10E + (i + st->scroll) * 0x26;
        if (x + 0x20 <= 0x10B || x >= 0x1C9)
            continue;
        uint32_t v = i == st->row - st->scroll ? 0xFF : 0x60;
        picture_draw(list[i].pic, x, 0xA7, 0, BLEND_ALPHA, 0xFF000000u | v << 16 | v << 8 | v, 0, 0);
    }
    sceGuScissor(0, 0, 480, 272);
    Picture *l = hard_part(HP_SD_SARO0), *r = hard_part(HP_SD_SARO1);
    if (st->scroll != 0 && l)
        picture_draw(l, 0xFD, 0xB0, 0, BLEND_ALPHA, 0xFF909090u, 0, 0);
    if (st->scroll != STRIP_ROWS - n && r)
        picture_draw(r, 0x1C8, 0xB0, 0, BLEND_ALPHA, 0xFF909090u, 0, 0);
    ui_frame(ui_part(PART_LC_CUR), 0x10B + st->row * 0x26, 0xA4, 0x26, 0x26, 6, ui_pulse(SDL_GetTicks() / 1000.0f));
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
    if (g.mode == E_OVAL && g.stage == 1) /* the oval's box */
        draw_range_box(rx0(), ry0(), rw(), rh(), p);
    if (g.mode == E_RANGE_MOVE)
        draw_clip();
    if (g.mode == E_RANGE || g.mode == E_RANGE_MENU || g.mode == E_RANGE_MOVE)
        draw_range_box(rx0(), ry0(), rw(), rh(), g.mode == E_RANGE_MENU ? 1.0f : p);
    if (g.mode == E_SAMPLES)
        draw_strip(g.samples, g.nsamples, &g.strip);
    if (g.mode == E_USER && g.nuser > 0)
        draw_strip(g.user, g.nuser, &g.ustrip);
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
    case E_MENU: return hard_string(0x9E + tool_at(g.tool));
    case E_PENCIL: return hard_string(0xAE);
    case E_LINE: return hard_string(g.stage ? 0xB0 : 0xAF);
    case E_RECT: return hard_string(g.stage ? 0xB2 : 0xB1);
    case E_OVAL: return hard_string(g.stage ? 0xB4 : 0xB3);
    case E_PAINT: return hard_string(0xB5);
    case E_PALETTE: return hard_string(0xB6);
    case E_PLACE: return hard_string(0xA7);
    case E_SAMPLES:
    case E_USER: return hard_string(0xB7);
    case E_RANGE: return hard_string(0xAA);
    case E_RANGE_MENU:
    case E_RANGE_MOVE: return hard_string(0xAB + g.range_item);
    default: return NULL;
    }
}
