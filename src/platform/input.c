/* PSP shim: controller input. The keyboard stands in for the PSP's buttons, and a
 * script (psp_ctrl_script_press) can press buttons on given displayed frames, for
 * unattended runs:
 *
 *   arrows    D-pad          Z / Enter      circle      X / Backspace  cross
 *   A         square         S              triangle    Q / W          L / R
 *   Space     start          Right Shift    select
 */
#include <string.h>

#include <SDL3/SDL.h>

#include "psp_imports.h"
#include "psp_shim.h"

/* ---- keyboard state, written by the main thread's event pump ---- */

static SDL_AtomicInt g_keys; /* PSP button bits held on the keyboard */

static uint32_t button_for_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: return PSP_CTRL_UP;
    case SDLK_DOWN: return PSP_CTRL_DOWN;
    case SDLK_LEFT: return PSP_CTRL_LEFT;
    case SDLK_RIGHT: return PSP_CTRL_RIGHT;
    case SDLK_Z: case SDLK_RETURN: return PSP_CTRL_CIRCLE;
    case SDLK_X: case SDLK_BACKSPACE: return PSP_CTRL_CROSS;
    case SDLK_A: return PSP_CTRL_SQUARE;
    case SDLK_S: return PSP_CTRL_TRIANGLE;
    case SDLK_Q: return PSP_CTRL_LTRIGGER;
    case SDLK_W: return PSP_CTRL_RTRIGGER;
    case SDLK_SPACE: return PSP_CTRL_START;
    case SDLK_RSHIFT: return PSP_CTRL_SELECT;
    default: return 0;
    }
}

void psp_ctrl_key_event(int keycode, bool down)
{
    uint32_t b = button_for_key((SDL_Keycode)keycode);
    if (!b)
        return;
    int old, n;
    do {
        old = SDL_GetAtomicInt(&g_keys);
        n = down ? (int)((uint32_t)old | b) : (int)((uint32_t)old & ~b);
    } while (!SDL_CompareAndSwapAtomicInt(&g_keys, old, n));
}

/* ---- scripted presses ---- */

#define MAX_SCRIPT 256 /* --press entries (typing a name on the keyboard takes many) */
static struct { uint32_t frame, buttons; } g_script[MAX_SCRIPT];
static int g_script_count;

/* Hold `buttons` for a few frames starting with displayed frame `frame`. */
void psp_ctrl_script_press(uint32_t frame, uint32_t buttons)
{
    if (g_script_count < MAX_SCRIPT)
        g_script[g_script_count++] = (typeof(g_script[0])){frame, buttons};
}

static uint32_t scripted_buttons(void)
{
    uint32_t now = psp_display_frame_count(), b = 0;
    for (int i = 0; i < g_script_count; i++)
        if (now >= g_script[i].frame && now < g_script[i].frame + 4)
            b |= g_script[i].buttons;
    return b;
}

/* ---- sceCtrl (2 imports) ---- */

static int g_sampling_mode; /* 0 digital, 1 analog */

int32_t sceCtrlSetSamplingMode(int mode)
{
    int32_t r = mode == 0 || mode == 1 ? g_sampling_mode : (int32_t)0x80000107;
    if (mode == 0 || mode == 1)
        g_sampling_mode = mode;
    PSP_TRACE(r, "%d", mode);
    return r;
}

int32_t sceCtrlPeekBufferPositive(SceCtrlData *data, int count)
{
    if (!data || count < 1)
        return (int32_t)0x80000104;
    for (int i = 0; i < count; i++) {
        memset(&data[i], 0, sizeof data[i]);
        data[i].timestamp = (uint32_t)(SDL_GetTicksNS() / 1000);
        data[i].buttons = (uint32_t)SDL_GetAtomicInt(&g_keys) | scripted_buttons();
        data[i].lx = data[i].ly = 0x80; /* analog stick centred */
    }
    PSP_TRACE(count, "%p, %d", (void *)data, count);
    return count;
}
