/* Software Setup: CSoftDesignMngr (the menu and its modes) driving CPgChipWnd (the
 * chip areas and their cursor), CPgChipSelectWnd (the chip list) and
 * CPgChipConnectWnd (the exit step). Hand-written from:
 *   CSoftDesignMngr: setupTopMenu FUN_08a79b8c, the mode table FUN_08a7a46c / the
 *     begin table FUN_08a83b64, UpdateModeTopMenu FUN_08a851f4, Edit Chips (mode 3)
 *     FUN_08a7d644, UpdateModeChipSelect FUN_08a7e454, UpdateModeEditConnection
 *     FUN_08a85770, the individual selection (mode 6) FUN_08a7d9ec, Move Start
 *     (mode 0xF), Delete All, Copy from Card, Change Counter Name.
 *   CPgChipWnd: Create FUN_08a25f84 (the three areas side by side: main, then sub 1
 *     over sub 2; cells 0x40 with 0x10 between, 0x20 of margin), the chip faces
 *     (FUN_08a0c008 tiles, FUN_08a13430 face routines, FUN_08a13468 exit arrows).
 *   CPgChipSelectWnd: Create FUN_08a16bc8 (5 pages of 9 x 7, titles CDesignString
 *     0xCF-0xD3), the page rule FUN_08a155f8.
 * The parameter sub-menus are in oke_param.c, the rules in pgchip_edit.c. What isn't
 * ported (drag / pull, flipping and turning a pasted block, macros, the test
 * program, example solutions) is listed in docs/screens.md. */
#include "oke_soft.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "font.h"
#include "gamedata.h"
#include "gu.h"
#include "oke_card.h"
#include "oke_param.h"
#include "osk.h"
#include "pad.h"
#include "pgchip_edit.h"
#include "picture.h"
#include "psp_shim.h"
#include "savedata.h"
#include "sound.h"
#include "ui.h"

/* ---- the game's tables, read from the EBOOT at run time (file offsets) ---- */

#define T_SOFT_STRING 0x00486BA0u  /* CSoftString, PTR_DAT_08c8aba0, 0x2A3 entries */
#define T_SOFT_PICTURE 0x00486AD8u /* CSoftPicture's names (sd_chip0, sd_cicn0, ...) */
#define T_CHIP_NAME 0x00481100u    /* PGCHIP::GetCommandName, PTR_DAT_08c85100 */
#define T_COUNTER_NAME 0x004810B8u /* the default counter names CNT_A.., PTR_s_CNT_A_08c850b8 */
#define T_GLYPH 0x00419F90u        /* sd_cicn0's glyphs: u16 u, u16 v, u8 w, u8 h */
#define T_GLYPH_ASCII 0x0041A81Eu  /* a glyph per ASCII code, u16 */
#define T_ARROW 0x0041A91Eu        /* sd_cara0's arrows: u16 u, v, u8 w, h, s16 dx, dy */
#define T_ARROW_PICK 0x0041B228u   /* [processing mode][direction] -> arrow, s32 */
#define T_TILE_KIND 0x0041AA30u    /* sd_chip0 tiles [faded][kind], 6 bytes */
#define T_TILE_RETURN 0x0041AA60u
#define T_TILE_BLANK 0x0041AA6Cu
#define ADDR(a) ((a) - 0x08804000u) /* a table the face routines index, by its address */

#define SOFT_STRINGS 0x2A3
#define PART_BF_MENB2 0x08 /* the menu parts oke_menu.c uses (bf_menb2 / bf_menl2) */
#define PART_BF_MENL2 0x09
#define PART_BF_SWND2 0x99
enum { PIC_CHIP = 0, PIC_ICON = 1, PIC_START = 0x15 };

static uint8_t rd8(uint32_t a)
{
    return (uint8_t)(gamedata_word(a & ~3u, 0) >> ((a & 3) * 8));
}

static int rd16(uint32_t a)
{
    return rd8(a) | rd8(a + 1) << 8;
}

static int32_t rd32(uint32_t a)
{
    return (int32_t)gamedata_word(a, 0);
}

static const char *ptr_string(uint32_t table, int i)
{
    uint32_t p = gamedata_word(table + 4u * (uint32_t)i, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

/* CSoftString::GetString (FUN_08aa14f8). */
static const char *sstr(int id)
{
    const char *s = id >= 0 && id < SOFT_STRINGS ? ptr_string(T_SOFT_STRING, id) : NULL;
    return s ? s : "";
}

static const char *chip_name(int type)
{
    const char *s = pgchip_valid_type(type) ? ptr_string(T_CHIP_NAME, type) : NULL;
    return s ? s : "";
}

/* The chip's line in the help strings: 0x258 for type 1 on to 0x28C for Return. */
static const char *chip_help(int type)
{
    return pgchip_valid_type(type) ? sstr(0x257 + type) : sstr(0x24A);
}

static Picture *soft_pic(int i)
{
    static Picture *cache[0x32];
    static bool tried[0x32];
    if (i < 0 || i >= 0x32)
        return NULL;
    if (!tried[i]) {
        tried[i] = true;
        const char *name = ptr_string(T_SOFT_PICTURE, i);
        if (name && *name) {
            char path[64];
            snprintf(path, sizeof path, "parts/soft/%s.gim", name);
            cache[i] = picture_load(path);
        }
    }
    return cache[i];
}

static Picture *arrow_pic(void)
{
    static Picture *p;
    static bool tried;
    if (!tried) {
        tried = true;
        p = picture_load("parts/soft/sd_cara0.gim");
    }
    return p;
}

static const TextStyle *style(int size, uint32_t color)
{
    static TextStyle st[4];
    static int n;
    TextStyle *s = &st[n++ & 3];
    *s = text_style(0, size / 2, size, size, 0, 4, color, 0);
    return s;
}

/* ---- state ---- */

typedef enum {
    M_MENU,       /* mode 0x10: the Software Setup menu */
    M_EDIT,       /* mode 3: Edit Chips */
    M_SELECT,     /* mode 0x12: the chip list */
    M_CONNECT,    /* mode 0x1A: the exits of the chip just placed */
    M_PARAM,      /* mode 0x13: the parameter sub-menu (oke_param.c) */
    M_PICK,       /* mode 6: chips selected one by one */
    M_RANGE,      /* Select Area: a rectangle */
    M_RANGE_MENU, /* mode 0x11: Move / Copy / Delete / Save Macro */
    M_PASTE,      /* where a moved or copied block goes */
    M_START,      /* mode 0xF: Move Start Location */
    M_ASK,        /* a Yes / No question */
    M_CARDS,      /* Copy from Card: the card list */
    M_COUNTERS,   /* Change Counter Name: which counter */
    M_OSK,        /* ... and the keyboard */
} Mode;

enum { ASK_DELETE_ALL, ASK_COPY };

static struct {
    int card;
    SoftwareDataBase sw, undo;
    bool has_undo;
    Mode mode;
    UiMenu menu;
    int item_of[10];
    /* the cursor, in the cells of the three areas laid side by side */
    int cx, cy;
    float view_x, view_y; /* top-left of the view, in the areas' pixels */
    /* chip list */
    int page, list_cursor;
    int list[PGCHIP_TYPE_END], list_count;
    /* exit step */
    bool branch_step;
    Mode back_to;
    bool param_from_list; /* the sub-menu came from the chip list */
    /* selections */
    int ax, ay; /* Select Area's anchor */
    bool picked[PGCHIP_PROGRAM_MAX][PGCHIP_MAIN_CAP * PGCHIP_MAIN_CAP];
    int pick_program;
    int range_cursor;
    bool range_from_pick;
    PgChipClip clip;
    bool moving;
    int src_program, sx0, sy0, sx1, sy1;
    /* questions and lists */
    UiAsk ask;
    int ask_kind;
    int card_cursor, counter_cursor;
    const char *message;
    float t;
} g;

/* ---- the areas' layout (CPgChipWnd::Create) ---- */

#define CELL 0x40
#define GAP 0x10
#define PITCH (CELL + GAP)
#define MARGIN 0x20
#define SCALE 0.5f
#define VIEW_X 10
#define VIEW_Y 30
#define VIEW_W 460
#define VIEW_H 190

static int area_w(int program)
{
    PgChipArea a = pgchip_area(&g.sw, program);
    return a.width;
}

/* An area's cell origin in the combined grid: sub 1 one column right of the main
 * area, sub 2 one row under sub 1 (CPgChipWnd::Create's FUN_08a1ef44 offsets). */
static void area_cell_origin(int program, int *x, int *y)
{
    *x = program ? area_w(0) + 1 : 0;
    *y = program == 2 ? area_w(1) + 1 : 0;
}

static int area_px(int program)
{
    int w = area_w(program);
    return w ? PITCH * w + GAP + 2 * MARGIN : 0;
}

static void area_px_origin(int program, int *x, int *y)
{
    *x = program ? area_px(0) : 0;
    *y = program == 2 ? area_px(1) : 0;
}

/* FUN_08a1da40: which area a combined cell is in, and its cell there. */
static int area_at(int gx, int gy, int *x, int *y)
{
    for (int p = 0; p < PGCHIP_PROGRAM_MAX; p++) {
        int w = area_w(p), ox, oy;
        if (!w)
            continue;
        area_cell_origin(p, &ox, &oy);
        if (gx >= ox && gy >= oy && gx < ox + w && gy < oy + w) {
            *x = gx - ox;
            *y = gy - oy;
            return p;
        }
    }
    return -1;
}

static int cursor_area(int *x, int *y)
{
    return area_at(g.cx, g.cy, x, y);
}

static PgChipCommand *cursor_chip(void)
{
    int x, y, p = cursor_area(&x, &y);
    if (p < 0)
        return NULL;
    PgChipArea a = pgchip_area(&g.sw, p);
    return pgchip_at(&a, x, y);
}

static void cell_px(int program, int x, int y, int *px, int *py)
{
    int ox, oy;
    area_px_origin(program, &ox, &oy);
    *px = ox + MARGIN + GAP + PITCH * x;
    *py = oy + MARGIN + GAP + PITCH * y;
}

/* The cursor moves a cell at a time and jumps the gap between areas; it stays put
 * when nothing lies that way. */
static bool move_cursor(int dx, int dy)
{
    int gw = area_w(0) + (area_w(1) ? area_w(1) + 1 : 0);
    int gh = area_w(0);
    if (area_w(2) && area_w(1) + 1 + area_w(2) > gh)
        gh = area_w(1) + 1 + area_w(2);
    int x = g.cx, y = g.cy, ax, ay;
    for (int i = 0; i < gw + gh; i++) {
        x += dx;
        y += dy;
        if (x < 0 || y < 0 || x >= gw || y >= gh)
            return false;
        if (area_at(x, y, &ax, &ay) >= 0) {
            g.cx = x;
            g.cy = y;
            return true;
        }
    }
    return false;
}

static void place_cursor(int program, int x, int y)
{
    int ox, oy;
    area_cell_origin(program, &ox, &oy);
    g.cx = ox + x;
    g.cy = oy + y;
}

static void follow_cursor(float dt)
{
    int x, y, p = cursor_area(&x, &y);
    if (p < 0)
        return;
    int px, py;
    cell_px(p, x, y, &px, &py);
    float vw = VIEW_W / SCALE, vh = VIEW_H / SCALE, edge = PITCH;
    float tx = g.view_x, ty = g.view_y;
    if (px - edge < tx)
        tx = px - edge;
    if (px + CELL + edge > tx + vw)
        tx = px + CELL + edge - vw;
    if (py - edge < ty)
        ty = py - edge;
    if (py + CELL + edge > ty + vh)
        ty = py + CELL + edge - vh;
    float total_w = area_px(0) + area_px(1), total_h = area_px(0);
    if (area_px(1) + area_px(2) > total_h)
        total_h = area_px(1) + area_px(2);
    if (tx > total_w - vw)
        tx = total_w - vw;
    if (ty > total_h - vh)
        ty = total_h - vh;
    if (tx < 0)
        tx = 0;
    if (ty < 0)
        ty = 0;
    float k = dt * 12.0f > 1.0f ? 1.0f : dt * 12.0f;
    g.view_x += (tx - g.view_x) * k;
    g.view_y += (ty - g.view_y) * k;
}

/* ---- edits ---- */

static void backup(void)
{
    g.undo = g.sw; /* CPgChipAreaBufferBackup: one step */
    g.has_undo = true;
}

static void error_se(void)
{
    sound_se(0xF, 1.0f);
}

/* ---- the menu (setupTopMenu) ---- */

enum {
    ITEM_EDIT, ITEM_UNDO, ITEM_AREA, ITEM_TEST, ITEM_START, ITEM_COUNTER, ITEM_MACRO, ITEM_COPY,
    ITEM_EXAMPLE, ITEM_DELETE_ALL, ITEMS
};

#define LIMIT_SOFTWARE 0x22 /* CMenuLimitDataBase word 0x22 + item (FUN_0895e554 at +0x88) */

static void build_menu(void)
{
    int keep = g.menu.cursor;
    ui_menu_init(&g.menu, 0, 0x18 + 4, 24 - 5, 12, text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0));
    for (int id = 0; id < ITEMS; id++) {
        if (!savedata_menu_limit(LIMIT_SOFTWARE + id))
            continue;
        ui_menu_add(&g.menu, sstr(0x12 + id), id, 0x231 + id, PART_BF_MENB2, PART_BF_MENL2);
    }
    if (keep < g.menu.count)
        g.menu.cursor = keep;
}

void soft_open(int card)
{
    memset(&g, 0, sizeof g);
    g.card = card;
    g.sw = savedata_card(card)->software;
    pgchip_normalize(&g.sw);
    place_cursor(0, g.sw.start[0] >= 0 && g.sw.start[0] < area_w(0) ? g.sw.start[0] : 0, 0);
    build_menu();
    ui_menu_open(&g.menu);
    g.mode = M_MENU;
    g.page = savedata_chip_page();
    SDL_Log("[soft] Software Setup (CSoftDesignMngr), card %d: main %d x %d, subs %d / %d", card + 1, area_w(0),
            area_w(0), area_w(1), area_w(2));
}

static void close_editor(void)
{
    OkeDataBase *card = savedata_card_edit(g.card);
    pgchip_normalize(&g.sw);
    if (card && memcmp(&card->software, &g.sw, sizeof g.sw)) {
        card->software = g.sw;
        SDL_Log("[soft] card %d: the edited program written back", g.card + 1);
    }
}

/* ---- the parameter sub-menu (mode 0x13, oke_param.c) ---- */

static void open_param(const PgChipCommand *chip, bool from_list)
{
    param_open(chip, &g.sw, savedata_card(g.card));
    g.param_from_list = from_list;
    g.mode = M_PARAM;
}

static void open_list(void);
static void enter_connect(void);

/* UpdateModeSubMenu's ends: Confirm puts the chip on the cell and goes on to its
 * exits; cross keeps the chip as the type's default and goes back, putting it on the
 * cell only when it came from one (FUN_08a854fc). */
static void update_param(float dt)
{
    ParamResult r = param_update(dt);
    if (r == PARAM_OPEN)
        return;
    const PgChipCommand *chip = param_chip();
    int x, y, p = cursor_area(&x, &y);
    if (r == PARAM_CANCEL && g.param_from_list) {
        savedata_set_chip_default(chip);
        g.mode = M_SELECT;
        return;
    }
    PgChipCommand *cell = cursor_chip();
    if (!g.param_from_list && cell && !memcmp(cell, chip, sizeof *cell)) {
        /* unchanged: nothing to undo */
    } else {
        backup();
        if (p < 0 || !pgchip_place(&g.sw, p, x, y, chip)) {
            error_se();
            g.mode = M_EDIT;
            return;
        }
        SDL_Log("[soft] %s (0x%02X) at %d (%d, %d): %02X %02X %02X %02X %02X %02X", chip_name(pgchip_type(chip)),
                pgchip_type(chip), p, x, y, chip->b[2], chip->b[3], chip->b[4], chip->b[5], chip->b[6], chip->b[7]);
    }
    if (r == PARAM_CANCEL) {
        savedata_set_chip_default(chip); /* FUN_08a1cbd8 */
        g.mode = M_EDIT;
        return;
    }
    enter_connect();
}

/* ---- the chip list (CPgChipSelectWnd) ---- */

#define LIST_COLS 9
#define LIST_ROWS 7

/* FUN_08a155f8: types 1..0x34 the save has unlocked (all of them outside Story
 * Mode), on page 4 or their kind's page. */
static void build_list(void)
{
    g.list_count = 0;
    for (int t = 1; t < PGCHIP_RETURN; t++)
        if (g.page == 4 || pgchip_kind(t) == g.page)
            g.list[g.list_count++] = t;
    g.list_cursor = savedata_chip_page_cursor(g.page);
    if (g.list_cursor < 0 || g.list_cursor >= g.list_count)
        g.list_cursor = 0;
}

static void open_list(void)
{
    g.page = savedata_chip_page();
    build_list();
    g.mode = M_SELECT;
}

static void update_list(void)
{
    int old_page = g.page, old = g.list_cursor;
    if (pad_pressed(PSP_CTRL_RTRIGGER))
        g.page = (g.page + 1) % CHIP_PAGES;
    else if (pad_pressed(PSP_CTRL_LTRIGGER))
        g.page = (g.page + CHIP_PAGES - 1) % CHIP_PAGES;
    if (g.page != old_page) {
        savedata_set_chip_page(g.page);
        build_list();
        sound_se(SE_CURSOR, 1.0f);
        return;
    }
    int x = g.list_cursor % LIST_COLS, y = g.list_cursor / LIST_COLS;
    if (pad_repeat(PSP_CTRL_LEFT) && x > 0)
        x--;
    else if (pad_repeat(PSP_CTRL_RIGHT) && x < LIST_COLS - 1 && g.list_cursor + 1 < g.list_count)
        x++;
    else if (pad_repeat(PSP_CTRL_UP) && y > 0)
        y--;
    else if (pad_repeat(PSP_CTRL_DOWN) && (y + 1) * LIST_COLS + x < g.list_count)
        y++;
    g.list_cursor = y * LIST_COLS + x;
    if (g.list_cursor != old) {
        savedata_set_chip_page_cursor(g.page, g.list_cursor);
        sound_se(SE_CURSOR, 1.0f);
    }
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = M_EDIT;
        return;
    }
    if (!pad_pressed(PSP_CTRL_CIRCLE) || !g.list_count)
        return;
    /* The chip comes from the remembered defaults (the select window's area is
     * built on them) and goes straight onto the cell (BeginModeEditConnection). */
    int x0, y0, p = cursor_area(&x0, &y0);
    PgChipCommand chip = *savedata_chip_default(g.list[g.list_cursor]);
    if (p >= 0 && param_has(pgchip_type(&chip))) { /* its parameters first (mode 0x13) */
        sound_se(SE_DECIDE, 1.0f);
        open_param(&chip, true);
        return;
    }
    backup();
    if (p < 0 || !pgchip_place(&g.sw, p, x0, y0, &chip)) {
        error_se();
        g.mode = M_EDIT;
        return;
    }
    SDL_Log("[soft] placed %s (0x%02X) at %d (%d, %d)", chip_name(pgchip_type(&chip)), pgchip_type(&chip), p, x0, y0);
    sound_se(SE_DECIDE, 1.0f);
    enter_connect();
}

static void enter_connect(void)
{
    g.branch_step = false;
    g.back_to = M_EDIT;
    g.mode = M_CONNECT;
}

/* ---- the exit step (UpdateModeEditConnection) ---- */

/* FUN_08a809ac: a direction from the d-pad (two keys make a diagonal). */
static int pad_direction(void)
{
    int dx = 0, dy = 0;
    bool fresh = pad_repeat(PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT);
    if (!fresh)
        return -1;
    if (pad_held(PSP_CTRL_LEFT))
        dx--;
    if (pad_held(PSP_CTRL_RIGHT))
        dx++;
    if (pad_held(PSP_CTRL_UP))
        dy--;
    if (pad_held(PSP_CTRL_DOWN))
        dy++;
    for (int d = 0; d < PGCHIP_DIRS; d++)
        if (pgchip_dir_step[d][0] == dx && pgchip_dir_step[d][1] == dy)
            return d;
    return -1;
}

static void finish_connect(void)
{
    const PgChipCommand *c = cursor_chip();
    if (c && pgchip_is_chip(c))
        savedata_set_chip_default(c); /* FUN_08a1cbd8 */
    g.mode = g.back_to;
}

static void update_connect(void)
{
    int x, y, p = cursor_area(&x, &y);
    PgChipCommand *c = cursor_chip();
    if (p < 0 || !c || !pgchip_is_chip(c)) {
        g.mode = M_EDIT;
        return;
    }
    bool judge = pgchip_kind(pgchip_type(c)) == 1;
    if (!g.branch_step) {
        if (pad_pressed(PSP_CTRL_CROSS)) {
            if (judge) { /* on to the branch */
                g.branch_step = true;
                sound_se(0xE, 1.0f);
            } else {
                sound_se(0xE, 1.0f);
                finish_connect();
            }
            return;
        }
        if (pad_pressed(PSP_CTRL_CIRCLE)) {
            sound_se(0x12, 1.0f);
            finish_connect();
            return;
        }
    } else {
        if (pad_pressed(PSP_CTRL_CROSS)) {
            sound_se(0xE, 1.0f);
            finish_connect();
            return;
        }
        if (pad_pressed(PSP_CTRL_CIRCLE)) { /* back to the default exit */
            g.branch_step = false;
            sound_se(0x12, 1.0f);
            return;
        }
    }
    bool turned = false;
    if (pad_pressed(PSP_CTRL_RTRIGGER))
        turned = pgchip_turn_exit(&g.sw, p, x, y, g.branch_step, 1);
    else if (pad_pressed(PSP_CTRL_LTRIGGER))
        turned = pgchip_turn_exit(&g.sw, p, x, y, g.branch_step, -1);
    else {
        int d = pad_direction();
        if (d >= 0)
            turned = pgchip_set_exit(&g.sw, p, x, y, g.branch_step, d);
    }
    if (turned)
        sound_se(0x10, 1.0f);
}

/* ---- Edit Chips (mode 3) ---- */

static void update_edit(void)
{
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = M_MENU;
        return;
    }
    int x, y, p = cursor_area(&x, &y);
    PgChipCommand *c = cursor_chip();
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        if (!c || !pgchip_is_chip(c)) {
            open_list();
        } else if (param_has(pgchip_type(c))) { /* its parameters (mode 0x13) */
            open_param(c, false);
        } else { /* a chip without parameters: its exits */
            g.branch_step = false;
            g.back_to = M_EDIT;
            g.mode = M_CONNECT;
        }
        return;
    }
    if (pad_pressed(PSP_CTRL_TRIANGLE)) { /* mode 6: select chips one by one */
        if (c && pgchip_is_chip(c)) {
            memset(g.picked, 0, sizeof g.picked);
            g.pick_program = p;
            g.picked[p][y * area_w(p) + x] = true;
            g.mode = M_PICK;
            sound_se(SE_DECIDE, 1.0f);
        } else {
            error_se();
        }
        return;
    }
    if (pad_pressed(PSP_CTRL_SQUARE)) { /* mode 0xD: drag / pull, not ported */
        SDL_Log("[soft] drag / pull (CPgChipWnd mode 6) isn't ported");
        error_se();
        return;
    }
    if (pad_pressed(PSP_CTRL_RTRIGGER) || pad_pressed(PSP_CTRL_LTRIGGER)) {
        bool branch = pad_pressed(PSP_CTRL_LTRIGGER);
        SoftwareDataBase keep = g.sw;
        if (p >= 0 && pgchip_turn_exit(&g.sw, p, x, y, branch, 1)) {
            g.undo = keep;
            g.has_undo = true;
            sound_se(0x10, 1.0f);
        }
        return;
    }
    int ox = g.cx, oy = g.cy;
    if (pad_repeat(PSP_CTRL_UP))
        move_cursor(0, -1);
    else if (pad_repeat(PSP_CTRL_DOWN))
        move_cursor(0, 1);
    else if (pad_repeat(PSP_CTRL_LEFT))
        move_cursor(-1, 0);
    else if (pad_repeat(PSP_CTRL_RIGHT))
        move_cursor(1, 0);
    if (g.cx != ox || g.cy != oy)
        sound_se(SE_CURSOR, 1.0f);
}

/* ---- selections: one by one (mode 6) and Select Area ---- */

static void cursor_moves(void)
{
    int ox = g.cx, oy = g.cy;
    if (pad_repeat(PSP_CTRL_UP))
        move_cursor(0, -1);
    else if (pad_repeat(PSP_CTRL_DOWN))
        move_cursor(0, 1);
    else if (pad_repeat(PSP_CTRL_LEFT))
        move_cursor(-1, 0);
    else if (pad_repeat(PSP_CTRL_RIGHT))
        move_cursor(1, 0);
    if (g.cx != ox || g.cy != oy)
        sound_se(SE_CURSOR, 1.0f);
}

static int pick_count(void)
{
    int n = 0, w = area_w(g.pick_program);
    for (int i = 0; i < w * w; i++)
        n += g.picked[g.pick_program][i];
    return n;
}

/* The picked chips as a block: their bounding box, other cells blank. */
static void clip_picked(int *x0, int *y0, int *x1, int *y1)
{
    int w = area_w(g.pick_program);
    *x0 = *y0 = w;
    *x1 = *y1 = -1;
    for (int i = 0; i < w * w; i++)
        if (g.picked[g.pick_program][i]) {
            int x = i % w, y = i / w;
            *x0 = x < *x0 ? x : *x0;
            *y0 = y < *y0 ? y : *y0;
            *x1 = x > *x1 ? x : *x1;
            *y1 = y > *y1 ? y : *y1;
        }
    PgChipArea a = pgchip_area(&g.sw, g.pick_program);
    pgchip_clip_take(&g.clip, &a, *x0, *y0, *x1, *y1);
    for (int y = *y0; y <= *y1; y++)
        for (int x = *x0; x <= *x1; x++)
            if (!g.picked[g.pick_program][y * w + x])
                memset(&g.clip.chips[(y - *y0) * g.clip.w + (x - *x0)], 0, sizeof(PgChipCommand));
}

static void update_pick(void)
{
    if (pick_count() < 1) { /* FUN_08a1b43c < 1: back to Edit Chips */
        g.mode = M_EDIT;
        return;
    }
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(0xE, 1.0f);
        g.mode = M_EDIT;
        return;
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        g.range_from_pick = true;
        g.range_cursor = 0;
        g.mode = M_RANGE_MENU;
        sound_se(SE_DECIDE, 1.0f);
        return;
    }
    int x, y, p = cursor_area(&x, &y);
    if (pad_pressed(PSP_CTRL_TRIANGLE)) {
        PgChipCommand *c = cursor_chip();
        if (p != g.pick_program || !c || !pgchip_is_chip(c)) {
            error_se();
            return;
        }
        bool *b = &g.picked[p][y * area_w(p) + x];
        *b = !*b;
        sound_se(*b ? SE_DECIDE : 0xE, 1.0f);
        return;
    }
    if (pad_pressed(PSP_CTRL_SQUARE)) { /* mode 0xC: delete the picked chips */
        backup();
        int w = area_w(g.pick_program);
        for (int i = 0; i < w * w; i++)
            if (g.picked[g.pick_program][i])
                pgchip_erase_rect(&g.sw, g.pick_program, i % w, i / w, i % w, i / w);
        sound_se(SE_DECIDE, 1.0f);
        g.mode = M_EDIT;
        return;
    }
    cursor_moves();
}

static void update_range(void)
{
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = M_MENU;
        return;
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        g.range_from_pick = false;
        g.range_cursor = 0;
        g.mode = M_RANGE_MENU;
        sound_se(SE_DECIDE, 1.0f);
        return;
    }
    /* The rectangle stays in the anchor's area. */
    int ox = g.cx, oy = g.cy, x, y;
    cursor_moves();
    int ax, ay;
    if (area_at(g.cx, g.cy, &x, &y) != area_at(g.ax, g.ay, &ax, &ay)) {
        g.cx = ox;
        g.cy = oy;
    }
}

static void range_rect(int *p, int *x0, int *y0, int *x1, int *y1)
{
    *p = area_at(g.ax, g.ay, x0, y0);
    area_at(g.cx, g.cy, x1, y1);
}

/* The menu of mode 0x11: Move, Copy, Delete, Save Macro (CSoftString 0x1C-0x1F). */
static void update_range_menu(void)
{
    int old = g.range_cursor;
    if (pad_repeat(PSP_CTRL_UP))
        g.range_cursor = (g.range_cursor + 3) % 4;
    else if (pad_repeat(PSP_CTRL_DOWN))
        g.range_cursor = (g.range_cursor + 1) % 4;
    if (g.range_cursor != old)
        sound_se(SE_CURSOR, 1.0f);
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = g.range_from_pick ? M_PICK : M_RANGE;
        return;
    }
    if (!pad_pressed(PSP_CTRL_CIRCLE))
        return;
    int p, x0, y0, x1, y1;
    if (g.range_from_pick) {
        p = g.pick_program;
        clip_picked(&x0, &y0, &x1, &y1);
    } else {
        range_rect(&p, &x0, &y0, &x1, &y1);
        PgChipArea a = pgchip_area(&g.sw, p);
        pgchip_clip_take(&g.clip, &a, x0, y0, x1, y1);
    }
    switch (g.range_cursor) {
    case 0: /* Move */
    case 1: /* Copy */
        g.moving = g.range_cursor == 0;
        g.src_program = p;
        g.sx0 = x0 < x1 ? x0 : x1;
        g.sy0 = y0 < y1 ? y0 : y1;
        g.sx1 = x0 < x1 ? x1 : x0;
        g.sy1 = y0 < y1 ? y1 : y0;
        place_cursor(p, g.sx0, g.sy0);
        g.mode = M_PASTE;
        sound_se(SE_DECIDE, 1.0f);
        break;
    case 2: /* Delete */
        backup();
        if (g.range_from_pick) {
            int w = area_w(p);
            for (int i = 0; i < w * w; i++)
                if (g.picked[p][i])
                    pgchip_erase_rect(&g.sw, p, i % w, i / w, i % w, i / w);
        } else {
            pgchip_erase_rect(&g.sw, p, x0, y0, x1, y1);
        }
        sound_se(SE_DECIDE, 1.0f);
        g.mode = M_EDIT;
        break;
    default: /* Save Macro: macros aren't ported */
        SDL_Log("[soft] Save Macro isn't ported");
        error_se();
        break;
    }
}

static void update_paste(void)
{
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = M_EDIT;
        return;
    }
    int x, y, p = cursor_area(&x, &y);
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        backup();
        if (g.moving) {
            if (g.range_from_pick) {
                int w = area_w(g.src_program);
                for (int i = 0; i < w * w; i++)
                    if (g.picked[g.src_program][i])
                        pgchip_erase_rect(&g.sw, g.src_program, i % w, i / w, i % w, i / w);
            } else {
                pgchip_erase_rect(&g.sw, g.src_program, g.sx0, g.sy0, g.sx1, g.sy1);
            }
        }
        pgchip_clip_paste(&g.sw, p, &g.clip, x, y);
        SDL_Log("[soft] %s %d x %d chips to %d (%d, %d)", g.moving ? "moved" : "copied", g.clip.w, g.clip.h, p, x, y);
        sound_se(SE_DECIDE, 1.0f);
        g.mode = M_EDIT;
        return;
    }
    if (pad_pressed(PSP_CTRL_TRIANGLE) || pad_pressed(PSP_CTRL_SQUARE) || pad_pressed(PSP_CTRL_LTRIGGER) ||
        pad_pressed(PSP_CTRL_RTRIGGER)) {
        SDL_Log("[soft] flipping and turning a block isn't ported");
        error_se();
        return;
    }
    cursor_moves();
}

/* ---- Move Start Location (mode 0xF) ---- */

static void update_start(void)
{
    int x, y, p = cursor_area(&x, &y);
    if (p < 0)
        p = 0;
    if (pad_pressed(PSP_CTRL_CIRCLE) || pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(pad_pressed(PSP_CTRL_CIRCLE) ? SE_DECIDE : SE_CANCEL, 1.0f);
        g.mode = M_MENU;
        return;
    }
    if (pad_pressed(PSP_CTRL_RTRIGGER) || pad_pressed(PSP_CTRL_LTRIGGER)) { /* the next area */
        int n = p;
        do
            n = (n + (pad_pressed(PSP_CTRL_RTRIGGER) ? 1 : PGCHIP_PROGRAM_MAX - 1)) % PGCHIP_PROGRAM_MAX;
        while (!area_w(n) && n != p);
        if (n != p) {
            place_cursor(n, g.sw.start[n], 0);
            sound_se(SE_CURSOR, 1.0f);
        }
        return;
    }
    int step = pad_repeat(PSP_CTRL_LEFT) ? -1 : pad_repeat(PSP_CTRL_RIGHT) ? 1 : 0;
    if (!step)
        return;
    SoftwareDataBase keep = g.sw;
    if (pgchip_move_start(&g.sw, p, step)) {
        g.undo = keep;
        g.has_undo = true;
        place_cursor(p, g.sw.start[p], 0);
        sound_se(SE_CURSOR, 1.0f);
    }
}

/* ---- Copy from Card ---- */

static bool card_private(int card)
{
    int32_t v;
    memcpy(&v, savedata_card(card)->header + 0x20, sizeof v);
    return v == 1;
}

static void update_cards(void)
{
    int old = g.card_cursor;
    if (pad_repeat(PSP_CTRL_UP))
        g.card_cursor = (g.card_cursor + SAVE_CARDS - 1) % SAVE_CARDS;
    else if (pad_repeat(PSP_CTRL_DOWN))
        g.card_cursor = (g.card_cursor + 1) % SAVE_CARDS;
    if (old != g.card_cursor) {
        sound_se(SE_CURSOR, 1.0f);
        g.message = NULL;
    }
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.message = NULL;
        g.mode = M_MENU;
        return;
    }
    if (!pad_pressed(PSP_CTRL_CIRCLE))
        return;
    int c = g.card_cursor;
    int st = savedata_card_status(c);
    if (c == g.card) {
        g.message = sstr(0x186);
    } else if (!savedata_card_used(c)) {
        g.message = sstr(0x184);
    } else if ((st == 2 || st == 3) && card_private(c)) {
        g.message = sstr(0x185);
    } else {
        SoftwareDataBase test = g.sw;
        bool lost = pgchip_copy_software(&test, &savedata_card(c)->software);
        g.ask_kind = ASK_COPY;
        ui_ask_open(&g.ask, sstr(lost ? 0x183 : 0x182), sstr(0x17F), sstr(0x180), 1);
        g.mode = M_ASK;
        sound_se(SE_DECIDE, 1.0f);
        return;
    }
    error_se();
}

/* ---- Change Counter Name ---- */

static void counter_name(int i, char *out, size_t n)
{
    char name[7] = {0};
    memcpy(name, g.sw.counter_name[i], 6);
    if (!name[0]) {
        const char *d = ptr_string(T_COUNTER_NAME, i);
        snprintf(out, n, "%s", d ? d : "");
        return;
    }
    snprintf(out, n, "%s", name);
}

static void update_counters(void)
{
    int old = g.counter_cursor;
    if (pad_repeat(PSP_CTRL_UP))
        g.counter_cursor = (g.counter_cursor + 7) % 8;
    else if (pad_repeat(PSP_CTRL_DOWN))
        g.counter_cursor = (g.counter_cursor + 1) % 8;
    if (old != g.counter_cursor)
        sound_se(SE_CURSOR, 1.0f);
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.message = NULL;
        g.mode = M_MENU;
        return;
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        char name[16];
        counter_name(g.counter_cursor, name, sizeof name);
        osk_open(sstr(0x17D), name, 5);
        g.mode = M_OSK;
        sound_se(SE_DECIDE, 1.0f);
    }
}

/* Counter names take A-Z and 0-9 (0x189 otherwise). */
static void update_osk(void)
{
    int r = osk_update();
    if (r == OSK_RUNNING)
        return;
    g.mode = M_COUNTERS;
    if (r != OSK_OK)
        return;
    const char *s = osk_text();
    for (const char *q = s; *q; q++)
        if (!((*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9'))) {
            g.message = sstr(0x189);
            error_se();
            return;
        }
    backup();
    memset(g.sw.counter_name[g.counter_cursor], 0, 6);
    strncpy(g.sw.counter_name[g.counter_cursor], s, 5);
    g.message = NULL;
    SDL_Log("[soft] counter %d named \"%s\"", g.counter_cursor + 1, s);
}

/* ---- the menu ---- */

static void update_menu(void)
{
    if (ui_menu_input(&g.menu))
        sound_se(SE_CURSOR, 1.0f);
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        close_editor();
        g.mode = -1;
        return;
    }
    if (!pad_pressed(PSP_CTRL_CIRCLE) || !g.menu.count)
        return;
    g.message = NULL;
    int x, y;
    switch (ui_menu_id(&g.menu)) {
    case ITEM_EDIT:
        g.mode = M_EDIT;
        break;
    case ITEM_UNDO: /* CPgChipWnd FUN_08a18c50: back one step */
        if (!g.has_undo) {
            error_se();
            return;
        }
        g.sw = g.undo;
        g.has_undo = false;
        break;
    case ITEM_AREA:
        if (cursor_area(&x, &y) < 0)
            place_cursor(0, 0, 0);
        g.ax = g.cx;
        g.ay = g.cy;
        g.mode = M_RANGE;
        break;
    case ITEM_START:
        place_cursor(0, g.sw.start[0], 0);
        g.mode = M_START;
        break;
    case ITEM_COUNTER:
        g.counter_cursor = 0;
        g.mode = M_COUNTERS;
        break;
    case ITEM_COPY:
        g.card_cursor = g.card;
        g.mode = M_CARDS;
        break;
    case ITEM_DELETE_ALL:
        g.ask_kind = ASK_DELETE_ALL;
        ui_ask_open(&g.ask, sstr(0x181), sstr(0x17F), sstr(0x180), 1);
        g.mode = M_ASK;
        break;
    case ITEM_EXAMPLE: /* Battle Mode has none (the item's limit word is off there) */
        g.message = sstr(0x2A2);
        error_se();
        return;
    default: /* Start Test Program, Paste Macro */
        SDL_Log("[soft] \"%s\" isn't ported", sstr(0x12 + ui_menu_id(&g.menu)));
        error_se();
        return;
    }
    sound_se(SE_DECIDE, 1.0f);
}

static void update_ask(float dt)
{
    int r = ui_ask_update(&g.ask, dt);
    if (r < 0)
        return;
    if (r == 1) {
        backup();
        if (g.ask_kind == ASK_DELETE_ALL) {
            pgchip_clear(&g.sw);
            SDL_Log("[soft] Delete All");
        } else {
            pgchip_copy_software(&g.sw, &savedata_card(g.card_cursor)->software);
            SDL_Log("[soft] software copied from card %d", g.card_cursor + 1);
        }
        sound_se(SE_DECIDE, 1.0f);
    } else {
        sound_se(SE_CANCEL, 1.0f);
    }
    g.mode = g.ask_kind == ASK_COPY && r != 1 ? M_CARDS : M_MENU;
}

int soft_update(float dt)
{
    g.t += dt;
    ui_menu_update(&g.menu, dt);
    g.menu.active = g.mode == M_MENU;
    switch (g.mode) {
    case M_MENU: update_menu(); break;
    case M_EDIT: update_edit(); break;
    case M_SELECT: update_list(); break;
    case M_CONNECT: update_connect(); break;
    case M_PARAM: update_param(dt); break;
    case M_PICK: update_pick(); break;
    case M_RANGE: update_range(); break;
    case M_RANGE_MENU: update_range_menu(); break;
    case M_PASTE: update_paste(); break;
    case M_START: update_start(); break;
    case M_ASK: update_ask(dt); break;
    case M_CARDS: update_cards(); break;
    case M_COUNTERS: update_counters(); break;
    case M_OSK: update_osk(); break;
    default: return 0;
    }
    if ((int)g.mode < 0)
        return 0;
    follow_cursor(dt);
    return 1;
}

/* ---- drawing the chips ---- */

typedef struct Sprite {
    int u, v, w, h;
} Sprite;

static Sprite glyph(int i)
{
    uint32_t a = T_GLYPH + 6u * (uint32_t)i;
    return (Sprite){rd16(a), rd16(a + 2), rd8(a + 4), rd8(a + 5)};
}

/* The 12 placements behind PTR_FUN_08c1ea78 (FUN_08a0caa8 ...), inside the 0x3E
 * square of a chip: centred, then upper-half and lower-half variants. */
static void glyph_pos(int layout, Sprite s, int *x, int *y)
{
    int h0 = glyph(0xAC).h;
    *x = (0x3E - s.w) / 2;
    switch (layout) {
    case 0: *y = (0x3E - s.h) / 2; break;
    case 1: *y = (0x1F - s.h) / 2; break;
    case 2: *y = 0x1F - s.h; break;
    case 3: *y = 0x1F - s.h + (s.h >> 3); break;
    case 4: *y = 0x1F - s.h + (s.h >> 2); break;
    case 5: case 10: *y = (0x1F - s.h) / 2 + 0x1F; break;
    case 6: *y = 0x1F; break;
    case 7: *y = 0x1F - (s.h >> 3); break;
    case 8: *y = 0x1F - (s.h >> 2); break;
    case 9: *y = (0x1F - s.h) / 3 + 0x1F; break;
    default: *y = (0x1F - h0) * 2 / 3 + 0x1F; break;
    }
}

/* Drawing in the areas' pixels: everything goes through the view's scale. */
static int sx(float x) { return VIEW_X + (int)((x - g.view_x) * SCALE); }
static int sy(float y) { return VIEW_Y + (int)((y - g.view_y) * SCALE); }
static int sl(int len) { return (int)(len * SCALE + 0.5f); }

static void draw_sprite(Picture *pic, Sprite s, float x, float y, uint32_t color)
{
    if (!pic || s.w <= 0 || s.h <= 0)
        return;
    picture_draw_ex(pic, sx(x), sy(y), 0, sl(s.w), sl(s.h), s.u, s.v, s.w, s.h, BLEND_ALPHA, color, 0, 0, 0, 0);
}

static void draw_glyph(int layout, int index, float x, float y, uint32_t color)
{
    if (index < 0)
        return;
    Sprite s = glyph(index);
    int gx, gy;
    glyph_pos(layout, s, &gx, &gy);
    draw_sprite(soft_pic(PIC_ICON), s, x + 1 + gx, y + 1 + gy, color);
}

/* FUN_08a0d94c: glyph text by the ASCII table, centred on a row. */
static void draw_glyph_text(const char *s, float x, float y, uint32_t color)
{
    int n = (int)strlen(s), w = 0;
    for (int i = 0; i < n; i++)
        w += glyph(rd16(T_GLYPH_ASCII + 2u * (uint8_t)(s[i] & 0x7F))).w;
    float px = x + (0x40 - w) / 2;
    for (int i = 0; i < n; i++) {
        Sprite gs = glyph(rd16(T_GLYPH_ASCII + 2u * (uint8_t)(s[i] & 0x7F)));
        draw_sprite(soft_pic(PIC_ICON), gs, px, y, color);
        px += gs.w;
    }
}

/* The main glyph of each type's face routine (the table at 0x08C1F150): a fixed
 * glyph, or one picked from a table by a parameter byte. The numbers and smaller
 * marks the routines add under it aren't drawn. */
typedef struct Face {
    int8_t layout;
    int16_t glyph;     /* -1: from the table */
    uint32_t table;    /* s32 glyphs by parameter */
    int8_t byte, shift; /* the parameter: (b[byte] >> shift) & 0xF, or the whole byte */
} Face;

static const Face k_face[PGCHIP_TYPE_END] = {
    [0x01] = {0, 0, 0, 0, 0},
    [0x02] = {0, 1, 0, 0, 0},
    [0x03] = {4, 2, 0, 0, 0},
    [0x04] = {3, 71, 0, 0, 0},
    [0x09] = {0, 34, 0, 0, 0},
    [0x0A] = {1, 48, 0, 0, 0},
    [0x0B] = {3, 22, 0, 0, 0},
    [0x0C] = {1, -1, ADDR(0x08C1EF84u), 6, 4},
    [0x0D] = {1, 109, 0, 0, 0},
    [0x0E] = {1, -1, ADDR(0x08C1EFACu), 6, -1},
    [0x0F] = {1, 68, 0, 0, 0},
    [0x10] = {3, 68, 0, 0, 0},
    [0x11] = {3, 69, 0, 0, 0},
    [0x12] = {1, 70, 0, 0, 0},
    [0x13] = {2, 73, 0, 0, 0},
    [0x14] = {1, 74, 0, 0, 0},
    [0x15] = {3, 72, 0, 0, 0},
    [0x16] = {3, 72, 0, 0, 0},
    [0x17] = {3, 75, 0, 0, 0},
    [0x18] = {3, 72, 0, 0, 0},
    [0x19] = {0, -1, ADDR(0x08C1F0F4u), 2, -1},
    [0x1C] = {0, 0x7B, 0, 0, 0},
    [0x1D] = {3, 3, 0, 0, 0},
    [0x1E] = {3, -1, ADDR(0x08C1EEF8u), 2, -1},
    [0x1F] = {3, -1, ADDR(0x08C1EF18u), 2, -1},
    [0x20] = {0, -1, ADDR(0x08C1EF20u), 2, -1},
    [0x21] = {0, -1, ADDR(0x08C1EF44u), 2, -1},
    [0x22] = {0, -1, ADDR(0x08C1EF64u), 2, -1},
    [0x23] = {0, -1, ADDR(0x08C1EF6Cu), 2, -1},
    [0x24] = {3, -1, ADDR(0x08C1EF7Cu), 2, -1},
    [0x25] = {4, 19, 0, 0, 0},
    [0x26] = {1, 49, 0, 0, 0},
    [0x27] = {1, 53, 0, 0, 0},
    [0x28] = {2, 125, 0, 0, 0},
    [0x29] = {2, 125, 0, 0, 0},
    [0x2A] = {2, 125, 0, 0, 0},
    [0x2B] = {2, 126, 0, 0, 0},
    [0x2C] = {3, 21, 0, 0, 0},
    [0x2D] = {4, 20, 0, 0, 0},
    [0x2E] = {1, 110, 0, 0, 0},
    [0x2F] = {3, 23, 0, 0, 0},
    [0x30] = {3, 93, 0, 0, 0},
    [0x32] = {0, 110, 0, 0, 0},
    [0x33] = {3, 127, 0, 0, 0},
    [0x34] = {0, 26, 0, 0, 0},
};

static int face_glyph(const PgChipCommand *c, int *layout)
{
    const Face *f = &k_face[pgchip_type(c)];
    *layout = f->layout;
    if (f->glyph >= 0)
        return f->glyph || f->layout || pgchip_type(c) == 1 ? f->glyph : -1;
    int v = c->b[f->byte];
    if (f->shift >= 0)
        v = (v >> f->shift) & 0xF;
    return rd32(f->table + 4u * (uint32_t)v);
}

/* The counter chips (FUN_08a0f660): the counter's name across the upper half. */
static bool counter_face(const PgChipCommand *c, float x, float y, uint32_t color)
{
    switch (pgchip_type(c)) {
    case 0x05: case 0x06: case 0x07: case 0x08: case 0x1A: case 0x31: break;
    default: return false;
    }
    int i = c->b[2] < 8 ? c->b[2] : 0;
    char name[16];
    counter_name(i, name, sizeof name);
    draw_glyph_text(name, x, y + 0x10, color);
    return true;
}

static Sprite tile_rec(uint32_t a)
{
    return (Sprite){rd16(a), rd16(a + 2), rd8(a + 4), rd8(a + 5)};
}

/* FUN_08a0c008: the chip's tile (blue / green / red / yellow by kind, the Return
 * tile), the faded row when it's switched off (bit 7 clear). */
static void draw_tile(const PgChipCommand *c, float x, float y)
{
    int t = pgchip_type(c);
    if (!pgchip_valid_type(t))
        return;
    int faded = !(c->b[0] & 0x80) && t != PGCHIP_RETURN;
    Sprite s = t == PGCHIP_RETURN ? tile_rec(T_TILE_RETURN + 6u * (uint32_t)faded)
                                  : tile_rec(T_TILE_KIND + 0x18u * (uint32_t)faded + 6u * (uint32_t)pgchip_kind(t));
    draw_sprite(soft_pic(PIC_CHIP), s, x, y, 0xFFFFFFFF);
    if (t == PGCHIP_RETURN)
        return;
    uint32_t color = faded ? 0x80FFFFFF : 0xFFFFFFFF;
    if (counter_face(c, x, y, color))
        return;
    int layout, gi = face_glyph(c, &layout);
    draw_glyph(layout, gi, x, y, color);
}

/* FUN_08a13468: an exit's arrow, from sd_cara0 by the processing mode and the
 * direction; the branch's arrows are the row 0x80 down, the faded ones 0x100 right. */
static void draw_arrow(const PgChipCommand *c, int dir, bool branch, float x, float y, bool faded)
{
    if (dir < 0)
        return;
    int attr = pgchip_attribute(c) ? 1 : 0;
    int k = rd32(T_ARROW_PICK + 0x20u * (uint32_t)attr + 4u * (uint32_t)dir);
    uint32_t a = T_ARROW + 10u * (uint32_t)k;
    Sprite s = {rd16(a) + (faded ? 0x100 : 0), rd16(a + 2) + (branch ? 0x80 : 0), rd8(a + 4), rd8(a + 5)};
    int dx = (int16_t)rd16(a + 6), dy = (int16_t)rd16(a + 8);
    draw_sprite(arrow_pic(), s, x + dx, y + dy, 0xFFFFFFFF);
}

static void draw_area(int program)
{
    PgChipArea a = pgchip_area(&g.sw, program);
    if (!a.width)
        return;
    int ox, oy;
    area_px_origin(program, &ox, &oy);
    int w = area_px(program);
    /* the area's panel (a plain one; CAreaWnd's own frame isn't ported) */
    ui_fill(sx(ox + 8), sy(oy + 8), sl(w - 16), sl(w - 16), 0x50303030);
    for (int y = 0; y < a.width; y++)
        for (int x = 0; x < a.width; x++) {
            int px, py;
            cell_px(program, x, y, &px, &py);
            const PgChipCommand *c = pgchip_at(&a, x, y);
            if (!pgchip_valid_type(pgchip_type(c))) {
                ui_fill(sx(px + 4), sy(py + 4), sl(CELL - 8), sl(CELL - 8), 0x28000000);
                draw_sprite(soft_pic(PIC_CHIP), tile_rec(T_TILE_BLANK), px, py, 0xFFFFFFFF);
            } else
                draw_tile(c, px, py);
        }
    /* the arrows over the chips */
    for (int y = 0; y < a.width; y++)
        for (int x = 0; x < a.width; x++) {
            const PgChipCommand *c = pgchip_at(&a, x, y);
            if (!pgchip_is_chip(c))
                continue;
            int px, py;
            cell_px(program, x, y, &px, &py);
            bool faded = !(c->b[0] & 0x80);
            draw_arrow(c, pgchip_exit_dir(c), false, px, py, faded);
            draw_arrow(c, pgchip_branch_dir(c), true, px, py, faded);
        }
    /* START over the start column (sd_start, CSoftPicture 0x15) */
    int px, py;
    cell_px(program, *a.start, 0, &px, &py);
    Picture *start = soft_pic(PIC_START);
    if (start)
        picture_draw_ex(start, sx(px), sy(py - 0x20), 0, sl(0x40), sl(0x20), 0, 0, picture_width(start),
                        picture_height(start), BLEND_ALPHA, 0xFFFFFFFF, 0, 0, 0, 0);
    /* the selections */
    if (g.mode == M_PICK && program == g.pick_program)
        for (int i = 0; i < a.width * a.width; i++)
            if (g.picked[program][i]) {
                cell_px(program, i % a.width, i / a.width, &px, &py);
                ui_fill(sx(px), sy(py), sl(CELL), sl(CELL), 0x6000C0FF);
            }
    int ax, ay;
    if ((g.mode == M_RANGE || (g.mode == M_RANGE_MENU && !g.range_from_pick)) && area_at(g.ax, g.ay, &ax, &ay) == program) {
        int bx, by;
        area_at(g.cx, g.cy, &bx, &by);
        int x0 = ax < bx ? ax : bx, x1 = ax < bx ? bx : ax, y0 = ay < by ? ay : by, y1 = ay < by ? by : ay;
        int qx, qy;
        cell_px(program, x0, y0, &qx, &qy);
        ui_fill(sx(qx - 4), sy(qy - 4), sl(PITCH * (x1 - x0) + CELL + 8), sl(PITCH * (y1 - y0) + CELL + 8), 0x5000C0FF);
    }
}

/* The block being moved or copied, where it would go. */
static void draw_paste(void)
{
    int x, y, p = cursor_area(&x, &y);
    if (p < 0)
        return;
    PgChipArea a = pgchip_area(&g.sw, p);
    for (int cy = 0; cy < g.clip.h; cy++)
        for (int cx = 0; cx < g.clip.w; cx++) {
            const PgChipCommand *c = &g.clip.chips[cy * g.clip.w + cx];
            if (!pgchip_is_chip(c) || !pgchip_in_area(&a, x + cx, y + cy))
                continue;
            int px, py;
            cell_px(p, x + cx, y + cy, &px, &py);
            draw_tile(c, px, py);
            ui_fill(sx(px), sy(py), sl(CELL), sl(CELL), 0x4000FF00);
        }
}

static void draw_cursor_box(int x, int y, int w, int h)
{
    ui_frame(ui_part(PART_BF_CUR0), x, y, w, h, 6, ui_pulse(g.t));
}

static void draw_areas(void)
{
    ui_frame(ui_part(PART_BF_SWND4), 4, 22, 472, 202, 8, 1.0f);
    sceGuScissor(VIEW_X, VIEW_Y - 4, VIEW_W, VIEW_H + 2);
    sceGuEnable(GU_SCISSOR_TEST);
    for (int p = 0; p < PGCHIP_PROGRAM_MAX; p++)
        draw_area(p);
    if (g.mode == M_PASTE)
        draw_paste();
    int x, y, p = cursor_area(&x, &y);
    if (p >= 0 && g.mode != M_MENU && g.mode != M_CARDS && g.mode != M_COUNTERS && g.mode != M_OSK) {
        int px, py;
        cell_px(p, x, y, &px, &py);
        draw_cursor_box(sx(px) - 3, sy(py) - 3, sl(CELL) + 6, sl(CELL) + 6);
    }
    sceGuScissor(0, 0, 480, 272);
    sceGuDisable(GU_SCISSOR_TEST);
}

/* The line over the help: the area and the chip under the cursor, with its error. */
static void draw_status(void)
{
    int x, y, p = cursor_area(&x, &y);
    if (p < 0)
        return;
    static const int k_area[3] = {0, 0xC2, 0xC3}; /* the sub-areas' names; the main area has none */
    char line[64];
    const PgChipCommand *c = cursor_chip();
    int t = c ? pgchip_type(c) : 0;
    snprintf(line, sizeof line, "%s%s(%d, %d)", sstr(k_area[p]), p ? " " : "", x + 1, y + 1);
    const int bx = 300, by = 28, bw = 172;
    ui_fill(bx, by, bw, 46, 0xC0202020);
    ui_text(line, style(10, 0xFFC0C0C0), bx + 6, by + 3, 1.0f);
    if (pgchip_valid_type(t))
        ui_text(chip_name(t), style(12, 0xFFFFFFFF), bx + 6, by + 16, 1.0f);
    PgChipArea a = pgchip_area(&g.sw, p);
    int err = pgchip_error(&a, p, pgchip_has_subs(&g.sw), x, y);
    if (err >= 0)
        ui_text(sstr(0x28D + err), style(10, 0xFF6060FF), bx + 6, by + 32, 1.0f);
}

/* ---- the windows over the areas ---- */

/* CPgChipSelectWnd: the page's title (CDesignString 0xCF + page), its chips 9 to a
 * row, the cursor and the chip's name. */
static void draw_list(void)
{
    const int x = 52, y = 30, w = 376, h = 192;
    ui_frame(ui_part(PART_BF_SWND4), x, y, w, h, 8, 1.0f);
    /* the page tabs: sd_tag0a-4a (CSoftPicture 0x17-0x1B), the current one in front;
     * the window behind them is CDesignPicture 0xCF-0xD3, which isn't drawn */
    for (int k = 0; k < CHIP_PAGES; k++) {
        int p = (g.page + 1 + k) % CHIP_PAGES; /* the current page last, on top */
        Picture *tab = soft_pic(0x17 + p);
        if (!tab)
            continue;
        picture_draw_ex(tab, x + 6 + p * 73, y + 5, 0, 92, 16, 0, 0, picture_width(tab), picture_height(tab),
                        BLEND_ALPHA, p == g.page ? 0xFFFFFFFF : 0xFF909090, 0, 0, 0, 0);
    }
    const float scale = 0.42f;
    const int pitch = 38, gx = x + 18, gy = y + 24;
    Picture *tiles = soft_pic(PIC_CHIP), *icons = soft_pic(PIC_ICON);
    for (int i = 0; i < g.list_count; i++) {
        int cx = gx + (i % LIST_COLS) * pitch, cy = gy + (i / LIST_COLS) * (pitch - 15);
        PgChipCommand c;
        pgchip_default(&c, g.list[i]);
        c.b[0] |= 0x80;
        int kind = pgchip_kind(g.list[i]);
        Sprite s = tile_rec(T_TILE_KIND + 6u * (uint32_t)kind);
        int d = (int)(0x40 * scale);
        if (tiles)
            picture_draw_ex(tiles, cx, cy, 0, d, d, s.u, s.v, s.w, s.h, BLEND_ALPHA, 0xFFFFFFFF, 0, 0, 0, 0);
        int layout, gi = face_glyph(&c, &layout);
        if (gi >= 0 && icons) {
            Sprite gs = glyph(gi);
            int ox, oy;
            glyph_pos(layout, gs, &ox, &oy);
            picture_draw_ex(icons, cx + (int)((1 + ox) * scale), cy + (int)((1 + oy) * scale), 0,
                            (int)(gs.w * scale + 0.5f), (int)(gs.h * scale + 0.5f), gs.u, gs.v, gs.w, gs.h, BLEND_ALPHA,
                            0xFFFFFFFF, 0, 0, 0, 0);
        }
        if (i == g.list_cursor)
            draw_cursor_box(cx - 3, cy - 3, d + 6, d + 6);
    }
    if (g.list_count)
        ui_text(chip_name(g.list[g.list_cursor]), style(12, 0xFF303030), x + 12, y + h - 22, 1.0f);
}

static void draw_range_menu(void)
{
    const int x = 150, y = 70;
    ui_frame(ui_part(PART_BF_SWND2), x, y, 120, 4 * 14 + 12, 8, 1.0f);
    for (int i = 0; i < 4; i++)
        ui_text(sstr(0x1C + i), style(12, i == 3 ? 0xFF909090 : 0xFF303030), x + 14, y + 6 + i * 14, 1.0f);
    draw_cursor_box(x + 8, y + 5 + g.range_cursor * 14, 104, 14);
}

static void draw_cards(void)
{
    const int x = 8, y = 26;
    ui_frame(ui_part(PART_BF_SWND4), x, y, 190, 196, 8, 1.0f);
    int first = g.card_cursor - 6 < 0 ? 0 : g.card_cursor - 6;
    if (first > SAVE_CARDS - 13)
        first = SAVE_CARDS - 13;
    for (int i = 0; i < 13; i++) {
        int c = first + i;
        char name[40], line[56];
        savedata_card_name(c, name, sizeof name);
        snprintf(line, sizeof line, "%02d %s", c + 1, savedata_card_used(c) ? name : "----");
        ui_text(line, style(10, 0xFF303030), x + 12, y + 8 + i * 14, 1.0f);
        if (c == g.card_cursor)
            draw_cursor_box(x + 6, y + 6 + i * 14, 178, 14);
    }
    oke_card_draw(g.card_cursor, 205, 22, true);
}

static void draw_counters(void)
{
    const int x = 150, y = 40;
    ui_frame(ui_part(PART_BF_SWND2), x, y, 150, 8 * 14 + 12, 8, 1.0f);
    for (int i = 0; i < 8; i++) {
        char name[16];
        counter_name(i, name, sizeof name);
        ui_text(sstr(0x12A + i), style(10, 0xFF606060), x + 12, y + 7 + i * 14, 1.0f);
        ui_text(name, style(12, 0xFF303030), x + 90, y + 6 + i * 14, 1.0f);
    }
    draw_cursor_box(x + 6, y + 5 + g.counter_cursor * 14, 138, 14);
}

void soft_draw(void)
{
    draw_areas();
    if (g.mode != M_MENU && g.mode != M_CARDS && g.mode != M_COUNTERS)
        draw_status();
    switch (g.mode) {
    case M_MENU:
        ui_menu_draw(&g.menu);
        break;
    case M_SELECT:
        draw_list();
        break;
    case M_PARAM:
        param_draw();
        break;
    case M_RANGE_MENU:
        draw_range_menu();
        break;
    case M_ASK:
        if (g.ask_kind == ASK_COPY)
            draw_cards();
        ui_ask_draw(&g.ask);
        break;
    case M_CARDS:
        draw_cards();
        break;
    case M_COUNTERS:
    case M_OSK:
        draw_counters();
        break;
    default:
        break;
    }
}

/* The help line: the menu item's, else the mode's (CSoftString 0x24A-0x257), the chip
 * under the cursor's in Edit Chips. */
const char *soft_help(void)
{
    if (g.message)
        return g.message;
    const PgChipCommand *c = cursor_chip();
    switch (g.mode) {
    case M_MENU: return g.menu.count ? sstr(g.menu.item[g.menu.cursor].help) : NULL;
    case M_EDIT: return c && pgchip_valid_type(pgchip_type(c)) ? chip_help(pgchip_type(c)) : sstr(0x24A);
    case M_SELECT: return g.list_count ? chip_help(g.list[g.list_cursor]) : sstr(0x24E);
    case M_CONNECT: return sstr(g.branch_step ? 0x252 : 0x251);
    case M_PARAM: {
        const char *h = param_help();
        return h ? h : chip_help(pgchip_type(param_chip()));
    }
    case M_PICK: return sstr(0x24D);
    case M_RANGE: return sstr(0x24C);
    case M_RANGE_MENU:
        return sstr((g.range_from_pick ? 0x240 : 0x23C) + g.range_cursor);
    case M_PASTE: return sstr(0x24B);
    case M_START: return sstr(0x235);
    case M_CARDS: return sstr(0x246);
    case M_COUNTERS: case M_OSK: return sstr(0x236);
    default: return NULL;
    }
}
