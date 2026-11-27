/* See battle_menu.h and docs/screens.md. Hand-written from the functions named
 * there; sound calls, the save data, the 3D scenes behind the menus, network play and
 * the screens not listed are left out and logged. */
#include "battle_menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "font.h"
#include "gamedata.h"
#include "gu.h"
#include "pad.h"
#include "psp_shim.h"
#include "ui.h"

/* ---- data in the EBOOT (splat addresses) ---- */

#define STR_MODE 0x004766B0u    /* CBattleModeMngr's strings (FUN_088af824), 0x3F */
#define STR_SELECT 0x00475410u  /* CBattleSelectString (FUN_088861e4), 0x16 */
#define STR_TEST 0x00466D50u    /* CBattleTestString (FUN_0882bce4), 0x5A */
#define STR_ARENA 0x00465338u   /* CBattleArenaString (FUN_0880ff74), 0x33 */
#define STAGE_NAMES 0x004812F8u /* CPresetStage names (FUN_08a359dc), 0xAF */
#define STAGE_TABLE 0x0041D854u /* CPresetStage entries, 0x14 bytes: map, local type, ... */
#define STAGE_PART 0x00434FC4u  /* CStageSelectWnd: stage -> preview part (FUN_08aaf62c) */
#define STAGE_ORDER 0x0043502Cu /* stage -> list position (FUN_08aaf678) */
#define MODE_ITEMS 0x00472BE0u  /* the top menu's item ids, 5 words */
#define ARENA_NAMES 0x0044D8F0u /* the opponents' names, 100 pointers */
#define ARENA_RECORDS 0x0044DC10u /* their CBattleSettingInfo records (FUN_08807dbc) */
#define ARENA_STRIDE 0x3C0u

#define ARENA_COUNT 100
#define STAGE_LIST 26 /* CStageSelectWnd: stages 0-24 and 25, "Select Randomly" */
#define STAGE_RANDOM 25

static const char *table_string(uint32_t table, int id, int count)
{
    if (id < 0 || id >= count)
        id = 1; /* "<<Error>>", as the getters do */
    uint32_t p = gamedata_word(table + 4u * (uint32_t)id, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

static const char *s_mode(int id) { return table_string(STR_MODE, id, 0x3F); }
static const char *s_select(int id) { return table_string(STR_SELECT, id, 0x16); }
static const char *s_test(int id) { return table_string(STR_TEST, id, 0x5A); }
static const char *s_arena(int id) { return table_string(STR_ARENA, id, 0x33); }
static const char *s_stage(int stage) { return table_string(STAGE_NAMES, stage, 0xAF); }

static int stage_map(int stage)
{
    return (int)gamedata_word(STAGE_TABLE + (uint32_t)stage * 0x14u, (uint32_t)(stage < 25 ? stage : -1));
}

static int stage_part(int stage)
{
    return (int)gamedata_word(STAGE_PART + 4u * (uint32_t)stage, (uint32_t)(PART_BF_MPI00 + stage));
}

static int arena_word(int i, uint32_t off, uint32_t fallback)
{
    return (int)gamedata_word(ARENA_RECORDS + (uint32_t)i * ARENA_STRIDE + off, fallback);
}

static float arena_float(int i, uint32_t off)
{
    uint32_t w = gamedata_word(ARENA_RECORDS + (uint32_t)i * ARENA_STRIDE + off, 0);
    float f;
    memcpy(&f, &w, sizeof f);
    return f;
}

/* CBattleSettingInfo::GetEnemyOke (FUN_08886384): the record's +0x14 + 0x40 n. */
static const char *arena_oke(int i, int n)
{
    return gamedata_string(ARENA_RECORDS + (uint32_t)i * ARENA_STRIDE + 0x14u + 0x40u * (uint32_t)n);
}

static const char *arena_name(int i)
{
    return table_string(ARENA_NAMES, i, ARENA_COUNT);
}

/* ---- the OKE cards ----
 * CCarnageHeart::Reinitialize (FUN_0889f260) leaves the player's 32 cards
 * (FUN_0889ec18) blank and loads the two simulation OKEs into slots 32 + 6 and
 * 32 + 7. Save data isn't read, so that's what the card list holds. */

#define PLAYER_CARDS 32
#define CARD_COUNT (PLAYER_CARDS + 2)

static const char *card_path(int card)
{
    if (card == PLAYER_CARDS)
        return "preset/oke_t01_1.bin";
    if (card == PLAYER_CARDS + 1)
        return "preset/oke_t01_2.bin";
    return NULL; /* blank */
}

static void card_label(int card, char *out, size_t n)
{
    if (card >= PLAYER_CARDS)
        snprintf(out, n, "Simulation OKE %d", card - PLAYER_CARDS + 1);
    else
        snprintf(out, n, "%02d  -", card + 1);
}

/* ---- screens ---- */

typedef enum {
    SCR_MODE,       /* CBattleModeMngr top menu */
    SCR_SELECT,     /* CBattleSelectMngr */
    SCR_TEST,       /* CBattleTestMngr top menu */
    SCR_TEST_TEAM,  /* Select OKE to Test: the team windows */
    SCR_CARDS,      /* the OKE card list (COkeCardSelectMngr) */
    SCR_FIELD,      /* the field menu: Select Map, Map Size, Barricade */
    SCR_STAGES,     /* CStageSelectWnd */
    SCR_ARENA,      /* CBattleArenaMngr top menu */
    SCR_OPPONENTS,  /* CBattleArenaSelectWnd */
    SCR_ASK,        /* the Yes/No before the battle */
} ScreenId;

enum { TEAM_ENEMY, TEAM_FRIEND };

static struct {
    ScreenId stack[8];
    int depth;
    UiMenu mode, select, test, field, arena;
    int team_side;          /* Select OKE to Test: 0 Enemy, 1 Friend */
    int team[2];            /* the registered card per team, -1 none */
    int card_cursor, card_scroll;
    int card_for;           /* SCR_TEST_TEAM or SCR_ARENA */
    int stage;              /* the Test Program's stage, 0-25 */
    int stage_cursor, stage_scroll;
    int map_size;           /* 0-2: 180 / 360 / 540 m */
    int barricade;          /* 0-2 */
    int test_time;          /* seconds, 30-300 */
    int opponent, opp_scroll;
    int arena_card;         /* the arena team's card, -1 none */
    UiAsk ask;
    bool ask_arena;
    const char *message;    /* an error in the help window, until the next move */
    char value[4][24];
    BattleSetup setup;
    bool back_to_title;
} g;

static ScreenId top(void)
{
    return g.stack[g.depth - 1];
}

static void push(ScreenId s)
{
    if (g.depth < (int)SDL_arraysize(g.stack))
        g.stack[g.depth++] = s;
    g.message = NULL;
}

static void pop(void)
{
    if (g.depth > 1)
        g.depth--;
    else
        g.back_to_title = true;
    g.message = NULL;
}

static UiMenu *menu_of(ScreenId s)
{
    switch (s) {
    case SCR_MODE: return &g.mode;
    case SCR_SELECT: return &g.select;
    case SCR_TEST: return &g.test;
    case SCR_FIELD: return &g.field;
    case SCR_ARENA: return &g.arena;
    default: return NULL;
    }
}

/* The CTopMenuWnd most menus use: bf_menb4 / bf_menl4 / bf_new00 at (0, 0x18 + 4),
 * buttons 24 high with spacing -5 (FUN_088838f8, FUN_08828db8). */
static void top_menu(UiMenu *m, int x)
{
    ui_menu_init(m, x, 0x18 + 4, 24 - 5, 12, text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0));
}

/* CBattleModeMngr::Initialize (FUN_08838aa0): five buttons from the ids at
 * 0x08c76be0 (1-5), pictures parts 0x87 + id and 0x8D + id (bf_scmb2-6, bf_scml2-6), text id + 4, help id + 0x10 - 1
 * (FUN_088387a4 shows string cursor + 0x10). Shop is disabled when FUN_0883892c
 * finds nothing to buy; nothing is for sale without save data, so it is. */
static void build_mode_menu(void)
{
    ui_menu_init(&g.mode, -8, 0x18 * 2, 32, 40, text_style(0, 7, 14, 14, 0, 0, 0xFFFFFFFF, 0));
    for (int i = 0; i < 5; i++) {
        int id = (int)gamedata_word(MODE_ITEMS + 4u * (uint32_t)i, (uint32_t)(i + 1));
        ui_menu_add(&g.mode, s_mode(id + 4), id, i + 0x10, PART_BF_SCMB1 + id, PART_BF_SCML1 + id);
    }
    g.mode.cursor = 2; /* Battle */
}

/* CBattleSelectMngr::setupTopMenu (FUN_088838f8): items 0-3 (Test Program, Mission,
 * Battle Arena, Matchmaking), text id + 3; in Battle Mode (game mode 2) Mission is
 * left out. Help from FUN_088832a0. */
static void build_select_menu(void)
{
    top_menu(&g.select, 0);
    static const int k_ids[3] = {0, 2, 3}, k_help[3] = {0x10, 0x12, 0x14};
    for (int i = 0; i < 3; i++)
        ui_menu_add(&g.select, s_select(k_ids[i] + 3), k_ids[i], k_help[i], PART_BF_MENB4, PART_BF_MENL4);
}

/* CBattleTestMngr::setupTopMenu (FUN_08828db8): Select OKE to Test, Map, Test Time,
 * Damage Settings, Conditions, Start Test; text id + 3, help id + 0x39. */
static void build_test_menu(void)
{
    top_menu(&g.test, 0);
    for (int id = 0; id < 6; id++)
        ui_menu_add(&g.test, s_test(id + 3), id, id + 0x39, PART_BF_MENB4, PART_BF_MENL4);
}

/* setupFieldMenu (FUN_088291cc): Select Map, Map Size, Barricade; help 0x44-0x46. */
static void build_field_menu(void)
{
    top_menu(&g.field, 136);
    g.field.value_x = 134;
    for (int i = 0; i < 3; i++)
        ui_menu_add(&g.field, s_test(11 + i), i, 0x44 + i, PART_BF_MENB4, PART_BF_MENL4);
}

/* CBattleArenaMngr::setupTopMenu (FUN_0880d424): Select Opponent, Select Team, Start
 * Battle Arena; help 0x2D-0x2F. */
static void build_arena_menu(void)
{
    top_menu(&g.arena, 0);
    for (int i = 0; i < 3; i++)
        ui_menu_add(&g.arena, s_arena(3 + i), i, 0x2D + i, PART_BF_MENB4, PART_BF_MENL4);
}

static void refresh_values(void)
{
    static const int k_size[3] = {180, 360, 540};
    (void)k_size;
    snprintf(g.value[0], sizeof g.value[0], "%d", g.test_time);
    g.test.item[2].value = g.value[0];
    g.field.item[0].value = s_stage(g.stage);
    g.field.item[1].value = s_test(14 + g.map_size);
    g.field.item[2].value = s_test(17 + g.barricade);
}

void battle_menu_start(void)
{
    memset(&g, 0, sizeof g);
    build_mode_menu();
    build_select_menu();
    build_test_menu();
    build_field_menu();
    build_arena_menu();
    g.team[0] = g.team[1] = -1;
    g.arena_card = -1;
    g.stage = 2;         /* normalizeDataBase (FUN_08824084): stage 2 if none is set */
    g.map_size = 2;      /* CBattleParam's reset (FUN_0887e190): 540 m */
    g.barricade = 1;     /* +0x10 = 1 */
    g.test_time = 30;    /* 30 s */
    g.opponent = 50;     /* the first three-OKE team */
    g.opp_scroll = 50 - 3;
    refresh_values();
    g.stack[0] = SCR_MODE;
    g.depth = 1;
    ui_menu_open(&g.mode);
    SDL_Log("[battle-menu] Battle Mode top menu (CBattleModeMngr)");
}

const BattleSetup *battle_menu_setup(void)
{
    return &g.setup;
}

void battle_menu_resume(void)
{
    g.message = NULL;
    UiMenu *m = menu_of(top());
    if (m)
        ui_menu_open(m);
}

/* ---- building the battle (CBattleTestFlow::buildBattle FUN_08823774,
 * CBattleArenaFlow::CImpl::buildBattle FUN_088098e0) ---- */

static int resolve_map(int stage)
{
    int map = stage_map(stage);
    if (map < 0) /* "Select Randomly" */
        map = (int)(SDL_GetTicks() % 25);
    return map;
}

static void build_test_battle(void)
{
    static const float k_size[3] = {180.0f, 360.0f, 540.0f};
    BattleSetup *s = &g.setup;
    memset(s, 0, sizeof *s);
    snprintf(s->oke_path[0], sizeof s->oke_path[0], "%s", card_path(g.team[TEAM_FRIEND]));
    snprintf(s->oke_path[1], sizeof s->oke_path[1], "%s", card_path(g.team[TEAM_ENEMY]));
    card_label(g.team[TEAM_FRIEND], s->oke_label[0], sizeof s->oke_label[0]);
    card_label(g.team[TEAM_ENEMY], s->oke_label[1], sizeof s->oke_label[1]);
    s->stage = g.stage;
    s->map = resolve_map(g.stage);
    s->time_limit = (float)g.test_time;
    s->map_size = k_size[g.map_size];
    s->barricade = g.barricade;
    SDL_Log("[battle-menu] Test Program: %s vs %s on stage %d (map %02d), %d s", s->oke_label[0],
            s->oke_label[1], s->stage, s->map, g.test_time);
}

static void build_arena_battle(void)
{
    BattleSetup *s = &g.setup;
    memset(s, 0, sizeof *s);
    snprintf(s->oke_path[0], sizeof s->oke_path[0], "%s", card_path(g.arena_card));
    card_label(g.arena_card, s->oke_label[0], sizeof s->oke_label[0]);
    const char *oke = arena_oke(g.opponent, 0);
    snprintf(s->oke_path[1], sizeof s->oke_path[1], "%s", oke ? oke : "");
    const char *name = arena_name(g.opponent);
    snprintf(s->oke_label[1], sizeof s->oke_label[1], "%s", name ? name : "?");
    s->stage = arena_word(g.opponent, 0, 75);
    s->map = resolve_map(s->stage);
    float t = arena_float(g.opponent, 0x114); /* -> CBattleParam +8 (FUN_0887e2f8: 30-300) */
    s->time_limit = t >= 30.0f && t <= 300.0f ? t : 30.0f;
    s->map_size = 540.0f;
    s->barricade = 1;
    s->arena = true;
    SDL_Log("[battle-menu] Battle Arena: %s vs %s (%d OKE, the leader fights) on stage %d (map %02d), %.0f s",
            s->oke_label[0], s->oke_label[1], arena_word(g.opponent, 4, 1), s->stage, s->map, s->time_limit);
}

/* ---- per-screen input ---- */

static int update_mode(void)
{
    UiMenu *m = &g.mode;
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        UiMenuItem *it = &m->item[m->cursor];
        if (it->disabled) {
            g.message = s_mode(0x16);
        } else if (it->id == 3) { /* eTOP_MENU_TYPE 2: CBattleSelectFlow */
            push(SCR_SELECT);
            ui_menu_open(&g.select);
        } else {
            SDL_Log("[battle-menu] \"%s\" chosen: that flow isn't ported", it->text ? it->text : "?");
            g.message = it->text;
        }
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        /* The game asks whether to go back to the title (CSystemMngr); the port
         * goes back at once. */
        pop();
    }
    return 1;
}

static int update_select(void)
{
    UiMenu *m = &g.select;
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        switch (ui_menu_id(m)) {
        case 0: /* CBattleTestFlow */
            push(SCR_TEST);
            ui_menu_open(&g.test);
            break;
        case 2: /* CBattleArenaFlow */
            push(SCR_ARENA);
            ui_menu_open(&g.arena);
            break;
        default:
            SDL_Log("[battle-menu] Matchmaking (CMatchMakeFlow, network play) isn't ported");
            g.message = s_select(6);
            break;
        }
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        pop();
        ui_menu_open(&g.mode);
    }
    return 1;
}

static void open_ask(bool arena)
{
    g.ask_arena = arena;
    ui_ask_open(&g.ask, arena ? s_arena(36) : s_test(33), s_test(31), s_test(32), 0);
    push(SCR_ASK);
}

static int update_test(void)
{
    UiMenu *m = &g.test;
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        switch (ui_menu_id(m)) {
        case 0:
            push(SCR_TEST_TEAM);
            break;
        case 1:
            push(SCR_FIELD);
            ui_menu_open(&g.field);
            break;
        case 5: /* Start Test: the checks of procPopUiSynchronizerMode, then the question */
            if (g.team[TEAM_ENEMY] < 0)
                g.message = s_test(84);
            else if (g.team[TEAM_FRIEND] < 0)
                g.message = s_test(85);
            else
                open_ask(false);
            break;
        case 2:
            g.message = s_test(0x3B);
            break;
        default:
            SDL_Log("[battle-menu] \"%s\" isn't ported", m->item[m->cursor].text ? m->item[m->cursor].text : "?");
            g.message = m->item[m->cursor].text;
            break;
        }
    } else if (ui_menu_id(m) == 2 && pad_repeat(PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) {
        /* Test Time: the game takes a number (string 52 "Start Inputting Value");
         * the port steps it by 10 s within CBattleParam's 30-300. */
        g.test_time += pad_repeat(PSP_CTRL_RIGHT) ? 10 : -10;
        g.test_time = SDL_clamp(g.test_time, 30, 300);
        refresh_values();
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        pop();
        ui_menu_open(&g.select);
    }
    return 1;
}

static int update_test_team(void)
{
    if (pad_pressed(PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) {
        g.team_side ^= 1; /* string 47: "Change Team" */
        g.message = NULL;
    } else if (pad_pressed(PSP_CTRL_CIRCLE)) {
        g.card_for = SCR_TEST_TEAM;
        int cur = g.team[g.team_side];
        g.card_cursor = cur >= 0 ? cur : PLAYER_CARDS;
        push(SCR_CARDS);
    } else if (pad_pressed(PSP_CTRL_SQUARE) && g.team[g.team_side] >= 0) {
        g.team[g.team_side] = -1; /* string 48: "Unregister" */
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        pop();
    }
    return 1;
}

#define CARD_ROWS 9

static int update_cards(void)
{
    if (pad_repeat(PSP_CTRL_UP)) {
        if (g.card_cursor > 0)
            g.card_cursor--;
        else if (pad_trigger(PSP_CTRL_UP))
            g.card_cursor = CARD_COUNT - 1;
        g.message = NULL;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        if (g.card_cursor < CARD_COUNT - 1)
            g.card_cursor++;
        else if (pad_trigger(PSP_CTRL_DOWN))
            g.card_cursor = 0;
        g.message = NULL;
    }
    if (g.card_cursor < g.card_scroll)
        g.card_scroll = g.card_cursor;
    if (g.card_cursor >= g.card_scroll + CARD_ROWS)
        g.card_scroll = g.card_cursor - CARD_ROWS + 1;
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        if (!card_path(g.card_cursor)) {
            g.message = g.card_for == SCR_ARENA ? s_arena(49) : s_test(44);
        } else {
            if (g.card_for == SCR_ARENA)
                g.arena_card = g.card_cursor;
            else
                g.team[g.team_side] = g.card_cursor;
            refresh_values();
            pop();
        }
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        pop();
    }
    return 1;
}

static int update_field(void)
{
    UiMenu *m = &g.field;
    int id = ui_menu_id(m);
    if (pad_pressed(PSP_CTRL_CIRCLE) && id == 0) {
        g.stage_cursor = (int)gamedata_word(STAGE_ORDER + 4u * (uint32_t)g.stage, (uint32_t)g.stage);
        push(SCR_STAGES);
    } else if (id > 0 && pad_repeat(PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) {
        int d = pad_repeat(PSP_CTRL_RIGHT) ? 1 : -1;
        int *v = id == 1 ? &g.map_size : &g.barricade;
        *v = (*v + d + 3) % 3;
        refresh_values();
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        pop();
        ui_menu_open(&g.test);
    }
    return 1;
}

/* The stage at a list position: the inverse of the order table. */
static int stage_at(int pos)
{
    for (int s = 0; s < STAGE_LIST; s++)
        if ((int)gamedata_word(STAGE_ORDER + 4u * (uint32_t)s, (uint32_t)s) == pos)
            return s;
    return pos;
}

#define STAGE_ROWS 9

static int update_stages(void)
{
    if (pad_repeat(PSP_CTRL_UP)) {
        if (g.stage_cursor > 0)
            g.stage_cursor--;
        else if (pad_trigger(PSP_CTRL_UP))
            g.stage_cursor = STAGE_LIST - 1;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        if (g.stage_cursor < STAGE_LIST - 1)
            g.stage_cursor++;
        else if (pad_trigger(PSP_CTRL_DOWN))
            g.stage_cursor = 0;
    }
    if (g.stage_cursor < g.stage_scroll)
        g.stage_scroll = g.stage_cursor;
    if (g.stage_cursor >= g.stage_scroll + STAGE_ROWS)
        g.stage_scroll = g.stage_cursor - STAGE_ROWS + 1;
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        g.stage = stage_at(g.stage_cursor);
        refresh_values();
        pop();
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        pop();
    }
    return 1;
}

static int update_arena(void)
{
    UiMenu *m = &g.arena;
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        switch (ui_menu_id(m)) {
        case 0:
            push(SCR_OPPONENTS);
            break;
        case 1: /* the game lists the Team Formation teams; the port takes one card */
            g.card_for = SCR_ARENA;
            g.card_cursor = g.arena_card >= 0 ? g.arena_card : PLAYER_CARDS;
            push(SCR_CARDS);
            break;
        default:
            if (g.arena_card < 0)
                g.message = s_arena(48);
            else
                open_ask(true);
            break;
        }
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        pop();
        ui_menu_open(&g.select);
    }
    return 1;
}

#define OPP_ROWS 9

static int update_opponents(void)
{
    if (pad_repeat(PSP_CTRL_UP)) {
        if (g.opponent > 0)
            g.opponent--;
        else if (pad_trigger(PSP_CTRL_UP))
            g.opponent = ARENA_COUNT - 1;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        if (g.opponent < ARENA_COUNT - 1)
            g.opponent++;
        else if (pad_trigger(PSP_CTRL_DOWN))
            g.opponent = 0;
    } else if (pad_repeat(PSP_CTRL_LEFT)) {
        g.opponent = SDL_max(g.opponent - OPP_ROWS, 0);
    } else if (pad_repeat(PSP_CTRL_RIGHT)) {
        g.opponent = SDL_min(g.opponent + OPP_ROWS, ARENA_COUNT - 1);
    }
    if (g.opponent < g.opp_scroll)
        g.opp_scroll = g.opponent;
    if (g.opponent >= g.opp_scroll + OPP_ROWS)
        g.opp_scroll = g.opponent - OPP_ROWS + 1;
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        refresh_values();
        pop();
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        pop();
    }
    return 1;
}

static int update_ask(float dt)
{
    int r = ui_ask_update(&g.ask, dt);
    if (r < 0)
        return 1;
    pop();
    if (r == 0)
        return 1;
    if (g.ask_arena)
        build_arena_battle();
    else
        build_test_battle();
    return 2;
}

int battle_menu_update(float dt)
{
    for (int i = 0; i < g.depth; i++) {
        UiMenu *m = menu_of(g.stack[i]);
        if (m)
            ui_menu_update(m, dt);
    }
    UiMenu *m = menu_of(top());
    if (m) {
        m->active = true;
        if (ui_menu_input(m))
            g.message = NULL;
    }
    int r = 1;
    switch (top()) {
    case SCR_MODE: r = update_mode(); break;
    case SCR_SELECT: r = update_select(); break;
    case SCR_TEST: r = update_test(); break;
    case SCR_TEST_TEAM: r = update_test_team(); break;
    case SCR_CARDS: r = update_cards(); break;
    case SCR_FIELD: r = update_field(); break;
    case SCR_STAGES: r = update_stages(); break;
    case SCR_ARENA: r = update_arena(); break;
    case SCR_OPPONENTS: r = update_opponents(); break;
    case SCR_ASK: r = update_ask(dt); break;
    }
    for (int i = 0; i < g.depth - 1; i++) { /* only the top menu takes input */
        UiMenu *under = menu_of(g.stack[i]);
        if (under && under != menu_of(top()))
            under->active = false;
    }
    if (g.back_to_title) {
        SDL_Log("[battle-menu] back to the title");
        return 0;
    }
    return r;
}

/* ---- drawing ---- */

static const TextStyle *style_dark(void)
{
    static TextStyle st;
    st = text_style(0, 6, 12, 12, 0, 4, 0xFF303030, 0);
    return &st;
}

/* The title bar: pushMenuTitle (FUN_08826ad0) stacks the menu names; the port draws
 * the latest one on its banner. bf_ttl4 (the fist, like bf_scmb4) is the Battle menus' banner. */
static void draw_title(int banner, const char *text)
{
    Picture *p = ui_part(banner);
    ui_pic(p, 0, 0, 1.0f, false);
    TextStyle st = text_style(0, 7, 14, 14, 0, 0, 0xFFFFFFFF, 0);
    ui_text(text, &st, 64, (p ? picture_height(p) : 24) / 2 - 8, 1.0f);
}

static const char *help_text(void)
{
    if (g.message)
        return g.message;
    switch (top()) {
    case SCR_MODE: return s_mode(g.mode.item[g.mode.cursor].help);
    case SCR_SELECT: return s_select(g.select.item[g.select.cursor].help);
    case SCR_TEST: return s_test(g.test.item[g.test.cursor].help);
    case SCR_FIELD: return s_test(g.field.item[g.field.cursor].help);
    case SCR_ARENA: return s_arena(g.arena.item[g.arena.cursor].help);
    case SCR_TEST_TEAM: return s_test(g.team_side == TEAM_ENEMY ? 63 : 64);
    case SCR_CARDS: return s_test(67);
    case SCR_STAGES: return s_test(68);
    case SCR_OPPONENTS: return s_arena(45);
    default: return NULL;
    }
}

static void draw_team_box(int x, int y, int side, bool selected)
{
    Picture *frame = ui_part(PART_BF_SWND4);
    ui_frame(frame, x, y, 150, 50, 8, 1.0f);
    if (selected)
        ui_frame(ui_part(PART_BF_CUR0), x - 2, y - 2, 154, 54, 8, ui_pulse(SDL_GetTicks() / 1000.0f));
    ui_text(s_test(side == TEAM_ENEMY ? 9 : 10), style_dark(), x + 10, y + 6, 1.0f);
    int card = g.team[side];
    if (card < 0) {
        Picture *none = ui_part(PART_BF_NOENTRY0);
        if (none)
            picture_draw_ex(none, x + 50, y + 20, 0, 50, 26, 0, 0, picture_width(none), picture_height(none),
                            BLEND_ALPHA, 0xFFFFFFFF, 0, 0, 0, 0);
    } else {
        char label[48];
        card_label(card, label, sizeof label);
        ui_text(label, style_dark(), x + 14, y + 26, 1.0f);
    }
}

/* CStageSelectWnd's preview (the stage's bf_mpi picture) with its name window
 * (CNameWnd, bf_mpwind behind the name centred, FUN_08aaff5c). Placement is the
 * port's. */
static void draw_stage_panel(int x, int y, int stage, const char *caption)
{
    /* Stages past the list (the arena's 75-174, named for the developers) show as
     * the list stage of their map, CPresetStage's first field. */
    if (stage >= STAGE_LIST) {
        int map = stage_map(stage);
        stage = map >= 0 && map < STAGE_RANDOM ? map : STAGE_RANDOM;
    }
    Picture *pic = ui_part(stage_part(stage));
    int pw = pic ? picture_width(pic) : 128, ph = pic ? picture_height(pic) : 72;
    ui_fill(x - 2, y - 2, pw + 4, ph + 4, 0xFF303030);
    ui_pic(pic, x, y, 1.0f, false);
    Picture *bar = ui_part(PART_BF_MPWIND);
    if (bar)
        picture_draw_ex(bar, x, y + ph - 16, 0, pw, 16, 0, 0, picture_width(bar), picture_height(bar),
                        BLEND_ALPHA, 0xFFFFFFFF, 0, 0, 0, 0);
    const char *name = s_stage(stage);
    TextStyle st = text_style(0, 5, 10, 10, 0, 0, 0xFFFFFFFF, 0);
    int w = ui_text_width(name, &st);
    ui_text(name, &st, x + pw / 2 - w / 2, y + ph - 14, 1.0f);
    if (caption)
        ui_text(caption, style_dark(), x, y - 16, 1.0f);
}

static void draw_test_side(void)
{
    if (top() == SCR_FIELD || top() == SCR_STAGES) {
        int st = top() == SCR_STAGES ? stage_at(g.stage_cursor) : g.stage;
        draw_stage_panel(330, 120, st, NULL);
        return;
    }
    draw_team_box(310, 30, TEAM_ENEMY, top() == SCR_TEST_TEAM && g.team_side == TEAM_ENEMY);
    draw_team_box(310, 84, TEAM_FRIEND, top() == SCR_TEST_TEAM && g.team_side == TEAM_FRIEND);
    draw_stage_panel(326, 142, g.stage, NULL);
}

static void draw_list_window(int x, int y, int w, int rows, int first, int count, int cursor,
                             void (*label)(int, char *, size_t), const char *title)
{
    ui_frame(ui_part(PART_BF_SWND4), x, y, w, rows * 18 + 30, 8, 1.0f);
    if (title)
        ui_text(title, style_dark(), x + 10, y + 6, 1.0f);
    for (int r = 0; r < rows && first + r < count; r++) {
        int i = first + r, ry = y + 24 + r * 18;
        if (i == cursor)
            ui_frame(ui_part(PART_BF_CUR0), x + 4, ry - 3, w - 8, 18, 6, ui_pulse(SDL_GetTicks() / 1000.0f));
        char text[64];
        label(i, text, sizeof text);
        ui_text(text, style_dark(), x + 12, ry, 1.0f);
    }
}

static void stage_label(int pos, char *out, size_t n)
{
    const char *s = s_stage(stage_at(pos));
    snprintf(out, n, "%s", s ? s : "?");
}

static void opp_label(int i, char *out, size_t n)
{
    const char *s = arena_name(i);
    snprintf(out, n, "%3d  %s", i + 1, s ? s : "?");
}

static void draw_opponent_info(int x, int y)
{
    ui_frame(ui_part(PART_BF_SWND4), x, y, 170, 70, 8, 1.0f);
    ui_text(s_arena(15), style_dark(), x + 10, y + 6, 1.0f);
    const char *name = arena_name(g.opponent);
    char line[64];
    snprintf(line, sizeof line, "%s  %s", name ? name : "?", s_arena(22 + arena_word(g.opponent, 4, 1)));
    ui_text(line, style_dark(), x + 14, y + 24, 1.0f);
    if (g.arena_card >= 0) {
        card_label(g.arena_card, line, sizeof line);
        ui_text(line, style_dark(), x + 14, y + 44, 1.0f);
    } else {
        TextStyle small = text_style(0, 5, 10, 10, 0, 0, 0xFF303030, 0);
        ui_text(s_arena(48), &small, x + 10, y + 46, 1.0f);
    }
    draw_stage_panel(x + 20, y + 98, arena_word(g.opponent, 0, 75), s_arena(16));
}

static void draw_screen(ScreenId s, bool is_top)
{
    switch (s) {
    case SCR_MODE: {
        /* The picture of the item under the cursor, bf_ttl(id + 1)_pic. */
        int id = g.mode.item[g.mode.cursor].id;
        if (is_top)
            ui_pic(ui_part(PART_BF_TTL1_PIC + id), 480 - 288, 32, 1.0f, false);
        draw_title(PART_BF_TTL0, s_mode(3));
        ui_menu_draw(&g.mode);
        break;
    }
    case SCR_SELECT:
        ui_pic(ui_part(PART_BF_TTL1_PIC + 3), 480 - 288, 32, 0.5f, false);
        draw_title(PART_BF_TTL0 + 4, s_select(2));
        ui_menu_draw(&g.select);
        break;
    case SCR_TEST:
        draw_title(PART_BF_TTL0 + 4, s_test(2));
        ui_menu_draw(&g.test);
        draw_test_side();
        break;
    case SCR_TEST_TEAM:
        break; /* drawn by SCR_TEST's side panel with its cursor */
    case SCR_CARDS:
        draw_list_window(150, 30, 150, CARD_ROWS, g.card_scroll, CARD_COUNT, g.card_cursor, card_label,
                         g.card_for == SCR_ARENA ? s_arena(4) : s_test(g.team_side == TEAM_ENEMY ? 9 : 10));
        break;
    case SCR_FIELD:
        ui_menu_draw(&g.field);
        break;
    case SCR_STAGES: {
        draw_list_window(140, 30, 170, STAGE_ROWS, g.stage_scroll, STAGE_LIST, g.stage_cursor, stage_label,
                         s_test(11));
        break;
    }
    case SCR_ARENA:
        draw_title(PART_BF_TTL0 + 4, s_arena(2));
        ui_menu_draw(&g.arena);
        draw_opponent_info(300, 30);
        break;
    case SCR_OPPONENTS:
        draw_list_window(130, 30, 170, OPP_ROWS, g.opp_scroll, ARENA_COUNT, g.opponent, opp_label, s_arena(3));
        break;
    case SCR_ASK:
        ui_ask_draw(&g.ask);
        break;
    }
}

void battle_menu_draw(void)
{
    sceGuEnable(GU_BLEND);
    Picture *back = ui_part(PART_BF_BACK);
    if (back)
        picture_draw(back, 0, 0, 0, BLEND_REPLACE, 0xFFFFFFFF, 0, 0);
    else
        ui_fill(0, 0, 480, 272, 0xFF404040);
    /* Only the latest titled screen and what's pushed on it are drawn. */
    int from = 0;
    for (int i = 0; i < g.depth; i++)
        if (g.stack[i] == SCR_MODE || g.stack[i] == SCR_SELECT || g.stack[i] == SCR_TEST ||
            g.stack[i] == SCR_ARENA)
            from = i;
    for (int i = from; i < g.depth; i++)
        draw_screen(g.stack[i], i == g.depth - 1);
    ui_help_draw(help_text());
    sceGuEnable(GU_TEXTURE_2D);
}
