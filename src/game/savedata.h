/* The game's save data: CSaveMngr / CUtility / CSystemMngr's save paths, and the
 * CCarnageHeart blocks they store. See docs/screens.md ("Save data").
 *
 * Three kinds of save are ported:
 *   - System data: ULJM05698/DATA.BIN, the 0x7C-byte block at CCarnageHeart + 0x3AE00
 *     (sound volumes, options, the Story Mode flag, play time). Checked at start-up
 *     (CSaveMngr save type 4, exec type 3): loaded when it exists, created when not.
 *   - Battle Data: ULJM05698B/<NAME>.CHE, one file per save the player names:
 *     the version "0.0.44", the shared block (CCarnageHeart + 0x3AE80, 0x483A0 bytes:
 *     the 32 OKE cards first) and the Battle Mode block (+0x860F0, 0x431C bytes).
 *   - Story Data: ULJM05698S/<NAME>.CHE, the same with the Story Mode block
 *     (CScenarioModeDataBase, +0x83570, 0x2B80 bytes) in place of the Battle Mode one.
 *
 * The sequences run through sceUtilitySavedata* one call per frame, as the game's do;
 * their *_update functions return 1 while running and 0 when done. */
#ifndef GAME_SAVEDATA_H
#define GAME_SAVEDATA_H

#include <stdbool.h>
#include <stdint.h>

#include "okedb.h"
#include "psp_types.h"

#define SAVE_GAME_NAME "ULJM05698"
#define SAVE_VERSION "0.0.44"           /* CSystemMngr's data version (0x08c8b760..) */
#define SYSTEM_DATA_SIZE 0x7C           /* CSystemMngr::GetSaveSize (FUN_08ab8114), type 4 */
#define GAME_DB_SIZE 0x483A0            /* CCarnageHeart + 0x3AE80 */
#define BATTLE_DATA_SIZE 0x431C         /* CCarnageHeart + 0x860F0 */
#define BATTLE_SAVE_SIZE (6 + GAME_DB_SIZE + BATTLE_DATA_SIZE) /* strlen(version) + 0x4C6BC */
#define STORY_DATA_SIZE 0x2B80          /* CCarnageHeart + 0x83570 */
#define STORY_SAVE_SIZE (6 + GAME_DB_SIZE + STORY_DATA_SIZE)   /* strlen(version) + 0x4AF20 */
#define SAVE_CARDS 32                   /* FUN_0889ec18 */
#define SAVE_TEAMS 16
#define TEAM_CARD_SIZE 0x340
#define SAVE_FILE_MAX 13                /* "NAMENAME.CHE" + NUL */
#define SAVE_LIST_MAX 32

/* The shared block (FUN_0889c92c): 32 COkeDataBase cards, 16 team cards, then the
 * rest (rankings, shop, matches...), kept as bytes. */
typedef struct GameDataBase {
    OkeDataBase card[SAVE_CARDS];                 /* 0x00000 */
    uint8_t team[SAVE_TEAMS][TEAM_CARD_SIZE];     /* 0x3D800 */
    uint8_t rest[GAME_DB_SIZE - 0x3D800 - SAVE_TEAMS * TEAM_CARD_SIZE]; /* 0x40C00 */
} GameDataBase;

_Static_assert(sizeof(GameDataBase) == GAME_DB_SIZE, "CCarnageHeart shared block");

typedef struct SaveState {
    uint8_t system[SYSTEM_DATA_SIZE];
    GameDataBase db;
    uint8_t battle[BATTLE_DATA_SIZE];
    uint8_t story[STORY_DATA_SIZE];
    char battle_file[SAVE_FILE_MAX]; /* the Battle Data file loaded or last saved, "" none */
    char story_file[SAVE_FILE_MAX];  /* the same for Story Data */
} SaveState;

/* The game-save kinds (CSystemMngr save types; save names "S" / "B" from 0x08c858f8). */
typedef enum { SAVE_STORY = 1, SAVE_BATTLE = 2 } SaveKind;

extern SaveState g_save;

/* CCarnageHeart::Reinitialize's blocks for a new game: blank cards, default teams.
 * The parts of the shared block the port doesn't use start zeroed (docs/screens.md). */
void savedata_new_game(void);
void savedata_new_system(void); /* FUN_0889c0c0 */

/* A card is in use when its first word is set (FUN_089eea0c clears it). */
bool savedata_card_used(int card);
const OkeDataBase *savedata_card(int card);
/* The card's name (COkeDataBase + 4), "" when it has none. */
void savedata_card_name(int card, char *out, size_t n);
/* The card's first word (FUN_089eea18): 0 blank, 1 the player's, 2 / 3 installed
 * from others (FUN_089eea58: those can't be edited). */
int savedata_card_status(int card);
/* The card to change in place (OKE Configuration), NULL out of range. */
OkeDataBase *savedata_card_edit(int card);
/* CGameCommonDataBase::SetCurrentOkeIndex / its getter (FUN_0889cd3c / FUN_0889cdd0):
 * the card OKE Configuration works on, kept in the shared block at +0x40C00. */
int savedata_current_card(void);
void savedata_set_current_card(int card);
/* FUN_089eea80 (a blank card) and a whole-card copy, as Select OKE Card does. */
void savedata_card_clear(int card);
void savedata_card_copy(int from, int to);
/* System data +0x74 (FUN_0889c400 / FUN_0889c408): Story Mode was completed. */
bool savedata_story_completed(void);
void savedata_set_story_completed(bool done);

/* ---- the Story Mode block (CScenarioModeDataBase, 0x2B80 bytes) ----
 *   +0x0000 the current step (FUN_0889d498 / FUN_0889d4a0), -1 before the first
 *   +0x0088 0x34 lesson state bytes, +0x00BC 0x34 lesson-cleared words (FUN_0889d46c)
 *   +0x018C the lesson's mode (FUN_0889d534): 0 none, 1 a review, 2 a first time
 *   +0x0190 the lesson taken (its number, FUN_0889d544), -1 none
 *   +0x0198 the hidden scripts (CEventScriptField, FUN_0889d564)
 *   +0x2B28 set by script ops 0x2D / 0x2E (FUN_0889d574), -1 none
 *   +0x2B2C the current battle mission (FUN_0889d710), +0x2B30 the mission kept
 *           while a lesson is taken on a mission step (FUN_0889d720)
 *   +0x2B34 the briefing's script file, 0x40 bytes (FUN_0889d584)
 *   +0x2B74 / +0x2B78 the most OKE / team cards (2 / 4 at first)
 *   +0x2B7C a mission's briefing script has ended (FUN_0889d730) */
#define STORY_STEP_NONE (-1)
int savedata_story_step(void);
void savedata_set_story_step(int step);
int savedata_story_battle(void);
void savedata_story_set_2b28(int v); /* FUN_0889d574 */
void savedata_set_story_battle(int mission);
/* The lesson cards (CLectureCredit's 49 records): a state byte (bit 0 the lesson is
 * done) and an offered word (bit 0 the card is listed). */
int savedata_lesson_state(int card);
void savedata_set_lesson_state(int card, int state);
uint32_t savedata_lesson_offered(int card);
void savedata_set_lesson_offered(int card, uint32_t v);
int savedata_story_saved_battle(void);
void savedata_set_story_saved_battle(int mission);
bool savedata_story_briefed(void);
void savedata_set_story_briefed(bool seen);
int savedata_lesson_mode(void);
void savedata_set_lesson_mode(int mode);
int savedata_lesson_current(void);
void savedata_set_lesson_current(int lesson);
/* The briefing's script file (FUN_0889d584), "" none. */
const char *savedata_story_script(void);
void savedata_set_story_script(const char *path);
/* The hidden scripts (CEventScriptField at +0x198): 0x13 menus, each up to 8 script
 * ids by key. Hide stores one (FUN_088df4b0), Seek finds it (FUN_088df540, NULL
 * none), Clear removes it (FUN_088df5d4). */
#define STORY_FIELD_MENUS 0x13
bool savedata_story_field_hide(int menu, int key, const char *id);
const char *savedata_story_field_seek(int menu, int key);
void savedata_story_field_clear(int menu, int key);
bool savedata_story_field_exists(int menu, int key);
void savedata_story_field_reset(void);

/* A new Story Mode game: CScenarioModeDataBase's reset (FUN_0889d3dc), then
 * FUN_088d3d80 mode 0: the menu limits (FUN_0895ddd4), the stock (FUN_08918cb0), the
 * maps (FUN_0882f64c) and +0x2B28 = -1. CTutorialFlow::loadPreset's cards come after
 * (story.c, from the pack). */
void savedata_story_new(void);
/* CTutorialFlow::loadPreset (FUN_08ae1090): OKE cards 0 and 1 from
 * preset/lecture/oke_l00_1.bin / _2.bin, team card 0 from preset/lecture/team_f00.bin
 * (read from the pack), and the two battle teams' first members. False when a file
 * is missing. */
bool savedata_story_preset(void);

/* ---- the team cards (CTeamDataBase, 0x340 bytes; FUN_0889cb10) ----
 *   +0x000 status: 0 unused, 1 the player's, 2 / 3 installed from others (FUN_089eea18)
 *   +0x010 emblem: 32 x 32 4-bit pixels, then 16 palette words (0x240 bytes)
 *   +0x250 team name; +0x269 the owner's name
 *   +0x290 three CFormationDataBase entries of 0x30: two vectors, +0x20 the member's
 *          OKE card (-1 none, FUN_08ad3204), +0x24 its formation spot (FUN_08ad320c)
 *   +0x320 the date it was made; +0x330, +0x334 counters; +0x338 1000 */
#define TEAM_MEMBERS 3
#define TEAM_NAME_MAX 0x19

int savedata_team_status(int team);
int savedata_team_member(int team, int m);
int savedata_team_spot(int team, int m);
int savedata_team_count(int team); /* registered members */
void savedata_team_name(int team, char *out, size_t n);
const uint8_t *savedata_team_emblem(int team);
/* CTeamFormationMngr::createTeam (FUN_08ac42f4 with FUN_089f38a0): a new team of the
 * player's, named `name`, owned by `owner`, with no members and the default emblem
 * (blank pixels, the palette at 0x08c83db8). */
void savedata_team_create(int team, const char *name, const char *owner);
void savedata_team_delete(int team);
/* Registers card (or -1 to unregister) as member m, on formation spot `spot`. */
void savedata_team_set_member(int team, int m, int card, int spot);

/* ---- what the player owns (shared block; all saved with the Battle Data) ----
 * The parts stock (CCarnageHeart + 0x3AE80 + 0x47BBC, FUN_0889d288): a word per item,
 * 0 not offered, 1 for sale in the Shop, 2 owned (the OKE Configuration lists show
 * owned parts only). Groups and their words (FUN_08918bcc): */
enum {
    STOCK_BODY,      /* +0x000, 0x25 */
    STOCK_CPU,       /* +0x094, 8 */
    STOCK_ARMOR,     /* +0x0B4, 12 */
    STOCK_ADD_ARMOR, /* +0x0E4, 8 */
    STOCK_WEAPON,    /* +0x104, 0x3C */
    STOCK_OPTION,    /* +0x1F4, 11 */
    STOCK_CHIP,      /* +0x220, 0x36 */
    STOCK_OTHER,     /* +0x2F8, 0x1F: the Shop's "Other" items (0x08c8a570) */
    STOCK_GROUPS,
};
int savedata_stock(int group, int id);
/* FUN_08919354: then FUN_08919694 keeps the "none" entries (no additional armour,
 * no weapon, no option, the first chip) owned. */
void savedata_set_stock(int group, int id, int value);

/* Victory Points (+0x48394, FUN_0889d2f8): add (FUN_0889d308, capped at 99999) and
 * spend (FUN_0889d338, floored at 0). */
int savedata_money(void);
void savedata_add_money(int amount);
void savedata_spend_money(int amount);

/* CMenuLimitDataBase (+0x479B0, FUN_0889d278): words of 0 / 1 that switch menu items
 * off and on. Word 6 + n is OKE Configuration's item n (FUN_0895e3c4), word 0x67 + n
 * the System Menu's (FUN_0895e8d4). */
#define LIMIT_DESIGN 6
#define LIMIT_SYSTEM 0x67
int savedata_menu_limit(int word);
void savedata_set_menu_limit(int word, int value);

/* CSoftwareSceneDataBase (+0x41370, FUN_0889ce8c; built by FUN_0889c724): the chip
 * editor's memory. A chip per type, the last one placed, which new chips of that
 * type copy (FUN_08a1cbd8 stores it); the chip list's page (+0x1B0, 4 = every chip)
 * and its cursor on each of the 5 pages (+0x1B4). The 8 macros after it (+0x1C8,
 * 0x72C each) aren't used yet. */
#define CHIP_PAGES 5
const PgChipCommand *savedata_chip_default(int type);
void savedata_set_chip_default(const PgChipCommand *chip);
int savedata_chip_page(void);
void savedata_set_chip_page(int page);
int savedata_chip_page_cursor(int page);
void savedata_set_chip_page_cursor(int page, int index);

/* CBattleFieldStateDataBase (+0x47F30, FUN_0889d2b8): a word per stage, 1 when its map
 * can be chosen. FUN_0889ed84: available when 1, never stage 13. */
#define SAVE_STAGES 0xAF
/* The BGM Player settings (CGameCommonDataBase +0x44E98..): three lists, 0 the
 * menus, 1 the software designer, 2 battles, each a flag per BGM table row (99, all
 * off but row 20, 17 and 21 in a new game) and a play mode (+0x4533C..: 0 in order,
 * 1 shuffled, 2 at random). FUN_0889cf68 / FUN_0889d048 / FUN_0889d128, FUN_0889d208.. */
bool savedata_bgm_enabled(int list, int row);
int savedata_bgm_mode(int list);

int savedata_map_state(int stage);
void savedata_set_map_state(int stage, int value);
bool savedata_stage_available(int stage);

/* A new Battle Mode game (FUN_088d3d80 mode 1): the menu limits (FUN_0895de04), the
 * stock (FUN_08918d5c: everything owned but bodies 14 and 33 and the Other items
 * 0x14-0x1E, which are for sale) and the first 14 maps (FUN_0882f65c). */
void savedata_battle_mode_new(void);

/* ---- the sequences ---- */

/* The start-up check (CSystemMngr mode 0x17, CSaveMngr type 4 exec 3). */
void savedata_check_start(void);
int savedata_check_update(void);

/* A kind's files (FILES on ULJM05698S / ULJM05698B, the ".CHE" ones; FUN_08a672f8). */
typedef struct SaveListEntry {
    char file[SAVE_FILE_MAX];
    bool broken; /* not the kind's size (FUN_08ab8114(3, kind); the list's +0x30 flag) */
    ScePspDateTime mtime;
} SaveListEntry;
void savedata_list_start(SaveKind kind);
int savedata_list_update(void);
int savedata_list(const SaveListEntry **out); /* after the list is done */

/* Load or save one game file of the kind (READSECURE / MAKEDATA or WRITE). */
void savedata_load_start(SaveKind kind, const char *file);
void savedata_save_start(SaveKind kind, const char *file);
int savedata_io_update(void);
/* The last sequence's outcome: 0 fine, else the sceUtility result (or -1). */
int32_t savedata_result(void);

/* Runs a started sequence to the end without waiting for frames (tools, tests). */
int32_t savedata_run(int (*update)(void));

/* --make-test-save: a Battle Data file named `file` whose first cards are shipped
 * preset OKEs (read from the pack at run time), on a new Battle Mode game with
 * TEST_SAVE_MONEY Victory Points and a few parts back on sale to try the Shop with
 * (see docs/screens.md). Returns false if it couldn't. */
#define TEST_SAVE_MONEY 5000
bool savedata_make_test_save(const char *file);

#endif
