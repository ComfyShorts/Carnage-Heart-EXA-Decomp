/* See oke_menu.h and docs/screens.md. Hand-written from:
 *   CDesignFlow FUN_088b94f0: the top menu's item starts a sub-flow (0 the card
 *     select, 1 CHardDesignFlow FUN_088f26c4, 2 CSoftDesignFlow FUN_08a7868c);
 *   CDesignMngr: constructor FUN_088bd918, setupTopMenu FUN_088b9c28 (items from
 *     DAT_08bfe348, text CDesignString id + 0x18, help id + 0x23, the menu-limit block
 *     FUN_0895e3c4 that Battle Mode fills with FUN_0895de04: Snapshot off; an
 *     installed card leaves only Notes, FUN_089eea58), the OKE Customization
 *     sub-menu FUN_088b9edc;
 *   COkeCard::drawHardware FUN_089e6324 for what the card window lists (oke_card.c);
 *   CHardDesignMngr: the mode switches FUN_088f301c / FUN_088f3248, the category
 *     menu FUN_088f6748 / FUN_088f7b00 built by FUN_088f9694 (CHardString 0xCF + item;
 *     on a blank card everything but Body disabled), the part lists of
 *     CEquipmentSelectWnd (FUN_088da8b8) and the confirms in oke_parts.c;
 *   CSoftDesignMngr: the chip grid, viewed only (chip names from PGCHIP's table at
 *     0x08c85100).
 * OKE Customization (oke_custom.c), Emblems (oke_emblem.c), Name OKE (the stand-in
 * keyboard osk.c), Notes (oke_notes.c) and Privacy Level (CSecretLevelWnd, here);
 * the card itself is COkeCard (oke_card.c). Snapshot and chip editing aren't ported. */
#include "oke_menu.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "font.h"
#include "gamedata.h"
#include "gu.h"
#include "oke_card.h"
#include "oke_custom.h"
#include "oke_emblem.h"
#include "oke_notes.h"
#include "oke_parts.h"
#include "osk.h"
#include "pad.h"
#include "sound.h"
#include "psp_shim.h"
#include "savedata.h"
#include "ui.h"

#define PART_BF_MENB2 0x08
#define PART_BF_MENL2 0x09
#define PART_BF_SWND2 0x99
#define PART_BF_SWND5 0x9C
#define PART_OC_HDBG 0xC6
#define PART_OP_SVF_L 0x44 /* the gauge */
#define PART_BF_TWND2 0x9E /* the slider and colour windows */
#define PART_BF_CUR7 0xDD  /* the gauge's fill */

#define NAME_MAX_INPUT 12  /* FUN_08b2552c's limit for kind 1, the OKE name */
#define CARD_NAME_SIZE 0x1C /* COkeDataBase + 4 .. + 0x20 */

#define T_CHIP_NAME 0x00481100u /* PGCHIP's chip names, 0x36 pointers (0x08c85100) */
#define T_CHIP_KIND 0x00418BE4u /* PGCHIP's chip kinds, 0x36 bytes (0x08c1cbe4, FUN_089fd528) */

/* The top menu's items (DAT_08bfe348). */
enum {
    ITEM_SELECT_CARD = 0,
    ITEM_HARDWARE = 1,
    ITEM_SOFTWARE = 2,
    ITEM_CUSTOMIZE = 3,
    ITEM_EMBLEMS = 4,
    ITEM_SNAPSHOT = 5,
    ITEM_NAME = 6,
    ITEM_NOTES = 7,
    ITEM_PRIVACY = 8,
    ITEM_COUNT = 9,
};

typedef enum {
    MODE_TOP,
    MODE_CARDS,      /* Select OKE Card */
    MODE_COPY,       /* ... choosing where to copy */
    MODE_ASK,        /* Delete this design? / Copy OKE Card here? */
    MODE_HARDWARE,   /* CHardDesignMngr mode 1: the categories */
    MODE_PARTS,      /* modes 2-0xE: one category's parts */
    MODE_SOFTWARE,   /* CSoftDesignMngr: the program grid */
    MODE_SLIDER,     /* CHardDesignMngr modes 0x10 / 0x11: Energy Rate, Engine Output */
    MODE_NAME,       /* CDesignMngr mode 9: Name Input, the on-screen keyboard */
    MODE_PRIVACY,    /* mode 0xB: CSecretLevelWnd */
    MODE_NOTES,      /* mode 10: CCommentWnd (oke_notes.c) */
    MODE_CUSTOM,     /* modes 2-6: OKE Customization (oke_custom.c) */
    MODE_EMBLEM,     /* mode 7: CEmblemEditor (oke_emblem.c) */
} Mode;

typedef enum { ASK_DELETE, ASK_COPY } AskKind;

static struct {
    Mode mode;
    UiMenu top;
    int card_cursor, card_scroll;
    int copy_from;
    AskKind ask_kind;
    UiAsk ask;
    int hw_items[HWI_COUNT], hw_count, hw_cursor;
    int parts[PART_LIST_MAX], part_count, part_cursor, part_scroll;
    int program, chip_x, chip_y;
    int slider_item, slider_value, slider_keep; /* the tuning slider (CTuneWnd's +0xB4 / +0xB8) */
    int privacy;     /* CSecretLevelWnd +0xB0: 0 Public, 1 Private */
    const char *message;
    bool closing;
} g;

static const char *dstr(int id)
{
    return design_string(id, 1);
}

static const char *chip_name(int type)
{
    if (type < 0 || type >= PGCHIP_TYPE_END)
        return NULL;
    uint32_t p = gamedata_word(T_CHIP_NAME + 4u * (uint32_t)type, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

/* The Yes / No of the questions: CBattleTestString 0x1F / 0x20, as Battle Mode's
 * other questions use. */
static const char *yes_no(int id)
{
    uint32_t p = gamedata_word(0x00466D50u + 4u * (uint32_t)id, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

/* FUN_089fd528: 0 system chips, 1 judges (two exits), 2 actions, 3 the rest,
 * -1 none and Return. */
static int chip_kind(int type)
{
    if (type <= 0 || type >= PGCHIP_TYPE_END)
        return -1;
    uint32_t a = T_CHIP_KIND + (uint32_t)type;
    return (int8_t)(gamedata_word(a & ~3u, 0xFFFFFFFFu) >> ((a & 3) * 8));
}

static const char *nz(const char *s)
{
    return s ? s : "";
}

static int current(void)
{
    return savedata_current_card();
}

#define CARD_PRIVACY 0x20 /* COkeDataBase + 0x20: 0 public, 1 private */

static int32_t card_privacy(int card)
{
    int32_t v;
    memcpy(&v, savedata_card(card)->header + CARD_PRIVACY, 4);
    return v;
}

static bool installed(int card)
{
    int st = savedata_card_status(card);
    return st == 2 || st == 3;
}

/* ---- text styles ---- */

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

static const TextStyle *small_color(uint32_t c)
{
    static TextStyle st;
    st = text_style(0, 5, 10, 10, 0, 4, c, 0);
    return &st;
}

/* ---- the top menu ---- */

/* setupTopMenu (FUN_088b9c28): the CTopMenuWnd on parts 8 / 9 / 0x85 (bf_menb2,
 * bf_menl2, bf_new00), placed as Team Formation's. Items whose menu-limit word is
 * off are left out: a new Battle Mode game (FUN_0895de04) drops item 5, Snapshot,
 * until the Shop sells it. */
static void build_top_menu(void)
{
    int keep = g.top.cursor;
    ui_menu_init(&g.top, 0, 0x18 + 4, 24 - 5, 12, text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0));
    int card = current();
    for (int id = 0; id < ITEM_COUNT; id++) {
        if (!savedata_menu_limit(LIMIT_DESIGN + id)) /* FUN_0895e3c4 */
            continue;
        UiMenuItem *it = ui_menu_add(&g.top, dstr(id + 0x18), id, id + 0x23, PART_BF_MENB2, PART_BF_MENL2);
        if (it && installed(card) && id != ITEM_NOTES)
            it->disabled = true;
    }
    if (keep < g.top.count)
        g.top.cursor = keep;
}

void oke_menu_open(void)
{
    memset(&g, 0, sizeof g);
    build_top_menu();
    g.mode = MODE_TOP;
    ui_menu_open(&g.top);
    char name[32];
    savedata_card_name(current(), name, sizeof name);
    SDL_Log("[oke] OKE Configuration (CDesignMngr), card %d \"%s\"", current() + 1, name);
}

/* ---- the hardware menu ---- */

static void build_hw_items(void)
{
    const OkeDataBase *card = savedata_card(current());
    g.hw_count = 0;
    for (int i = 0; i < HWI_COUNT; i++)
        if (hw_item_shown(card, i))
            g.hw_items[g.hw_count++] = i;
    if (g.hw_cursor >= g.hw_count)
        g.hw_cursor = 0;
}

static bool hw_item_enabled(int item)
{
    if (!savedata_card_used(current()))
        return item == HWI_BODY; /* FUN_088f9694: FUN_08ad8944 on the rest */
    return true;
}

/* The help line of each category: CHardString 2 Body, 3 CPU, 0xF3 / 0xF4 armour,
 * 4 weapons, 0xED / 0xEE the energy settings, 6 options. */
static const char *hw_item_help(int item)
{
    switch (item) {
    case HWI_BODY: return hard_string(2);
    case HWI_CPU: return hard_string(3);
    case HWI_ARMOR: return hard_string(0xF3);
    case HWI_ADD_ARMOR: return hard_string(0xF4);
    case HWI_ENERGY: return hard_string(0xED);
    case HWI_OUTPUT: return hard_string(0xEE);
    default: return item >= HWI_OPTION1 ? hard_string(6) : hard_string(4);
    }
}

static void open_parts(int item)
{
    const OkeDataBase *card = savedata_card(current());
    g.part_count = part_list(card, item, g.parts);
    g.part_cursor = 0;
    int now = hw_item_part(card, item);
    for (int i = 0; i < g.part_count; i++)
        if (g.parts[i] == now)
            g.part_cursor = i;
    g.part_scroll = 0;
    g.mode = MODE_PARTS;
}

/* ---- per-mode input ---- */

static void enter_item(int id)
{
    int card = current();
    switch (id) {
    case ITEM_SELECT_CARD: /* FUN_089e6ba8 */
        g.mode = MODE_CARDS;
        g.card_cursor = card;
        break;
    case ITEM_HARDWARE: /* CHardDesignFlow */
        g.mode = MODE_HARDWARE;
        g.hw_cursor = 0;
        build_hw_items();
        break;
    case ITEM_SOFTWARE: /* CSoftDesignFlow; string 0x2E until the hardware is set */
        if (!savedata_card_used(card)) {
            g.message = dstr(0x2E);
            return;
        }
        g.mode = MODE_SOFTWARE;
        g.program = 0;
        g.chip_x = g.chip_y = 0;
        break;
    case ITEM_NAME: { /* mode 9, FUN_088bafa8 */
        if (!savedata_card_used(card)) {
            g.message = dstr(0x2E);
            return;
        }
        /* The keyboard starts from FUN_089eebc0: the card's name, or its body's. */
        char name[32];
        savedata_card_name(card, name, sizeof name);
        const char *start = name[0] ? name : nz(oke_body_name(hw_get(savedata_card(card), HW_BODY)));
        osk_open(gamedata_string(0x003DF1A4u) /* "Name Input", 0x08be31a4 */, start, NAME_MAX_INPUT);
        g.mode = MODE_NAME;
        break;
    }
    case ITEM_CUSTOMIZE: /* mode 2, FUN_088ba534 */
        if (!savedata_card_used(card)) {
            g.message = dstr(0x2E);
            return;
        }
        custom_open(card);
        g.mode = MODE_CUSTOM;
        break;
    case ITEM_EMBLEMS: /* mode 7, FUN_088bad64 */
        if (!savedata_card_used(card)) {
            g.message = dstr(0x2E);
            return;
        }
        emblem_open(card);
        g.mode = MODE_EMBLEM;
        break;
    case ITEM_NOTES: /* mode 10, FUN_088bb110: view only on an installed card */
        if (!savedata_card_used(card)) {
            g.message = dstr(0x2E);
            return;
        }
        notes_open(card, installed(card));
        g.mode = MODE_NOTES;
        break;
    case ITEM_PRIVACY: /* mode 0xB, FUN_088bb318: starts on Private when card+0x20 is 1 */
        if (!savedata_card_used(card)) {
            g.message = dstr(0x2E);
            return;
        }
        g.privacy = card_privacy(card) == 1;
        g.mode = MODE_PRIVACY;
        break;
    default:
        if (!savedata_card_used(card)) {
            g.message = dstr(0x2E);
            return;
        }
        SDL_Log("[oke] \"%s\" isn't ported", nz(dstr(id + 0x18)));
        g.message = dstr(id + 0x18);
        return;
    }
    g.message = NULL;
}

static void update_top(void)
{
    if (ui_menu_input(&g.top))
        g.message = NULL;
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        UiMenuItem *it = &g.top.item[g.top.cursor];
        if (it->disabled)
            g.message = dstr(0x6F);
        else
            enter_item(it->id);
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.closing = true;
    }
}

#define CARD_ROWS 9

static void card_line(int card, char *out, size_t n)
{
    if (!savedata_card_used(card)) {
        snprintf(out, n, "%02d  -", card + 1);
        return;
    }
    char name[32];
    savedata_card_name(card, name, sizeof name);
    snprintf(out, n, "%02d  %s", card + 1, name[0] ? name : nz(oke_body_name(hw_get(savedata_card(card), HW_BODY))));
}

static void move_card_cursor(void)
{
    int old_cursor = g.card_cursor;
    if (pad_repeat(PSP_CTRL_UP)) {
        g.card_cursor = g.card_cursor > 0 ? g.card_cursor - 1 : pad_trigger(PSP_CTRL_UP) ? SAVE_CARDS - 1 : 0;
        g.message = NULL;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        g.card_cursor = g.card_cursor < SAVE_CARDS - 1 ? g.card_cursor + 1 : pad_trigger(PSP_CTRL_DOWN) ? 0 : g.card_cursor;
        g.message = NULL;
    }
    if (g.card_cursor != old_cursor)
        sound_se(SE_CURSOR, 1.0f);
    if (g.card_cursor < g.card_scroll)
        g.card_scroll = g.card_cursor;
    if (g.card_cursor >= g.card_scroll + CARD_ROWS)
        g.card_scroll = g.card_cursor - CARD_ROWS + 1;
}

/* Select OKE Card: circle makes the card current (an installed one can't be,
 * string 0x6F); square deletes, triangle copies (strings 0x63..0x68). */
static void update_cards(void)
{
    move_card_cursor();
    int c = g.card_cursor;
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        if (installed(c)) {
            g.message = dstr(0x6F);
            return;
        }
        savedata_set_current_card(c);
        build_top_menu();
        g.mode = MODE_TOP;
        g.message = savedata_card_used(c) ? dstr(0x124) : dstr(0x125);
        SDL_Log("[oke] editing card %d", c + 1);
    } else if (pad_pressed(PSP_CTRL_SQUARE) && savedata_card_used(c)) {
        g.ask_kind = ASK_DELETE;
        ui_ask_open(&g.ask, dstr(0x66), yes_no(0x1F), yes_no(0x20), 1);
        g.mode = MODE_ASK;
    } else if (pad_pressed(PSP_CTRL_TRIANGLE) && savedata_card_used(c)) {
        g.copy_from = c;
        g.mode = MODE_COPY;
        g.message = dstr(0x65);
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = MODE_TOP;
        g.message = NULL;
    }
}

static void update_copy(void)
{
    move_card_cursor();
    if (g.message == NULL)
        g.message = dstr(0x65);
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        if (g.card_cursor == g.copy_from || installed(g.card_cursor)) {
            g.message = dstr(0x6F);
            return;
        }
        g.ask_kind = ASK_COPY;
        ui_ask_open(&g.ask, dstr(savedata_card_used(g.card_cursor) ? 0x68 : 0x67), yes_no(0x1F), yes_no(0x20), 1);
        g.mode = MODE_ASK;
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = MODE_CARDS;
        g.message = NULL;
    }
}

static void update_ask(float dt)
{
    int r = ui_ask_update(&g.ask, dt);
    if (r < 0)
        return;
    int c = g.card_cursor;
    if (r == 1 && g.ask_kind == ASK_DELETE) {
        savedata_card_clear(c);
        for (int t = 0; t < SAVE_TEAMS; t++) /* the card leaves the teams it was on */
            for (int m = 0; m < TEAM_MEMBERS; m++)
                if (savedata_team_status(t) && savedata_team_member(t, m) == c)
                    savedata_team_set_member(t, m, -1, -1);
        build_top_menu();
        SDL_Log("[oke] card %d deleted", c + 1);
    } else if (r == 1 && g.ask_kind == ASK_COPY) {
        savedata_card_copy(g.copy_from, c);
        SDL_Log("[oke] card %d copied to %d", g.copy_from + 1, c + 1);
    }
    g.mode = MODE_CARDS;
    g.message = NULL;
}

/* ---- Energy Rate / Engine Output (CHardDesignMngr modes 0x10 / 0x11) ----
 * The slider window (FUN_08adf134; input FUN_08adf454, draw FUN_08adf624) holds a
 * percentage from 50 to 150. Left / right step it by one, holding repeats, and a
 * fresh press past either end wraps. The mode's update (FUN_088f4018 / FUN_088f4170)
 * writes each change into the card at once (+0x29C / +0x2A0), so the weight and the
 * card window follow; circle keeps it, cross puts the value it opened with back. */
#define SLIDER_MIN 50
#define SLIDER_MAX 150

static int slider_field(int item)
{
    return item == HWI_ENERGY ? HW_ENERGY : HW_OUTPUT;
}

static void open_slider(int item)
{
    const OkeDataBase *card = savedata_card(current());
    int v = hw_get(card, slider_field(item));
    if (v < SLIDER_MIN || v > SLIDER_MAX) /* FUN_08adf0f4 / FUN_08adf114 keep 50 then */
        v = SLIDER_MIN;
    g.slider_item = item;
    g.slider_value = g.slider_keep = v;
    g.mode = MODE_SLIDER;
    g.message = NULL;
}

static void update_slider(void)
{
    int old = g.slider_value;
    if (pad_repeat(PSP_CTRL_LEFT)) {
        if (g.slider_value > SLIDER_MIN)
            g.slider_value--;
        else if (pad_trigger(PSP_CTRL_LEFT))
            g.slider_value = SLIDER_MAX;
    } else if (pad_repeat(PSP_CTRL_RIGHT)) {
        if (g.slider_value < SLIDER_MAX)
            g.slider_value++;
        else if (pad_trigger(PSP_CTRL_RIGHT))
            g.slider_value = SLIDER_MIN;
    }
    if (g.slider_value != old)
        sound_se(0x10, 1.0f);
    bool done = false;
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        done = true;
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.slider_value = g.slider_keep;
        done = true;
    }
    OkeDataBase *card = savedata_card_edit(current());
    if (hw_get(card, slider_field(g.slider_item)) != g.slider_value)
        hw_set(card, slider_field(g.slider_item), g.slider_value);
    if (done) {
        SDL_Log("[oke] card %d: %s = %d%% (weight %d / %d)", current() + 1, nz(hard_string(0xCF + g.slider_item)),
                g.slider_value, oke_weight(card), oke_payload(card));
        g.mode = MODE_HARDWARE;
    }
}

/* Mode 9's update (FUN_088bc378): when the keyboard ends with OK, its text (up to
 * 0x19 bytes) becomes the card's name (COkeDataBase + 4, FUN_089ee9f0); either way
 * back to the top menu. */
static void update_name(void)
{
    int r = osk_update();
    if (r == OSK_RUNNING)
        return;
    if (r == OSK_OK) {
        OkeDataBase *card = savedata_card_edit(current());
        memset(card->header + 4, 0, CARD_NAME_SIZE);
        strncpy((char *)card->header + 4, osk_text(), CARD_NAME_SIZE - 1);
        SDL_Log("[oke] card %d named \"%s\"", current() + 1, osk_text());
        build_top_menu();
    }
    g.mode = MODE_TOP;
}

/* Mode 0xB. CSecretLevelWnd's input (FUN_08a6d45c): up / down move between the two
 * rows, held keys stop at the ends and a fresh press wraps; circle accepts, cross
 * leaves. The mode's update (FUN_088bc53c) then writes card+0x20 (FUN_089eeae8 /
 * FUN_089eeaf0): 1 for Private, 0 for Public. */
static void update_privacy(void)
{
    int old = g.privacy;
    if (pad_repeat(PSP_CTRL_UP) || pad_repeat(PSP_CTRL_DOWN)) {
        bool up = pad_repeat(PSP_CTRL_UP);
        int to = g.privacy + (up ? -1 : 1);
        if (to >= 0 && to <= 1)
            g.privacy = to;
        else if (pad_trigger(up ? PSP_CTRL_UP : PSP_CTRL_DOWN))
            g.privacy = to < 0 ? 1 : 0;
    }
    if (g.privacy != old)
        sound_se(SE_CURSOR, 1.0f);
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        OkeDataBase *card = savedata_card_edit(current());
        int32_t v = g.privacy != 0;
        memcpy(card->header + CARD_PRIVACY, &v, 4);
        SDL_Log("[oke] card %d: privacy %s", current() + 1, nz(dstr(0x61 + v)));
        g.mode = MODE_TOP;
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = MODE_TOP;
    }
}

static void update_hardware(void)
{
    int old_cursor = g.hw_cursor;
    if (pad_repeat(PSP_CTRL_UP)) {
        g.hw_cursor = (g.hw_cursor + g.hw_count - 1) % g.hw_count;
        g.message = NULL;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        g.hw_cursor = (g.hw_cursor + 1) % g.hw_count;
        g.message = NULL;
    }
    if (g.hw_cursor != old_cursor)
        sound_se(SE_CURSOR, 1.0f);
    int item = g.hw_items[g.hw_cursor];
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        if (!hw_item_enabled(item)) {
            g.message = hard_string(0xF6); /* "Unavailable when no body is selected." */
            return;
        }
        if (item == HWI_ENERGY || item == HWI_OUTPUT) {
            open_slider(item);
            return;
        }
        open_parts(item);
        g.message = NULL;
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = MODE_TOP;
        g.message = NULL;
    }
}

#define PART_ROWS 9

static void update_parts(void)
{
    if (g.part_count <= 0) {
        g.mode = MODE_HARDWARE;
        return;
    }
    int old_cursor = g.part_cursor;
    if (pad_repeat(PSP_CTRL_UP))
        g.part_cursor = g.part_cursor > 0 ? g.part_cursor - 1 : pad_trigger(PSP_CTRL_UP) ? g.part_count - 1 : 0;
    else if (pad_repeat(PSP_CTRL_DOWN))
        g.part_cursor = g.part_cursor < g.part_count - 1 ? g.part_cursor + 1 : pad_trigger(PSP_CTRL_DOWN) ? 0 : g.part_cursor;
    if (g.part_cursor != old_cursor)
        sound_se(SE_CURSOR, 1.0f);
    if (g.part_cursor < g.part_scroll)
        g.part_scroll = g.part_cursor;
    if (g.part_cursor >= g.part_scroll + PART_ROWS)
        g.part_scroll = g.part_cursor - PART_ROWS + 1;
    int item = g.hw_items[g.hw_cursor], id = g.parts[g.part_cursor];
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        int c = current();
        OkeDataBase *card = savedata_card_edit(c);
        if (!savedata_card_used(c)) { /* a new design: the card becomes the player's */
            hw_new_design(card, id);
            card->header[0] = 1;
            SDL_Log("[oke] card %d: new design, body %d", c + 1, id);
        } else {
            hw_equip(card, item, id);
        }
        SDL_Log("[oke] card %d: %s = %s (weight %d / %d)", c + 1, nz(hard_string(0xCF + item)), nz(part_name(item, id)),
                oke_weight(card), oke_payload(card));
        build_hw_items();
        build_top_menu();
        g.mode = MODE_HARDWARE;
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = MODE_HARDWARE;
    }
}

static int program_count(const OkeDataBase *card)
{
    int n = 1;
    while (n < PGCHIP_PROGRAM_MAX && card->software.width[n] > 0)
        n++;
    return n;
}

static void update_software(void)
{
    const OkeDataBase *card = savedata_card(current());
    int n = program_count(card);
    if (pad_pressed(PSP_CTRL_RTRIGGER) || pad_pressed(PSP_CTRL_LTRIGGER)) {
        g.program = (g.program + (pad_pressed(PSP_CTRL_RTRIGGER) ? 1 : n - 1)) % n;
        g.chip_x = g.chip_y = 0;
    }
    int w = card->software.width[g.program];
    if (w <= 0)
        w = 1;
    int old_cursor = g.chip_y * 64 + g.chip_x;
    if (pad_repeat(PSP_CTRL_UP) && g.chip_y > 0)
        g.chip_y--;
    else if (pad_repeat(PSP_CTRL_DOWN) && g.chip_y < w - 1)
        g.chip_y++;
    else if (pad_repeat(PSP_CTRL_LEFT) && g.chip_x > 0)
        g.chip_x--;
    else if (pad_repeat(PSP_CTRL_RIGHT) && g.chip_x < w - 1)
        g.chip_x++;
    if (g.chip_y * 64 + g.chip_x != old_cursor)
        sound_se(SE_CURSOR, 1.0f);
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        SDL_Log("[oke] chip editing (CSoftDesignMngr's Edit Chips) isn't ported");
        g.message = dstr(0x25);
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = MODE_TOP;
        g.message = NULL;
    }
}

int oke_menu_update(float dt)
{
    ui_menu_update(&g.top, dt);
    g.top.active = g.mode == MODE_TOP;
    switch (g.mode) {
    case MODE_TOP: update_top(); break;
    case MODE_CARDS: update_cards(); break;
    case MODE_COPY: update_copy(); break;
    case MODE_ASK: update_ask(dt); break;
    case MODE_HARDWARE: update_hardware(); break;
    case MODE_PARTS: update_parts(); break;
    case MODE_SOFTWARE: update_software(); break;
    case MODE_SLIDER: update_slider(); break;
    case MODE_NAME: update_name(); break;
    case MODE_PRIVACY: update_privacy(); break;
    case MODE_EMBLEM:
        if (!emblem_update(dt))
            g.mode = MODE_TOP;
        break;
    case MODE_CUSTOM:
        if (!custom_update(dt))
            g.mode = MODE_TOP;
        break;
    case MODE_NOTES:
        if (!notes_update(dt))
            g.mode = MODE_TOP;
        break;
    }
    if (g.closing) {
        SDL_Log("[oke] back to the Battle Mode menu");
        return 0;
    }
    return 1;
}

/* ---- drawing ---- */

static void label_value(const char *label, const char *value, int x, int y, int vx, const TextStyle *vs)
{
    ui_text(nz(label), small(), x, y, 1.0f);
    ui_text(nz(value), vs ? vs : small(), vx, y, 1.0f);
}

static uint32_t weight_color(int state)
{
    static const uint32_t k[4] = {0xFF303030, 0xFF206020, 0xFF0070C0, 0xFF0000C0};
    return k[state & 3];
}

/* The card window (COkeCard::drawHardware FUN_089e6324 lists the body, the weapons,
 * the armour and the options; the stats are CDesignString's labels 0x30..0x57). */
static void draw_card(const OkeDataBase *card, int number, int x, int y)
{
    const int w = 266, h = 200;
    ui_frame(ui_part(PART_BF_SWND4), x, y, w, h, 8, 1.0f);
    /* the game's card (COkeCard) on top, the stats under it */
    int ch = oke_card_draw(number, x + (w - OKE_CARD_W) / 2, y + 6, false);
    char line[80];
    bool used = card && card->header[0];
    if (!used) {
        ui_text(nz(dstr(0x125)), small(), x + 14, y + 12 + ch, 1.0f);
        return;
    }
    int body = hw_get(card, HW_BODY), lx = x + 12, vx = x + 104, ly = y + 6 + ch;
    label_value(dstr(0x30), oke_body_name(body), lx, ly, vx, NULL);
    snprintf(line, sizeof line, "%d", body_durability(body));
    label_value(dstr(0x31), line, lx, ly += 11, vx, NULL);
    snprintf(line, sizeof line, "%d / %d", oke_weight(card), oke_payload(card));
    label_value(dstr(0x32), line, lx, ly += 11, vx, small_color(weight_color(oke_weight_state(card))));
    label_value(hard_string(0xD0), oke_cpu_name(hw_get(card, HW_CPU)), lx, ly += 11, vx, NULL);
    label_value(hard_string(0xD1), oke_armor_name(hw_get(card, HW_ARMOR)), lx, ly += 11, vx, NULL);
    label_value(hard_string(0xD2), oke_add_armor_name(hw_get(card, HW_ADD_ARMOR)), lx, ly += 11, vx, NULL);
    for (int s = 0; s < body_weapons(body) && s < OKE_WEAPON_SLOTS; s++) {
        int wp = hw_get(card, HW_WEAPON + 4 * s);
        if (wp)
            snprintf(line, sizeof line, "%s  x%d", nz(oke_weapon_name(wp)), hw_get(card, HW_AMMO + 4 * s));
        else
            snprintf(line, sizeof line, "%s", nz(oke_weapon_name(0)));
        label_value(hard_string(0xD3 + s), line, lx, ly += 11, vx, NULL);
    }
    for (int s = 0; s < body_options(body) && s < OKE_OPTION_SLOTS; s++)
        label_value(hard_string(0xDA + s), oke_option_name(hw_get(card, HW_OPTION + 4 * s)), lx, ly += 11, vx, NULL);
}

static void draw_cursor(int x, int y, int w, int h)
{
    ui_frame(ui_part(PART_BF_CUR0), x, y, w, h, 6, ui_pulse(SDL_GetTicks() / 1000.0f));
}

/* CSecretLevelWnd's draw (FUN_08a6d6a0) on part 0x99 (bf_swnd2), opened beside the
 * top menu at x 0x8C, at the height of the cursor's row: CDesignString 0x61 / 0x62
 * at (+0xC, +6) a row of 0xC apart, the cursor 0x58 x 0xC at (8, 6 + row * 0xC). */
static void draw_privacy(void)
{
    int x = 0x8C, y = g.top.y + g.top.cursor * g.top.pitch;
    ui_frame(ui_part(PART_BF_SWND2), x, y, 0x68, 0x24, 8, 1.0f);
    for (int i = 0; i < 2; i++)
        ui_text(nz(dstr(0x61 + i)), dark(), x + 0xC, y + 6 + i * 0xC, 1.0f);
    draw_cursor(x + 8, y + 6 + g.privacy * 0xC, 0x58, 0xC);
}

static void draw_card_list(int x, int y, int w)
{
    ui_frame(ui_part(PART_BF_SWND4), x, y, w, CARD_ROWS * 18 + 30, 8, 1.0f);
    ui_text(nz(dstr(0x18)), dark(), x + 10, y + 6, 1.0f);
    for (int r = 0; r < CARD_ROWS && g.card_scroll + r < SAVE_CARDS; r++) {
        int i = g.card_scroll + r, ry = y + 24 + r * 18;
        if (i == g.card_cursor)
            draw_cursor(x + 4, ry - 3, w - 8, 18);
        char text[64];
        card_line(i, text, sizeof text);
        const TextStyle *st = dark();
        if (g.mode == MODE_COPY && i == g.copy_from)
            st = small_color(0xFF0070C0);
        else if (i == current())
            st = small_color(0xFF206020);
        ui_text(text, st, x + 12, ry, 1.0f);
    }
}

static void draw_hardware(int x, int y)
{
    const OkeDataBase *card = savedata_card(current());
    const int w = 196, pitch = 13;
    ui_frame(ui_part(PART_BF_SWND4), x, y, w, g.hw_count * pitch + 30, 8, 1.0f);
    ui_text(nz(dstr(0x19)), dark(), x + 10, y + 6, 1.0f);
    for (int r = 0; r < g.hw_count; r++) {
        int item = g.hw_items[r], ry = y + 24 + r * pitch;
        if (r == g.hw_cursor)
            draw_cursor(x + 4, ry - 2, w - 8, pitch + 1);
        const TextStyle *st = hw_item_enabled(item) ? small() : small_color(0xFF808080);
        ui_text(nz(hard_string(0xCF + item)), st, x + 10, ry, 1.0f);
        char v[48] = "";
        if (savedata_card_used(current())) {
            int part = hw_item_part(card, item);
            if (item == HWI_ENERGY || item == HWI_OUTPUT)
                snprintf(v, sizeof v, "%d%%", part);
            else
                snprintf(v, sizeof v, "%s", nz(part_name(item, part)));
        }
        ui_text(v, st, x + 92, ry, 1.0f);
    }
}

/* The slider (FUN_08adf624): its title, the gauge (op_svf_l's left end and body,
 * bf_cur7 filled to value / 150 of its width) and the value; beside it the numbers
 * the setting changes (FUN_08adff5c for the energy, FUN_08ae07a0 for the output). */
static void draw_slider(int x, int y)
{
    const OkeDataBase *card = savedata_card(current());
    const int w = 214, h = 44;
    ui_frame(ui_part(PART_BF_TWND2), x, y, w, h, 8, 1.0f);
    /* the window's title is a literal of the class (0x08c40a18 / 0x08c40a24) */
    const char *title = gamedata_string(g.slider_item == HWI_ENERGY ? 0x0043CA18u : 0x0043CA24u);
    ui_text(nz(title), dark(), x + 0xC, y + 4, 1.0f);
    Picture *gauge = ui_part(PART_OP_SVF_L), *fill = ui_part(PART_BF_CUR7);
    if (gauge) {
        int gh = picture_height(gauge) < 0x57 ? picture_height(gauge) : 0x57;
        picture_draw_ex(gauge, x + 0x12, y + 0x17, 0, 0x10, 12, 0, gh > 12 ? 0 : 0, 0x10, gh, BLEND_ALPHA, 0xFFFFFFFF,
                        0, 0, 0, 0);
        picture_draw_ex(gauge, x + 0x22, y + 0x17, 0, 0xA6, 12, 0x10, 0, picture_width(gauge) - 0x10, gh, BLEND_ALPHA,
                        0xFFFFFFFF, 0, 0, 0, 0);
    } else {
        ui_fill(x + 0x12, y + 0x17, 0xB6, 12, 0xFF404040);
    }
    int fw = (fill ? picture_width(fill) : 0xA0) * g.slider_value / SLIDER_MAX;
    if (fill)
        picture_draw_ex(fill, x + 0x1F, y + 0x19, 0, fw, picture_height(fill), 0, 0, fw, picture_height(fill),
                        BLEND_ALPHA, 0xFFFFFFFF, 0, 0, 0, 0);
    else
        ui_fill(x + 0x1F, y + 0x19, fw, 8, 0xFF20A0E0);
    char v[32];
    snprintf(v, sizeof v, "%d%%", g.slider_value);
    ui_text(v, dark(), x + w - 44, y + 4, 1.0f);
    /* the numbers it changes */
    int ix = x, iy = y + h + 4, lx = ix + 10, vx = ix + 0x46 + 40;
    ui_frame(ui_part(PART_BF_SWND4), ix, iy, w, 0x52, 8, 1.0f);
    int body = hw_get(card, HW_BODY);
    if (g.slider_item == HWI_ENERGY) {
        int amount = oke_energy_amount(card);
        snprintf(v, sizeof v, "%d", hw_get(card, HW_ENERGY));
        label_value(hard_string(0xE0), v, lx, iy + 9, vx, NULL);
        snprintf(v, sizeof v, "%d", amount);
        label_value(hard_string(0xE2), v, lx, iy + 0x1B, vx, NULL);
        snprintf(v, sizeof v, "%d", oke_energy_weight(card));
        label_value(hard_string(0xE3), v, lx, iy + 0x2D, vx, NULL);
        snprintf(v, sizeof v, "%d", oke_heat_resist(card));
        label_value(hard_string(0xE1), v, lx, iy + 0x3F, vx, NULL);
    } else {
        snprintf(v, sizeof v, "%d", hw_get(card, HW_OUTPUT));
        label_value(hard_string(0xE4), v, lx, iy + 9, vx, NULL);
        snprintf(v, sizeof v, "%d", oke_payload(card));
        label_value(hard_string(0xE5), v, lx, iy + 0x1B, vx, NULL);
        snprintf(v, sizeof v, "%d", oke_consumption(card));
        label_value(hard_string(0xE6), v, lx, iy + 0x2D, vx, NULL);
    }
    (void)body;
}

/* The parts window (CEquipmentSelectWnd) and beside it the part's numbers, with the
 * card's weight as it would be with the part (FUN_088dad64 does the same on a copy). */
static void draw_parts(int x, int y)
{
    int item = g.hw_items[g.hw_cursor];
    const int w = 178;
    ui_frame(ui_part(PART_BF_SWND4), x, y, w, PART_ROWS * 18 + 30, 8, 1.0f);
    ui_text(nz(hard_string(0xCF + item)), dark(), x + 10, y + 6, 1.0f);
    const OkeDataBase *card = savedata_card(current());
    int now = savedata_card_used(current()) ? hw_item_part(card, item) : -1;
    for (int r = 0; r < PART_ROWS && g.part_scroll + r < g.part_count; r++) {
        int i = g.part_scroll + r, ry = y + 24 + r * 18, id = g.parts[i];
        if (i == g.part_cursor)
            draw_cursor(x + 4, ry - 3, w - 8, 18);
        ui_text(nz(part_name(item, id)), id == now ? small_color(0xFF206020) : dark(), x + 12, ry, 1.0f);
    }
    if (g.part_count > PART_ROWS) {
        char pos[24]; /* two ints and a slash */
        snprintf(pos, sizeof pos, "%d/%d", g.part_cursor + 1, g.part_count);
        ui_text(pos, small(), x + w - 44, y + 8, 1.0f);
    }
    /* the info window */
    int ix = x + w + 4, iy = y, iw = 476 - ix;
    ui_frame(ui_part(PART_BF_SWND5), ix, iy, iw, 150, 8, 1.0f);
    if (g.part_count <= 0)
        return;
    int id = g.parts[g.part_cursor], lx = ix + 10, vx = ix + 116, ly = iy + 10;
    char v[48];
    OkeDataBase preview = *card;
    if (!savedata_card_used(current()))
        hw_new_design(&preview, id);
    else
        hw_equip(&preview, item, id);
    if (item == HWI_BODY) {
        snprintf(v, sizeof v, "%d", body_durability(id));
        label_value(dstr(0x31), v, lx, ly, vx, NULL);
        snprintf(v, sizeof v, "%d", body_weight(id));
        label_value(dstr(0x32), v, lx, ly += 11, vx, NULL);
        snprintf(v, sizeof v, "%d", body_payload(id));
        label_value(dstr(0x33), v, lx, ly += 11, vx, NULL);
        snprintf(v, sizeof v, "%d", body_weapons(id));
        label_value(dstr(0x34), v, lx, ly += 11, vx, NULL);
        snprintf(v, sizeof v, "%d", body_energy(id));
        label_value(dstr(0x35), v, lx, ly += 11, vx, NULL);
        snprintf(v, sizeof v, "%d", body_heat(id));
        label_value(dstr(0x36), v, lx, ly += 11, vx, NULL);
    } else if (item == HWI_CPU) {
        snprintf(v, sizeof v, "%d x %d", cpu_program_width(id, 0), cpu_program_width(id, 0));
        label_value(dstr(0x37), v, lx, ly, vx, NULL);
        snprintf(v, sizeof v, "%d", cpu_weight(id));
        label_value(dstr(0x39), v, lx, ly += 11, vx, NULL);
    } else if (item == HWI_ARMOR) {
        snprintf(v, sizeof v, "%d", armor_weight(&preview, id));
        label_value(dstr(0x45), v, lx, ly, vx, NULL);
    } else if (item == HWI_ADD_ARMOR) {
        snprintf(v, sizeof v, "%d", add_armor_weight(&preview, id));
        label_value(dstr(0x47), v, lx, ly, vx, NULL);
    } else if (item >= HWI_WEAPON1 && item < HWI_WEAPON1 + OKE_WEAPON_SLOTS) {
        int slot = item - HWI_WEAPON1, ammo = hw_get(&preview, HW_AMMO + 4 * slot);
        label_value(dstr(0x3A), oke_category_name(weapon_category(id)), lx, ly, vx, NULL);
        snprintf(v, sizeof v, "%d", ammo);
        label_value(dstr(0x3E), v, lx, ly += 11, vx, NULL);
        snprintf(v, sizeof v, "%d", id ? slot_capacity(&preview, slot, weapon_type(id)) : 0);
        label_value(dstr(0x40), v, lx, ly += 11, vx, NULL);
        snprintf(v, sizeof v, "%d", weapon_weight(id, ammo));
        label_value(dstr(0x3F), v, lx, ly += 11, vx, NULL);
    } else {
        snprintf(v, sizeof v, "%d", option_weight(id));
        label_value(dstr(0x51), v, lx, ly, vx, NULL);
    }
    ly = iy + 150 - 24;
    snprintf(v, sizeof v, "%d / %d", oke_weight(&preview), oke_payload(&preview));
    label_value(dstr(0x32), v, lx, ly, vx, small_color(weight_color(oke_weight_state(&preview))));
}

/* The program grid. Each chip is drawn with a tile of parts/soft/sd_chip0 (64 x 64
 * cells: blue, green, red, yellow, then the Return tile; the faded rows below for
 * chips switched off), picked by the chip's kind; the start chip is marked. */
static void draw_chip(Picture *tiles, const PgChipCommand *c, int x, int y, int size)
{
    int t = pgchip_type(c);
    if (t == 0) {
        ui_fill(x + 1, y + 1, size - 2, size - 2, 0x30303030);
        return;
    }
    int kind = chip_kind(t), cell = t == PGCHIP_RETURN ? 4 : kind >= 0 && kind < 4 ? kind : 0;
    int u = (cell % 3) * 64, v = (cell / 3) * 64 + (pgchip_active(c) || t == PGCHIP_RETURN ? 0 : 128);
    if (tiles)
        picture_draw_ex(tiles, x, y, 0, size, size, u, v, 64, 64, BLEND_ALPHA, 0xFFFFFFFF, 0, 0, 0, 0);
    else
        ui_fill(x + 1, y + 1, size - 2, size - 2, 0xFFC08040);
}

static void draw_software(int x, int y)
{
    const OkeDataBase *card = savedata_card(current());
    const SoftwareDataBase *sw = &card->software;
    int w = sw->width[g.program];
    const PgChipCommand *chips = oke_program_chips(card, g.program);
    ui_frame(ui_part(PART_BF_SWND4), x, y, 210, 202, 8, 1.0f);
    char title[48];
    snprintf(title, sizeof title, "%s  %d/%d", nz(dstr(0x1A)), g.program + 1, program_count(card));
    ui_text(title, small(), x + 10, y + 6, 1.0f);
    if (w <= 0)
        return;
    static Picture *tiles;
    if (!tiles)
        tiles = picture_load("parts/soft/sd_chip0.gim");
    int cell = 172 / w, gx = x + 19, gy = y + 22;
    for (int cy = 0; cy < w; cy++)
        for (int cx = 0; cx < w; cx++) {
            int px = gx + cx * cell, py = gy + cy * cell;
            draw_chip(tiles, &chips[cy * w + cx], px, py, cell);
            if (cy * w + cx == sw->start[g.program])
                ui_fill(px + 2, py + 2, cell / 4, cell / 4, 0xFF2020E0);
            if (cx == g.chip_x && cy == g.chip_y)
                draw_cursor(px - 2, py - 2, cell + 4, cell + 4);
        }
    /* the chip under the cursor */
    const PgChipCommand *c = &chips[g.chip_y * w + g.chip_x];
    int ix = x + 214, iw = 476 - ix;
    ui_frame(ui_part(PART_BF_SWND5), ix, y, iw, 120, 8, 1.0f);
    char v[64];
    snprintf(v, sizeof v, "(%d, %d)", g.chip_x, g.chip_y);
    ui_text(v, small(), ix + 10, y + 10, 1.0f);
    ui_text(nz(chip_name(pgchip_type(c))), dark(), ix + 10, y + 26, 1.0f);
    snprintf(v, sizeof v, "%02X %02X %02X %02X %02X %02X %02X %02X", c->b[0], c->b[1], c->b[2], c->b[3], c->b[4], c->b[5],
             c->b[6], c->b[7]);
    ui_text(v, small(), ix + 10, y + 44, 1.0f);
    snprintf(v, sizeof v, "%s %d x %d", nz(dstr(0x37)), w, w);
    ui_text(v, small(), ix + 10, y + 60, 1.0f);
}

static const char *help_text(void)
{
    if (g.message)
        return g.message;
    switch (g.mode) {
    case MODE_TOP: return dstr(g.top.item[g.top.cursor].help);
    case MODE_CARDS: return dstr(0x23);
    case MODE_COPY: return dstr(0x65);
    case MODE_HARDWARE: return g.hw_count ? hw_item_help(g.hw_items[g.hw_cursor]) : NULL;
    case MODE_PARTS:
        return g.part_count ? part_help(g.hw_items[g.hw_cursor], g.parts[g.part_cursor]) : NULL;
    case MODE_SOFTWARE: return dstr(0x25);
    case MODE_SLIDER: return hw_item_help(g.slider_item);
    case MODE_PRIVACY: return dstr(0x2B);
    case MODE_NOTES: return notes_help();
    case MODE_CUSTOM: return custom_help();
    case MODE_EMBLEM: return emblem_help();
    default: return NULL;
    }
}

void oke_menu_draw(void)
{
    sceGuEnable(GU_BLEND);
    Picture *back = ui_part(PART_BF_BACK);
    if (back)
        picture_draw(back, 0, 0, 0, BLEND_REPLACE, 0xFFFFFFFF, 0, 0);
    else
        ui_fill(0, 0, 480, 272, 0xFF404040);
    /* The title: Battle Mode's banner for its item 1, bf_ttl1. */
    Picture *banner = ui_part(PART_BF_TTL0 + 1);
    ui_pic(banner, 0, 0, 1.0f, false);
    TextStyle tst = text_style(0, 7, 14, 14, 0, 0, 0xFFFFFFFF, 0);
    ui_text(nz(dstr(0xC6)), &tst, 64, (banner ? picture_height(banner) : 24) / 2 - 8, 1.0f);
    int c = current();
    switch (g.mode) {
    case MODE_TOP:
        ui_menu_draw(&g.top);
        draw_card(savedata_card(c), c, 205, 22);
        break;
    case MODE_CARDS:
    case MODE_COPY:
    case MODE_ASK:
        draw_card_list(4, 22, 196);
        draw_card(savedata_card(g.card_cursor), g.card_cursor, 205, 22);
        if (g.mode == MODE_ASK)
            ui_ask_draw(&g.ask);
        break;
    case MODE_HARDWARE:
        draw_hardware(4, 22);
        draw_card(savedata_card(c), c, 205, 22);
        break;
    case MODE_PARTS:
        draw_parts(4, 22);
        break;
    case MODE_SOFTWARE:
        draw_software(4, 22);
        break;
    case MODE_NAME:
    case MODE_PRIVACY:
        ui_menu_draw(&g.top);
        draw_card(savedata_card(c), c, 205, 22);
        if (g.mode == MODE_PRIVACY)
            draw_privacy();
        break;
    case MODE_NOTES:
        notes_draw();
        break;
    case MODE_EMBLEM:
        emblem_draw();
        break;
    case MODE_CUSTOM:
        custom_draw();
        draw_card(savedata_card(c), c, 205, 22);
        break;
    case MODE_SLIDER:
        draw_hardware(4, 22);
        draw_card(savedata_card(c), c, 205, 22);
        draw_slider(130, 90);
        break;
    }
    if (!(g.mode == MODE_NOTES && notes_help_hidden()))
        ui_help_draw(help_text());
    osk_draw(); /* over everything, as the system keyboard is; nothing when it's closed */
    sceGuEnable(GU_TEXTURE_2D);
}
