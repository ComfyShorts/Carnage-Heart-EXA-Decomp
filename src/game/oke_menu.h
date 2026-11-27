/* OKE Configuration (CDesignFlow FUN_088b94f0 / CDesignMngr), opened from the Battle
 * Mode top menu: the card being worked on, Select OKE Card, Hardware (CHardDesignMngr)
 * and Software (CSoftDesignMngr). Hand-written from the functions named in
 * oke_menu.c; what is a stand-in is listed in docs/screens.md.
 *
 * The cards are the save's (savedata.h); changes go straight into them, so the
 * battles and the next save use them. */
#ifndef GAME_OKE_MENU_H
#define GAME_OKE_MENU_H

/* CDesignMngr's constructor (FUN_088bd918) and its top menu (FUN_088b9c28). */
void oke_menu_open(void);

/* One step: 1 while the screen runs, 0 once the player backs out. */
int oke_menu_update(float dt);

/* The whole screen, background and help window included. */
void oke_menu_draw(void);

#endif
