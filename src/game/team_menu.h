/* Team Formation (CTeamFormationFlow FUN_08ac01b8 / CTeamFormationMngr), opened from
 * the Battle Mode top menu: the player's 16 team cards, each up to three OKE cards
 * on formation spots. Hand-written from the functions named in team_menu.c; what is
 * a stand-in is listed in docs/screens.md.
 *
 * The team cards themselves are in the save (savedata.h). */
#ifndef GAME_TEAM_MENU_H
#define GAME_TEAM_MENU_H

#include <stdbool.h>
#include <stddef.h>

/* CTeamFormationMngr::Initialize (FUN_08ac536c) and the top menu. */
void team_menu_open(void);

/* One step: 1 while the screen runs, 0 once the player backs out. */
int team_menu_update(float dt);

/* The whole screen, background and help window included. */
void team_menu_draw(void);

/* The team the player last chose in Select Team (CCarnageHeart + 0xD0170, not saved),
 * -1 none. */
int team_menu_selected(void);

/* ---- shared with the Battle Arena's Select Team ---- */

/* CTeamFormationString (FUN_08ac7d54, the table at 0x08c8ba60). */
const char *team_string(int id);

/* A team card's line in the team list: number, name and OKE count. */
void team_label(int team, char *out, size_t n);

/* The team window: emblem, name, OKE count and the three members. */
void team_panel_draw(int team, int x, int y, int cursor);

#endif
