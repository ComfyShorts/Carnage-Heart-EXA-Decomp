/* The PC port's controls: which keyboard keys and gamepad inputs stand for the PSP's
 * buttons and analog stick. Pure functions over a Controls table, so they can be
 * tested without SDL's event loop; input.c feeds them the live state.
 *
 * Keys are SDL scancodes (where a key is, not what it types), named as SDL names them
 * ("Z", "Return", "Right Shift"). Gamepad inputs use SDL's gamepad names: buttons
 * ("a", "b", "dpup", "leftshoulder", ...; "south", "east", "west", "north" also work)
 * and axes with a direction ("leftx-", "lefty+", "lefttrigger").
 *
 * The config file (controls_config_path) is plain text:
 *
 *   [keyboard]
 *   circle = Z, Return
 *   [gamepad]
 *   circle = b
 *   [options]
 *   stick_moves_dpad = yes
 *   deadzone = 50
 *
 * A control named in the file replaces its default list; "none" unbinds it; " #"
 * starts a comment, on its own line or after a value. See
 * docs/controls.md. */
#ifndef PLATFORM_CONTROLS_H
#define PLATFORM_CONTROLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CTL_UP, CTL_DOWN, CTL_LEFT, CTL_RIGHT,
    CTL_CIRCLE, CTL_CROSS, CTL_TRIANGLE, CTL_SQUARE,
    CTL_L, CTL_R, CTL_START, CTL_SELECT,
    CTL_STICK_UP, CTL_STICK_DOWN, CTL_STICK_LEFT, CTL_STICK_RIGHT,
    CTL_COUNT
} ControlId;

#define CTL_MAX_BINDS 4
#define CTL_SCANCODES 512 /* SDL_SCANCODE_COUNT */
#define CTL_PAD_BUTTONS 32
#define CTL_PAD_AXES 8

enum { PAD_NONE, PAD_BUTTON, PAD_AXIS_POS, PAD_AXIS_NEG };

typedef struct PadSource {
    int kind; /* PAD_* */
    int code; /* SDL_GamepadButton or SDL_GamepadAxis */
} PadSource;

typedef struct Controls {
    int keys[CTL_COUNT][CTL_MAX_BINDS]; /* SDL_Scancode; 0 = none */
    PadSource pad[CTL_COUNT][CTL_MAX_BINDS];
    bool stick_moves_dpad; /* the stick also presses the D-pad (the PSP's doesn't) */
    int deadzone;          /* percent of full tilt, for the stick as D-pad and axes as buttons */
} Controls;

/* The PSP's pad state as sceCtrlPeekBufferPositive reports it. */
typedef struct CtlState {
    uint32_t buttons; /* PSP_CTRL_* bits */
    uint8_t lx, ly;   /* 0x80 centred, 0 left / up, 0xFF right / down */
} CtlState;

const char *controls_name(ControlId id);   /* "circle", "stick_up", ... */
const char *controls_symbol(ControlId id); /* "○", "L", "D-pad up", ... (UTF-8) */
uint32_t controls_psp_bit(ControlId id);   /* 0 for the stick's directions */

void controls_defaults(Controls *c);

/* Applies a config file's text over `c`. Returns the number of lines it couldn't use;
 * the first one is described in `err`. */
int controls_parse(Controls *c, const char *text, char *err, size_t err_size);
/* The config file for `c`, with comments. Returns the length (as snprintf). */
size_t controls_write(const Controls *c, char *buf, size_t size);

/* `down` indexed by scancode, CTL_SCANCODES entries. */
CtlState controls_from_keys(const Controls *c, const bool *down);
/* `buttons` indexed by SDL_GamepadButton, `axes` by SDL_GamepadAxis (-32768..32767). */
CtlState controls_from_gamepad(const Controls *c, const bool buttons[CTL_PAD_BUTTONS],
                               const int16_t axes[CTL_PAD_AXES]);
/* Buttons of both; the stick of `b` when it's off centre, else of `a`. */
CtlState controls_merge(CtlState a, CtlState b);

/* Every control with its keys and gamepad inputs, a line each. */
size_t controls_describe(const Controls *c, char *buf, size_t size);

/* The PSP buttons a screen's help text names (the game's own words: "○ button",
 * "directional buttons", "L and R buttons", "START button", ...), as a ControlId
 * bit mask (1u << id). The text may be Shift-JIS, as the game's is, or UTF-8. */
uint32_t controls_help_buttons(const char *help);
/* Those buttons with their keys (and the gamepad's, if `pad`): "○ Z  × X  D-pad
 * arrows". Empty when the text names none. */
size_t controls_help_hint(const Controls *c, uint32_t mask, bool pad, char *buf, size_t size);

/* CHEXA_CONTROLS, else the user's config folder: %APPDATA%\chexa-pc\controls.ini,
 * $XDG_CONFIG_HOME/chexa-pc/controls.ini or ~/.config/chexa-pc/controls.ini (macOS:
 * ~/Library/Application Support/chexa-pc/controls.ini). NULL if none can be found. */
const char *controls_config_path(void);

#endif
