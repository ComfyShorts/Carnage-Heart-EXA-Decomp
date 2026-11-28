/* COkeCard: one card as the game draws it in its lists, a strip of three bands (the
 * weight line, the name, the hardware with the OKE's picture, its emblem and the
 * equipment icons). See oke_card.c. */
#ifndef GAME_OKE_CARD_H
#define GAME_OKE_CARD_H

#include <stdbool.h>

#define OKE_CARD_W 0x9E

/* Draws card `number` at (x, y), highlighted when `selected`; returns its height. */
int oke_card_draw(int number, int x, int y, bool selected);

#endif
