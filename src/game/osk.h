/* A stand-in for the PSP's on-screen keyboard (sceUtilityOsk), which is system
 * software, not part of the game. The game opens it through CUtility (FUN_08b2552c:
 * the text to start from, a description shown above the field, and a length limit)
 * and reads the text back when it ends with OK (status 2, FUN_08b23c3c / FUN_08b25730).
 *
 * This one draws a character grid over the screen: the d-pad moves (wrapping), circle
 * types the character, square deletes the last one, triangle adds a space, Start
 * accepts and cross cancels. ASCII only. See docs/screens.md ("On-screen keyboard"). */
#ifndef GAME_OSK_H
#define GAME_OSK_H

#include <stdbool.h>

#define OSK_TEXT_MAX 128

enum { OSK_RUNNING = 1, OSK_OK = 2, OSK_CANCEL = 0 };

/* Opens it on `text` (cut to `max_len` characters) with `title` above the field. */
void osk_open(const char *title, const char *text, int max_len);
bool osk_active(void);
/* Each frame while active: OSK_RUNNING, then OSK_OK or OSK_CANCEL once. */
int osk_update(void);
/* The text typed, after OSK_OK. */
const char *osk_text(void);
void osk_draw(void);

#endif
