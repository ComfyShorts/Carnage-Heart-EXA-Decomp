/* See team_menu.h and docs/screens.md. Hand-written from CTeamFormationMngr:
 *   Initialize FUN_08ac536c, setupTopMenu FUN_08ac58f0, the mode switches FUN_08ac10c0
 *   (enter) and FUN_08ac1344 (update), and the modes:
 *     3  the top menu (FUN_08ac5d08 / FUN_08ac6820)
 *     4  Select Team: the 16 team cards (FUN_08ac1710 / FUN_08ac2964)
 *     5  Register Member: the team's three slots (FUN_08ac5edc / FUN_08ac2ba4)
 *     6  Select OKE Card for a slot (FUN_08ac188c / FUN_08ac2dcc, the checks of
 *        CTeamMemberCardListWnd FUN_08ac8144)
 *     7  "Unregister OKE?" (FUN_08ac854c unregisters)
 *     8  Formation
 *     0x12 "Delete this team?"
 *   createTeam FUN_08ac42f4, setSelect FUN_08ac452c, the selectable check FUN_08ac468c.
 *     9  Enter Team Name: the keyboard on the team's name (FUN_08ac1bec)
 *     10 Emblems: CEmblemEditor's team variant (FUN_08ac1d94 / FUN_08ac30f0)
 * Transfer Team Data is not ported; choosing it is logged. */
#include "team_menu.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "battle_menu.h"
#include "font.h"
#include "gamedata.h"
#include "gu.h"
#include "oke_emblem.h"
#include "osk.h"
#include "pad.h"
#include "sound.h"
#include "psp_shim.h"
#include "savedata.h"
#include "ui.h"

#define STR_TEAM 0x00487A60u   /* CTeamFormationString, 0x4B pointers (0x08c8ba60) */
#define TEAM_NAMES 0x00487A08u /* "Team 01".. "Team 16" (0x08c8ba08), createTeam's names */
#define SPOT_ORDER 0x0043A228u /* the free-spot order (0x08c3e228, FUN_08ac8eec) */
#define SPOTS 9

/* The parts CTeamFormationMngr::Initialize asks of the file set. */
#define PART_BF_MENB3 0x0A
#define PART_BF_MENL3 0x0B
#define PART_BF_SWND2 0x99
#define PART_BF_SWND3 0x9A

static const char *table_string(uint32_t table, int id, int count)
{
    if (id < 0 || id >= count)
        id = 1; /* "<<Error>>" */
    uint32_t p = gamedata_word(table + 4u * (uint32_t)id, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

const char *team_string(int id)
{
    return table_string(STR_TEAM, id, 0x4B);
}

/* The Yes / No of the questions: CBattleTestString 0x1F / 0x20, as the Battle Mode
 * questions use. */
static const char *yes_no(int id)
{
    return table_string(0x00466D50u, id, 0x5A);
}

/* ---- the team and card lines ---- */

void team_label(int team, char *out, size_t n)
{
    if (savedata_team_status(team) == 0) {
        snprintf(out, n, "%02d  -", team + 1);
        return;
    }
    char name[TEAM_NAME_MAX + 1];
    savedata_team_name(team, name, sizeof name);
    snprintf(out, n, "%02d  %s  (%d)", team + 1, name, savedata_team_count(team));
}

static void card_line(int card, char *out, size_t n)
{
    if (!savedata_card_used(card)) {
        snprintf(out, n, "%02d  -", card + 1);
        return;
    }
    char name[32];
    savedata_card_name(card, name, sizeof name);
    snprintf(out, n, "%02d  %s", card + 1, name[0] ? name : "No Name");
}

/* ---- state ---- */

typedef enum {
    MODE_TOP = 3,
    MODE_TEAMS = 4,
    MODE_MEMBERS = 5,
    MODE_CARDS = 6,
    MODE_UNREGISTER = 7,
    MODE_FORMATION = 8,
    MODE_NAME = 9,
    MODE_EMBLEM = 10,
    MODE_DELETE = 0x12,
} Mode;

static int g_selected = -1; /* CCarnageHeart + 0xD0170 */

static struct {
    Mode mode;
    UiMenu top;
    int team_cursor, team_scroll;
    int slot;              /* Register Member: the slot 0-2 */
    int card_cursor, card_scroll;
    int spot;              /* Formation: the slot's spot being moved */
    UiAsk ask;
    const char *message;
    bool closing;
} g;

int team_menu_selected(void)
{
    return g_selected >= 0 && g_selected < SAVE_TEAMS && savedata_team_status(g_selected) != 0 ? g_selected : -1;
}

/* setupTopMenu (FUN_08ac58f0): the five items, text id + 4, help id + 0x33, on
 * CTopMenuWnd parts 10 / 0xB / 0x85 (bf_menb3, bf_menl3, bf_new00), below the title. */
static void build_top_menu(void)
{
    ui_menu_init(&g.top, 0, 0x18 + 4, 24 - 5, 12, text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0));
    for (int id = 0; id < 5; id++)
        ui_menu_add(&g.top, team_string(id + 4), id, id + 0x33, PART_BF_MENB3, PART_BF_MENL3);
}

void team_menu_open(void)
{
    memset(&g, 0, sizeof g);
    build_top_menu();
    g.mode = MODE_TOP;
    ui_menu_open(&g.top);
    int sel = team_menu_selected();
    g.team_cursor = sel >= 0 ? sel : 0;
    SDL_Log("[team] Team Formation (CTeamFormationMngr)");
}

/* ---- the modes ---- */

/* FUN_08ac8eec: the first spot, in the order at 0x08c3e228, that no other member of
 * the team stands on. */
static int free_spot(int team, int except)
{
    static const int k_order[SPOTS] = {4, 3, 5, 1, 0, 2, 7, 6, 8};
    for (int k = 0; k < SPOTS; k++) {
        int spot = (int)gamedata_word(SPOT_ORDER + 4u * (uint32_t)k, (uint32_t)k_order[k]);
        bool taken = false;
        for (int m = 0; m < TEAM_MEMBERS; m++)
            if (m != except && savedata_team_member(team, m) >= 0 && savedata_team_spot(team, m) == spot)
                taken = true;
        if (!taken)
            return spot;
    }
    return 0;
}

/* FUN_08ac4628: the selected team when it's the player's own (status 1), else -1. */
static int own_team(void)
{
    int t = team_menu_selected();
    return t >= 0 && savedata_team_status(t) == 1 ? t : -1;
}

#define TEAM_NAME_INPUT 12 /* FUN_08ac1bec's limit (string 0x35: "up to 12 characters") */

/* Mode 9's end (LAB_08ac4bac, then FUN_08ac30e8 back to the top): OK writes the name. */
static void update_name(void)
{
    int r = osk_update();
    if (r == OSK_RUNNING)
        return;
    if (r == OSK_OK) {
        savedata_team_set_name(own_team(), osk_text());
        SDL_Log("[team] team %d named \"%s\"", own_team() + 1, osk_text());
    }
    g.mode = MODE_TOP;
}

static void update_top(void)
{
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        switch (ui_menu_id(&g.top)) {
        case 0: /* -> mode 4 */
            g.mode = MODE_TEAMS;
            battle_menu_field(0x29, 0); /* Story Mode: a lesson's script on the team cards */
            if (g.team_cursor < g.team_scroll)
                g.team_scroll = g.team_cursor;
            break;
        case 1: /* -> mode 5; FUN_08ac5edc needs a selected team */
            if (team_menu_selected() < 0) {
                g.message = team_string(0x44);
            } else {
                g.mode = MODE_MEMBERS;
                battle_menu_field(0x2A, 0); /* on the members */
                g.slot = 0;
            }
            break;
        case 2: /* -> mode 9 (FUN_08ac1bec): the selected team must be the player's own
                 * (FUN_08ac4628 / FUN_08ac4740, status 1) */
        case 3: { /* -> mode 10 (FUN_08ac1d94) */
            int team = own_team();
            if (team < 0) {
                g.message = team_string(0x44);
                sound_se(SE_ERROR, 1.0f);
                break;
            }
            if (ui_menu_id(&g.top) == 2) {
                char name[TEAM_NAME_MAX + 1];
                savedata_team_name(team, name, sizeof name);
                osk_open(team_string(6), name, TEAM_NAME_INPUT); /* "Enter Team Name", 12 */
                g.mode = MODE_NAME;
            } else {
                emblem_open_team(team);
                g.mode = MODE_EMBLEM;
            }
            break;
        }
        default:
            SDL_Log("[team] \"%s\" isn't ported (team transfer)", g.top.item[g.top.cursor].text);
            g.message = g.top.item[g.top.cursor].text;
            break;
        }
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.closing = true; /* mode 1: the exit window, then CTeamFormationFlow ends */
    }
}

#define TEAM_ROWS 8

static void update_teams(void)
{
    int old_cursor = g.team_cursor;
    if (pad_repeat(PSP_CTRL_UP)) {
        g.team_cursor = g.team_cursor > 0 ? g.team_cursor - 1 : pad_trigger(PSP_CTRL_UP) ? SAVE_TEAMS - 1 : 0;
        g.message = NULL;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        g.team_cursor = g.team_cursor < SAVE_TEAMS - 1 ? g.team_cursor + 1 : pad_trigger(PSP_CTRL_DOWN) ? 0 : g.team_cursor;
        g.message = NULL;
    }
    if (g.team_cursor != old_cursor)
        sound_se(SE_CURSOR, 1.0f);
    if (g.team_cursor < g.team_scroll)
        g.team_scroll = g.team_cursor;
    if (g.team_cursor >= g.team_scroll + TEAM_ROWS)
        g.team_scroll = g.team_cursor - TEAM_ROWS + 1;
    int t = g.team_cursor, status = savedata_team_status(t);
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        sound_se(SE_DECIDE, 1.0f);
        if (status != 0 && status != 1) { /* FUN_08ac468c: installed teams can't be set up */
            g.message = team_string(status == 2 || status == 3 ? 0x1E : 0x1F);
            return;
        }
        if (status == 0) { /* createTeam: the "Team NN" name, owner string 0x19 */
            savedata_team_create(t, table_string(TEAM_NAMES, t, 16), team_string(0x19));
            SDL_Log("[team] team %d created", t + 1);
        }
        g_selected = t; /* setSelect */
        g.mode = MODE_TOP;
        battle_menu_field(0x28, 0); /* back on the top menu */
        g.message = NULL;
    } else if (pad_pressed(PSP_CTRL_SQUARE) && status != 0) {
        ui_ask_open(&g.ask, team_string(0x1A), yes_no(0x1F), yes_no(0x20), 1);
        g.mode = MODE_DELETE;
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = MODE_TOP;
        battle_menu_field(0x28, 0); /* back on the top menu */
        g.message = NULL;
    }
}

static void update_members(void)
{
    int team = team_menu_selected();
    if (team < 0) {
        g.mode = MODE_TOP;
        battle_menu_field(0x28, 0); /* back on the top menu */
        return;
    }
    int old_cursor = g.slot;
    if (pad_repeat(PSP_CTRL_UP)) {
        g.slot = (g.slot + TEAM_MEMBERS - 1) % TEAM_MEMBERS;
        g.message = NULL;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        g.slot = (g.slot + 1) % TEAM_MEMBERS;
        g.message = NULL;
    }
    if (g.slot != old_cursor)
        sound_se(SE_CURSOR, 1.0f);
    int card = savedata_team_member(team, g.slot);
    if (pad_pressed(PSP_CTRL_CIRCLE)) { /* -> mode 6 */
        sound_se(SE_DECIDE, 1.0f);
        g.card_cursor = card >= 0 ? card : 0;
        if (card < 0)
            for (int i = 0; i < SAVE_CARDS; i++)
                if (savedata_card_used(i)) {
                    g.card_cursor = i;
                    break;
                }
        g.mode = MODE_CARDS;
        g.message = NULL;
    } else if (pad_pressed(PSP_CTRL_SQUARE) && card >= 0) { /* -> mode 7 */
        ui_ask_open(&g.ask, team_string(0x1D), yes_no(0x1F), yes_no(0x20), 1);
        g.mode = MODE_UNREGISTER;
    } else if (pad_pressed(PSP_CTRL_TRIANGLE) && card >= 0) { /* -> mode 8 */
        g.spot = savedata_team_spot(team, g.slot);
        if (g.spot < 0)
            g.spot = free_spot(team, g.slot);
        g.mode = MODE_FORMATION;
        g.message = NULL;
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = MODE_TOP;
        battle_menu_field(0x28, 0); /* back on the top menu */
        g.message = NULL;
    }
}

#define CARD_ROWS 9

static void update_cards(void)
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
    int team = team_menu_selected();
    if (pad_pressed(PSP_CTRL_CIRCLE) && team >= 0) {
        sound_se(SE_DECIDE, 1.0f);
        /* FUN_08ac8144: a blank card returns 9, string 0x24. The weight, manual-OKE
         * and team-count rules it also checks (FUN_08891fc8) aren't ported. */
        if (!savedata_card_used(g.card_cursor)) {
            g.message = team_string(0x24);
            return;
        }
        /* A new member takes the first free spot (FUN_08ac8eec); a replaced one
         * keeps its own. */
        int spot = savedata_team_member(team, g.slot) >= 0 ? savedata_team_spot(team, g.slot) : -1;
        if (spot < 0)
            spot = free_spot(team, g.slot);
        savedata_team_set_member(team, g.slot, g.card_cursor, spot);
        SDL_Log("[team] team %d member %d: card %d on spot %d", team + 1, g.slot + 1, g.card_cursor + 1, spot);
        g.mode = MODE_MEMBERS;
        g.message = NULL;
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = MODE_MEMBERS;
        g.message = NULL;
    }
}

/* Formation: the slot's member moves over the 3 x 3 grid (spot = row x 3 + column;
 * row 0 is the front, column 0 the left as the team faces the enemy: the grid's
 * x = +60 at 0x08bd7d10, which battle_sim.c mirrors for your side). A spot
 * another member stands on is swapped. */
static void update_formation(void)
{
    int team = team_menu_selected();
    int row = g.spot / 3, col = g.spot % 3;
    if (pad_repeat(PSP_CTRL_UP) && row > 0)
        row--;
    else if (pad_repeat(PSP_CTRL_DOWN) && row < 2)
        row++;
    else if (pad_repeat(PSP_CTRL_LEFT) && col > 0)
        col--;
    else if (pad_repeat(PSP_CTRL_RIGHT) && col < 2)
        col++;
    if (row * 3 + col != g.spot)
        sound_se(SE_CURSOR, 1.0f);
    g.spot = row * 3 + col;
    if (pad_pressed(PSP_CTRL_CIRCLE) && team >= 0) {
        sound_se(SE_DECIDE, 1.0f);
        int old = savedata_team_spot(team, g.slot);
        for (int m = 0; m < TEAM_MEMBERS; m++)
            if (m != g.slot && savedata_team_member(team, m) >= 0 && savedata_team_spot(team, m) == g.spot)
                savedata_team_set_member(team, m, savedata_team_member(team, m), old >= 0 ? old : free_spot(team, m));
        savedata_team_set_member(team, g.slot, savedata_team_member(team, g.slot), g.spot);
        g.mode = MODE_MEMBERS;
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        g.mode = MODE_MEMBERS;
    }
}

static void update_ask(float dt)
{
    int r = ui_ask_update(&g.ask, dt);
    if (r < 0)
        return;
    int team = g.mode == MODE_DELETE ? g.team_cursor : team_menu_selected();
    if (r == 1 && g.mode == MODE_DELETE) { /* deleteTeam */
        savedata_team_delete(team);
        if (g_selected == team)
            g_selected = -1;
        SDL_Log("[team] team %d deleted", team + 1);
    } else if (r == 1 && g.mode == MODE_UNREGISTER) { /* FUN_08ac854c */
        savedata_team_set_member(team, g.slot, -1, -1);
    }
    g.mode = g.mode == MODE_DELETE ? MODE_TEAMS : MODE_MEMBERS;
}

int team_menu_update(float dt)
{
    ui_menu_update(&g.top, dt);
    g.top.active = g.mode == MODE_TOP;
    if (g.mode == MODE_TOP && ui_menu_input(&g.top))
        g.message = NULL;
    switch (g.mode) {
    case MODE_TOP: update_top(); break;
    case MODE_TEAMS: update_teams(); break;
    case MODE_MEMBERS: update_members(); break;
    case MODE_CARDS: update_cards(); break;
    case MODE_FORMATION: update_formation(); break;
    case MODE_NAME: update_name(); break;
    case MODE_EMBLEM: /* FUN_08ac30f0: back to the top when the editor closes */
        if (!emblem_update(dt))
            g.mode = MODE_TOP;
        break;
    case MODE_UNREGISTER:
    case MODE_DELETE: update_ask(dt); break;
    }
    if (g.closing) {
        SDL_Log("[team] back to the Battle Mode menu");
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

/* The emblem as CTeamDataBase keeps it (FUN_08ad2b54 makes a texture of it): 32 x 32
 * pixels of 4 bits, the low nibble first, then 16 palette words (A B G R). */
static void draw_emblem(const uint8_t *e, int x, int y)
{
    ui_fill(x - 1, y - 1, 34, 34, 0xFF303030);
    if (!e)
        return;
    for (int py = 0; py < 32; py++)
        for (int px = 0; px < 32;) {
            int c = (e[py * 16 + px / 2] >> ((px & 1) * 4)) & 15, run = 1;
            while (px + run < 32 && ((e[py * 16 + (px + run) / 2] >> (((px + run) & 1) * 4)) & 15) == c)
                run++;
            uint32_t w;
            memcpy(&w, e + 0x200 + 4 * c, 4);
            if (w >> 24)
                ui_fill(x + px, y + py, run, 1, w);
            px += run;
        }
}

void team_panel_draw(int team, int x, int y, int cursor)
{
    ui_frame(ui_part(PART_BF_SWND4), x, y, 250, 136, 8, 1.0f);
    if (team < 0 || savedata_team_status(team) == 0) {
        Picture *none = ui_part(PART_BF_NOENTRY0);
        if (none)
            picture_draw_ex(none, x + 90, y + 50, 0, 70, 36, 0, 0, picture_width(none), picture_height(none),
                            BLEND_ALPHA, 0xFFFFFFFF, 0, 0, 0, 0);
        return;
    }
    draw_emblem(savedata_team_emblem(team), x + 12, y + 10);
    char name[TEAM_NAME_MAX + 1], line[64];
    savedata_team_name(team, name, sizeof name);
    ui_text(name, dark(), x + 54, y + 10, 1.0f);
    /* "# of OKEs  n / 3 OKE(s)" (strings 0x12, 0x14, 0x15) */
    snprintf(line, sizeof line, "%s  %d %s %d %s", team_string(0x12), savedata_team_count(team), team_string(0x14),
             TEAM_MEMBERS, team_string(0x15));
    ui_text(line, small(), x + 54, y + 28, 1.0f);
    for (int m = 0; m < TEAM_MEMBERS; m++) {
        int ry = y + 52 + m * 26;
        if (m == cursor)
            ui_frame(ui_part(PART_BF_CUR0), x + 6, ry - 3, 238, 24, 6, ui_pulse(SDL_GetTicks() / 1000.0f));
        ui_text(team_string(0x16 + m), small(), x + 12, ry, 1.0f); /* First / Second / Third OKE */
        int card = savedata_team_member(team, m);
        if (card >= 0)
            card_line(card, line, sizeof line);
        else
            snprintf(line, sizeof line, "-");
        ui_text(line, dark(), x + 90, ry + 4, 1.0f);
    }
}

/* The formation grid beside the team window: the members' spots, the moving one
 * highlighted. The front row is at the top. */
static void draw_formation(int team, int x, int y)
{
    ui_frame(ui_part(PART_BF_SWND2), x, y, 92, 92, 8, 1.0f);
    for (int s = 0; s < SPOTS; s++) {
        int cx = x + 12 + (s % 3) * 24, cy = y + 12 + (s / 3) * 24;
        ui_fill(cx, cy, 20, 20, 0x60303030);
        for (int m = 0; m < TEAM_MEMBERS; m++) {
            bool moving = g.mode == MODE_FORMATION && m == g.slot;
            int spot = moving ? g.spot : savedata_team_spot(team, m);
            if (savedata_team_member(team, m) < 0 || spot != s)
                continue;
            ui_fill(cx + 2, cy + 2, 16, 16, moving ? 0xFF40C0FF : 0xFFC08040);
            char n[4];
            snprintf(n, sizeof n, "%d", m + 1);
            ui_text(n, small(), cx + 6, cy + 4, 1.0f);
        }
    }
}

static void draw_list(int x, int y, int w, int rows, int first, int count, int cursor,
                      void (*label)(int, char *, size_t), const char *title)
{
    ui_frame(ui_part(PART_BF_SWND4), x, y, w, rows * 18 + 30, 8, 1.0f);
    if (title)
        ui_text(title, dark(), x + 10, y + 6, 1.0f);
    for (int r = 0; r < rows && first + r < count; r++) {
        int i = first + r, ry = y + 24 + r * 18;
        if (i == cursor)
            ui_frame(ui_part(PART_BF_CUR0), x + 4, ry - 3, w - 8, 18, 6, ui_pulse(SDL_GetTicks() / 1000.0f));
        char text[64];
        label(i, text, sizeof text);
        ui_text(text, dark(), x + 12, ry, 1.0f);
    }
}

static const char *help_text(void)
{
    if (g.message)
        return g.message;
    switch (g.mode) {
    case MODE_TOP: return team_string(g.top.item[g.top.cursor].help);
    case MODE_TEAMS: return team_string(0x3B);
    case MODE_MEMBERS: return team_string(0x34);
    case MODE_CARDS: return team_string(0x3C);
    case MODE_FORMATION: return team_string(0x3D);
    case MODE_EMBLEM: return emblem_help();
    default: return NULL;
    }
}

void team_menu_draw(void)
{
    sceGuEnable(GU_BLEND);
    Picture *back = ui_part(PART_BF_BACK);
    if (back)
        picture_draw(back, 0, 0, 0, BLEND_REPLACE, 0xFFFFFFFF, 0, 0);
    else
        ui_fill(0, 0, 480, 272, 0xFF404040);
    /* The title: Battle Mode's banner for its item 2, bf_ttl2. */
    Picture *banner = ui_part(PART_BF_TTL0 + 2);
    ui_pic(banner, 0, 0, 1.0f, false);
    TextStyle tst = text_style(0, 7, 14, 14, 0, 0, 0xFFFFFFFF, 0);
    /* the title pushed with the mode (pushMenu FUN_08ac408c: 7 for Emblems) */
    ui_text(team_string(g.mode == MODE_EMBLEM ? 7 : 2), &tst, 64, (banner ? picture_height(banner) : 24) / 2 - 8, 1.0f);
    if (g.mode != MODE_EMBLEM)
        ui_menu_draw(&g.top);
    int sel = team_menu_selected();
    switch (g.mode) {
    case MODE_EMBLEM:
        emblem_draw();
        break;
    case MODE_NAME:
    case MODE_TOP:
        team_panel_draw(sel, 190, 65, -1); /* CTeamDataWnd at (0xBE, 0x41) */
        break;
    case MODE_TEAMS:
    case MODE_DELETE:
        draw_list(190, 16, 226, TEAM_ROWS, g.team_scroll, SAVE_TEAMS, g.team_cursor, team_label, team_string(4));
        break;
    case MODE_MEMBERS:
    case MODE_UNREGISTER:
    case MODE_FORMATION:
        team_panel_draw(sel, 205, 12, g.slot); /* the Register Member window at (0xCD, 0xC) */
        draw_formation(sel, 370, 152);
        break;
    case MODE_CARDS:
        team_panel_draw(sel, 205, 12, g.slot);
        draw_list(102, 40, 190, CARD_ROWS, g.card_scroll, SAVE_CARDS, g.card_cursor, card_line, team_string(3));
        break;
    }
    if (g.mode == MODE_DELETE || g.mode == MODE_UNREGISTER)
        ui_ask_draw(&g.ask);
    ui_help_draw(help_text());
    osk_draw(); /* over everything, as the system keyboard is */
    sceGuEnable(GU_TEXTURE_2D);
}
