/* Team Formation's Transfer Team Data, without the screens (team_menu.c draws and
 * drives them; docs/screens.md, "Team Formation"):
 *   - From Save Data (CTeamFormationMngr modes 0xC / 0xF, FUN_08ac6aec / FUN_08ac6db8):
 *     a team of another save, installed with its OKEs;
 *   - the Team Trade Folder (modes 0xE / 0x10 / 0x11): CExportTeamData, the
 *     TEAM_FILE_SIZE-byte file of one team and its OKEs (built by FUN_08ab7820, checked
 *     by FUN_08abba9c, installed by FUN_08ab7b54).
 * CTeamDataDockInfoWnd (FUN_08abd300, FUN_08abd210 / FUN_08abd238) counts the teams
 * and OKE cards in use against 16 and 32. */
#ifndef GAME_TEAM_TRADE_H
#define GAME_TEAM_TRADE_H

#include <stdbool.h>
#include <stdint.h>

#include "savedata.h"

/* The teams and OKE cards in use (FUN_08abd300: status not 0). */
int team_trade_teams_used(void);
int team_trade_cards_used(void);
/* No room for another team, or none for another card (FUN_08abd210 / FUN_08abd238):
 * From Save Data and the folder aren't offered then. */
bool team_trade_full(void);

/* FUN_08ac4c98: a team card that is in use and has a member. */
bool team_trade_has_members(const uint8_t *team);
/* FUN_08ac4d20: how many different OKE cards a team card's members use. */
int team_trade_distinct_cards(const uint8_t *team);

/* FUN_08ac6db8 (circle): installs `team` of another save's shared block `from` in the
 * first free team card, status 2, its OKEs (each card once) in the first free OKE
 * cards, status 2, the members pointed at them. False, with nothing changed, when it
 * has no member or there isn't room (FUN_08abd25c / FUN_08abd284). */
bool team_trade_install(const GameDataBase *from, int team);

/* FUN_08ab7820: the player's team `team` as a CExportTeamData file. False when it
 * isn't the player's or has no member (FUN_08ac3444). */
bool team_trade_export(int team, uint8_t out[TEAM_FILE_SIZE]);
/* FUN_08abba9c (type 5) and FUN_08ab7b54: checks a file's sum and its counts and
 * installs it (status 2); the team card it went to, or -1. */
int team_trade_import(const uint8_t file[TEAM_FILE_SIZE]);

/* FUN_089ee26c: a program has a chip that reads the pad (the manual OKE chips: types
 * 9, 0x1B, 0x1C, or type 5 with 0x26-0x28 in its fourth byte). */
bool team_trade_manual(const OkeDataBase *card);

#endif
