/* See savedata.h. Hand-written from:
 *   CUtility: the param setup FUN_08b25354 / FUN_08b24da4 / FUN_08b25090 and the
 *     InitStart helpers FUN_08b2448c (FILES), FUN_08b243b0 (READ), FUN_08b2409c
 *     (MAKEDATA), FUN_08b241a0 (WRITE), FUN_08b24770 (GETSIZE); Update FUN_08b25a98.
 *   CSaveMngr: Initialize FUN_08a64838, UpdateUT FUN_08a65b54 (states 0x16-0x22, 0x2B),
 *     the result tables at 0x08c89d98 (FUN_08a63dd8 / FUN_08a68330), the file list
 *     FUN_08a672f8 / FUN_08a641dc, BeginDataSave FUN_08a63290.
 *   CSystemMngr: GetSaveSize FUN_08ab8114, the save names FUN_08ab82f0, the save
 *     buffer FUN_08abb680, the version check FUN_08ab8604, and the load that unpacks
 *     a game save (FUN_088b7xxx at 0x088b7590).
 *   CCarnageHeart: the blocks' constructors FUN_0889c0c0 (system), FUN_0889c92c
 *     (shared), FUN_08ad321c (team card), FUN_0889d794 (battle), FUN_0889d3dc (story;
 *     its CEventScriptField FUN_088df3bc / FUN_088df878).
 *   The Story Mode new game: FUN_088d3d80 mode 0 (FUN_0895ddd4, FUN_08918cb0,
 *     FUN_0882f64c). */
#include "savedata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gamedata.h"
#include "packfile.h"
#include "pgchip_edit.h"
#include "psp_imports.h"
#include "psp_shim.h"
#include "storydb.h"

SaveState g_save;

/* ---- strings and names in the EBOOT (splat addresses, read at run time) ---- */

#define STR_GAME_TITLE_PTR 0x004818C8u  /* "CARNAGE HEART EXA" (SFO title) */
#define STR_SYSTEM_DATA 0x00437D40u
#define STR_BATTLE_DATA 0x00437D64u
#define STR_STORY_DATA 0x00437D58u
#define STR_PLAYER_NAME_PTR 0x004879B0u
#define STR_TEAM_FOLDER 0x00437D78u            /* "Team Data folder" (0x08c3bd78) */
#define STR_TEAM_FOLDER_DETAIL_PTR 0x004879B8u /* "Team Cards for transfers are stored here." */
#define STR_STORY_OPEN_PTR 0x004879ACu  /* "Story Mode: Not Completed" */
#define STR_STORY_DONE_PTR 0x004879A8u
#define STR_PLAY_TIME 0x00437ECCu       /* the play-time line, a format with three %d */
#define ICON_PATHS 0x004818D4u          /* parts/icon/icon0_*.png by save type */
#define PARENTAL_LEVEL 0x00423904u      /* DAT_08c27904, a byte */
#define ARENA_NAMES 0x0044D8F0u         /* as battle_menu.c */
#define ARENA_RECORDS 0x0044DC10u
#define ARENA_STRIDE 0x3C0u
#define TEAM_NAMES 0x00487A08u          /* "Team 01".. (PTR_s_Team_01_08c8ba08), 16 */
#define TEAM_STRINGS 0x00487A60u        /* CTeamFormationString (FUN_08ac7d54), 0x4B */

enum { SAVE_TYPE_STORY = 1, SAVE_TYPE_BATTLE = 2, SAVE_TYPE_SYSTEM = 4 };

static const char *ptr_string(uint32_t ptr_addr)
{
    uint32_t p = gamedata_word(ptr_addr, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

static const char *or_empty(const char *s) { return s ? s : ""; }

/* ---- the blocks ---- */

static void put_word(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put_half(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static uint32_t get_word(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

void savedata_new_system(void)
{
    uint8_t *s = g_save.system;
    memset(s, 0, SYSTEM_DATA_SIZE);
    put_word(s + 0x00, 0x3F800000); /* 1.0f */
    put_word(s + 0x04, 0x3F800000);
    put_word(s + 0x0C, 5);
    put_word(s + 0x10, 0xFFFFFFFF);
}

/* FUN_08ad321c */
static void new_team_card(uint8_t *t)
{
    memset(t, 0, TEAM_CARD_SIZE);
    put_half(t + 0x320, 1);
    put_half(t + 0x322, 1);
    put_half(t + 0x324, 1);
}

/* ---- the chip editor's block ---- */

#define SOFT_SCENE 0x41370
#define SOFT_PAGE 0x1B0
#define SOFT_CURSORS 0x1B4

/* FUN_0889c724, without the macros: each type's default chip, page 4. */
static void new_software_scene(uint8_t *s)
{
    for (int t = 0; t < PGCHIP_TYPE_END; t++) {
        PgChipCommand c;
        pgchip_default(&c, t);
        memcpy(s + 8 * t, c.b, 8);
    }
    put_word(s + SOFT_PAGE, 4);
    for (int i = 0; i < CHIP_PAGES; i++)
        put_word(s + SOFT_CURSORS + 4 * i, 0);
}

const PgChipCommand *savedata_chip_default(int type)
{
    static PgChipCommand fresh;
    if (!pgchip_valid_type(type))
        type = 0;
    const PgChipCommand *c = (const PgChipCommand *)((uint8_t *)&g_save.db + SOFT_SCENE + 8 * type);
    if (pgchip_type(c) == type && type)
        return c;
    pgchip_default(&fresh, type); /* a save whose block was never built */
    return &fresh;
}

void savedata_set_chip_default(const PgChipCommand *chip)
{
    int t = pgchip_type(chip);
    if (pgchip_valid_type(t))
        memcpy((uint8_t *)&g_save.db + SOFT_SCENE + 8 * t, chip->b, 8);
}

int savedata_chip_page(void)
{
    int p = (int)get_word((uint8_t *)&g_save.db + SOFT_SCENE + SOFT_PAGE);
    return p >= 0 && p < CHIP_PAGES ? p : 4;
}

void savedata_set_chip_page(int page)
{
    if (page >= 0 && page < CHIP_PAGES)
        put_word((uint8_t *)&g_save.db + SOFT_SCENE + SOFT_PAGE, (uint32_t)page);
}

int savedata_chip_page_cursor(int page)
{
    return page >= 0 && page < CHIP_PAGES
               ? (int)get_word((uint8_t *)&g_save.db + SOFT_SCENE + SOFT_CURSORS + 4 * page)
               : 0;
}

void savedata_set_chip_page_cursor(int page, int index)
{
    if (page >= 0 && page < CHIP_PAGES)
        put_word((uint8_t *)&g_save.db + SOFT_SCENE + SOFT_CURSORS + 4 * page, (uint32_t)index);
}

/* ---- the Story Mode block ---- */

#define STORY_STEP 0x0000
#define STORY_LESSON_STATE 0x0088
#define STORY_LESSON_CLEAR 0x00BC
#define STORY_LESSONS 0x34
#define STORY_LESSON_MODE 0x018C
#define STORY_BRIEF_LESSON 0x0190
#define STORY_FIELD 0x0198
#define STORY_2B28 0x2B28
#define STORY_BATTLE 0x2B2C
#define STORY_SCRIPT 0x2B34
#define STORY_SCRIPT_MAX 0x40
#define STORY_MAX_OKE 0x2B74
#define STORY_MAX_TEAM 0x2B78
/* CEventScriptField: 0x13 menus of 0x230 bytes, each 8 keys (-1 free), 8 script ids
 * of 0x41 bytes from +0x20 and the count at +0x228 (CEventScriptFieldData). */
#define FIELD_MENUS 0x13
#define FIELD_SIZE 0x230
#define FIELD_IDS 0x20
#define FIELD_COUNT 0x228

_Static_assert(STORY_FIELD + FIELD_MENUS * FIELD_SIZE == STORY_2B28, "CEventScriptField size");

/* FUN_0889d3dc */
static void story_reset(void)
{
    uint8_t *b = g_save.story;
    memset(b, 0, STORY_DATA_SIZE);
    put_word(b + STORY_STEP, (uint32_t)STORY_STEP_NONE);
    put_word(b + STORY_BRIEF_LESSON, 0xFFFFFFFFu);
    for (int m = 0; m < FIELD_MENUS; m++) /* FUN_088df878 */
        memset(b + STORY_FIELD + m * FIELD_SIZE, 0xFF, 0x20);
    put_word(b + STORY_2B28, 0xFFFFFFFFu);
    put_word(b + STORY_BATTLE, 0xFFFFFFFFu);
    put_word(b + 0x2B30, 0xFFFFFFFFu);
    put_word(b + STORY_MAX_OKE, 2);
    put_word(b + STORY_MAX_TEAM, 4);
    g_save.story_file[0] = 0;
}

int savedata_story_step(void) { return (int)get_word(g_save.story + STORY_STEP); }
void savedata_set_story_step(int step) { put_word(g_save.story + STORY_STEP, (uint32_t)step); }
void savedata_story_set_2b28(int v) { put_word(g_save.story + STORY_2B28, (uint32_t)v); }
int savedata_story_battle(void) { return (int)get_word(g_save.story + STORY_BATTLE); }
void savedata_set_story_battle(int mission) { put_word(g_save.story + STORY_BATTLE, (uint32_t)mission); }

/* FUN_0889d4a8 / FUN_0889d4c8, FUN_0889d4ec / FUN_0889d510 */
int savedata_lesson_state(int card)
{
    return card >= 0 && card < STORY_LESSONS ? (int)(int8_t)g_save.story[STORY_LESSON_STATE + card] : 0;
}
void savedata_set_lesson_state(int card, int state)
{
    if (card >= 0 && card < STORY_LESSONS)
        g_save.story[STORY_LESSON_STATE + card] = (uint8_t)state;
}
uint32_t savedata_lesson_offered(int card)
{
    return card >= 0 && card < STORY_LESSONS ? get_word(g_save.story + STORY_LESSON_CLEAR + 4 * card) : 0;
}
void savedata_set_lesson_offered(int card, uint32_t v)
{
    if (card >= 0 && card < STORY_LESSONS)
        put_word(g_save.story + STORY_LESSON_CLEAR + 4 * card, v);
}
/* FUN_0889d718 / FUN_0889d720, FUN_0889d728 / FUN_0889d730 */
int savedata_story_saved_battle(void) { return (int)get_word(g_save.story + 0x2B30); }
void savedata_set_story_saved_battle(int mission) { put_word(g_save.story + 0x2B30, (uint32_t)mission); }
bool savedata_story_briefed(void) { return get_word(g_save.story + 0x2B7C) != 0; }
void savedata_set_story_briefed(bool seen) { put_word(g_save.story + 0x2B7C, seen ? 1u : 0u); }
/* FUN_0889d534 / FUN_0889d53c, FUN_0889d544 / FUN_0889d54c */
int savedata_lesson_mode(void) { return (int)get_word(g_save.story + STORY_LESSON_MODE); }
void savedata_set_lesson_mode(int mode) { put_word(g_save.story + STORY_LESSON_MODE, (uint32_t)mode); }
int savedata_lesson_current(void) { return (int)get_word(g_save.story + STORY_BRIEF_LESSON); }
void savedata_set_lesson_current(int lesson) { put_word(g_save.story + STORY_BRIEF_LESSON, (uint32_t)lesson); }

const char *savedata_story_script(void)
{
    g_save.story[STORY_SCRIPT + STORY_SCRIPT_MAX - 1] = 0;
    return (const char *)g_save.story + STORY_SCRIPT;
}

void savedata_set_story_script(const char *path)
{
    /* FUN_0889d584 wants 1..0x3F characters. */
    memset(g_save.story + STORY_SCRIPT, 0, STORY_SCRIPT_MAX);
    if (path)
        snprintf((char *)g_save.story + STORY_SCRIPT, STORY_SCRIPT_MAX, "%s", path);
}

void savedata_new_game(void)
{
    GameDataBase *db = &g_save.db;
    memset(db, 0, sizeof *db); /* the cards: FUN_089eea80 clears their words and names */
    for (int i = 0; i < SAVE_TEAMS; i++)
        new_team_card(db->team[i]);
    uint8_t *b = (uint8_t *)db;
    put_word(b + 0x40C08 + 0x2C, 1);          /* FUN_0889c420 [0xB] */
    for (int i = 0; i < 2; i++) {             /* FUN_0889c51c: two team cards, word 0 = 1 */
        new_team_card(b + 0x40C80 + TEAM_CARD_SIZE * i);
        put_word(b + 0x40C80 + TEAM_CARD_SIZE * i, 1);
    }
    put_word(b + 0x40C40 + 0x6C0, 0x19);
    new_software_scene(b + SOFT_SCENE);
    put_word(b + 0x44EE8, 1);
    put_word(b + 0x45068, 1);
    put_word(b + 0x45204, 1);
    memset(g_save.battle, 0, sizeof g_save.battle);
    put_word(g_save.battle + 0x4230, 0xFFFFFFFF);
    g_save.battle_file[0] = 0;
    story_reset();
}

bool savedata_card_used(int card)
{
    return card >= 0 && card < SAVE_CARDS && get_word(g_save.db.card[card].header) != 0;
}

const OkeDataBase *savedata_card(int card)
{
    return card >= 0 && card < SAVE_CARDS ? &g_save.db.card[card] : NULL;
}

int savedata_card_status(int card)
{
    return card >= 0 && card < SAVE_CARDS ? (int)get_word(g_save.db.card[card].header) : 0;
}

OkeDataBase *savedata_card_edit(int card)
{
    return card >= 0 && card < SAVE_CARDS ? &g_save.db.card[card] : NULL;
}

#define CURRENT_CARD 0x40C00 /* in the shared block */

int savedata_current_card(void)
{
    int v = (int)get_word((const uint8_t *)&g_save.db + CURRENT_CARD);
    return v >= 0 && v < SAVE_CARDS ? v : 0;
}

void savedata_set_current_card(int card)
{
    if (card >= 0 && card < SAVE_CARDS)
        put_word((uint8_t *)&g_save.db + CURRENT_CARD, (uint32_t)card);
}

void savedata_card_clear(int card)
{
    if (card >= 0 && card < SAVE_CARDS)
        memset(&g_save.db.card[card], 0, sizeof g_save.db.card[card]);
}

void savedata_card_copy(int from, int to)
{
    if (from >= 0 && from < SAVE_CARDS && to >= 0 && to < SAVE_CARDS && from != to)
        g_save.db.card[to] = g_save.db.card[from];
}

void savedata_card_name(int card, char *out, size_t n)
{
    out[0] = 0;
    if (card < 0 || card >= SAVE_CARDS || !n)
        return;
    const char *name = (const char *)g_save.db.card[card].header + 4;
    size_t len = strnlen(name, 0x1C);
    if (len >= n)
        len = n - 1;
    for (size_t i = 0; i < len; i++) /* the presets fill unused bytes with 0xCD/0xFF */
        out[i] = (unsigned char)name[i] < 0x20 || (unsigned char)name[i] >= 0x7F ? '?' : name[i];
    out[len] = 0;
}

/* ---- stock, money, menu limits, maps ---- */

#define STOCK_BASE 0x47BBC
#define LIMIT_BASE 0x479B0
#define MAP_BASE 0x47F30
#define MONEY 0x48394
#define BGM_FLAGS 0x44E98 /* + list * 0x18C */
#define BGM_MODE 0x4533C  /* + list * 4 */
#define LIMIT_WORDS 0x83

static const struct { uint16_t off, count; } k_stock[STOCK_GROUPS] = {
    {0x000, 0x25}, {0x094, 8}, {0x0B4, 12}, {0x0E4, 8}, {0x104, 0x3C}, {0x1F4, 11}, {0x220, 0x36}, {0x2F8, 0x1F},
};

static uint8_t *db_bytes(void)
{
    return (uint8_t *)&g_save.db;
}

bool savedata_bgm_enabled(int list, int row)
{
    if (list < 0 || list > 2 || row < 0 || row > 0x62)
        return false;
    return get_word(db_bytes() + BGM_FLAGS + 0x18C * list + 4 * row) != 0;
}

int savedata_bgm_mode(int list)
{
    return list >= 0 && list <= 2 ? (int)get_word(db_bytes() + BGM_MODE + 4 * list) : 0;
}

static uint8_t *stock_word(int group, int id)
{
    if (group < 0 || group >= STOCK_GROUPS || id < 0 || id >= k_stock[group].count)
        return NULL;
    return db_bytes() + STOCK_BASE + k_stock[group].off + 4 * id;
}

int savedata_stock(int group, int id)
{
    uint8_t *w = stock_word(group, id);
    return w ? (int)get_word(w) : 0;
}

/* FUN_08919694 */
static void stock_keep_none(void)
{
    put_word(stock_word(STOCK_ADD_ARMOR, 0), 2);
    put_word(stock_word(STOCK_WEAPON, 0), 2);
    put_word(stock_word(STOCK_OPTION, 0), 2);
    put_word(stock_word(STOCK_CHIP, 0), 2);
}

/* ---- CEventScriptField (FUN_088df4b0 Hide, FUN_088df540 Seek, FUN_088df5d4 Clear,
 * FUN_088df690 IsExist; the entries FUN_088df8c8 / FUN_088dfa64 / FUN_088dfab0 /
 * FUN_088dfbcc) ---- */

static uint8_t *field_menu(int menu)
{
    return menu >= 0 && menu < FIELD_MENUS ? g_save.story + STORY_FIELD + FIELD_SIZE * menu : NULL;
}

bool savedata_story_field_hide(int menu, int key, const char *id)
{
    uint8_t *f = field_menu(menu);
    size_t n = id ? strlen(id) : 0;
    if (!f || n < 1 || n > 0x40)
        return false;
    int count = (int)get_word(f + FIELD_COUNT);
    for (int i = 0; i < count; i++)
        if ((int)get_word(f + 4 * i) == key) { /* the key's script is replaced */
            memset(f + FIELD_IDS + 0x41 * i, 0, 0x41);
            memcpy(f + FIELD_IDS + 0x41 * i, id, n);
            return true;
        }
    if (count >= 8)
        return false;
    put_word(f + 4 * count, (uint32_t)key);
    memset(f + FIELD_IDS + 0x41 * count, 0, 0x41);
    memcpy(f + FIELD_IDS + 0x41 * count, id, n);
    put_word(f + FIELD_COUNT, (uint32_t)(count + 1));
    return true;
}

const char *savedata_story_field_seek(int menu, int key)
{
    uint8_t *f = field_menu(menu);
    if (!f)
        return NULL;
    int count = (int)get_word(f + FIELD_COUNT);
    for (int i = 0; i < count && i < 8; i++)
        if ((int)get_word(f + 4 * i) == key)
            return (const char *)f + FIELD_IDS + 0x41 * i;
    return NULL;
}

void savedata_story_field_clear(int menu, int key)
{
    uint8_t *f = field_menu(menu);
    if (!f)
        return;
    int count = (int)get_word(f + FIELD_COUNT);
    for (int i = 0; i < count && i < 8; i++)
        if ((int)get_word(f + 4 * i) == key) { /* the later entries move up one */
            for (int j = i; j + 1 < count; j++) {
                put_word(f + 4 * j, get_word(f + 4 * (j + 1)));
                memcpy(f + FIELD_IDS + 0x41 * j, f + FIELD_IDS + 0x41 * (j + 1), 0x41);
            }
            put_word(f + 4 * (count - 1), 0xFFFFFFFFu);
            memset(f + FIELD_IDS + 0x41 * (count - 1), 0, 0x41);
            put_word(f + FIELD_COUNT, (uint32_t)(count - 1));
            return;
        }
}

/* FUN_088df668 (FUN_088df468): every menu emptied, as a new lesson starts. */
void savedata_story_field_reset(void)
{
    for (int m = 0; m < FIELD_MENUS; m++) {
        uint8_t *f = field_menu(m);
        memset(f, 0, FIELD_SIZE);
        memset(f, 0xFF, 0x20);
    }
}

bool savedata_story_field_exists(int menu, int key) { return savedata_story_field_seek(menu, key) != NULL; }

static void stock_set_all(int value)
{
    for (int g = 0; g < STOCK_GROUPS; g++)
        for (int id = 0; id < k_stock[g].count; id++)
            put_word(stock_word(g, id), (uint32_t)value);
}

void savedata_set_stock(int group, int id, int value)
{
    uint8_t *w = stock_word(group, id);
    if (!w)
        return;
    put_word(w, (uint32_t)value);
    stock_keep_none();
}

int savedata_money(void)
{
    return (int)get_word(db_bytes() + MONEY);
}

void savedata_add_money(int amount)
{
    int v = savedata_money() + amount;
    put_word(db_bytes() + MONEY, (uint32_t)(v < 100000 ? v : 99999));
}

void savedata_spend_money(int amount)
{
    int v = savedata_money() - amount;
    put_word(db_bytes() + MONEY, (uint32_t)(v >= 0 ? v : 0));
}

int savedata_menu_limit(int word)
{
    return word >= 0 && word < LIMIT_WORDS ? (int)get_word(db_bytes() + LIMIT_BASE + 4 * word) : 0;
}

void savedata_set_menu_limit(int word, int value)
{
    if (word >= 0 && word < LIMIT_WORDS)
        put_word(db_bytes() + LIMIT_BASE + 4 * word, (uint32_t)value);
}

int savedata_map_state(int stage)
{
    return stage >= 0 && stage < SAVE_STAGES ? (int)get_word(db_bytes() + MAP_BASE + 4 * stage) : -1;
}

void savedata_set_map_state(int stage, int value)
{
    if (stage >= 0 && stage < SAVE_STAGES)
        put_word(db_bytes() + MAP_BASE + 4 * stage, (uint32_t)value);
}

bool savedata_stage_available(int stage)
{
    return stage != 13 && savedata_map_state(stage) == 1;
}

void savedata_story_new(void)
{
    story_reset();
    /* FUN_0895ddd4: every menu word off, then words 0 and 0x68 = 2. */
    for (int w = 0; w < LIMIT_WORDS; w++)
        savedata_set_menu_limit(w, 0);
    savedata_set_menu_limit(0, 2);
    savedata_set_menu_limit(0x68, 2);
    /* FUN_08918cb0: nothing owned; bodies 0xE and 0x20-0x24 at 3; the Other items
     * 7, 8 and 11-19 for sale, 9, 10 and 20-30 owned. */
    stock_set_all(0);
    static const uint8_t k_bodies[] = {0x0E, 0x20, 0x21, 0x22, 0x23, 0x24};
    for (unsigned i = 0; i < sizeof k_bodies; i++)
        put_word(stock_word(STOCK_BODY, k_bodies[i]), 3);
    for (int id = 7; id < 0x1F; id++)
        put_word(stock_word(STOCK_OTHER, id), (id == 7 || id == 8 || (id >= 11 && id <= 19)) ? 1 : 2);
    /* FUN_0882f64c: maps 2 and 12. */
    for (int st = 0; st < SAVE_STAGES; st++)
        savedata_set_map_state(st, 0);
    savedata_set_map_state(2, 1);
    savedata_set_map_state(12, 1);
    put_word(g_save.story + STORY_2B28, 0xFFFFFFFFu); /* FUN_0889d574(-1) */
}

void savedata_battle_mode_new(void)
{
    /* FUN_0895de04: groups of menu words switched on, a few left off. */
    static const struct { uint8_t from, count; } k_on[] = {
        {0x00, 6}, {0x06, 0xD}, {0x13, 0xF}, {0x22, 0xE}, {0x30, 5}, {0x35, 0x2A}, {0x5F, 8}, {0x67, 9}, {0x70, 0x13},
    };
    for (int w = 0; w < LIMIT_WORDS; w++)
        savedata_set_menu_limit(w, 0);
    for (unsigned i = 0; i < sizeof k_on / sizeof k_on[0]; i++)
        for (int w = k_on[i].from; w < k_on[i].from + k_on[i].count; w++)
            savedata_set_menu_limit(w, 1);
    static const uint8_t k_off[] = {0x0B, 0x2A, 0x36, 0x6E, 0x6A};
    for (unsigned i = 0; i < sizeof k_off; i++)
        savedata_set_menu_limit(k_off[i], 0);
    /* FUN_08918d5c: FUN_08919594 owns everything, then some go on sale. */
    stock_set_all(2);
    stock_keep_none();
    put_word(stock_word(STOCK_BODY, 14), 1);
    put_word(stock_word(STOCK_BODY, 33), 1);
    for (int id = 0x14; id <= 0x1E; id++)
        put_word(stock_word(STOCK_OTHER, id), 1);
    /* FUN_0882f65c: the first 14 maps. */
    for (int st = 0; st < 14; st++)
        savedata_set_map_state(st, 1);
}

/* ---- team cards ---- */

#define EMBLEM_PALETTE 0x0047FDB8u /* DAT_08c83db8, 16 words (FUN_089ed05c) */

static uint8_t *team_card(int team)
{
    return team >= 0 && team < SAVE_TEAMS ? g_save.db.team[team] : NULL;
}

int savedata_team_status(int team)
{
    uint8_t *t = team_card(team);
    return t ? (int)get_word(t) : 0;
}

int savedata_team_member(int team, int m)
{
    uint8_t *t = team_card(team);
    if (!t || m < 0 || m >= TEAM_MEMBERS || get_word(t) == 0)
        return -1;
    int card = (int)get_word(t + 0x290 + 0x30 * m + 0x20);
    return card >= 0 && card < SAVE_CARDS ? card : -1;
}

int savedata_team_spot(int team, int m)
{
    uint8_t *t = team_card(team);
    if (!t || m < 0 || m >= TEAM_MEMBERS)
        return -1;
    int spot = (int)get_word(t + 0x290 + 0x30 * m + 0x24);
    return spot >= 0 && spot < 9 ? spot : -1;
}

int savedata_team_count(int team)
{
    int n = 0;
    for (int m = 0; m < TEAM_MEMBERS; m++)
        n += savedata_team_member(team, m) >= 0;
    return n;
}

static void copy_text(char *out, size_t n, const uint8_t *src, size_t max)
{
    if (!n)
        return;
    size_t len = strnlen((const char *)src, max);
    if (len >= n)
        len = n - 1;
    for (size_t i = 0; i < len; i++)
        out[i] = src[i] < 0x20 || src[i] >= 0x7F ? '?' : (char)src[i];
    out[len] = 0;
}

void savedata_team_name(int team, char *out, size_t n)
{
    uint8_t *t = team_card(team);
    if (!t) {
        if (n)
            out[0] = 0;
        return;
    }
    copy_text(out, n, t + 0x250, TEAM_NAME_MAX);
}

const uint8_t *savedata_team_emblem(int team)
{
    uint8_t *t = team_card(team);
    return t ? t + 0x10 : NULL;
}

uint8_t *savedata_team_emblem_edit(int team)
{
    uint8_t *t = team_card(team);
    return t ? t + 0x10 : NULL;
}

void savedata_team_set_name(int team, const char *name)
{
    uint8_t *t = team_card(team);
    if (!t)
        return;
    memset(t + 0x250, 0, TEAM_NAME_MAX);
    snprintf((char *)t + 0x250, TEAM_NAME_MAX, "%s", name ? name : "");
}

void savedata_team_create(int team, const char *name, const char *owner)
{
    uint8_t *t = team_card(team);
    if (!t)
        return;
    new_team_card(t);
    put_word(t, 1);
    snprintf((char *)t + 0x250, TEAM_NAME_MAX, "%s", name ? name : "");
    snprintf((char *)t + 0x269, 0x290 - 0x269, "%s", owner ? owner : "");
    ScePspDateTime now;
    memset(&now, 0, sizeof now);
    int tz = 0; /* FUN_089f38a0: the clock in the system's time zone (param 6) */
    sceUtilityGetSystemParamInt(6, &tz);
    sceRtcGetCurrentClock(&now, tz);
    memcpy(t + 0x320, &now, 16);
    put_word(t + 0x338, 1000);
    for (int m = 0; m < TEAM_MEMBERS; m++) { /* FUN_08ad3188(-1, -1, 0, 0) */
        put_word(t + 0x290 + 0x30 * m + 0x20, 0xFFFFFFFF);
        put_word(t + 0x290 + 0x30 * m + 0x24, 0xFFFFFFFF);
    }
    for (int i = 0; i < 16; i++)
        put_word(t + 0x10 + 0x200 + 4 * i, gamedata_word(EMBLEM_PALETTE + 4u * (uint32_t)i, 0));
}

void savedata_team_delete(int team)
{
    uint8_t *t = team_card(team);
    if (t)
        new_team_card(t);
}

void savedata_team_set_member(int team, int m, int card, int spot)
{
    uint8_t *t = team_card(team);
    if (!t || m < 0 || m >= TEAM_MEMBERS)
        return;
    put_word(t + 0x290 + 0x30 * m + 0x20, card >= 0 ? (uint32_t)card : 0xFFFFFFFFu);
    put_word(t + 0x290 + 0x30 * m + 0x24, card >= 0 && spot >= 0 ? (uint32_t)spot : 0xFFFFFFFFu);
}

bool savedata_story_completed(void)
{
    return get_word(g_save.system + 0x74) != 0;
}

void savedata_set_story_completed(bool done)
{
    put_word(g_save.system + 0x74, done ? 1u : 0u);
}

/* ---- CUtility ---- */

static SceUtilitySavedataParam g_param;             /* CUtility + 0x648 */
static SceUtilitySavedataFileListInfo g_file_info;  /* + 0x1040 */
static SceUtilitySavedataFileListEntry g_file_entries[0x400]; /* the 0x14000-byte list */
static SceUtilitySavedataSizeInfo g_size_info;
static SceUtilitySavedataSizeEntry g_size_entry;
static PspUtilitySavedataListSaveNewData g_new_data;
static uint8_t g_icon[0x10000];
static long g_icon_size = -1;
static int g_icon_type = -1;
static bool g_util_busy;

static uint8_t g_buf[BATTLE_SAVE_SIZE];
_Static_assert(STORY_SAVE_SIZE <= BATTLE_SAVE_SIZE, "one buffer for both game saves");
_Static_assert(TEAM_FILE_SIZE <= BATTLE_SAVE_SIZE, "and the team files");
static GameDataBase g_peek;              /* From Save Data's other save */
static uint8_t g_team_file[TEAM_FILE_SIZE];

/* FUN_08a617f8: the save names at 0x08c858f8 ("T", "S", "B", ...); the system data's
 * is empty. */
static const char *save_name(int type)
{
    return type == SAVE_TYPE_BATTLE ? "B" : type == SAVE_TYPE_STORY ? "S" : type == SAVE_TEAM ? "TEAM" : "";
}

/* A game save's size (FUN_08ab8114 type 3, by kind) and its mode block. */
static SceSize game_save_size(SaveKind kind)
{
    return kind == SAVE_STORY ? STORY_SAVE_SIZE : kind == SAVE_TEAM ? TEAM_FILE_SIZE : BATTLE_SAVE_SIZE;
}
static uint8_t *mode_block(SaveKind kind) { return kind == SAVE_STORY ? g_save.story : g_save.battle; }
static size_t mode_block_size(SaveKind kind) { return kind == SAVE_STORY ? STORY_DATA_SIZE : BATTLE_DATA_SIZE; }
static char *kind_file(SaveKind kind) { return kind == SAVE_STORY ? g_save.story_file : g_save.battle_file; }
static const char *kind_label(SaveKind kind)
{
    return kind == SAVE_STORY ? "Story Data" : kind == SAVE_TEAM ? "Team Data" : "Battle Data";
}

/* FUN_08b25354 + FUN_08b24da4: the common header and the default file. */
static void param_begin(int type, int mode)
{
    memset(&g_param, 0, sizeof g_param);
    g_param.base.size = sizeof g_param;
    g_param.base.language = 1; /* CUtility +8 */
    g_param.base.buttonSwap = 0;
    g_param.base.graphicsThread = 0x11;
    g_param.base.accessThread = 0x13;
    g_param.base.fontThread = 0x12;
    g_param.base.soundThread = 0x10;
    g_param.mode = mode;
    snprintf(g_param.gameName, sizeof g_param.gameName, "%s", SAVE_GAME_NAME);
    snprintf(g_param.saveName, sizeof g_param.saveName, "%s", save_name(type));
    snprintf(g_param.fileName, sizeof g_param.fileName, "DATA.BIN");
    memcpy(g_param.key, "CHEXA", 5); /* padded with zeroes to 16 bytes */
    g_param.multiStatus = 1;
}

static bool util_start(void)
{
    int32_t r = sceUtilitySavedataInitStart(&g_param);
    if (r < 0) {
        SDL_Log("[save] sceUtilitySavedataInitStart failed: 0x%08X", (unsigned)r);
        g_param.base.result = r;
        return false;
    }
    g_util_busy = true;
    return true;
}

/* FUN_08b25a98 for the savedata dialog; true once the utility is done. */
static bool util_poll(void)
{
    if (!g_util_busy)
        return true;
    int32_t st = sceUtilitySavedataGetStatus();
    if (st == PSP_UTILITY_STATUS_VISIBLE)
        sceUtilitySavedataUpdate(1);
    else if (st == PSP_UTILITY_STATUS_QUIT)
        sceUtilitySavedataShutdownStart();
    else if (st == PSP_UTILITY_STATUS_NONE)
        g_util_busy = false;
    return !g_util_busy;
}

/* FUN_08b2448c: list the save's files. Secure: up to 99 (0x1EF0 bytes); normal: 0x400. */
static void util_files(int type, bool normal)
{
    param_begin(type, PSP_UTILITY_SAVEDATA_FILES);
    memset(&g_file_info, 0, sizeof g_file_info);
    if (normal) {
        g_file_info.maxNormalEntries = 0x400;
        g_file_info.normalEntries = g_file_entries;
    } else {
        g_file_info.maxSecureEntries = 99;
        g_file_info.secureEntries = g_file_entries;
    }
    g_param.fileList = &g_file_info;
    util_start();
}

/* FUN_08b243b0: secure 0xF, else 0x10. */
static void util_read(int type, const char *file, void *buf, SceSize size, bool secure)
{
    param_begin(type, secure ? PSP_UTILITY_SAVEDATA_READSECURE : PSP_UTILITY_SAVEDATA_READ);
    snprintf(g_param.fileName, sizeof g_param.fileName, "%s", file);
    g_param.dataBuf = buf;
    g_param.dataBufSize = g_param.dataSize = size;
    util_start();
}

/* The save's icon, FUN_08a619f8: parts/icon/icon0_*.png from the pack. */
static void load_icon(int type)
{
    if (g_icon_type == type)
        return;
    g_icon_type = type;
    g_icon_size = -1;
    const char *path = ptr_string(ICON_PATHS + 4u * (uint32_t)type);
    Pack pack;
    if (!path || pack_open(&pack, "disc0:/PSP_GAME/USRDIR") < 0)
        return;
    const PackEntry *e = pack_find(&pack, path);
    if (e)
        g_icon_size = pack_read(&pack, e, g_icon, sizeof g_icon);
    pack_close(&pack);
}

/* The EBOOT's format is only used when it takes exactly three ints. */
static bool three_ints(const char *fmt)
{
    int n = 0;
    for (const char *p = fmt; p && *p; p++)
        if (*p == '%') {
            if (p[1] == 'd')
                n++, p++;
            else
                return false;
        }
    return fmt && n == 3;
}

/* CSystemMngr::GetSaveGameDataName (FUN_08ab82f0) for types 4, 1 and 2. */
static void sfo_strings(int type, char *title, size_t tn, char *detail, size_t dn)
{
    if (type == SAVE_TYPE_SYSTEM) {
        snprintf(title, tn, "%s", or_empty(gamedata_string(STR_SYSTEM_DATA)));
        /* FUN_08ab953c: play time from +0x78, in seconds. */
        uint32_t t = get_word(g_save.system + 0x78);
        const char *fmt = gamedata_string(STR_PLAY_TIME);
        detail[0] = 0;
        if (three_ints(fmt))
            snprintf(detail, dn, fmt, (int)(t / 3600), (int)(t % 3600 / 60), (int)(t % 60));
        size_t n = strlen(detail);
        snprintf(detail + n, dn - n, "%s",
                 or_empty(ptr_string(savedata_story_completed() ? STR_STORY_DONE_PTR : STR_STORY_OPEN_PTR)));
    } else if (type == SAVE_TYPE_STORY) {
        /* The step's full progress title (FUN_08a69b74 with 1): "Progress :Chapter 1". */
        snprintf(title, tn, "%s", or_empty(gamedata_string(STR_STORY_DATA)));
        snprintf(detail, dn, "%s", or_empty(storydb_progress(savedata_story_step(), true)));
    } else if (type == SAVE_TEAM) {
        snprintf(title, tn, "%s", or_empty(gamedata_string(STR_TEAM_FOLDER)));
        snprintf(detail, dn, "%s", or_empty(ptr_string(STR_TEAM_FOLDER_DETAIL_PTR)));
    } else {
        snprintf(title, tn, "%s", or_empty(gamedata_string(STR_BATTLE_DATA)));
        /* "%s%s": "Player Name:" and the name at the battle block's start. */
        char name[17];
        memcpy(name, g_save.battle, 16);
        name[16] = 0;
        snprintf(detail, dn, "%s%s", or_empty(ptr_string(STR_PLAYER_NAME_PTR)), name);
    }
}

/* FUN_08b2409c / FUN_08b241a0 with FUN_08b25090: create or write, with the SFO
 * strings and icon. Secure for the system data (param_4 0), normal for game data. */
static void util_write(int type, const char *file, void *buf, SceSize size, bool create, bool secure)
{
    param_begin(type, create ? (secure ? PSP_UTILITY_SAVEDATA_MAKEDATASECURE : PSP_UTILITY_SAVEDATA_MAKEDATA)
                             : (secure ? PSP_UTILITY_SAVEDATA_WRITESECURE : PSP_UTILITY_SAVEDATA_WRITE));
    snprintf(g_param.fileName, sizeof g_param.fileName, "%s", file);
    g_param.dataBuf = buf;
    g_param.dataBufSize = g_param.dataSize = size;
    g_param.overwrite = 1;
    snprintf(g_param.sfoParam.title, sizeof g_param.sfoParam.title, "%s",
             or_empty(ptr_string(STR_GAME_TITLE_PTR)));
    sfo_strings(type, g_param.sfoParam.savedataTitle, sizeof g_param.sfoParam.savedataTitle,
                g_param.sfoParam.detail, sizeof g_param.sfoParam.detail);
    g_param.sfoParam.parentalLevel = (uint8_t)gamedata_word(PARENTAL_LEVEL, 1);
    load_icon(type);
    if (g_icon_size > 0) {
        g_param.icon0FileData.buf = g_icon;
        g_param.icon0FileData.bufSize = g_param.icon0FileData.size = (SceSize)g_icon_size;
    }
    memset(&g_new_data, 0, sizeof g_new_data);
    g_param.newData = &g_new_data;
    util_start();
}

/* FUN_08b24770: the space check. */
static void util_getsize(int type, const char *file, SceSize size, bool secure)
{
    param_begin(type, PSP_UTILITY_SAVEDATA_GETSIZE);
    snprintf(g_param.fileName, sizeof g_param.fileName, "%s", file);
    memset(&g_size_info, 0, sizeof g_size_info);
    memset(&g_size_entry, 0, sizeof g_size_entry);
    g_size_entry.size = size;
    snprintf(g_size_entry.name, sizeof g_size_entry.name, "%s", file);
    if (secure) {
        g_size_info.numSecureEntries = 1;
        g_size_info.secureEntries = &g_size_entry;
    } else {
        g_size_info.numNormalEntries = 1;
        g_size_info.normalEntries = &g_size_entry;
    }
    g_param.sizeInfo = &g_size_info;
    g_param.dataSize = size;
    util_start();
}

/* ---- CSaveMngr ---- */

typedef enum {
    SEQ_IDLE,
    SEQ_CHECK,  /* type 4, exec 3 */
    SEQ_LIST,   /* type 1 / 2, the list of an exec 1 */
    SEQ_LOAD,   /* type 1 / 2, state 0x2B */
    SEQ_SAVE,   /* type 1 / 2, BeginDataSave */
} SeqKind;

/* The UpdateUT states used here, numbered as in the game. */
enum {
    ST_CHECK_FILES = 0x16,
    ST_CHECK_READ = 0x17,
    ST_CHECK_SIZE = 0x18,
    ST_SYS_FILES = 0x20,
    ST_SYS_SIZE = 0x21,
    ST_SYS_WRITE = 0x22,
    ST_GAME_FILES = 0x00,
    ST_GAME_READ = 0x2B,
    ST_GAME_SIZE = 0x0E,
    ST_GAME_WRITE = 0x10,
    ST_DONE = -1,
};

static struct {
    SeqKind kind;
    SaveKind save_kind; /* the game save's type for LIST / LOAD / SAVE */
    int state;
    bool started;   /* the state's utility call was made */
    bool exists;    /* +0x50: FILES found the save */
    int secure_files; /* the FILES result's secure count (state 0x21 reads +0xC) */
    int32_t result;
    char file[SAVE_FILE_MAX];
    SaveListEntry list[SAVE_LIST_MAX];
    int list_count;
    bool peek;      /* the load goes to g_peek, not the game */
} g_seq;

static void seq_begin(SeqKind kind, int state)
{
    g_seq.kind = kind;
    g_seq.state = state;
    g_seq.started = false;
    g_seq.result = 0;
}

static void seq_goto(int state)
{
    g_seq.state = state;
    g_seq.started = false;
}

/* Runs the current state's call once and reports its result when it's done:
 * returns false while waiting. */
static bool seq_call(void (*start)(void), int32_t *result)
{
    if (!g_seq.started) {
        g_seq.started = true;
        start();
        return false;
    }
    if (!util_poll())
        return false;
    *result = g_param.base.result;
    return true;
}

/* -- the start-up check -- */

static uint8_t g_sys_buf[SYSTEM_DATA_SIZE];

static void call_sys_files(void) { util_files(SAVE_TYPE_SYSTEM, false); }
static void call_sys_read(void) { util_read(SAVE_TYPE_SYSTEM, "DATA.BIN", g_sys_buf, SYSTEM_DATA_SIZE, true); }
static void call_sys_size(void) { util_getsize(SAVE_TYPE_SYSTEM, "DATA.BIN", SYSTEM_DATA_SIZE, true); }
static void call_sys_write(void)
{
    /* FUN_08a63794 with type 4: the block itself, 0x7C bytes. */
    memcpy(g_sys_buf, g_save.system, SYSTEM_DATA_SIZE);
    util_write(SAVE_TYPE_SYSTEM, "DATA.BIN", g_sys_buf, SYSTEM_DATA_SIZE, !g_seq.exists || g_seq.secure_files == 0,
               true);
}

void savedata_check_start(void)
{
    savedata_new_system();
    seq_begin(SEQ_CHECK, ST_CHECK_FILES);
    SDL_Log("[save] start-up check (CSaveMngr type 4, exec 3): %s/%s", psp_savedata_root(), SAVE_GAME_NAME);
}

static int check_update(void)
{
    int32_t r;
    switch (g_seq.state) {
    case ST_CHECK_FILES: /* table 0x16: 0 -> 0x17, NO_DATA -> 0x18, else a message */
        if (!seq_call(call_sys_files, &r))
            return 1;
        if (r == 0) {
            seq_goto(ST_CHECK_READ);
        } else if (r == SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA) {
            SDL_Log("[save] no system data: creating it");
            seq_goto(ST_CHECK_SIZE);
        } else {
            SDL_Log("[save] system data check failed (0x%08X): going on with new data", (unsigned)r);
            g_seq.result = r;
            seq_goto(ST_DONE);
        }
        return 1;
    case ST_CHECK_READ: /* table 0x17: 0 -> done (the result copies the block in) */
        if (!seq_call(call_sys_read, &r))
            return 1;
        if (r == 0 && g_param.dataSize == SYSTEM_DATA_SIZE) {
            memcpy(g_save.system, g_sys_buf, SYSTEM_DATA_SIZE);
            SDL_Log("[save] system data loaded (Story Mode %s, play time %u s)",
                    savedata_story_completed() ? "completed" : "not completed",
                    (unsigned)get_word(g_save.system + 0x78));
        } else {
            SDL_Log("[save] system data unreadable (0x%08X, %u bytes): going on with new data", (unsigned)r,
                    (unsigned)g_param.dataSize);
            g_seq.result = r ? r : -1;
        }
        seq_goto(ST_DONE);
        return 1;
    case ST_CHECK_SIZE: /* 0x18: the space for a new save, then (0x51) the create path */
        if (!seq_call(call_sys_size, &r))
            return 1;
        seq_goto(ST_SYS_FILES);
        return 1;
    case ST_SYS_FILES: /* 0x20: 0 or NO_DATA -> 0x21 */
        if (!seq_call(call_sys_files, &r))
            return 1;
        g_seq.exists = r == 0;
        g_seq.secure_files = (int)g_file_info.resultNumSecureEntries;
        if (r != 0 && r != SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA) {
            g_seq.result = r;
            seq_goto(ST_DONE);
        } else {
            seq_goto(ST_SYS_SIZE);
        }
        return 1;
    case ST_SYS_SIZE: /* 0x21: no file yet -> create (+0x44 = 1), else write */
        if (!seq_call(call_sys_size, &r))
            return 1;
        seq_goto(ST_SYS_WRITE);
        return 1;
    case ST_SYS_WRITE: /* 0x22 */
        if (!seq_call(call_sys_write, &r))
            return 1;
        g_seq.result = r;
        if (r == 0)
            SDL_Log("[save] system data saved");
        else
            SDL_Log("[save] saving the system data failed: 0x%08X", (unsigned)r);
        seq_goto(ST_DONE);
        return 1;
    default:
        g_seq.kind = SEQ_IDLE;
        return 0;
    }
}

int savedata_check_update(void)
{
    return g_seq.kind == SEQ_CHECK ? check_update() : 0;
}

/* -- Story Data and Battle Data -- */

static void call_game_files(void) { util_files((int)g_seq.save_kind, true); }
static void call_game_read(void)
{
    util_read((int)g_seq.save_kind, g_seq.file, g_buf, game_save_size(g_seq.save_kind), true);
}
static void call_game_size(void)
{
    util_getsize((int)g_seq.save_kind, g_seq.file, game_save_size(g_seq.save_kind), false);
}
static void call_game_write(void)
{
    /* FUN_08abb680, types 0-2: the version string without its NUL, the shared block,
     * then the mode's block. */
    SaveKind k = g_seq.save_kind;
    if (k == SAVE_TEAM) { /* FUN_08abb680 type 5: built by the caller */
        memcpy(g_buf, g_team_file, TEAM_FILE_SIZE);
        util_write((int)k, g_seq.file, g_buf, TEAM_FILE_SIZE, !g_seq.exists, false);
        return;
    }
    size_t v = strlen(SAVE_VERSION);
    memcpy(g_buf, SAVE_VERSION, v);
    memcpy(g_buf + v, &g_save.db, GAME_DB_SIZE);
    memcpy(g_buf + v + GAME_DB_SIZE, mode_block(k), mode_block_size(k));
    util_write((int)k, g_seq.file, g_buf, game_save_size(k), !g_seq.exists, false);
}

/* FUN_08a641dc: at least five characters, ending in ".CHE". */
static bool is_game_file(const char *name)
{
    size_t n = strlen(name);
    return n >= 5 && !memcmp(name + n - 4, ".CHE", 4);
}

static void collect_list(void)
{
    g_seq.list_count = 0;
    for (uint32_t i = 0; i < g_file_info.resultNumNormalEntries && g_seq.list_count < SAVE_LIST_MAX; i++) {
        const SceUtilitySavedataFileListEntry *e = &g_file_entries[i];
        char name[17];
        memcpy(name, e->name, 16);
        name[16] = 0;
        if (!is_game_file(name) || strlen(name) >= SAVE_FILE_MAX)
            continue;
        SaveListEntry *l = &g_seq.list[g_seq.list_count++];
        snprintf(l->file, sizeof l->file, "%.12s", name);
        l->broken = e->stat.st_size != game_save_size(g_seq.save_kind);
        l->mtime = e->stat.sce_mtime;
    }
}

void savedata_list_start(SaveKind kind)
{
    g_seq.list_count = 0;
    seq_begin(SEQ_LIST, ST_GAME_FILES);
    g_seq.save_kind = kind;
}

int savedata_list_update(void)
{
    if (g_seq.kind != SEQ_LIST)
        return 0;
    int32_t r;
    if (g_seq.state == ST_GAME_FILES) {
        if (!seq_call(call_game_files, &r))
            return 1;
        if (r == 0)
            collect_list();
        else if (r != SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA)
            g_seq.result = r;
        SDL_Log("[save] %s: %d file(s) in %s/%s%s", kind_label(g_seq.save_kind), g_seq.list_count,
                psp_savedata_root(), SAVE_GAME_NAME, save_name((int)g_seq.save_kind));
        seq_goto(ST_DONE);
        return 1;
    }
    g_seq.kind = SEQ_IDLE;
    return 0;
}

int savedata_list(const SaveListEntry **out)
{
    *out = g_seq.list;
    return g_seq.list_count;
}

void savedata_load_start(SaveKind kind, const char *file)
{
    g_seq.peek = false;
    seq_begin(SEQ_LOAD, ST_GAME_READ);
    g_seq.save_kind = kind;
    snprintf(g_seq.file, sizeof g_seq.file, "%s", file);
}

void savedata_peek_start(SaveKind kind, const char *file)
{
    savedata_load_start(kind, file);
    g_seq.peek = true;
}

const GameDataBase *savedata_peeked(void)
{
    return &g_peek;
}

const uint8_t *savedata_team_file(void)
{
    return g_team_file;
}

void savedata_team_file_set(const uint8_t *data)
{
    memcpy(g_team_file, data, TEAM_FILE_SIZE);
}

void savedata_save_start(SaveKind kind, const char *file)
{
    seq_begin(SEQ_SAVE, ST_GAME_FILES);
    g_seq.save_kind = kind;
    snprintf(g_seq.file, sizeof g_seq.file, "%s", file);
}

/* The load's unpacking (0x088b7590..): skip the version, copy the shared block and the
 * mode's block. */
static bool unpack_game(void)
{
    SaveKind k = g_seq.save_kind;
    size_t v = strlen(SAVE_VERSION);
    if (g_param.dataSize != game_save_size(k)) {
        SDL_Log("[save] %s: %u bytes, expected %u", g_seq.file, (unsigned)g_param.dataSize,
                (unsigned)game_save_size(k));
        return false;
    }
    if (k == SAVE_TEAM) { /* checked and installed by the caller (FUN_08abba9c type 5) */
        memcpy(g_team_file, g_buf, TEAM_FILE_SIZE);
        return true;
    }
    if (memcmp(g_buf, SAVE_VERSION, v)) /* FUN_08ab8604 compares, the game goes on anyway */
        SDL_Log("[save] %s: version \"%.6s\", expected \"%s\"", g_seq.file, (const char *)g_buf, SAVE_VERSION);
    if (g_seq.peek) {
        memcpy(&g_peek, g_buf + v, GAME_DB_SIZE);
        return true;
    }
    memcpy(&g_save.db, g_buf + v, GAME_DB_SIZE);
    memcpy(mode_block(k), g_buf + v + GAME_DB_SIZE, mode_block_size(k));
    snprintf(kind_file(k), SAVE_FILE_MAX, "%s", g_seq.file);
    return true;
}

static int io_update(void)
{
    int32_t r;
    switch (g_seq.state) {
    case ST_GAME_READ:
        if (!seq_call(call_game_read, &r))
            return 1;
        if (r != 0 || !unpack_game()) {
            g_seq.result = r ? r : -1;
            SDL_Log("[save] loading %s failed (0x%08X)", g_seq.file, (unsigned)r);
        } else if (g_seq.peek || g_seq.save_kind == SAVE_TEAM) {
            SDL_Log("[save] %s %s read", kind_label(g_seq.save_kind), g_seq.file);
        } else {
            int used = 0;
            for (int i = 0; i < SAVE_CARDS; i++)
                used += savedata_card_used(i);
            SDL_Log("[save] %s %s loaded: %d OKE card(s)", kind_label(g_seq.save_kind), g_seq.file, used);
        }
        seq_goto(ST_DONE);
        return 1;
    case ST_GAME_FILES:
        if (!seq_call(call_game_files, &r))
            return 1;
        g_seq.exists = r == 0;
        if (r != 0 && r != SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA) {
            g_seq.result = r;
            seq_goto(ST_DONE);
        } else {
            seq_goto(ST_GAME_SIZE);
        }
        return 1;
    case ST_GAME_SIZE:
        if (!seq_call(call_game_size, &r))
            return 1;
        seq_goto(ST_GAME_WRITE);
        return 1;
    case ST_GAME_WRITE: /* BeginDataSave: McCreate when the save is new, else McSave */
        if (!seq_call(call_game_write, &r))
            return 1;
        g_seq.result = r;
        if (r == 0) {
            if (g_seq.save_kind != SAVE_TEAM)
                snprintf(kind_file(g_seq.save_kind), SAVE_FILE_MAX, "%s", g_seq.file);
            SDL_Log("[save] %s saved as %s/%s%s/%s", kind_label(g_seq.save_kind), psp_savedata_root(),
                    SAVE_GAME_NAME, save_name((int)g_seq.save_kind), g_seq.file);
        } else {
            SDL_Log("[save] saving %s failed: 0x%08X", g_seq.file, (unsigned)r);
        }
        seq_goto(ST_DONE);
        return 1;
    default:
        g_seq.kind = SEQ_IDLE;
        return 0;
    }
}

int savedata_io_update(void)
{
    return g_seq.kind == SEQ_LOAD || g_seq.kind == SEQ_SAVE ? io_update() : 0;
}

int32_t savedata_result(void)
{
    return g_seq.result;
}

int32_t savedata_run(int (*update)(void))
{
    for (int guard = 0; guard < 1000 && update(); guard++) {
    }
    return g_seq.result;
}

static bool read_preset(Pack *pack, const char *path, OkeDataBase *out)
{
    const PackEntry *e = path ? pack_find(pack, path) : NULL;
    return e && pack_read(pack, e, out, sizeof *out) == (long)sizeof *out;
}

/* ---- CTutorialFlow::loadPreset (FUN_08ae1090) ---- */

#define BATTLE_TEAMS 0x40C80 /* CGameCommonDataBase's two battle teams (FUN_0889ce7c + 0x40) */

static void battle_team_member(int team, int m, int card, int spot)
{
    /* FUN_08ad34cc / FUN_08ad332c: team * 0x340, member + 0x290 + m * 0x30; the card at
     * +0x20 (FUN_08ad31fc), the spot at +0x24 (FUN_08ad3214). */
    uint8_t *f = (uint8_t *)&g_save.db + BATTLE_TEAMS + TEAM_CARD_SIZE * team + 0x290 + 0x30 * m;
    put_word(f + 0x20, (uint32_t)card);
    put_word(f + 0x24, (uint32_t)spot);
}

bool savedata_story_preset(void)
{
    Pack pack;
    if (pack_open(&pack, "disc0:/PSP_GAME/USRDIR") < 0) {
        SDL_Log("[save] story preset: can't open the pack");
        return false;
    }
    static const char *const k_cards[2] = {"preset/lecture/oke_l00_1.bin", "preset/lecture/oke_l00_2.bin"};
    bool ok = true;
    for (int i = 0; i < 2; i++)
        if (!read_preset(&pack, k_cards[i], &g_save.db.card[i])) {
            SDL_Log("[save] story preset: %s missing", k_cards[i]);
            ok = false;
        }
    const PackEntry *e = pack_find(&pack, "preset/lecture/team_f00.bin");
    if (!e || pack_read(&pack, e, g_save.db.team[0], TEAM_CARD_SIZE) != TEAM_CARD_SIZE) {
        SDL_Log("[save] story preset: preset/lecture/team_f00.bin missing");
        ok = false;
    }
    pack_close(&pack);
    /* The battle teams: the player's first member is card 32 + 6 (the game-common
     * block's cards follow the 32 saved ones, FUN_0889eaf8), the other team's card 1;
     * both on spot 4. */
    battle_team_member(0, 0, SAVE_CARDS + 6, 4);
    battle_team_member(1, 0, 1, 4);
    return ok;
}

/* ---- the test save ---- */

static void set_card_name(OkeDataBase *card, const char *name)
{
    memset(card->header + 4, 0, 0x1C);
    snprintf((char *)card->header + 4, 0x1C, "%s", name);
}

bool savedata_make_test_save(const char *file)
{
    Pack pack;
    if (pack_open(&pack, "disc0:/PSP_GAME/USRDIR") < 0) {
        SDL_Log("[save] test save: can't open the pack");
        return false;
    }
    savedata_new_game();
    savedata_battle_mode_new();
    memcpy(g_save.battle, "TESTER", 6); /* the player name */
    /* Money to try the Shop with, and a few owned parts put back on sale so every
     * category has something (a test-only change: a real new game sells only bodies
     * 14 and 33 and the Other items 0x14-0x1E). */
    savedata_add_money(TEST_SAVE_MONEY);
    static const struct { uint8_t group, id; } k_sale[] = {
        {STOCK_CPU, 7}, {STOCK_ARMOR, 11}, {STOCK_ADD_ARMOR, 6}, {STOCK_WEAPON, 0x23}, {STOCK_WEAPON, 0x2C},
        {STOCK_OPTION, 10},
    };
    for (unsigned i = 0; i < sizeof k_sale / sizeof k_sale[0]; i++)
        savedata_set_stock(k_sale[i].group, k_sale[i].id, 1);
    int n = 0;
    /* The two simulation OKEs, then the leaders of the first ten arena teams. */
    static const char *const k_sim[2] = {"preset/oke_t01_1.bin", "preset/oke_t01_2.bin"};
    for (int i = 0; i < 2; i++)
        if (read_preset(&pack, k_sim[i], &g_save.db.card[n])) {
            char name[24];
            snprintf(name, sizeof name, "Simulation %d", i + 1);
            set_card_name(&g_save.db.card[n++], name);
        }
    int first_arena = n;
    for (int i = 0; i < 10 && n < SAVE_CARDS; i++) {
        const char *path = gamedata_string(ARENA_RECORDS + (uint32_t)i * ARENA_STRIDE + 0x14u);
        if (!read_preset(&pack, path, &g_save.db.card[n]))
            continue;
        const char *name = ptr_string(ARENA_NAMES + 4u * (uint32_t)i);
        set_card_name(&g_save.db.card[n++], name ? name : "Arena OKE");
    }
    /* Two teams, named and owned as createTeam names them (the "Team 01" table at
     * 0x08c8ba08, CTeamFormationString 0x19): three arena OKEs, and the two
     * simulation OKEs. Members take the free spots in the game's order. */
    const char *owner = ptr_string(TEAM_STRINGS + 4u * 0x19);
    static const int k_spots[3] = {4, 3, 5};
    if (n - first_arena >= 3) {
        savedata_team_create(0, ptr_string(TEAM_NAMES), owner);
        for (int m = 0; m < 3; m++)
            savedata_team_set_member(0, m, first_arena + m, k_spots[m]);
    }
    if (first_arena >= 2) {
        savedata_team_create(1, ptr_string(TEAM_NAMES + 4u), owner);
        for (int m = 0; m < 2; m++)
            savedata_team_set_member(1, m, m, k_spots[m]);
    }
    pack_close(&pack);
    if (n == 0) {
        SDL_Log("[save] test save: no preset OKEs could be read");
        return false;
    }
    savedata_save_start(SAVE_BATTLE, file);
    int32_t r = savedata_run(savedata_io_update);
    SDL_Log("[save] test save %s with %d preset OKE card(s), %d team(s) and %d Victory Points: %s", file, n,
            (savedata_team_status(0) != 0) + (savedata_team_status(1) != 0), savedata_money(),
            r == 0 ? "written" : "failed");
    return r == 0;
}
