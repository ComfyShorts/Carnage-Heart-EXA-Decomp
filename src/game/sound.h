/* The game's sound effects: CSound / CAplSound (the SE table and the banks) on top of
 * CSoundDriver (the sound thread) and the Sony sound library's sceSas voices.
 * Hand-written from the functions named in sound.c; see docs/screens.md ("Sound").
 *
 * An effect is a row of sound/sound_se_tbl.bin (in the "boot" file set): either a
 * program of one of three banks (sound/se/seset_00sys, _01bat, _02adv: .phd header,
 * .pbd ADPCM samples) or an ATRAC3plus file (sound/se/at3se/NNN.at3). */
#ifndef GAME_SOUND_H
#define GAME_SOUND_H

#include <stdbool.h>
#include <stdint.h>

/* The menu effects (rows of the SE table) the game plays by number. */
enum {
    SE_CURSOR = 9,   /* a menu cursor moved */
    SE_START = 10,   /* Start / a mode chosen on the title */
    SE_CANCEL = 0xB, /* back out (cross) */
    SE_DECIDE = 0xC, /* choose (circle) */
    SE_OK = 0xD,     /* a question answered yes, a purchase */
    SE_ERROR = 0xF,  /* not allowed */
};

/* The banks (CAplSound's three PHD slots). */
enum { SE_BANK_SYS = 0, SE_BANK_BATTLE = 1, SE_BANK_ADV = 2 };

/* CSoundDriver::initLibrary (FUN_08b2d01c) and the start of CSound: the sas core
 * (32 voices, grain 0x200), the output channel and its thread, the SE table
 * (FUN_08b3697c) and the system bank. Needs the "boot" file set open. */
void sound_init(void);
void sound_shutdown(void);
/* For tests: the same without the thread or the output channel; sound_mix then
 * renders one grain (0x200 stereo samples) at a time. */
void sound_init_offline(void);
void sound_mix(int16_t *out);

/* CSound::ResPHDOpen (FUN_08b26998) / its release: a bank in or out. */
bool sound_bank_open(int bank);
void sound_bank_close(int bank);

/* FUN_08b36b6c: SE table row `id` at `volume` (1 = the table's). Returns a handle
 * (> 0) or 0 when nothing plays. */
int sound_se(int id, float volume);
/* FUN_08b36cac's simplified form: the same with a pan (-1 left .. 1 right) and a
 * volume worked out by the caller from the listener (battle effects). */
int sound_se_pan(int id, float volume, float pan);

/* CSound::seCalc3DVolume (FUN_08b37174) for FUN_08b36cac: the listener (the camera's
 * eye and the point it looks at), then an effect at a world position. Full volume
 * within the row's +0x2C units, then less by its +0x28 per unit; the pan is the
 * direction across the view. */
void sound_set_listener(const float eye[3], const float at[3]);
int sound_se_3d(int id, float volume, const float pos[3]);

/* Once a frame: the delayed effects (the table's random delays). */
void sound_update(void);

#endif
