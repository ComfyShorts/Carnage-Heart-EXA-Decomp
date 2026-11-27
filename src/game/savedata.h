/* The game's save data: CSaveMngr / CUtility / CSystemMngr's save paths, and the
 * CCarnageHeart blocks they store. See docs/screens.md ("Save data").
 *
 * Two kinds of save are ported:
 *   - System data: ULJM05698/DATA.BIN, the 0x7C-byte block at CCarnageHeart + 0x3AE00
 *     (sound volumes, options, the Story Mode flag, play time). Checked at start-up
 *     (CSaveMngr save type 4, exec type 3): loaded when it exists, created when not.
 *   - Battle Data: ULJM05698B/<NAME>.CHE, one file per save the player names:
 *     the version "0.0.44", the shared block (CCarnageHeart + 0x3AE80, 0x483A0 bytes:
 *     the 32 OKE cards first) and the Battle Mode block (+0x860F0, 0x431C bytes).
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
    char battle_file[SAVE_FILE_MAX]; /* the Battle Data file loaded or last saved, "" none */
} SaveState;

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
/* System data +0x74 (FUN_0889c400): Story Mode was completed. */
bool savedata_story_completed(void);

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

/* ---- the sequences ---- */

/* The start-up check (CSystemMngr mode 0x17, CSaveMngr type 4 exec 3). */
void savedata_check_start(void);
int savedata_check_update(void);

/* The Battle Data files (FILES on ULJM05698B, the ".CHE" ones; FUN_08a672f8). */
typedef struct SaveListEntry {
    char file[SAVE_FILE_MAX];
    bool broken; /* not BATTLE_SAVE_SIZE bytes (the list's +0x30 flag) */
    ScePspDateTime mtime;
} SaveListEntry;
void savedata_list_start(void);
int savedata_list_update(void);
int savedata_list(const SaveListEntry **out); /* after the list is done */

/* Load or save one Battle Data file (READSECURE / MAKEDATA or WRITE). */
void savedata_load_start(const char *file);
void savedata_save_start(const char *file);
int savedata_io_update(void);
/* The last sequence's outcome: 0 fine, else the sceUtility result (or -1). */
int32_t savedata_result(void);

/* Runs a started sequence to the end without waiting for frames (tools, tests). */
int32_t savedata_run(int (*update)(void));

/* --make-test-save: a Battle Data file named `file` whose first cards are shipped
 * preset OKEs (read from the pack at run time). Returns false if it couldn't. */
bool savedata_make_test_save(const char *file);

#endif
