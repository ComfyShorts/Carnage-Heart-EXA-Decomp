/* Emblems (CDesignMngr mode 7): CEmblemEditor. See oke_emblem.c and
 * docs/screens.md ("Emblems"). */
#ifndef GAME_OKE_EMBLEM_H
#define GAME_OKE_EMBLEM_H

void emblem_open(int card);
/* Team Formation's Emblems (CTeamFormationMngr mode 10, FUN_08ac1d94): the editor's
 * team variant (+0xC4 set): the team card's emblem (+0x10), ten tools (the table at
 * 0x08be6f60, no Placement) and no OKE beside it. */
void emblem_open_team(int team);
/* 1 while open, 0 once it has written the emblem back and closed. */
int emblem_update(float dt);
void emblem_draw(void);
const char *emblem_help(void);

#endif
