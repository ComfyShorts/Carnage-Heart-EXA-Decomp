/* The Battle Mode screens between the title and a battle, as the game runs them:
 *
 *   CBattleModeFlow / CBattleModeMngr (FUN_08837654, FUN_08838aa0): the top menu
 *     (OKE Configuration, Team Formation, Battle, Shop, System Menu).
 *   CBattleSelectFlow / CBattleSelectMngr (FUN_088838f8): Test Program, Battle
 *     Arena, Matchmaking.
 *   CBattleTestMngr (FUN_08827c98, FUN_08828db8, FUN_088291cc): Select OKE to Test
 *     (Enemy and Friend teams from the OKE cards), Map (CStageSelectWnd, map size,
 *     barricade), Test Time, Start Test and its "Start battle?" question.
 *   CBattleArenaMngr (FUN_0880a310, FUN_0880d424): Select Opponent (the 100
 *     opponents in the EBOOT, each with its own stage), Select Team, Start Battle
 *     Arena and "Start Battle Arena?".
 *
 * Text, opponents and stages are read from the user's EBOOT at run time; pictures are
 * the game's parts. What is a stand-in is listed in docs/screens.md. */
#ifndef GAME_BATTLE_MENU_H
#define GAME_BATTLE_MENU_H

#include <stdbool.h>

/* What the screens hand to the battle. */
#define SETUP_TEAM_MAX 3 /* CTeamDataBase's three members */

typedef struct BattleOke {
    int card;             /* a saved card (0-31), */
    char path[64];        /* or -1 and a record in the pack */
    char label[48];
    int spot;             /* formation spot 0-8 (CFormationDataBase +0x24), -1 by order */
} BattleOke;

typedef struct BattleSetup {
    BattleOke oke[2][SETUP_TEAM_MAX]; /* [0] the player's team, [1] the enemy's */
    int count[2];         /* members per team, 1-3 */
    char team_label[2][48];
    int stage;            /* CPresetStage index (0xAF entries) */
    int map;              /* land/mapNN */
    float time_limit;     /* seconds */
    float map_size;       /* metres: 180, 360 or 540 */
    int barricade;        /* 0 none, 1 normal, 2 damaging */
    bool arena;           /* from the Battle Arena (else the Test Program) */
} BattleSetup;

void battle_menu_start(void);

/* Story Mode's briefing hub (CBriefingMngr, FUN_08892c1c): the same screens with the
 * hub's six items, Mission under Battle, and the scripts hidden on the screens. */
void battle_menu_start_story(void);
/* After a battle: a won mission moves the story on (FUN_08818ff0). */
void battle_menu_battle_over(int result);
/* The hub has ended because the step is done (battle_menu_update returned 0). */
bool battle_menu_story_advanced(void);

/* One step. Returns 1 while the menus run, 2 when a battle should start
 * (battle_menu_setup() has it), 0 when the player backs out to the title (Story
 * Mode: when the step is done). */
int battle_menu_update(float dt);

void battle_menu_draw(void);

const BattleSetup *battle_menu_setup(void);

/* After the battle: back to the screen it was started from. */
void battle_menu_resume(void);

#endif
