/* Emblems (CDesignMngr mode 7): CEmblemEditor. See oke_emblem.c and
 * docs/screens.md ("Emblems"). */
#ifndef GAME_OKE_EMBLEM_H
#define GAME_OKE_EMBLEM_H

void emblem_open(int card);
/* 1 while open, 0 once it has written the emblem back and closed. */
int emblem_update(float dt);
void emblem_draw(void);
const char *emblem_help(void);

#endif
