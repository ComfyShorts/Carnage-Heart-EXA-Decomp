/* PSP shim: controller input. The keyboard and any plugged-in gamepads stand in for
 * the PSP's buttons and analog stick (controls.c holds the bindings, read from the
 * user's controls.ini when there is one; docs/controls.md), and a script
 * (psp_ctrl_script_press) can press buttons on given displayed frames, for unattended
 * runs.
 *
 * The main thread's event pump keeps the keyboard and gamepad state; the game thread
 * reads it through sceCtrlPeekBufferPositive. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "controls.h"
#include "psp_imports.h"
#include "psp_shim.h"

static Controls g_controls;
static bool g_controls_ready;
static char g_config_status[256];

/* Each source's CtlState packed into one word: buttons (16 bits), lx, ly. */
static SDL_AtomicInt g_key_state, g_pad_state;

static int pack(CtlState s) { return (int)((s.buttons & 0xFFFFu) | (uint32_t)s.lx << 16 | (uint32_t)s.ly << 24); }

static CtlState unpack(int v)
{
    uint32_t u = (uint32_t)v;
    return (CtlState){u & 0xFFFFu, (uint8_t)(u >> 16), (uint8_t)(u >> 24)};
}

/* ---- the bindings ---- */

static void controls_ready(void)
{
    if (g_controls_ready)
        return;
    g_controls_ready = true;
    controls_defaults(&g_controls);
    SDL_SetAtomicInt(&g_key_state, pack((CtlState){0, 0x80, 0x80}));
    SDL_SetAtomicInt(&g_pad_state, pack((CtlState){0, 0x80, 0x80}));
    const char *path = controls_config_path();
    if (!path) {
        snprintf(g_config_status, sizeof g_config_status, "defaults (no config folder found)");
        return;
    }
    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    if (!text) {
        snprintf(g_config_status, sizeof g_config_status, "defaults (%s not found)", path);
        return;
    }
    char err[200];
    int bad = controls_parse(&g_controls, text, err, sizeof err);
    SDL_free(text);
    if (bad)
        snprintf(g_config_status, sizeof g_config_status, "%s, %d line(s) ignored, first: %s", path, bad, err);
    else
        snprintf(g_config_status, sizeof g_config_status, "%s", path);
}

const Controls *psp_controls(void)
{
    controls_ready();
    return &g_controls;
}

const char *psp_controls_status(void)
{
    controls_ready();
    return g_config_status;
}

void psp_controls_log(void)
{
    controls_ready();
    const Controls *c = &g_controls;
    char k[CTL_COUNT][48];
    for (int i = 0; i < CTL_COUNT; i++) {
        snprintf(k[i], sizeof k[i], "%s", c->keys[i][0] ? SDL_GetScancodeName((SDL_Scancode)c->keys[i][0]) : "-");
    }
    SDL_Log("[input] controls (%s):", g_config_status);
    SDL_Log("[input]   keyboard: D-pad %s/%s/%s/%s, circle %s, cross %s, triangle %s, square %s, L %s, R %s, "
            "START %s, SELECT %s, stick %s/%s/%s/%s; Escape quits",
            k[CTL_UP], k[CTL_DOWN], k[CTL_LEFT], k[CTL_RIGHT], k[CTL_CIRCLE], k[CTL_CROSS], k[CTL_TRIANGLE],
            k[CTL_SQUARE], k[CTL_L], k[CTL_R], k[CTL_START], k[CTL_SELECT], k[CTL_STICK_UP], k[CTL_STICK_DOWN],
            k[CTL_STICK_LEFT], k[CTL_STICK_RIGHT]);
    char p[CTL_COUNT][48];
    for (int i = 0; i < CTL_COUNT; i++) {
        PadSource s = c->pad[i][0];
        const char *n = s.kind == PAD_BUTTON ? SDL_GetGamepadStringForButton((SDL_GamepadButton)s.code)
                        : s.kind != PAD_NONE ? SDL_GetGamepadStringForAxis((SDL_GamepadAxis)s.code)
                                             : NULL;
        snprintf(p[i], sizeof p[i], "%s%s", n ? n : "-",
                 s.kind == PAD_AXIS_NEG ? "-" : s.kind == PAD_AXIS_POS && i >= CTL_STICK_UP ? "+" : "");
    }
    SDL_Log("[input]   gamepad: D-pad %s/%s/%s/%s, circle %s, cross %s, triangle %s, square %s, L %s, R %s, "
            "START %s, SELECT %s, stick %s/%s/%s/%s%s (chexa-pc --controls lists every input)",
            p[CTL_UP], p[CTL_DOWN], p[CTL_LEFT], p[CTL_RIGHT], p[CTL_CIRCLE], p[CTL_CROSS], p[CTL_TRIANGLE],
            p[CTL_SQUARE], p[CTL_L], p[CTL_R], p[CTL_START], p[CTL_SELECT], p[CTL_STICK_UP], p[CTL_STICK_DOWN],
            p[CTL_STICK_LEFT], p[CTL_STICK_RIGHT], c->stick_moves_dpad ? ", the stick also moves the D-pad" : "");
}

/* ---- keyboard ---- */

static bool g_down[CTL_SCANCODES];

static void keys_changed(void)
{
    SDL_SetAtomicInt(&g_key_state, pack(controls_from_keys(&g_controls, g_down)));
}

void psp_ctrl_key_event(int scancode, bool down)
{
    controls_ready();
    if (scancode <= 0 || scancode >= CTL_SCANCODES)
        return;
    g_down[scancode] = down;
    keys_changed();
}

void psp_ctrl_release_all(void)
{
    memset(g_down, 0, sizeof g_down);
    keys_changed();
}

/* ---- gamepads ---- */

#define MAX_PADS 8
static SDL_Gamepad *g_pads[MAX_PADS];

void psp_ctrl_pad_added(uint32_t id)
{
    controls_ready();
    for (int i = 0; i < MAX_PADS; i++)
        if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == id)
            return;
    for (int i = 0; i < MAX_PADS; i++)
        if (!g_pads[i]) {
            g_pads[i] = SDL_OpenGamepad(id);
            if (g_pads[i])
                SDL_Log("[input] gamepad connected: %s", SDL_GetGamepadName(g_pads[i]));
            else
                SDL_Log("[input] can't open gamepad %u: %s", (unsigned)id, SDL_GetError());
            return;
        }
}

void psp_ctrl_pad_removed(uint32_t id)
{
    for (int i = 0; i < MAX_PADS; i++)
        if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == id) {
            SDL_Log("[input] gamepad disconnected: %s", SDL_GetGamepadName(g_pads[i]));
            SDL_CloseGamepad(g_pads[i]);
            g_pads[i] = NULL;
        }
}

int psp_ctrl_pad_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_PADS; i++)
        n += g_pads[i] != NULL;
    return n;
}

/* Once per pump: every connected pad's buttons and stick, merged. */
void psp_ctrl_pads_poll(void)
{
    controls_ready();
    CtlState all = {0, 0x80, 0x80};
    for (int i = 0; i < MAX_PADS; i++) {
        if (!g_pads[i])
            continue;
        bool buttons[CTL_PAD_BUTTONS] = {false};
        int16_t axes[CTL_PAD_AXES] = {0};
        for (int b = 0; b < CTL_PAD_BUTTONS && b < SDL_GAMEPAD_BUTTON_COUNT; b++)
            buttons[b] = SDL_GetGamepadButton(g_pads[i], (SDL_GamepadButton)b);
        for (int a = 0; a < CTL_PAD_AXES && a < SDL_GAMEPAD_AXIS_COUNT; a++)
            axes[a] = SDL_GetGamepadAxis(g_pads[i], (SDL_GamepadAxis)a);
        all = controls_merge(all, controls_from_gamepad(&g_controls, buttons, axes));
    }
    SDL_SetAtomicInt(&g_pad_state, pack(all));
}

void psp_ctrl_pads_close(void)
{
    for (int i = 0; i < MAX_PADS; i++)
        if (g_pads[i]) {
            SDL_CloseGamepad(g_pads[i]);
            g_pads[i] = NULL;
        }
}

/* ---- the help text: which buttons this screen uses ---- */

static SDL_Mutex *g_help_lock;
static char g_help[512];
static uint32_t g_help_frame;
static SDL_AtomicInt g_help_changed;

/* From the game thread (ui_help_draw), every frame a help window is drawn. */
void psp_help_text(const char *text)
{
    if (!g_help_lock)
        return;
    SDL_LockMutex(g_help_lock);
    if (strncmp(g_help, text ? text : "", sizeof g_help - 1)) {
        snprintf(g_help, sizeof g_help, "%s", text ? text : "");
        SDL_SetAtomicInt(&g_help_changed, 1);
    }
    g_help_frame = psp_display_frame_count();
    SDL_UnlockMutex(g_help_lock);
}

void psp_help_init(void)
{
    if (!g_help_lock)
        g_help_lock = SDL_CreateMutex();
}

/* On the main thread: the hint for the help text shown now, or "" (none named, or no
 * help window drawn for 10 frames). True when it changed since the last call. */
bool psp_help_hint(char *buf, size_t size)
{
    static char last[256];
    static bool stale;
    if (!g_help_lock)
        return false;
    char text[512];
    SDL_LockMutex(g_help_lock);
    bool changed = SDL_SetAtomicInt(&g_help_changed, 0) != 0;
    bool old = psp_display_frame_count() > g_help_frame + 10;
    snprintf(text, sizeof text, "%s", old ? "" : g_help);
    SDL_UnlockMutex(g_help_lock);
    if (old != stale) {
        stale = old;
        changed = true;
    }
    if (!changed)
        return false;
    char hint[256];
    controls_help_hint(psp_controls(), controls_help_buttons(text), psp_ctrl_pad_count() > 0, hint, sizeof hint);
    if (!strcmp(hint, last))
        return false;
    snprintf(last, sizeof last, "%s", hint);
    snprintf(buf, size, "%s", hint);
    return true;
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
    controls_ready();
    CtlState s = controls_merge(unpack(SDL_GetAtomicInt(&g_key_state)), unpack(SDL_GetAtomicInt(&g_pad_state)));
    for (int i = 0; i < count; i++) {
        memset(&data[i], 0, sizeof data[i]);
        data[i].timestamp = (uint32_t)(SDL_GetTicksNS() / 1000);
        data[i].buttons = s.buttons | scripted_buttons();
        /* The stick in analog mode (the game sets it at start-up), centred otherwise. */
        data[i].lx = g_sampling_mode == 1 ? s.lx : 0x80;
        data[i].ly = g_sampling_mode == 1 ? s.ly : 0x80;
    }
    PSP_TRACE(count, "%p, %d", (void *)data, count);
    return count;
}
