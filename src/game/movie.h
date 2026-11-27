/* The game's PSMF movie player (FUN_08b1d0ec and its threads, FUN_08b1b08c ..
 * FUN_08b1e274) on the sceMpeg imports (src/platform/video.c). See docs/screens.md
 * ("Opening movie"). */
#ifndef GAME_MOVIE_H
#define GAME_MOVIE_H

#include <stdbool.h>

/* Which buttons stop it (FUN_08b1dafc): MOVIE_SKIP_ANY is the opening's (start,
 * select, the d-pad, L, R and the four face buttons), MOVIE_SKIP_START the ending's. */
enum { MOVIE_SKIP_START = 0, MOVIE_SKIP_ANY = 1 };

/* FUN_08b1d0ec: plays disc0:/PSP_GAME/USRDIR/<name> to the end or until a skip button,
 * on the calling (game) thread, which it holds meanwhile. Returns true when a button
 * stopped it. The overlay picture the game can pulse over the video (+0x28, unused by
 * both of its movies) isn't supported. */
bool movie_play(const char *name, int skip);

/* --skip-movie: movie_play returns at once. */
void movie_set_skip_all(bool skip);

#endif
