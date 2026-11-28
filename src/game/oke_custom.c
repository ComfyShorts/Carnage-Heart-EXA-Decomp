/* See oke_custom.h. Hand-written from CDesignMngr:
 *   mode 2, the sub-menu (FUN_088ba534 / FUN_088bb668, built by FUN_088b9edc):
 *     CDesignString 0x106 + i, help 0x10A + i, each item kept only while its
 *     menu-limit word 6 + 9 + i is on (FUN_0893ab4c);
 *   mode 3, Design: CColorDesignWnd (+0x4548, input FUN_088a08ac's class, update
 *     FUN_088bbaf8): the pattern 0-3, left / right, the card written as it changes;
 *   modes 4 / 5, Main / Accent Color: CColorEditWnd (+0x3E90 / +0x4124, FUN_088a1128;
 *     updates FUN_088bbc40 / FUN_088bbeac): R, G and B 0-100, up / down pick the row,
 *     left / right change it (sound 0x10), triangle picks all three at random
 *     (rand % 101, sound 0x11), packed by FUN_088a0fe8 into the card at once;
 *   mode 6, Polish: CColorLusterWnd (+0x47D4, FUN_088a3658, update FUN_088bc118):
 *     0-4, left / right.
 * In every window circle keeps the value (sound 0xC) and cross puts back the one it
 * opened with (sound 0xB); then the sub-menu again (FUN_088ba1f4(.., 2, 1)).
 *
 * The game shows the OKE's model turning beside these windows, painted as edited;
 * the port shows the card window instead (docs/screens.md). */
#include "oke_custom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "font.h"
#include "gamedata.h"
#include "gu.h"
#include "oke_parts.h"
#include "pad.h"
#include "picture.h"
#include "psp_shim.h"
#include "savedata.h"
#include "sound.h"
#include "ui.h"

#define PART_BF_MENB2 0x08
#define PART_BF_MENL2 0x09
#define PART_OP_SVF_L 0x43 /* the gauge's bed; 0x44 on the cursor's row */
#define PART_BF_TWND2 0x9E
#define PART_BF_LUSTER 0xA1 /* CColorLusterWnd's scale */
#define PART_BF_CEWND 0xA5  /* CColorEditWnd's frame */
#define PART_BF_CE_R 0xA6   /* R, G, B labels: 0xA6 / 0xA8 / 0xAA, + 1 on the cursor's row */
#define PART_BF_CE_FILL 0xD9 /* the bars, 0xD9 / 0xDA / 0xDB */

#define S_PAINT_PATTERN 0x003DBAD4u /* "Paint Pattern" (0x08bdfad4) */
#define S_POLISH 0x003DBE04u        /* "Polish" (0x08bdfe04) */

enum { SUB_DESIGN, SUB_MAIN, SUB_ACCENT, SUB_POLISH, SUB_COUNT };
typedef enum { C_MENU, C_DESIGN, C_COLOR, C_POLISH } CState;

static struct {
    int card;
    CState state;
    UiMenu menu;
    int item;                 /* the sub-menu item open */
    int value, keep;          /* Design / Polish */
    ColorEdit color;
    bool closed;
} g;

static const char *dstr(int id)
{
    return design_string(id, 1);
}

static const char *nz(const char *s)
{
    return s ? s : "";
}

static const TextStyle *dark(void)
{
    static TextStyle s;
    static bool init;
    if (!init) {
        s = text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0);
        init = true;
    }
    return &s;
}

static void build_menu(void)
{
    int keep = g.menu.cursor;
    ui_menu_init(&g.menu, 0, 0x18 + 4, 24 - 5, 12, *dark());
    for (int i = 0; i < SUB_COUNT; i++)
        if (savedata_menu_limit(LIMIT_DESIGN + 9 + i))
            ui_menu_add(&g.menu, dstr(0x106 + i), i, 0x10A + i, PART_BF_MENB2, PART_BF_MENL2);
    if (keep < g.menu.count)
        g.menu.cursor = keep;
}

void custom_open(int card)
{
    memset(&g, 0, sizeof g);
    g.card = card;
    build_menu();
    ui_menu_open(&g.menu);
    g.state = C_MENU;
}

static int color_field(void)
{
    return g.item == SUB_MAIN ? HW_MAIN_COLOR : HW_ACCENT_COLOR;
}

static void open_window(int item)
{
    const OkeDataBase *card = savedata_card(g.card);
    g.item = item;
    switch (item) {
    case SUB_DESIGN: /* FUN_088ba754 */
        g.value = g.keep = hw_get(card, HW_PAINT);
        g.state = C_DESIGN;
        break;
    case SUB_MAIN: /* FUN_088ba8b0 / FUN_088baa5c */
    case SUB_ACCENT:
        color_edit_open(&g.color, (uint32_t)hw_get(card, color_field()), 3, true);
        g.state = C_COLOR;
        break;
    default: /* FUN_088bac08 */
        g.value = g.keep = hw_get(card, HW_POLISH);
        g.state = C_POLISH;
        break;
    }
}

/* Left / right over 0 .. max: held keys stop at the ends, a fresh press wraps. */
static bool step(int *v, int max)
{
    int old = *v;
    if (pad_repeat(PSP_CTRL_LEFT)) {
        if (*v > 0)
            (*v)--;
        else if (pad_trigger(PSP_CTRL_LEFT))
            *v = max;
    } else if (pad_repeat(PSP_CTRL_RIGHT)) {
        if (*v < max)
            (*v)++;
        else if (pad_trigger(PSP_CTRL_RIGHT))
            *v = 0;
    }
    return *v != old;
}

/* Circle 1, cross 0, -1 neither. */
static int decide(void)
{
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        return 1;
    }
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        return 0;
    }
    return -1;
}

static void write_field(int field, int32_t v)
{
    if (hw_get(savedata_card(g.card), field) != v)
        hw_set(savedata_card_edit(g.card), field, v);
}

static void back_to_menu(const char *what, int32_t v)
{
    SDL_Log("[oke] card %d: %s = %d (0x%08X)", g.card + 1, nz(what), (int)v, (unsigned)v);
    g.state = C_MENU;
}

static void update_value(int field, int max, int sound)
{
    if (step(&g.value, max))
        sound_se(sound, 1.0f);
    int d = decide();
    if (d == 0)
        g.value = g.keep;
    write_field(field, g.value);
    if (d >= 0)
        back_to_menu(dstr(0x106 + g.item), g.value);
}

/* Modes 4 / 5 (FUN_088bbc40 / FUN_088bbeac): the window's input, triangle for a random
 * colour, the value into the card every frame. */
static void update_color(void)
{
    int d = color_edit_update(&g.color);
    if (d < 0 && pad_pressed(PSP_CTRL_TRIANGLE)) {
        for (int i = 0; i < 3; i++)
            g.color.v[i] = rand() % 101;
        sound_se(0x11, 1.0f);
    }
    write_field(color_field(), (int32_t)color_edit_value(&g.color));
    if (d >= 0)
        back_to_menu(dstr(0x106 + g.item), (int32_t)color_edit_value(&g.color));
}

int custom_update(float dt)
{
    ui_menu_update(&g.menu, dt);
    g.menu.active = g.state == C_MENU;
    switch (g.state) {
    case C_MENU:
        ui_menu_input(&g.menu);
        if (pad_pressed(PSP_CTRL_CIRCLE) && g.menu.count) {
            sound_se(SE_DECIDE, 1.0f);
            open_window(ui_menu_id(&g.menu));
        } else if (pad_pressed(PSP_CTRL_CROSS)) {
            sound_se(SE_CANCEL, 1.0f);
            g.closed = true;
        }
        break;
    case C_DESIGN: update_value(HW_PAINT, 3, SE_CURSOR); break;
    case C_COLOR: update_color(); break;
    case C_POLISH: update_value(HW_POLISH, 4, SE_CURSOR); break;
    }
    return g.closed ? 0 : 1;
}

/* ---- drawing ---- */

static int window_y(int below)
{
    Picture *b = ui_part(PART_BF_MENB2);
    return 0x18 + (b ? picture_height(b) : 0x14) * 4 + below;
}

static void cursor_box(int x, int y, int w, int h)
{
    ui_frame(ui_part(PART_BF_CUR0), x, y, w, h, 6, ui_pulse(SDL_GetTicks() / 1000.0f));
}

/* CColorDesignWnd: "Paint Pattern" and the four numbered boxes 0xE apart from 0x24. */
static void draw_design(int x, int y)
{
    ui_frame(ui_part(PART_BF_TWND2), x, y, 0x24 + 4 * 0xE + 0x10, 0x2C, 8, 1.0f);
    ui_text(nz(gamedata_string(S_PAINT_PATTERN)), dark(), x + 0xC, y + 4, 1.0f);
    for (int i = 0; i < 4; i++) {
        char n[4];
        snprintf(n, sizeof n, "%d", i + 1);
        ui_text(n, dark(), x + 0x24 + i * 0xE + 2, y + 0x17, 1.0f);
    }
    cursor_box(x + 0x24 + g.value * 0xE - 1, y + 0x17 - 1, 10 + 2, 12 + 2);
}

/* CColorLusterWnd (FUN_088a3d40): "Polish", the scale 0xA1 at (0x23, 0x19) between
 * two caps of 0x43, a marker 0xF x 9 at value * 16, the value + 1 at 0x91. */
static void draw_polish(int x, int y)
{
    ui_frame(ui_part(PART_BF_TWND2), x, y, 0xA8, 0x2C, 8, 1.0f);
    ui_text(nz(gamedata_string(S_POLISH)), dark(), x + 0xC, y + 4, 1.0f);
    Picture *scale = ui_part(PART_BF_LUSTER), *cap = ui_part(PART_OP_SVF_L);
    if (scale)
        picture_draw(scale, x + 0x23, y + 0x19, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    if (cap) {
        picture_draw_rect(cap, x + 0x18, y + 0x18, 0, 0x12, 0, 10, 0xD, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
        picture_draw_rect(cap, x + 0x73, y + 0x18, 0, 0x9E, 0, 10, 0xD, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    }
    ui_fill(x + 0x23 + g.value * 16, y + 0x19, 0xF, 9, 0xA0FFFFFF); /* the game adds it (FUN_08ae3fbc mode 2) */
    char n[16];
    snprintf(n, sizeof n, "%d", g.value + 1);
    ui_text(n, dark(), x + 0x91, y + 0x18, 1.0f);
}

void custom_draw(void)
{
    ui_menu_draw(&g.menu);
    switch (g.state) {
    case C_DESIGN: draw_design(2, window_y(0x2A)); break;
    case C_COLOR: color_edit_draw(&g.color, 2, window_y(0x16)); break;
    case C_POLISH: draw_polish(2, window_y(0x2A)); break;
    default: break;
    }
}

const char *custom_help(void)
{
    if (g.state == C_MENU)
        return g.menu.count ? dstr(g.menu.item[g.menu.cursor].help) : NULL;
    return dstr(0x10A + g.item);
}

/* ---- CColorEditWnd (FUN_088a1128 .. FUN_088a2aa8) ---- */

void color_edit_open(ColorEdit *c, uint32_t packed, int rows, bool restore)
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 4; i++) /* FUN_088a1014 / FUN_088a10f8 */
        c->v[i] = c->keep[i] = (int)(packed >> (8 * i) & 0xFF);
    c->rows = rows;
    c->restore = restore;
}

uint32_t color_edit_value(const ColorEdit *c) /* FUN_088a0fe8 */
{
    return (uint32_t)c->v[0] | (uint32_t)c->v[1] << 8 | (uint32_t)c->v[2] << 16 | (uint32_t)c->v[3] << 24;
}

/* FUN_088a1c10: the row (FUN_088a1640 / FUN_088a1740 for three or four rows), the
 * value 0-100 (FUN_088a1840, sound 0x10), the end (FUN_088a1a2c: circle keeps, cross
 * puts +0xA8.. back when the window was made to). */
int color_edit_update(ColorEdit *c)
{
    int old = c->row, last = c->rows - 1;
    if (pad_repeat(PSP_CTRL_UP)) {
        if (c->row > 0)
            c->row--;
        else if (pad_trigger(PSP_CTRL_UP))
            c->row = last;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        if (c->row < last)
            c->row++;
        else if (pad_trigger(PSP_CTRL_DOWN))
            c->row = 0;
    }
    if (c->row != old)
        sound_se(SE_CURSOR, 1.0f);
    int *v = &c->v[c->row], before = *v;
    if (pad_repeat(PSP_CTRL_LEFT)) {
        if (*v > 0)
            (*v)--;
        else if (pad_trigger(PSP_CTRL_LEFT))
            *v = 100;
    } else if (pad_repeat(PSP_CTRL_RIGHT)) {
        if (*v < 100)
            (*v)++;
        else if (pad_trigger(PSP_CTRL_RIGHT))
            *v = 0;
    }
    if (*v != before)
        sound_se(0x10, 1.0f);
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        return 1;
    }
    if (pad_pressed(PSP_CTRL_CROSS)) {
        if (c->restore)
            memcpy(c->v, c->keep, sizeof c->v);
        sound_se(SE_CANCEL, 1.0f);
        return 0;
    }
    return -1;
}

/* FUN_088a266c: the row's label (0xA6 + 2 * row, + 1 bright), the bed (0x43 from
 * u 0x10, 0x44 bright), the bar 0xD9 + row of value * 128 / 100 by 9 from + 0x28, the
 * number at + 0xB6. */
static void color_row(const ColorEdit *c, int x, int y, int row, bool hot)
{
    Picture *label = ui_part(PART_BF_CE_R + 2 * row + (hot ? 1 : 0));
    Picture *bed = ui_part(hot ? PART_OP_SVF_L + 1 : PART_OP_SVF_L);
    Picture *bar = ui_part(PART_BF_CE_FILL + row);
    if (label)
        picture_draw(label, x + 0xD, y + (0x10 - picture_height(label)) / 2, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    if (bed)
        picture_draw_rect(bed, x + 0x1B, y + (0x10 - picture_height(bed)) / 2, 0, 0x10, 0, picture_width(bed) - 0x25,
                          picture_height(bed), BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    int v = c->v[row], w = (v << 7) / 100;
    if (bar)
        picture_draw_ex(bar, x + 0x28, y + (0x10 - picture_height(bar)) / 2, 0, w, 9, 0, 0, picture_width(bar),
                        picture_height(bar), BLEND_ALPHA, 0xFFFFFFFF, 0, 0, 0, 0);
    char n[16];
    snprintf(n, sizeof n, "%d", v);
    ui_text(n, dark(), x + 0xB6, y + 2, 1.0f);
}

/* FUN_088a1e14 / FUN_088a21e4: the frame 0xA5 (0xDF by 0x3E, or 0x4E for four rows),
 * the rows 0x10 apart from + 6, the cursor 0xD3 by 0x10 at (8, row * 0x10 + 6). */
void color_edit_draw(const ColorEdit *c, int x, int y)
{
    ui_frame(ui_part(PART_BF_CEWND), x, y, 0xDF, c->rows > 3 ? 0x4E : 0x3E, 6, 1.0f);
    /* every row plain, then the cursor's row in its bright pictures (the game masks
     * that second pass to the cursor's box with the stencil) */
    for (int r = 0; r < c->rows; r++)
        color_row(c, x, y + 6 + r * 0x10, r, false);
    color_row(c, x, y + 6 + c->row * 0x10, c->row, true);
    cursor_box(x + 8, y + c->row * 0x10 + 6, 0xD3, 0x10);
}
