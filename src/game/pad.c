/* See pad.h. The game's repeat timing comes from small tables indexed by how long a
 * direction has been held (it speeds up); the port uses one fixed delay and rate. */
#include "pad.h"

#include "psp_imports.h"
#include "psp_shim.h"

#define REPEAT_DELAY 20 /* vertical blanks before the first repeat */
#define REPEAT_RATE 6   /* then one repeat every this many */

static uint32_t g_now, g_prev, g_repeat;
static int g_held_frames;

void pad_update(int frames)
{
    SceCtrlData d;
    if (sceCtrlPeekBufferPositive(&d, 1) < 0)
        return;
    g_prev = g_now;
    g_now = d.buttons;
    g_repeat = g_now & ~g_prev;
    if (g_now && g_now == g_prev) {
        int before = g_held_frames;
        g_held_frames += frames;
        if (g_held_frames >= REPEAT_DELAY &&
            (before < REPEAT_DELAY || (g_held_frames - REPEAT_DELAY) / REPEAT_RATE !=
                                          (before - REPEAT_DELAY) / REPEAT_RATE))
            g_repeat = g_now;
    } else {
        g_held_frames = 0;
    }
}

int pad_held(uint32_t mask)
{
    return (g_now & mask) != 0;
}

int pad_pressed(uint32_t mask)
{
    return (g_now & ~g_prev & mask) != 0;
}

int pad_repeat(uint32_t mask)
{
    return (g_repeat & mask) != 0;
}

int pad_trigger(uint32_t mask)
{
    return (g_now & ~g_prev & mask) != 0;
}
