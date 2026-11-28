/* See oke_notes.h. Hand-written from CCommentWnd (FUN_088a6a10 .. FUN_088af394) and
 * CKeyOskWnd (FUN_0891bb64 .. FUN_0891d2f4).
 *
 * A card keeps ten notes (COkeDataBase + 0x1A40, accessors FUN_089ee7c4 ..
 * FUN_089ee97c): each has a tag, up to 10 button symbols stored as the bytes 1..14
 * in an 11-byte field at + 11 * k, and a text of up to 100 characters at
 * + 0x6E + 0x65 * k. The battle menu lists them by their tags.
 *
 * The list (CCommentWnd state 0, FUN_088abb58): the cursor walks the ten rows and
 * the two columns (FUN_088a971c); circle on the tag opens CKeyOskWnd, on the text
 * the system keyboard ("Enter Comments", 100 characters, FUN_08b2552c); triangle
 * marks a cell and starts selecting (state 3); square opens the menu of Paste
 * Template File / Copy from Card / Delete All (state 4); cross closes.
 * Selecting (FUN_088a868c): triangle marks and unmarks, cross unmarks all, circle
 * opens Move / Copy / Delete / Save Template File (controlSelected FUN_088abe74).
 * Move and Copy (FUN_088a8838): the marked cells, packed from the first marked row
 * (FUN_088a7178), follow the cursor over a backup of the notes (FUN_088a7430 /
 * FUN_088a7470, pasted by FUN_088a7314); circle drops them, cross puts them back
 * where they started. Move clears the originals first (FUN_088a6e3c).
 *
 * Every change goes into the card in the save data at once. */
#include "oke_notes.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "font.h"
#include "gamedata.h"
#include "gu.h"
#include "osk.h"
#include "pad.h"
#include "picture.h"
#include "psp_shim.h"
#include "savedata.h"
#include "sound.h"
#include "ui.h"

#define NOTES 10
#define TAG_SIZE 0xB    /* FUN_089ee7f0 takes fewer than 0xB bytes */
#define TAG_MAX 10      /* CKeyOskWnd FUN_0891bffc */
#define TEXT_OFF 0x6E
#define TEXT_SIZE 0x65  /* FUN_089ee8f8 takes fewer than 0x65 bytes */
#define TEXT_MAX 100

#define PART_BF_MENB2 0x08
#define PART_BF_MENL2 0x09

/* The window (FUN_088a6a10): from y 0x1B to 0xCB, the width of the screen less the
 * scroll bar's. The rows' constants (+0xD8 .. +0xE4, set with the pictures). */
#define WIN_Y 0x1B
#define WIN_BOTTOM 0xCB
#define WIN_W 0x1D0
#define ROW_TOP 9    /* +0xD8: the first row's top margin */
#define ROW_LAST 12  /* +0xE0 + +0xE4: below the last row */

/* EBOOT tables read at run time (file offsets). */
#define T_KEYS 0x0047B850u      /* CKeyOskWnd's 30 keys, 8 words: type, x, y, flags, up, down, left, right */
#define T_KEY_LOOK 0x0047B5F8u  /* per key type, 6 words: picture, picture selected, label u, v, w, h */
#define T_ICON 0x003F2698u      /* CKeyPicture's symbols, 4 bytes: u, v, w, h (0x08bf6698) */
#define S_ENTER_COMMENTS 0x003DCBD8u /* "Enter Comments" (0x08be0bd8) */
#define KEY_COUNT 30
#define KEY_START 0xC /* FUN_0891bca4 */

typedef enum {
    ST_LIST,     /* state 0 */
    ST_TAG,      /* state 1: CKeyOskWnd */
    ST_TEXT,     /* state 2: the system keyboard */
    ST_SELECT,   /* state 3, +0x9C -1: marking */
    ST_MENU_SEL, /* state 3, +0x9C 0: Move / Copy / Delete / Save Template File */
    ST_MOVING,   /* state 3, +0x9C 1 / 2 */
    ST_MENU_ALL, /* state 4: Paste Template File / Copy from Card / Delete All */
    ST_ASK,      /* Delete all notes? */
} State;

typedef struct {
    uint8_t tag[NOTES][TAG_SIZE];
    uint8_t text[NOTES][TEXT_SIZE];
    bool has_tag[NOTES], has_text[NOTES];
} Clip;

static struct {
    int card;
    bool read_only, closed;
    State state;
    int row, col;            /* +0x90, +0x8C */
    bool mark[2][NOTES];     /* +0xE8 (tags), +0x110 (texts) */
    int scroll;              /* how far the rows are pushed up */
    UiMenu menu;
    UiAsk ask;
    const char *message;
    /* Move / Copy */
    bool moving_is_move;
    int move_start;          /* +0x3D9C */
    Clip clip;               /* +0x38E8 */
    uint8_t backup[0x460];   /* +0x3488 */
    /* CKeyOskWnd */
    uint8_t key_text[TAG_SIZE + 1];
    int key_pos, key_cursor;
    Picture *key_pic[9], *key_icons, *list_icons, *row_left, *row_right;
} g;

static const char *dstr(int id)
{
    return design_string(id, 1);
}

static const char *nz(const char *s)
{
    return s ? s : "";
}

static const char *yes_no(int id) /* CBattleTestString 0x1F / 0x20 */
{
    uint32_t p = gamedata_word(0x00466D50u + 4u * (uint32_t)id, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

static uint8_t *notes_rw(void)
{
    return savedata_card_edit(g.card)->comments;
}

static const uint8_t *notes_ro(void)
{
    return savedata_card(g.card)->comments;
}

static const uint8_t *tag_of(int k)
{
    return notes_ro() + TAG_SIZE * k;
}

static const char *text_of(int k)
{
    return (const char *)notes_ro() + TEXT_OFF + TEXT_SIZE * k;
}

static int tag_len(int k) /* FUN_089ee86c */
{
    return (int)strnlen((const char *)tag_of(k), TAG_SIZE - 1);
}

static void set_tag(int k, const uint8_t *t) /* FUN_089ee7f0 */
{
    size_t n = strnlen((const char *)t, TAG_SIZE);
    if (n < TAG_SIZE)
        memcpy(notes_rw() + TAG_SIZE * k, t, n + 1);
}

static void set_text(int k, const char *t) /* FUN_089ee8f8 */
{
    size_t n = strnlen(t, TEXT_SIZE);
    if (n < TEXT_SIZE)
        memcpy(notes_rw() + TEXT_OFF + TEXT_SIZE * k, t, n + 1);
}

static Picture *load(Picture **slot, const char *name)
{
    if (!*slot)
        *slot = picture_load(name);
    return *slot;
}

void notes_open(int card, bool read_only)
{
    Picture *keep[13];
    memcpy(keep, g.key_pic, sizeof g.key_pic);
    keep[9] = g.key_icons, keep[10] = g.list_icons, keep[11] = g.row_left, keep[12] = g.row_right;
    memset(&g, 0, sizeof g);
    memcpy(g.key_pic, keep, sizeof g.key_pic);
    g.key_icons = keep[9], g.list_icons = keep[10], g.row_left = keep[11], g.row_right = keep[12];
    g.card = card;
    g.read_only = read_only;
    g.state = ST_LIST;
    static const char *const k_keys[9] = {
        "parts/hard/cm_bf01.gim",  "parts/hard/cm_bf01l.gim", "parts/hard/cm_bf02.gim",
        "parts/hard/cm_bf02l.gim", "parts/hard/cm_bf03.gim",  "parts/hard/cm_bf03l.gim",
        "parts/hard/cm_btn_b.gim", "parts/hard/cm_btn_l.gim", "parts/hard/cm_wnd_p.gim",
    };
    for (int i = 0; i < 9; i++)
        load(&g.key_pic[i], k_keys[i]);
    load(&g.key_icons, "parts/hard/cm_btn_p.gim");
    load(&g.list_icons, "parts/bf/cm_btn_p.gim");
    load(&g.row_left, "parts/hard/cm_wln0.gim");
    load(&g.row_right, "parts/hard/cm_wrn0.gim");
    SDL_Log("[notes] card %d%s", card + 1, read_only ? " (installed: view only)" : "");
}

/* ---- geometry (FUN_088aa304) ---- */

static int tag_width(int k)
{
    int n = tag_len(k);
    return 10 + (n < 1 ? 1 : n) * 0x18;
}

/* Half-width characters on a line: two for every 0xE pixels of the text cell. */
static int line_chars(int k)
{
    return ((WIN_W - tag_width(k) - 10) / 0xE) * 2;
}

static int text_lines(int k)
{
    int len = (int)strnlen(text_of(k), TEXT_SIZE - 1), per = line_chars(k);
    int n = per > 0 ? (len + per - 1) / per : 1;
    return n < 1 ? 1 : n;
}

static int row_height(int k)
{
    int h = text_lines(k) * 0xE + 10;
    return (h + 3) & ~3;
}

/* The top of row k, before scrolling. */
static int row_top(int k)
{
    int y = WIN_Y + ROW_TOP;
    for (int i = 0; i < k; i++)
        y += row_height(i);
    return y;
}

/* FUN_088ab724 keeps the cursor's row inside 0x1B .. 0xCB. */
static void follow_cursor(void)
{
    int top = row_top(g.row) - g.scroll, bottom = top + row_height(g.row) + (g.row == NOTES - 1 ? ROW_LAST : 0);
    if (g.row == 0)
        top -= ROW_TOP;
    if (top < WIN_Y)
        g.scroll -= WIN_Y - top;
    else if (bottom > WIN_BOTTOM)
        g.scroll += bottom - WIN_BOTTOM;
}

/* ---- the clipboard and the backup ---- */

static bool any_mark(void) /* FUN_088a6d74 */
{
    for (int c = 0; c < 2; c++)
        for (int k = 0; k < NOTES; k++)
            if (g.mark[c][k])
                return true;
    return false;
}

static int first_marked(void) /* FUN_088a6e00 */
{
    for (int k = 0; k < NOTES; k++)
        if (g.mark[0][k] || g.mark[1][k])
            return k;
    return -1;
}

static void unmark_all(void) /* FUN_088a6650 */
{
    memset(g.mark, 0, sizeof g.mark);
}

/* FUN_088a7178: the marked cells, from the first marked row on, keeping the gaps. */
static void gather(void)
{
    memset(&g.clip, 0, sizeof g.clip);
    int start = first_marked();
    if (start < 0)
        return;
    for (int k = start; k < NOTES; k++) {
        int to = k - start;
        if (g.mark[0][k]) {
            memcpy(g.clip.tag[to], tag_of(k), TAG_SIZE);
            g.clip.has_tag[to] = true;
        }
        if (g.mark[1][k]) {
            memcpy(g.clip.text[to], text_of(k), TEXT_SIZE);
            g.clip.has_text[to] = true;
        }
    }
}

static void delete_marked(void) /* FUN_088a6e3c with FUN_089ee754 / FUN_089ee788 */
{
    uint8_t *n = notes_rw();
    for (int k = 0; k < NOTES; k++) {
        if (g.mark[0][k])
            memset(n + TAG_SIZE * k, 0, TAG_SIZE);
        if (g.mark[1][k])
            memset(n + TEXT_OFF + TEXT_SIZE * k, 0, TEXT_SIZE);
    }
}

static void backup(void)
{
    memcpy(g.backup, notes_ro(), sizeof g.backup);
}

static void restore(void)
{
    memcpy(notes_rw(), g.backup, sizeof g.backup);
}

static void paste(int row) /* FUN_088a7314 */
{
    for (int k = row; k < NOTES; k++) {
        int from = k - row;
        if (g.clip.has_tag[from])
            set_tag(k, g.clip.tag[from]);
        if (g.clip.has_text[from])
            set_text(k, (const char *)g.clip.text[from]);
    }
}

/* ---- the menus ---- */

static void open_menu(State st)
{
    TextStyle s = text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0);
    ui_menu_init(&g.menu, 0x130, 0x40, 0x1D, 12, s);
    if (st == ST_MENU_SEL) /* the menu at +0x548: CDesignString 0x71 .. 0x74, help 0x12B .. */
        for (int i = 0; i < 4; i++)
            ui_menu_add(&g.menu, dstr(0x71 + i), i, 0x12B + i, PART_BF_MENB2, PART_BF_MENL2);
    else /* +0x4A0: 0x75 .. 0x77 with help 0x130, 0x133, 0x132 */
        for (int i = 0; i < 3; i++)
            ui_menu_add(&g.menu, dstr(0x75 + i), i, i == 0 ? 0x130 : i == 1 ? 0x133 : 0x132, PART_BF_MENB2,
                        PART_BF_MENL2);
    ui_menu_open(&g.menu);
    g.state = st;
}

/* ---- input ---- */

/* FUN_088a971c: up / down over the rows and left / right over the columns; a held
 * key stops at the ends, a fresh press wraps. */
static bool move_cursor(void)
{
    int row = g.row, col = g.col;
    if (pad_repeat(PSP_CTRL_UP)) {
        if (g.row > 0)
            g.row--;
        else if (pad_trigger(PSP_CTRL_UP))
            g.row = NOTES - 1;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        if (g.row < NOTES - 1)
            g.row++;
        else if (pad_trigger(PSP_CTRL_DOWN))
            g.row = 0;
    } else if (pad_repeat(PSP_CTRL_LEFT)) {
        if (g.col == 1)
            g.col = 0;
        else if (pad_trigger(PSP_CTRL_LEFT))
            g.col = 1;
    } else if (pad_repeat(PSP_CTRL_RIGHT)) {
        if (g.col == 0)
            g.col = 1;
        else if (pad_trigger(PSP_CTRL_RIGHT))
            g.col = 0;
    }
    if (row == g.row && col == g.col)
        return false;
    sound_se(SE_CURSOR, 1.0f);
    follow_cursor();
    return true;
}

static void toggle_mark(void)
{
    bool *m = &g.mark[g.col][g.row];
    *m = !*m;
    sound_se(*m ? SE_DECIDE : 0xE, 1.0f);
}

/* CKeyOskWnd: FUN_0891bb64 sets the text with the caret at its end. */
static void key_open(void)
{
    memset(g.key_text, 0, sizeof g.key_text);
    memcpy(g.key_text, tag_of(g.row), (size_t)tag_len(g.row));
    g.key_pos = (int)strlen((const char *)g.key_text);
    g.key_cursor = KEY_START;
    g.state = ST_TAG;
}

static int key_word(int key, int field)
{
    return (int)gamedata_word(T_KEYS + 32u * (uint32_t)key + 4u * (uint32_t)field, 0);
}

static void key_insert(uint8_t c) /* FUN_0891bffc */
{
    int n = (int)strlen((const char *)g.key_text);
    if (n >= TAG_MAX)
        return;
    memmove(g.key_text + g.key_pos + 1, g.key_text + g.key_pos, (size_t)(n - g.key_pos + 1));
    g.key_text[g.key_pos++] = c;
}

static void key_backspace(void) /* FUN_0891c118 */
{
    if (g.key_pos <= 0)
        return;
    int n = (int)strlen((const char *)g.key_text);
    memmove(g.key_text + g.key_pos - 1, g.key_text + g.key_pos, (size_t)(n - g.key_pos + 1));
    g.key_pos--;
}

static void key_right(void) /* FUN_0891c1dc: past the end it types a blank */
{
    if (g.key_pos == (int)strlen((const char *)g.key_text))
        key_insert(0xE);
    else
        g.key_pos++;
}

static void key_left(void) /* FUN_0891c228 */
{
    if (g.key_pos > 0)
        g.key_pos--;
}

/* The byte each symbol key types (FUN_0891c7a8's switch). */
static int key_char(int type)
{
    static const int8_t k[13] = {1, 2, 4, 3, 5, 6, 0xC, 7, 8, 9, 10, 0xB, 0xD};
    return type >= 0 && type < 13 ? k[type] : -1;
}

/* The symbol drawn for a stored byte (FUN_088aa268 / FUN_0891c440). */
static int char_icon(int c)
{
    static const int8_t k[15] = {-1, 0, 1, 3, 2, 4, 5, 8, 9, 10, 11, 7, 6, 12, 14};
    return c >= 0 && c < 15 ? k[c] : -1;
}

/* 1 OK, 0 cancel, -1 still open. */
static int key_update(void)
{
    int cur = g.key_cursor;
    static const struct { uint32_t pad; int field, flag; } dirs[4] = {
        {PSP_CTRL_UP, 4, 1}, {PSP_CTRL_DOWN, 5, 2}, {PSP_CTRL_LEFT, 6, 0x10}, {PSP_CTRL_RIGHT, 7, 0x20}};
    for (int d = 0; d < 4; d++)
        if (pad_repeat(dirs[d].pad)) {
            /* a flagged edge only moves on a fresh press */
            if (!(key_word(cur, 3) & dirs[d].flag) || pad_trigger(dirs[d].pad))
                g.key_cursor = key_word(cur, dirs[d].field) % KEY_COUNT;
            break;
        }
    if (g.key_cursor != cur)
        sound_se(SE_CURSOR, 1.0f);
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        int type = key_word(g.key_cursor, 0);
        if (key_char(type) > 0) {
            key_insert((uint8_t)key_char(type));
            sound_se(0x11, 1.0f);
        } else {
            switch (type) {
            case 0xF: g.key_pos = 0; break;
            case 0x10: g.key_pos = (int)strlen((const char *)g.key_text); break;
            case 0x11: key_left(); sound_se(SE_CURSOR, 1.0f); break;
            case 0x12: key_right(); sound_se(SE_CURSOR, 1.0f); break;
            case 0x13: key_backspace(); sound_se(0xE, 1.0f); break;
            case 0x14: key_insert(0xE); sound_se(0x11, 1.0f); break;
            case 0x15: return 1;
            case 0x16: return 0;
            default: break;
            }
        }
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        return 0;
    } else if (pad_repeat(PSP_CTRL_TRIANGLE)) {
        key_insert(0xE);
        sound_se(0x11, 1.0f);
    } else if (pad_repeat(PSP_CTRL_SQUARE)) {
        key_backspace();
        sound_se(0xE, 1.0f);
    } else if (pad_repeat(PSP_CTRL_LTRIGGER)) {
        key_left();
        sound_se(SE_CURSOR, 1.0f);
    } else if (pad_repeat(PSP_CTRL_RTRIGGER)) {
        key_right();
        sound_se(SE_CURSOR, 1.0f);
    } else if (pad_pressed(PSP_CTRL_START)) {
        return 1;
    }
    return -1;
}

static void update_list(void)
{
    move_cursor();
    if (g.read_only) { /* FUN_088abb58 with +0x94 set: only cross */
        if (pad_pressed(PSP_CTRL_CROSS)) {
            sound_se(SE_CANCEL, 1.0f);
            g.closed = true;
        }
        return;
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        if (g.col == 0) {
            key_open();
        } else {
            osk_open(gamedata_string(S_ENTER_COMMENTS), text_of(g.row), TEXT_MAX);
            g.state = ST_TEXT;
        }
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.closed = true;
    } else if (pad_pressed(PSP_CTRL_TRIANGLE)) {
        toggle_mark();
        g.state = ST_SELECT;
    } else if (pad_pressed(PSP_CTRL_SQUARE)) {
        sound_se(SE_DECIDE, 1.0f);
        unmark_all();
        open_menu(ST_MENU_ALL);
    }
}

static void update_select(void)
{
    move_cursor();
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        open_menu(ST_MENU_SEL);
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        unmark_all();
        sound_se(SE_CANCEL, 1.0f);
        g.state = ST_LIST;
    } else if (pad_pressed(PSP_CTRL_TRIANGLE)) {
        toggle_mark();
        if (!any_mark())
            g.state = ST_LIST;
    }
}

static void not_ported(int id)
{
    SDL_Log("[notes] \"%s\" isn't ported", nz(dstr(id)));
    g.message = dstr(id);
    sound_se(SE_ERROR, 1.0f);
}

static void update_menu_sel(float dt)
{
    ui_menu_update(&g.menu, dt);
    ui_menu_input(&g.menu);
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.state = ST_SELECT;
        return;
    }
    if (!pad_pressed(PSP_CTRL_CIRCLE))
        return;
    switch (ui_menu_id(&g.menu)) {
    case 0: /* Move */
        g.move_start = first_marked();
        gather();
        delete_marked();
        backup();
        g.row = g.move_start;
        restore();
        paste(g.row);
        g.moving_is_move = true;
        break;
    case 1: /* Copy */
        backup();
        gather();
        g.move_start = g.row = first_marked();
        restore();
        paste(g.row);
        g.moving_is_move = false;
        break;
    case 2: /* Delete */
        delete_marked();
        unmark_all();
        SDL_Log("[notes] card %d: deleted the selected notes", g.card + 1);
        sound_se(SE_DECIDE, 1.0f);
        g.state = ST_LIST;
        return;
    default: /* Save Template File: the templates live in the shared block's CCommentSceneDataBase */
        not_ported(0x74);
        return;
    }
    sound_se(SE_DECIDE, 1.0f);
    follow_cursor();
    g.state = ST_MOVING;
}

static void update_moving(void)
{
    if (move_cursor()) {
        restore();
        paste(g.row);
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        restore();
        paste(g.row);
        unmark_all();
        SDL_Log("[notes] card %d: %s the selected notes to note %d", g.card + 1, g.moving_is_move ? "moved" : "copied",
                g.row + 1);
        sound_se(SE_DECIDE, 1.0f);
        g.state = ST_LIST;
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        restore();
        paste(g.move_start);
        g.row = g.move_start;
        follow_cursor();
        sound_se(SE_CANCEL, 1.0f);
        open_menu(ST_MENU_SEL);
    }
}

static void update_menu_all(float dt)
{
    ui_menu_update(&g.menu, dt);
    ui_menu_input(&g.menu);
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.state = ST_LIST;
        return;
    }
    if (!pad_pressed(PSP_CTRL_CIRCLE))
        return;
    switch (ui_menu_id(&g.menu)) {
    case 2: /* Delete All: CDesignString 0x123 */
        sound_se(SE_DECIDE, 1.0f);
        ui_ask_open(&g.ask, dstr(0x123), yes_no(0x1F), yes_no(0x20), 1);
        g.state = ST_ASK;
        break;
    default: /* Paste Template File, Copy from Card */
        not_ported(0x75 + ui_menu_id(&g.menu));
        break;
    }
}

int notes_update(float dt)
{
    g.message = g.state == ST_MENU_SEL || g.state == ST_MENU_ALL ? g.message : NULL;
    switch (g.state) {
    case ST_LIST: update_list(); break;
    case ST_SELECT: update_select(); break;
    case ST_MENU_SEL: update_menu_sel(dt); break;
    case ST_MOVING: update_moving(); break;
    case ST_MENU_ALL: update_menu_all(dt); break;
    case ST_ASK: {
        int r = ui_ask_update(&g.ask, dt);
        if (r == 1) {
            memset(notes_rw(), 0, sizeof g.backup); /* FUN_089ee718 */
            SDL_Log("[notes] card %d: deleted all notes", g.card + 1);
        }
        if (r >= 0)
            g.state = ST_LIST;
        break;
    }
    case ST_TAG: {
        int r = key_update();
        if (r < 0)
            break;
        if (r == 1) { /* FUN_088a7968: the tag goes in, sound 0x12 */
            set_tag(g.row, g.key_text);
            SDL_Log("[notes] card %d: note %d tag of %d buttons", g.card + 1, g.row + 1, (int)strlen((char *)g.key_text));
            sound_se(0x12, 1.0f);
        } else {
            sound_se(SE_CANCEL, 1.0f);
        }
        follow_cursor();
        g.state = ST_LIST;
        break;
    }
    case ST_TEXT: {
        int r = osk_update();
        if (r == OSK_RUNNING)
            break;
        if (r == OSK_OK) { /* FUN_088a84e0: up to 0x65 bytes back into the note */
            char t[TEXT_SIZE];
            snprintf(t, sizeof t, "%s", osk_text());
            set_text(g.row, t);
            SDL_Log("[notes] card %d: note %d \"%s\"", g.card + 1, g.row + 1, t);
        }
        follow_cursor();
        g.state = ST_LIST;
        break;
    }
    }
    return g.closed ? 0 : 1;
}

/* ---- drawing ---- */

static const TextStyle *text_dark(void)
{
    static TextStyle s;
    static bool init;
    if (!init) {
        s = text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0);
        init = true;
    }
    return &s;
}

/* CKeyPicture::Draw (FUN_0891d2f4): symbol `icon` from the 4-byte rectangles. */
static void draw_icon(Picture *sheet, int icon, int x, int y, float alpha)
{
    if (!sheet || icon < 0 || icon > 14)
        return;
    uint32_t r = gamedata_word(T_ICON + 4u * (uint32_t)icon, 0);
    int u = r & 0xFF, v = r >> 8 & 0xFF, w = r >> 16 & 0xFF, h = r >> 24 & 0xFF;
    if (w && h)
        picture_draw_rect(sheet, x, y, 0, u, v, w, h, BLEND_ALPHA, (uint32_t)(alpha * 255) << 24 | 0xFFFFFF, 0, 0);
}

static void draw_row(int k, int y)
{
    int tw = tag_width(k), h = row_height(k);
    /* the two cells on cm_wln0 / cm_wrn0 */
    if (g.row_left)
        ui_frame(g.row_left, 0, y, tw, h, 10, 1.0f);
    else
        ui_fill(0, y, tw - 1, h - 1, 0xFFD8D8D8);
    if (g.row_right)
        ui_frame(g.row_right, tw, y, WIN_W - tw, h, 10, 1.0f);
    else
        ui_fill(tw, y, WIN_W - tw, h - 1, 0xFFF0F0F0);
    /* FUN_088aef1c: a marked cell is tinted, the cells waiting to be dropped too */
    float p = ui_pulse(SDL_GetTicks() / 1000.0f);
    if (g.mark[0][k] && g.state != ST_MOVING)
        ui_fill(3, y, tw - 6, h, (uint32_t)(p * 127) << 24 | 0x00C08000);
    if (g.mark[1][k] && g.state != ST_MOVING)
        ui_fill(tw, y, WIN_W - tw - 5, h, (uint32_t)(p * 127) << 24 | 0x00C08000);
    if (g.state == ST_MOVING && k >= g.row) {
        if (g.clip.has_tag[k - g.row])
            ui_fill(3, y, tw - 6, h, 0x400000FF);
        if (g.clip.has_text[k - g.row])
            ui_fill(tw, y, WIN_W - tw - 5, h, 0x400000FF);
    }
    /* FUN_088af394: the tag's symbols 0x18 apart from (6, 1), the text from (tw + 5, 5) */
    const uint8_t *t = tag_of(k);
    for (int i = 0; i < tag_len(k); i++)
        draw_icon(g.list_icons, char_icon(t[i]), 6 + i * 0x18, y + 1, 1.0f);
    const char *s = text_of(k);
    int len = (int)strnlen(s, TEXT_SIZE - 1), per = line_chars(k), ly = y + 5;
    for (int at = 0; at < len && per > 0; ly += 0xE) {
        char line[TEXT_SIZE];
        int n = 0;
        while (at + n < len && n < per) {
            unsigned char c = (unsigned char)s[at + n];
            int step = ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) ? 2 : 1; /* Shift-JIS */
            if (n + step > per)
                break;
            n += step;
        }
        if (n == 0)
            break;
        memcpy(line, s + at, (size_t)n);
        line[n] = 0;
        ui_text(line, text_dark(), tw + 5, ly, 1.0f);
        at += n;
    }
}

static void draw_keyboard(void)
{
    ui_fill(0, 0, 480, 272, 0x80000000); /* +0x178, alpha 0x80 */
    float p = ui_pulse(SDL_GetTicks() / 1000.0f);
    for (int i = 0; i < KEY_COUNT; i++) { /* FUN_0891ce30 */
        int type = key_word(i, 0), x = key_word(i, 1), y = key_word(i, 2);
        uint32_t look = T_KEY_LOOK + 24u * (uint32_t)type;
        bool sel = i == g.key_cursor;
        int pic = (int)gamedata_word(look, 0), pic_sel = (int)gamedata_word(look + 4, 0);
        int u = (int)gamedata_word(look + 8, 0), v = (int)gamedata_word(look + 12, 0);
        int w = (int)gamedata_word(look + 16, 0), h = (int)gamedata_word(look + 20, 0);
        if (pic >= 0 && pic < 6 && g.key_pic[pic])
            picture_draw(g.key_pic[pic], x, y, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
        if (sel && pic_sel >= 0 && pic_sel < 6 && g.key_pic[pic_sel])
            picture_draw(g.key_pic[pic_sel], x, y, 0, BLEND_ALPHA, (uint32_t)(p * 255) << 24 | 0xFFFFFF, 0, 0);
        Picture *label = g.key_pic[sel ? 7 : 6];
        if (label && w > 0 && h > 0)
            picture_draw_rect(label, x, y, 0, u, v, w, h, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    }
    if (g.key_pic[8])
        picture_draw(g.key_pic[8], 0xF6, 0x28, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    int n = (int)strlen((const char *)g.key_text);
    for (int i = 0; i < n; i++)
        draw_icon(g.key_icons, char_icon(g.key_text[i]), 0xFB + i * 0x15, 0x2D, 1.0f);
    ui_fill(0xFB + g.key_pos * 0x15, 0x2D, 2, 0x15, 0xFF00007F);
}

void notes_draw(void)
{
    sceGuScissor(0, WIN_Y, 480, WIN_BOTTOM - WIN_Y);
    sceGuEnable(GU_SCISSOR_TEST);
    for (int k = 0; k < NOTES; k++) {
        int y = row_top(k) - g.scroll;
        if (y > WIN_BOTTOM || y + row_height(k) < WIN_Y)
            continue;
        draw_row(k, y);
    }
    /* the cursor (+0x2B4) on the cell */
    int y = row_top(g.row) - g.scroll, tw = tag_width(g.row);
    if (g.state != ST_TAG && g.state != ST_TEXT) {
        if (g.col == 0)
            ui_frame(ui_part(PART_BF_CUR0), 3, y - 2, tw - 6 + 4, row_height(g.row) + 4, 6,
                     ui_pulse(SDL_GetTicks() / 1000.0f));
        else
            ui_frame(ui_part(PART_BF_CUR0), tw - 2, y - 2, WIN_W - tw - 5 + 4, row_height(g.row) + 4, 6,
                     ui_pulse(SDL_GetTicks() / 1000.0f));
    }
    sceGuScissor(0, 0, 480, 272);
    sceGuDisable(GU_SCISSOR_TEST);
    if (g.state == ST_MENU_SEL || g.state == ST_MENU_ALL)
        ui_menu_draw(&g.menu);
    if (g.state == ST_ASK)
        ui_ask_draw(&g.ask);
    if (g.state == ST_TAG)
        draw_keyboard();
}

const char *notes_help(void)
{
    if (g.message)
        return g.message;
    switch (g.state) {
    case ST_LIST:
    case ST_SELECT: return g.mark[g.col][g.row] ? dstr(0x12A) : dstr(0x128);
    case ST_MOVING: return dstr(0x129);
    case ST_MENU_SEL:
    case ST_MENU_ALL: return dstr(g.menu.item[g.menu.cursor].help);
    default: return NULL;
    }
}

bool notes_help_hidden(void)
{
    return g.state == ST_TAG || g.state == ST_TEXT;
}
