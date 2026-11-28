/* The scenes' voices (op 0xE of the scene player): ATRAC3plus files in voice.dat
 * ("voice/<who>/<id>.at3", mono, 44.1 kHz), played on their own audio channel as the
 * music is (music.c), through the same sceAtrac decoder. A new voice or the end of a
 * message (op 0xF, the page turned) stops the one playing. */
#ifndef GAME_VOICE_H
#define GAME_VOICE_H

#include <stdbool.h>

void voice_init(void);
void voice_shutdown(void);

/* Reads `path` from the pack and plays it once, stopping what plays. False if the
 * file can't be read or decoded. */
bool voice_play(const char *path);
void voice_stop(void);
bool voice_playing(void);

#endif
