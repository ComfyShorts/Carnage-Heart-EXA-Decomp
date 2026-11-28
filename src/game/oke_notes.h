/* Notes (CDesignMngr mode 10): CCommentWnd and its button-tag keyboard CKeyOskWnd.
 * See oke_notes.c and docs/screens.md ("Notes"). */
#ifndef GAME_OKE_NOTES_H
#define GAME_OKE_NOTES_H

#include <stdbool.h>

/* Opens on `card`'s notes; an installed card's are only viewed (FUN_088a6dec). */
void notes_open(int card, bool read_only);
/* Each frame: 1 while open, 0 once it closes. */
int notes_update(float dt);
void notes_draw(void);
/* The help line for the current state, or NULL. */
const char *notes_help(void);
/* True while a keyboard is open: the help window is hidden then (FUN_088abb58). */
bool notes_help_hidden(void);

#endif
