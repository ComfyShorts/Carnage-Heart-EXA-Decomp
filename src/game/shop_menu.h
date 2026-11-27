/* The Shop (CShopFlow FUN_08a71dc8 / CShopMngr), opened from the Battle Mode top
 * menu: the categories with something for sale, each category's list with prices,
 * and buying with Victory Points. Hand-written from the functions named in
 * shop_menu.c; what is a stand-in is listed in docs/screens.md.
 *
 * Purchases go straight into the save (savedata.h): the money goes down, the part's
 * stock becomes 2 (owned) so OKE Configuration's lists offer it, and the "Other"
 * items take effect. */
#ifndef GAME_SHOP_MENU_H
#define GAME_SHOP_MENU_H

/* CShopMngr::Initialize (FUN_08a73798) with its top menu (FUN_08a73d78). */
void shop_menu_open(void);

/* One step: 1 while the screen runs, 0 once the player leaves (or when nothing is
 * for sale: the game's shop closes at once then). */
int shop_menu_update(float dt);

/* The whole screen, background and help window included. */
void shop_menu_draw(void);

#endif
