/* The game's pad state (FUN_08b1ed50 updates it once per main-loop frame from
 * sceCtrlPeekBufferPositive) and the queries screens use through the application's
 * pad object (vtable +0x14 held, +0x24 pressed, +0x2c pressed or repeating, +0x34
 * pressed without repeat). */
#ifndef GAME_PAD_H
#define GAME_PAD_H

#include <stdint.h>

/* frames: vertical blanks since the last update (the main loop's time step). */
void pad_update(int frames);

int pad_held(uint32_t mask);    /* FUN_08b0e974 */
int pad_pressed(uint32_t mask); /* FUN_08b0ea34 */
int pad_repeat(uint32_t mask);  /* FUN_08b0ea94 */
int pad_trigger(uint32_t mask); /* FUN_08b0eaf4 */

#endif
