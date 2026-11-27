/* The game's music: CSoundPlayerAT3 (an ATRAC3plus file on its own audio channel,
 * through sceAtrac3plus), the BGM table (sound/sound_bgm_tbl.bin) and CBgmPlayer, which
 * picks the tracks for the title, the menus and the battles. See docs/screens.md
 * ("Music"). */
#ifndef GAME_MUSIC_H
#define GAME_MUSIC_H

#include <stdbool.h>

/* What is playing (CSound +0x11d9c, FUN_08b35400): the picker that started it. */
enum { MUSIC_NONE = -1, MUSIC_TITLE = 0, MUSIC_MENU = 1, MUSIC_DESIGN = 2, MUSIC_BATTLE = 3 };

/* CSoundDriver::strInit (FUN_08b2d82c) and TblBGMOpen (FUN_08b27638): the decoder IDs,
 * the table, the channel and the player thread. sound_init calls it. */
void music_init(void);
void music_shutdown(void);

/* CSound::BGMPlay (FUN_08b34de8): BGM table row `row` for `kind`, fading out what plays.
 * `loop` 0 plays it once. */
void music_play(int row, int kind, bool loop);
/* FUN_08b34f44: fade out and stop. */
void music_stop(void);
int music_kind(void);

/* CBgmPlayer. FUN_088d3d80 builds the play lists from the save's BGM Player settings
 * when a mode starts (FUN_0888cbd4 / FUN_0888cd8c / FUN_0888cf44). */
void music_lists_build(void);
void music_title(void);  /* FUN_0888c718: row 0x24, "THE DAWN" */
void music_menu(void);   /* FUN_0888d2ac: the next track of the menu list */
void music_battle(void); /* FUN_0888dfb0: the next track of the battle list */

/* For tests: the title of row `row`, its file number (-1 none) and volume (0-100). */
bool music_row(int row, const char **title, int *file, int *volume);

#endif
