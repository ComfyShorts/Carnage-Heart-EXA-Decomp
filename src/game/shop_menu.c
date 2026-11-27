/* See shop_menu.h and docs/screens.md. Hand-written from:
 *   CShopMngr: Initialize FUN_08a73798 (the title "Shop" on banner 5, the windows),
 *     the update FUN_08a74058 (the mode switch on +0x28, the money window refreshed
 *     every frame), the top menu FUN_08a73d78 (categories 0-7 but 6, Program Chip,
 *     shown when FUN_08918e1c finds something for sale and the menu limit word
 *     0x5F + category is on; CDesignString 0xB0 + category; help 0x118 + id), a
 *     category chosen FUN_08a74510 / FUN_08a72be8, the per-category modes
 *     FUN_08a72a8c..FUN_08a73208 (the help text: CHardString id + 0xB bodies, + 0x30
 *     CPUs, + 0x96 additional armour, + 0x4D weapons, + 0x8A options; the Other
 *     items' own descriptions), buying FUN_08a733ac and the message closed
 *     FUN_08a7287c;
 *   CShopSelectWnd: the lists FUN_08a75b14 with their fillers FUN_08a75360..
 *     FUN_08a75760 (stock 1, for sale; bodies by class) and price tables, the input
 *     FUN_08a75ca4 (8 rows, circle buys, cross leaves), the draw FUN_08a7604c;
 *   the Other items (0x08c8a570, 16 bytes each: name, description, type, value) and
 *     their effects FUN_08a721a8.
 * The game's shop only buys; nothing in it sells (FUN_0889d338, the only place money
 * goes down, is called from FUN_08a733ac alone). */
#include "shop_menu.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "font.h"
#include "gamedata.h"
#include "gu.h"
#include "oke_parts.h"
#include "pad.h"
#include "sound.h"
#include "psp_shim.h"
#include "savedata.h"
#include "ui.h"

#define PART_BF_MENB5 0x0E
#define PART_BF_MENL5 0x0F
#define PART_BF_SWND5 0x9C
#define PART_BF_SW0T0 0xCA /* the money window: top, three middles, bottom */
#define PART_BF_SW0C0 0xCB
#define PART_BF_SW0B0 0xCE

/* splat addresses (Ghidra - 0x08804000) */
#define S_SHOP 0x004286D4u         /* "Shop" */
#define S_PURCHASED 0x004285D4u    /* "Purchased %s." */
#define S_NO_MONEY 0x004285E4u     /* "You don't have enough Victory Points." */
#define T_OTHER 0x00486570u        /* the Other items, 0x1F x 16 bytes */
#define T_BODY_CLASS 0x00410D80u   /* the body table's +0 word: the class 0-4 (FUN_089ee008) */
#define T_STAGE_NAMES 0x004812F8u  /* CPresetStage names, 0xAF */
#define T_STAGE_PART 0x00434FC4u   /* stage -> preview part (FUN_08aaf62c) */
#define LIMIT_SHOP 0x5F            /* CMenuLimitDataBase +0x17C (FUN_0895e7ac) */

#define CATEGORIES 8
#define CAT_CHIP 6  /* Program Chip: never offered */
#define CAT_OTHER 7
#define LIST_MAX 0x3D
#define ROWS 8
#define ROW_PITCH 16

/* The price tables FUN_08a75b14 picks, a word per item, by stock group. */
static const uint32_t k_price[CATEGORIES] = {
    0x00486760u, 0x004867F4u, 0x00486814u, 0x00486844u, 0x00486864u, 0x00486954u, 0, 0x00486980u,
};
/* The ids each list starts from (FUN_08a75440: CPUs from 0; additional armour,
 * weapons and options from 1, past "none"). */
static const uint8_t k_first[CATEGORIES] = {0, 0, 0, 1, 1, 1, 0, 0};
static const uint8_t k_count[CATEGORIES] = {OKE_BODIES, OKE_CPUS, OKE_ARMORS, OKE_ADD_ARMORS,
                                            OKE_WEAPONS, OKE_OPTIONS, 0x36, 0x1F};

typedef enum {
    MODE_TOP,     /* +0x28 == 0: the categories */
    MODE_LIST,    /* 1-8: a category's items */
    MODE_MESSAGE, /* +0x24 1 / 2: "Purchased ..." or not enough points */
} Mode;

static struct {
    Mode mode;
    UiMenu top;
    int cat; /* the category open, 0-7 */
    int ids[LIST_MAX], count, cursor, scroll;
    bool bought; /* the message is a purchase's (the list is rebuilt on closing it) */
    char message[100];
    bool done;
} g;

static const char *ptr_string(uint32_t table, int i)
{
    uint32_t p = gamedata_word(table + 4u * (uint32_t)i, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

static const char *nz(const char *s)
{
    return s ? s : "";
}

static const char *other_name(int i) { return ptr_string(T_OTHER + 16u * (uint32_t)i, 0); }   /* FUN_08a72150 */
static const char *other_text(int i) { return ptr_string(T_OTHER + 16u * (uint32_t)i, 1); }   /* FUN_08a7217c */
static int other_type(int i) { return (int)gamedata_word(T_OTHER + 16u * (uint32_t)i + 8u, 0xFFFFFFFFu); }
static int other_value(int i) { return (int)gamedata_word(T_OTHER + 16u * (uint32_t)i + 12u, 0); }

static int price(int cat, int id)
{
    return k_price[cat] ? (int)gamedata_word(k_price[cat] + 4u * (uint32_t)id, 0) : 0;
}

static const char *item_name(int cat, int id)
{
    switch (cat) {
    case 0: return oke_body_name(id);
    case 1: return oke_cpu_name(id);
    case 2: return oke_armor_name(id);
    case 3: return oke_add_armor_name(id);
    case 4: return oke_weapon_name(id);
    case 5: return oke_option_name(id);
    case CAT_OTHER: return other_name(id);
    default: return NULL;
    }
}

/* ---- the top menu ---- */

/* FUN_08918e1c: something in the group is for sale (stock 1). */
static bool on_sale(int cat)
{
    for (int id = 0; id < k_count[cat]; id++)
        if (savedata_stock(cat, id) == 1)
            return true;
    return false;
}

static bool category_shown(int cat)
{
    return cat != CAT_CHIP && on_sale(cat) && savedata_menu_limit(LIMIT_SHOP + cat) == 1;
}

/* FUN_08a73d78: the CTopMenuWnd on parts 0xE / 0xF / 0x85, below the title. Returns
 * the number of categories. */
static int build_top_menu(void)
{
    int keep = g.top.cursor;
    ui_menu_init(&g.top, 0, 0x18 + 4, 24 - 5, 12, text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0));
    for (int cat = 0; cat < CATEGORIES; cat++)
        if (category_shown(cat))
            ui_menu_add(&g.top, design_string(0xB0 + cat, 1), cat + 1, 0x118 + cat + 1, PART_BF_MENB5, PART_BF_MENL5);
    g.top.cursor = keep < g.top.count ? keep : g.top.count - 1;
    if (g.top.cursor < 0)
        g.top.cursor = 0;
    return g.top.count;
}

void shop_menu_open(void)
{
    memset(&g, 0, sizeof g);
    g.mode = MODE_TOP;
    if (build_top_menu() == 0) {
        SDL_Log("[shop] nothing for sale: the Shop closes");
        g.done = true;
        return;
    }
    ui_menu_open(&g.top);
    SDL_Log("[shop] Shop (CShopMngr): %d categories, %d Victory Points", g.top.count, savedata_money());
}

/* ---- a category's list ---- */

/* FUN_08a75b14 and its fillers: the items for sale; bodies class by class
 * (FUN_08a75360). */
static void build_list(void)
{
    g.count = 0;
    if (g.cat == 0) {
        for (int cls = 0; cls < 5; cls++)
            for (int b = 0; b < OKE_BODIES; b++)
                if ((int)gamedata_word(T_BODY_CLASS + 0x68u * (uint32_t)b, 0) == cls && savedata_stock(0, b) == 1)
                    g.ids[g.count++] = b;
        return;
    }
    for (int id = k_first[g.cat]; id < k_count[g.cat] && g.count < LIST_MAX; id++)
        if (savedata_stock(g.cat, id) == 1)
            g.ids[g.count++] = id;
}

static void open_list(int cat)
{
    g.cat = cat;
    g.cursor = g.scroll = 0;
    build_list();
    g.mode = MODE_LIST;
    g.top.active = false;
    SDL_Log("[shop] %s: %d item(s) for sale", nz(design_string(0xB0 + cat, 1)), g.count);
}

/* FUN_08a73698: back to the categories, rebuilt. */
static void back_to_top(void)
{
    g.mode = MODE_TOP;
    if (build_top_menu() == 0) {
        g.done = true;
        return;
    }
    ui_menu_open(&g.top);
}

/* FUN_08a721a8: what an Other item does. */
static void apply_other(int id)
{
    int type = other_type(id), value = other_value(id);
    switch (type) {
    case 2: /* a map: CBattleFieldStateDataBase (FUN_0882f578) */
        savedata_set_map_state(value, 1);
        SDL_Log("[shop] map %d (%s) can now be chosen", value, nz(ptr_string(T_STAGE_NAMES, value)));
        break;
    case 3: /* a menu item switched on */
        if ((value >> 16) == 1) { /* OKE Configuration's (FUN_0895e424) */
            savedata_set_menu_limit(LIMIT_DESIGN + (value & 0xFFFF), 1);
        } else if ((value >> 16) == 7 && (value & 0xFFFF) != 7) { /* the System Menu's (FUN_0895e8d4) */
            savedata_set_menu_limit(LIMIT_SYSTEM + (value & 0xFFFF), 1);
        }
        SDL_Log("[shop] menu item 0x%X switched on", value);
        break;
    default:
        /* 0 / 1: more OKE or team cards, in the Story Mode block (FUN_0889eac8 +
         * 0x2B74 / 0x2B78), which isn't ported. Battle Mode never sells these. */
        SDL_Log("[shop] Other item %d (type %d) isn't ported", id, type);
        break;
    }
}

/* FUN_08a733ac */
static void buy(void)
{
    int id = g.ids[g.cursor], cost = price(g.cat, id);
    if (savedata_money() < cost) {
        snprintf(g.message, sizeof g.message, "%s", nz(gamedata_string(S_NO_MONEY)));
        g.bought = false;
        sound_se(SE_ERROR, 1.0f); /* not enough Victory Points: 0xF */
        SDL_Log("[shop] %s: %d needed, %d held", nz(item_name(g.cat, id)), cost, savedata_money());
    } else {
        savedata_spend_money(cost);
        savedata_set_stock(g.cat, id, 2);
        if (g.cat == CAT_OTHER)
            apply_other(id);
        /* "Purchased %s.": the name put in place of the %s. */
        const char *fmt = nz(gamedata_string(S_PURCHASED)), *at = strstr(fmt, "%s");
        if (at)
            snprintf(g.message, sizeof g.message, "%.*s%s%s", (int)(at - fmt), fmt, nz(item_name(g.cat, id)), at + 2);
        else
            snprintf(g.message, sizeof g.message, "%s", nz(item_name(g.cat, id)));
        g.bought = true;
        sound_se(SE_OK, 1.0f); /* purchased: 0xD */
        SDL_Log("[shop] bought %s for %d, %d left", nz(item_name(g.cat, id)), cost, savedata_money());
    }
    g.mode = MODE_MESSAGE;
}

static void update_list(void)
{
    if (g.count <= 0) {
        back_to_top();
        return;
    }
    /* FUN_08a75ca4: up and down with repeat, wrapping on a fresh press. */
    int old_cursor = g.cursor;
    if (pad_repeat(PSP_CTRL_UP))
        g.cursor = g.cursor > 0 ? g.cursor - 1 : pad_trigger(PSP_CTRL_UP) ? g.count - 1 : 0;
    else if (pad_repeat(PSP_CTRL_DOWN))
        g.cursor = g.cursor < g.count - 1 ? g.cursor + 1 : pad_trigger(PSP_CTRL_DOWN) ? 0 : g.cursor;
    if (g.cursor != old_cursor)
        sound_se(SE_CURSOR, 1.0f);
    if (g.cursor < g.scroll)
        g.scroll = g.cursor;
    if (g.cursor >= g.scroll + ROWS)
        g.scroll = g.cursor - ROWS + 1;
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        buy();
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        back_to_top();
    }
}

/* FUN_08a7287c: circle or cross closes the message; after a purchase the list is
 * rebuilt, and an empty one goes back to the categories. */
static void update_message(void)
{
    if (!pad_pressed(PSP_CTRL_CIRCLE) && !pad_pressed(PSP_CTRL_CROSS))
        return;
    sound_se(SE_CANCEL, 1.0f);
    g.mode = MODE_LIST;
    if (!g.bought)
        return;
    build_list();
    if (g.count <= 0) {
        back_to_top();
        return;
    }
    if (g.cursor >= g.count)
        g.cursor = g.count - 1;
    if (g.scroll > 0 && g.scroll + ROWS > g.count)
        g.scroll = SDL_max(0, g.count - ROWS);
}

static void update_top(void)
{
    if (!g.top.count)
        return;
    ui_menu_input(&g.top);
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        open_list(ui_menu_id(&g.top) - 1);
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.done = true;
    }
}

int shop_menu_update(float dt)
{
    ui_menu_update(&g.top, dt);
    if (!g.done) {
        g.top.active = g.mode == MODE_TOP;
        switch (g.mode) {
        case MODE_TOP: update_top(); break;
        case MODE_LIST: update_list(); break;
        case MODE_MESSAGE: update_message(); break;
        }
    }
    if (g.done) {
        SDL_Log("[shop] back to the Battle Mode menu, %d Victory Points", savedata_money());
        return 0;
    }
    return 1;
}

/* ---- drawing ---- */

static const TextStyle *dark(void)
{
    static TextStyle st;
    st = text_style(0, 6, 12, 12, 0, 4, 0xFF303030, 0);
    return &st;
}

static const TextStyle *small(void)
{
    static TextStyle st;
    st = text_style(0, 5, 10, 10, 0, 4, 0xFF303030, 0);
    return &st;
}

static void text_right(const char *s, const TextStyle *st, int right, int y)
{
    ui_text(s, st, right - ui_text_width(s, st), y, 1.0f);
}

static void label_value(const char *label, const char *value, int x, int y, int vx)
{
    ui_text(nz(label), small(), x, y, 1.0f);
    ui_text(nz(value), small(), vx, y, 1.0f);
}

/* The money window (FUN_0885e0c4: parts 0xCA-0xCE, five digits), at the right
 * edge (0x1E0 - its width, y 2). */
static void draw_money(void)
{
    Picture *top = ui_part(PART_BF_SW0T0), *mid = ui_part(PART_BF_SW0C0), *bot = ui_part(PART_BF_SW0B0);
    int w = top ? picture_width(top) : 64, x = 480 - w - 4, y = 2;
    if (top && mid && bot) {
        ui_pic(top, x, y, 1.0f, false);
        ui_pic(mid, x, y + 16, 1.0f, false);
        ui_pic(bot, x, y + 24, 1.0f, false);
    } else {
        ui_fill(x, y, w, 40, 0xC0F0F0F0);
    }
    char v[16];
    snprintf(v, sizeof v, "%5d", savedata_money());
    text_right(v, dark(), x + w - 8, y + 14);
}

/* CShopSelectWnd (part 0x9C, bf_swnd5): eight rows of 16, the name at +0x1E, the
 * price at +0xA4; the cursor bar 0xD4 x 0xC at (8, row * 16 + 8). */
static void draw_list(int x, int y)
{
    const int w = 248, h = 144;
    ui_frame(ui_part(PART_BF_SWND5), x, y, w, h, 8, 1.0f);
    for (int r = 0; r < ROWS && g.scroll + r < g.count; r++) {
        int i = g.scroll + r, id = g.ids[i], ry = y + 8 + r * ROW_PITCH;
        if (i == g.cursor && g.mode == MODE_LIST)
            ui_frame(ui_part(PART_BF_CUR0), x + 8 - 2, ry - 2, 0xD4 + 4, ROW_PITCH, 6,
                     ui_pulse(SDL_GetTicks() / 1000.0f));
        bool afford = price(g.cat, id) <= savedata_money();
        TextStyle st = text_style(0, 6, 12, 12, 0, 4, afford ? 0xFF303030 : 0xFF909090, 0);
        const char *name = nz(item_name(g.cat, id));
        TextStyle narrow = text_style(0, 5, 10, 12, 0, 4, st.color, 0);
        bool fits = ui_text_width(name, &st) <= 0xA4 - 0x1E + 0x30 - 34;
        ui_text(name, fits ? &st : &narrow, x + 0x1E, ry + 1, 1.0f);
        char v[16];
        snprintf(v, sizeof v, "%d", price(g.cat, id));
        text_right(v, &st, x + 0xA4 + 0x30, ry + 1);
    }
    if (g.count > ROWS) {
        /* the scroll bar (FUN_088b28d0 at x 0xE0, 0x8D high) */
        int bar = h - 12, kh = SDL_max(8, bar * ROWS / g.count);
        int ky = y + 6 + (bar - kh) * g.scroll / SDL_max(1, g.count - ROWS);
        ui_fill(x + 0xE0, y + 6, 4, bar, 0x40000000);
        ui_fill(x + 0xE0, ky, 4, kh, 0xC0303030);
    }
}

/* The windows at the right (0x1E0 - width, y 0x23): the part's numbers (the parts
 * data window FUN_088de8e0) or, for a map, its preview (FUN_0882f6a4). */
static void draw_detail(int x, int y)
{
    if (g.count <= 0)
        return;
    int id = g.ids[g.cursor], w = 476 - x, lx = x + 10, vx = x + 108, ly = y + 10;
    ui_frame(ui_part(PART_BF_SWND5), x, y, w, 150, 8, 1.0f);
    ui_text(nz(item_name(g.cat, id)), dark(), lx, ly, 1.0f);
    ly += 18;
    char v[48];
    const OkeDataBase *card = savedata_card(savedata_current_card());
    bool used = savedata_card_used(savedata_current_card());
    switch (g.cat) {
    case 0:
        snprintf(v, sizeof v, "%d", body_durability(id));
        label_value(design_string(0x31, 1), v, lx, ly, vx);
        snprintf(v, sizeof v, "%d", body_weight(id));
        label_value(design_string(0x32, 1), v, lx, ly += 11, vx);
        snprintf(v, sizeof v, "%d", body_payload(id));
        label_value(design_string(0x33, 1), v, lx, ly += 11, vx);
        snprintf(v, sizeof v, "%d", body_weapons(id));
        label_value(design_string(0x34, 1), v, lx, ly += 11, vx);
        snprintf(v, sizeof v, "%d", body_energy(id));
        label_value(design_string(0x35, 1), v, lx, ly += 11, vx);
        snprintf(v, sizeof v, "%d", body_heat(id));
        label_value(design_string(0x36, 1), v, lx, ly += 11, vx);
        break;
    case 1:
        snprintf(v, sizeof v, "%d x %d", cpu_program_width(id, 0), cpu_program_width(id, 0));
        label_value(design_string(0x37, 1), v, lx, ly, vx);
        snprintf(v, sizeof v, "%d", cpu_weight(id));
        label_value(design_string(0x39, 1), v, lx, ly += 11, vx);
        break;
    case 2:
    case 3:
        if (used) { /* the weight depends on the body: the current card's */
            snprintf(v, sizeof v, "%d", g.cat == 2 ? armor_weight(card, id) : add_armor_weight(card, id));
            label_value(design_string(g.cat == 2 ? 0x45 : 0x47, 1), v, lx, ly, vx);
        }
        break;
    case 4:
        ui_text(nz(design_string(0x3A, 1)), small(), lx, ly, 1.0f); /* the type on its own line: names run long */
        ui_text(nz(oke_category_name(weapon_category(id))), small(), lx + 8, ly + 11, 1.0f);
        break;
    case 5:
        snprintf(v, sizeof v, "%d", option_weight(id));
        label_value(design_string(0x51, 1), v, lx, ly, vx);
        break;
    case CAT_OTHER:
        if (other_type(id) == 2) {
            int stage = other_value(id);
            Picture *pv = ui_part((int)gamedata_word(T_STAGE_PART + 4u * (uint32_t)stage, 0));
            ui_text(nz(ptr_string(T_STAGE_NAMES, stage)), small(), lx, ly, 1.0f);
            if (pv)
                picture_draw_ex(pv, lx, ly + 14, 0, 96, 96, 0, 0, picture_width(pv), picture_height(pv), BLEND_ALPHA,
                                0xFFFFFFFF, 0, 0, 0, 0);
        }
        break;
    }
}

static void draw_message(void)
{
    const int w = 300, h = 44, x = (480 - w) / 2, y = 100;
    ui_frame(ui_part(PART_BF_SWND4), x, y, w, h, 8, 1.0f);
    ui_text(g.message, dark(), x + (w - ui_text_width(g.message, dark())) / 2, y + 16, 1.0f);
}

static const char *help_text(void)
{
    if (g.mode == MODE_TOP)
        return g.top.count ? design_string(g.top.item[g.top.cursor].help, 1) : NULL;
    if (g.count <= 0)
        return NULL;
    int id = g.ids[g.cursor];
    switch (g.cat) {
    case 0: return hard_string(0x0B + id);
    case 1: return hard_string(0x30 + id);
    case 3: return hard_string(0x96 + id);
    case 4: return hard_string(0x4D + id);
    case 5: return hard_string(0x8A + id);
    case CAT_OTHER: return other_text(id);
    default: return NULL; /* armour: FUN_08a72eac sets none */
    }
}

void shop_menu_draw(void)
{
    sceGuEnable(GU_BLEND);
    Picture *back = ui_part(PART_BF_BACK);
    if (back)
        picture_draw(back, 0, 0, 0, BLEND_REPLACE, 0xFFFFFFFF, 0, 0);
    else
        ui_fill(0, 0, 480, 272, 0xFF404040);
    /* The title (FUN_0895ef54(.., "Shop", 5, 1)): banner bf_ttl5. */
    Picture *banner = ui_part(PART_BF_TTL0 + 5);
    ui_pic(banner, 0, 0, 1.0f, false);
    TextStyle tst = text_style(0, 7, 14, 14, 0, 0, 0xFFFFFFFF, 0);
    ui_text(nz(gamedata_string(S_SHOP)), &tst, 64, (banner ? picture_height(banner) : 24) / 2 - 8, 1.0f);
    draw_money();
    if (g.mode == MODE_TOP) {
        ui_menu_draw(&g.top);
    } else {
        draw_list(2, 0x18 + 2);
        draw_detail(256, 0x23);
        if (g.mode == MODE_MESSAGE)
            draw_message();
    }
    ui_help_draw(g.mode == MODE_MESSAGE ? NULL : help_text());
    sceGuEnable(GU_TEXTURE_2D);
}
