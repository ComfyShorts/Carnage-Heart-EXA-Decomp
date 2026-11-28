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
 *     0xB  Transfer Team Data: From Save Data, Wireless Connection, Team Trade
 *          Folder, beside CTeamDataDockInfoWnd's counts (FUN_08ac1e38 / FUN_08ac3138)
 *     0xC  From Save Data: a Story or Battle Data file read (FUN_08ac6318 / FUN_08ac6aec)
 *     0xF  its 16 teams, one installed with circle (FUN_08ac1f4c / FUN_08ac6db8)
 *     0xE  Team Trade Folder: Import, Export (FUN_08ac21e4 / FUN_08ac3444)
 *     0x10 Import, 0x11 Export: a team file of ULJM05698TEAM (FUN_08ac2264 / 361c,
 *          FUN_08ac2338 / 3704)
 * The installing and the team files are team_trade.c. Wireless Connection (network
 * play) is not ported; choosing it is logged. */
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
#include "team_trade.h"
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
    MODE_TRANSFER = 0xB,
    MODE_FROM_SAVE = 0xC,
    MODE_FOLDER = 0xE,
    MODE_INSTALL = 0xF,
    MODE_IMPORT = 0x10,
    MODE_EXPORT = 0x11,
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
    UiMenu sub;            /* Transfer Team Data's menu (+0x8C), the folder's (+0x2B0) */
    UiMenu files;          /* the save chooser (the port's stand-in for CSaveMngr's) */
    int io;                /* the chooser's step: 0 the kind, 1 listing, 2 choosing, 3 busy */
    SaveKind kind;
    int pick_cursor, pick_scroll; /* mode 0xF's team list */
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

#define TEAM_NAME_INPUT 12
#define TEAM_ROWS_PICK 7 /* FUN_08ac1bec's limit (string 0x35: "up to 12 characters") */

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

/* ---- Transfer Team Data ---- */

/* The menu of mode 0xB (items 0xC-0xE, help + 0x32) or 0xE (Import 0xF, Export 0x10,
 * help + 0x33), in the top menu's place while it's hidden. */
static void build_sub(int first, int count, int help)
{
    ui_menu_init(&g.sub, 0, 0x18 + 4, 24 - 5, 12, text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0));
    for (int i = 0; i < count; i++)
        ui_menu_add(&g.sub, team_string(first + i), first + i, first + i + help, PART_BF_MENB3, PART_BF_MENL3);
    ui_menu_open(&g.sub);
}

static void open_transfer(int cursor)
{
    build_sub(0xC, 3, 0x32);
    g.sub.cursor = cursor;
    g.mode = MODE_TRANSFER;
}

static void open_folder(int cursor)
{
    build_sub(0xF, 2, 0x33);
    g.sub.cursor = cursor;
    g.mode = MODE_FOLDER;
}

static const char *k_story_data(void) { return gamedata_string(0x00437D58u); }  /* "Story Data" */
static const char *k_battle_data(void) { return gamedata_string(0x00437D64u); } /* "Battle Data" */

/* The chooser: the kind of save (From Save Data only), then its files. */
static void chooser_kinds(void)
{
    ui_menu_init(&g.files, 200, 84, 24 - 5, 12, text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0));
    ui_menu_add(&g.files, k_story_data(), SAVE_STORY, 0, PART_BF_MENB4, PART_BF_MENL4);
    ui_menu_add(&g.files, k_battle_data(), SAVE_BATTLE, 0, PART_BF_MENB4, PART_BF_MENL4);
    ui_menu_open(&g.files);
    g.io = 0;
    g.message = "Choose the kind of data to install from.";
}

static void chooser_list(SaveKind kind)
{
    g.kind = kind;
    g.io = 1;
    savedata_list_start(kind);
}

static void chooser_build(void)
{
    const SaveListEntry *list;
    int n = savedata_list(&list);
    ui_menu_init(&g.files, 200, 84, 24 - 5, 12, text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0));
    static char names[UI_MENU_MAX][SAVE_FILE_MAX];
    for (int i = 0; i < n && i < UI_MENU_MAX; i++) {
        snprintf(names[i], sizeof names[i], "%.*s", (int)strlen(list[i].file) - 4, list[i].file);
        ui_menu_add(&g.files, names[i], i, 0, PART_BF_MENB4, PART_BF_MENL4)->disabled = list[i].broken;
    }
    g.io = 2;
    if (n > 0) {
        ui_menu_open(&g.files);
        g.message = g.kind == SAVE_TEAM    ? "Choose the Team Data to install."
                    : g.kind == SAVE_STORY ? "Choose the Story Data to install from."
                                           : "Choose the Battle Data to install from.";
    } else {
        g.message = savedata_result() ? "The save data couldn't be read."
                    : g.kind == SAVE_TEAM ? "There is no Team Data."
                    : g.kind == SAVE_STORY ? "There is no Story Data." : "There is no Battle Data.";
    }
}

/* The file under the chooser's cursor, NULL when there's none or it's damaged. */
static const char *chooser_file(void)
{
    const SaveListEntry *list;
    int n = savedata_list(&list);
    if (g.files.cursor < 0 || g.files.cursor >= n || list[g.files.cursor].broken)
        return NULL;
    return list[g.files.cursor].file;
}

/* Mode 0xB (FUN_08ac3138): From Save Data and the folder only while there's room for a
 * team and a card (FUN_08abd210 / FUN_08abd238), else the error sound. */
static void update_transfer(float dt)
{
    ui_menu_update(&g.sub, dt);
    if (ui_menu_input(&g.sub))
        g.message = NULL;
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        ui_menu_close(&g.sub);
        g.mode = MODE_TOP;
        g.message = NULL;
        return;
    }
    if (!pad_pressed(PSP_CTRL_CIRCLE))
        return;
    switch (ui_menu_id(&g.sub)) {
    case 0xC: /* From Save Data -> mode 0xC */
        if (team_trade_full()) {
            sound_se(SE_ERROR, 1.0f);
            g.message = team_string(team_trade_teams_used() >= SAVE_TEAMS ? 0x46 : 0x47);
            return;
        }
        sound_se(SE_DECIDE, 1.0f);
        g.mode = MODE_FROM_SAVE;
        chooser_kinds();
        break;
    case 0xD: /* Wireless Connection -> mode 0xD (network play) */
        sound_se(SE_DECIDE, 1.0f);
        SDL_Log("[team] \"%s\" isn't ported (network play)", team_string(0xD));
        g.message = team_string(0xD);
        break;
    default: /* Team Trade Folder -> mode 0xE */
        sound_se(SE_DECIDE, 1.0f);
        open_folder(0);
        g.message = NULL;
        break;
    }
}

/* Mode 0xC (FUN_08ac6aec): the save read, then its teams (mode 0xF); cancel goes back. */
static void update_from_save(float dt)
{
    ui_menu_update(&g.files, dt);
    switch (g.io) {
    case 0:
        ui_menu_input(&g.files);
        if (pad_pressed(PSP_CTRL_CROSS)) {
            sound_se(SE_CANCEL, 1.0f);
            open_transfer(0);
            g.message = NULL;
        } else if (pad_pressed(PSP_CTRL_CIRCLE)) {
            sound_se(SE_DECIDE, 1.0f);
            g.message = NULL;
            chooser_list((SaveKind)ui_menu_id(&g.files));
        }
        break;
    case 1:
        if (!savedata_list_update())
            chooser_build();
        break;
    case 2:
        if (g.files.count)
            ui_menu_input(&g.files);
        if (pad_pressed(PSP_CTRL_CROSS)) {
            sound_se(SE_CANCEL, 1.0f);
            chooser_kinds();
        } else if (pad_pressed(PSP_CTRL_CIRCLE) && g.files.count) {
            const char *f = chooser_file();
            if (!f) {
                sound_se(SE_ERROR, 1.0f);
                g.message = "This data is damaged.";
                break;
            }
            sound_se(SE_DECIDE, 1.0f);
            savedata_peek_start(g.kind, f);
            g.message = "Loading...";
            g.io = 3;
        }
        break;
    case 3:
        if (savedata_io_update())
            break;
        if (savedata_result() != 0) {
            g.message = "The save data couldn't be read.";
            g.io = 2;
            break;
        }
        g.message = NULL;
        g.pick_cursor = g.pick_scroll = 0;
        g.mode = MODE_INSTALL; /* FUN_08ac1f4c: title 0xC pushed */
        break;
    }
}

static void peek_label(int team, char *out, size_t n)
{
    const uint8_t *t = savedata_peeked()->team[team];
    uint32_t status;
    memcpy(&status, t, 4);
    if (status == 0) {
        snprintf(out, n, "%02d  -", team + 1);
        return;
    }
    char name[TEAM_NAME_MAX + 1];
    snprintf(name, sizeof name, "%.*s", TEAM_NAME_MAX - 1, (const char *)t + 0x250);
    snprintf(out, n, "%02d  %s  (%d)", team + 1, name, team_trade_distinct_cards(t));
}

/* Mode 0xF (FUN_08ac6db8): circle installs the team under the cursor and goes back to
 * the menu; one that has no member or doesn't fit gives the error sound; cross goes
 * back. */
static void update_install(void)
{
    int old = g.pick_cursor;
    if (pad_repeat(PSP_CTRL_UP))
        g.pick_cursor = g.pick_cursor > 0 ? g.pick_cursor - 1 : pad_trigger(PSP_CTRL_UP) ? SAVE_TEAMS - 1 : 0;
    else if (pad_repeat(PSP_CTRL_DOWN))
        g.pick_cursor = g.pick_cursor < SAVE_TEAMS - 1 ? g.pick_cursor + 1 : pad_trigger(PSP_CTRL_DOWN) ? 0 : g.pick_cursor;
    if (g.pick_cursor != old) {
        sound_se(SE_CURSOR, 1.0f);
        g.message = NULL;
    }
    if (g.pick_cursor < g.pick_scroll)
        g.pick_scroll = g.pick_cursor;
    if (g.pick_cursor >= g.pick_scroll + TEAM_ROWS_PICK)
        g.pick_scroll = g.pick_cursor - TEAM_ROWS_PICK + 1;
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        open_transfer(0);
        g.message = NULL;
    } else if (pad_pressed(PSP_CTRL_CIRCLE)) {
        if (team_trade_install(savedata_peeked(), g.pick_cursor)) {
            sound_se(SE_DECIDE, 1.0f);
            open_transfer(0);
            g.message = NULL;
        } else {
            sound_se(SE_ERROR, 1.0f);
        }
    }
}

/* Mode 0xE (FUN_08ac3444): Import needs room (as From Save Data); Export the selected
 * team of the player's with a member. */
static void update_folder(float dt)
{
    ui_menu_update(&g.sub, dt);
    if (ui_menu_input(&g.sub))
        g.message = NULL;
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        open_transfer(2);
        g.message = NULL;
        return;
    }
    if (!pad_pressed(PSP_CTRL_CIRCLE))
        return;
    if (ui_menu_id(&g.sub) == 0xF) { /* Import -> mode 0x10 */
        if (team_trade_full()) {
            sound_se(SE_ERROR, 1.0f);
            g.message = team_string(team_trade_teams_used() >= SAVE_TEAMS ? 0x46 : 0x47);
            return;
        }
        sound_se(SE_DECIDE, 1.0f);
        g.mode = MODE_IMPORT;
        chooser_list(SAVE_TEAM);
        g.message = NULL;
    } else { /* Export -> mode 0x11 */
        int team = own_team();
        if (team < 0 || savedata_team_count(team) < 1) {
            sound_se(SE_ERROR, 1.0f);
            g.message = team_string(team < 0 ? 0x48 : 0x49);
            return;
        }
        sound_se(SE_DECIDE, 1.0f);
        g.mode = MODE_EXPORT;
        chooser_list(SAVE_TEAM); /* the folder's names, for a free TEAM<n> */
        g.message = NULL;
    }
}

/* Mode 0x10: a team file chosen, read, checked and installed (FUN_08abba9c type 5). */
static void update_import(float dt)
{
    ui_menu_update(&g.files, dt);
    switch (g.io) {
    case 1:
        if (!savedata_list_update())
            chooser_build();
        break;
    case 2:
        if (g.files.count)
            ui_menu_input(&g.files);
        if (pad_pressed(PSP_CTRL_CROSS)) {
            sound_se(SE_CANCEL, 1.0f);
            open_folder(0);
            g.message = NULL;
        } else if (pad_pressed(PSP_CTRL_CIRCLE) && g.files.count) {
            const char *f = chooser_file();
            if (!f) {
                sound_se(SE_ERROR, 1.0f);
                g.message = "This data is damaged.";
                break;
            }
            sound_se(SE_DECIDE, 1.0f);
            savedata_load_start(SAVE_TEAM, f);
            g.message = "Loading...";
            g.io = 3;
        }
        break;
    case 3:
        if (savedata_io_update())
            break;
        if (savedata_result() != 0 || team_trade_import(savedata_team_file()) < 0) {
            sound_se(SE_ERROR, 1.0f);
            g.message = "This data is damaged.";
            g.io = 2;
            break;
        }
        sound_se(SE_DECIDE, 1.0f);
        open_folder(0);
        g.message = NULL;
        break;
    }
}

/* FUN_08a65374: the first TEAM<n> (1-1024) no file in the folder has. */
static void free_team_file(char *out, size_t n)
{
    const SaveListEntry *list;
    int count = savedata_list(&list);
    static bool used[0x401];
    memset(used, 0, sizeof used);
    for (int i = 0; i < count; i++) {
        if (strncmp(list[i].file, "TEAM", 4))
            continue;
        int v = -1;
        for (int k = 4; k < 8 && list[i].file[k]; k++)
            if (list[i].file[k] >= '0' && list[i].file[k] <= '9')
                v = (v < 0 ? 0 : v * 10) + (list[i].file[k] - '0');
        if (v >= 0 && v < 0x401)
            used[v] = true;
    }
    int v = 1;
    while (v < 0x401 && used[v])
        v++;
    snprintf(out, n, "TEAM%d.CHE", v);
}

/* Mode 0x11: the selected team written to a new file of the folder (FUN_08ab7820). */
static void update_export(void)
{
    switch (g.io) {
    case 1: {
        if (savedata_list_update())
            break;
        static uint8_t file[TEAM_FILE_SIZE];
        char name[SAVE_FILE_MAX];
        if (!team_trade_export(own_team(), file)) {
            sound_se(SE_ERROR, 1.0f);
            open_folder(1);
            break;
        }
        free_team_file(name, sizeof name);
        savedata_team_file_set(file);
        savedata_save_start(SAVE_TEAM, name);
        g.message = "Saving...";
        g.io = 3;
        break;
    }
    case 3:
        if (savedata_io_update())
            break;
        sound_se(savedata_result() == 0 ? SE_DECIDE : SE_ERROR, 1.0f);
        open_folder(1);
        g.message = savedata_result() == 0 ? NULL : "The data couldn't be saved.";
        break;
    }
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
        default: /* -> mode 0xB (FUN_08ac1e38: title 8 pushed, the counts backed up) */
            open_transfer(0);
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
    case MODE_TRANSFER: update_transfer(dt); break;
    case MODE_FROM_SAVE: update_from_save(dt); break;
    case MODE_INSTALL: update_install(); break;
    case MODE_FOLDER: update_folder(dt); break;
    case MODE_IMPORT: update_import(dt); break;
    case MODE_EXPORT: update_export(); break;
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

static bool transfer_mode(void)
{
    return g.mode >= MODE_TRANSFER && g.mode <= MODE_EXPORT && g.mode != 0xD;
}

/* CTeamDataDockInfoWnd's draw (FUN_08abd3c8): "# of Teams" and "# of OKEs", each the
 * cards in use plus what the team under mode 0xF's cursor would add, "/", the most
 * (16, 32), "Team(s)" / "OKE(s)"; the window is 0xCD x 0x30. */
static void draw_dock(int x, int y)
{
    int add_teams = 0, add_cards = 0;
    if (g.mode == MODE_INSTALL) {
        const uint8_t *t = savedata_peeked()->team[g.pick_cursor];
        uint32_t status;
        memcpy(&status, t, 4);
        add_teams = status != 0;
        add_cards = team_trade_distinct_cards(t);
    }
    ui_frame(ui_part(PART_BF_SWND4), x, y, 0xCD, 0x30, 8, 1.0f);
    char n[16];
    for (int row = 0; row < 2; row++) {
        int ry = y + (row ? 0x18 : 7);
        int used = row ? team_trade_cards_used() + add_cards : team_trade_teams_used() + add_teams;
        int most = row ? SAVE_CARDS : SAVE_TEAMS;
        ui_text(team_string(row ? 0x12 : 0x11), small(), x + 10, ry, 1.0f);
        snprintf(n, sizeof n, "%d", used);
        ui_text(n, small(), x + 0x4B, ry, 1.0f);
        ui_text(team_string(0x14), small(), x + 0x61, ry, 1.0f);
        snprintf(n, sizeof n, "%d", most);
        ui_text(n, small(), x + 0x69, ry, 1.0f);
        ui_text(team_string(row ? 0x15 : 0x13), small(), x + 0x7D, ry, 1.0f);
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
    case MODE_TRANSFER:
    case MODE_FOLDER: return team_string(g.sub.item[g.sub.cursor].help);
    case MODE_INSTALL: return team_string(0x41); /* "Select a Team Card." */
    case MODE_IMPORT: return team_string(0x42);
    case MODE_EXPORT: return team_string(0x43);
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
    /* the title pushed with the mode (pushMenu FUN_08ac408c: 7 Emblems, 8 Transfer
     * Team Data, 0xC From Save Data) */
    int title = g.mode == MODE_EMBLEM                                  ? 7
                : g.mode == MODE_FROM_SAVE || g.mode == MODE_INSTALL ? 0xC
                : transfer_mode()                                    ? 8
                                                                     : 2;
    ui_text(team_string(title), &tst, 64, (banner ? picture_height(banner) : 24) / 2 - 8, 1.0f);
    if (transfer_mode()) {
        ui_menu_draw(&g.sub);
        draw_dock(200, 24);
        if (g.mode == MODE_INSTALL)
            draw_list(200, 80, 226, TEAM_ROWS_PICK, g.pick_scroll, SAVE_TEAMS, g.pick_cursor, peek_label,
                      team_string(0x41));
        else if (g.mode == MODE_FROM_SAVE || g.mode == MODE_IMPORT)
            ui_menu_draw(&g.files);
    } else if (g.mode != MODE_EMBLEM)
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
    default: /* the transfer modes, drawn above */
        break;
    }
    if (g.mode == MODE_DELETE || g.mode == MODE_UNREGISTER)
        ui_ask_draw(&g.ask);
    ui_help_draw(help_text());
    osk_draw(); /* over everything, as the system keyboard is */
    sceGuEnable(GU_TEXTURE_2D);
}
