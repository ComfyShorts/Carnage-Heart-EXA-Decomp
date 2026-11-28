/* Software Setup (OKE Configuration's Software item): CSoftDesignMngr with its
 * CPgChipWnd, CPgChipSelectWnd and CPgChipConnectWnd. See oke_soft.c and
 * docs/screens.md ("Software Setup"). */
#ifndef GAME_OKE_SOFT_H
#define GAME_OKE_SOFT_H

void soft_open(int card);
/* 1 while open, 0 once it has written the program back into the card and closed. */
int soft_update(float dt);
void soft_draw(void);
const char *soft_help(void);

#endif
