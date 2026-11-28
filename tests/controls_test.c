/* Checks the PC port's controls (src/platform/controls.c):
 *   - by default every PSP button has a key and a gamepad input of its own, each giving
 *     exactly that button;
 *   - the stick from keys and from the gamepad's left stick (0x00 / 0x80 / 0xFF), the
 *     stick as D-pad past the deadzone and not under it, triggers as L / R;
 *   - a config file rebinds, unbinds, sets the options and reports what it can't use
 *     without applying it; the written config reads back as the bindings it came from;
 *   - the buttons a help text names, in the game's own words (Shift-JIS symbols);
 *   - a gamepad plugged in and out (SDL's virtual gamepad) through the event pump to
 *     sceCtrlPeekBufferPositive: its east button is circle, its stick the PSP's stick.
 *
 *   chexa-controls-test [-v]
 *
 * Needs no game files. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "controls.h"
#include "psp_imports.h"
#include "psp_shim.h"

static int failures;
static int verbose;

#define CHECK(cond, ...)                      \
    do {                                      \
        if (!(cond)) {                        \
            failures++;                       \
            printf("FAIL  ");                 \
            printf(__VA_ARGS__);              \
            printf("\n");                     \
        } else if (verbose) {                 \
            printf("ok    ");                 \
            printf(__VA_ARGS__);              \
            printf("\n");                     \
        }                                     \
    } while (0)

static bool g_keys[CTL_SCANCODES];
static bool g_buttons[CTL_PAD_BUTTONS];
static int16_t g_axes[CTL_PAD_AXES];

static void clear(void)
{
    memset(g_keys, 0, sizeof g_keys);
    memset(g_buttons, 0, sizeof g_buttons);
    memset(g_axes, 0, sizeof g_axes);
}

/* Pushes control `id`'s first gamepad input fully. */
static void push_pad(const Controls *c, int id, int amount)
{
    PadSource p = c->pad[id][0];
    if (p.kind == PAD_BUTTON)
        g_buttons[p.code] = true;
    else if (p.kind == PAD_AXIS_POS)
        g_axes[p.code] = (int16_t)amount;
    else if (p.kind == PAD_AXIS_NEG)
        g_axes[p.code] = (int16_t)-amount;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-v"))
            verbose = 1;
    Controls c;
    controls_defaults(&c);

    /* Every PSP button: its own bit, a key and a gamepad input giving just that bit. */
    static const uint32_t k_all = PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT | PSP_CTRL_CIRCLE |
                                  PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE | PSP_CTRL_SQUARE | PSP_CTRL_LTRIGGER |
                                  PSP_CTRL_RTRIGGER | PSP_CTRL_START | PSP_CTRL_SELECT;
    uint32_t seen = 0;
    for (int id = 0; id < CTL_COUNT; id++) {
        uint32_t bit = controls_psp_bit((ControlId)id);
        if (!bit)
            continue;
        CHECK(!(seen & bit), "%s has a bit of its own", controls_name((ControlId)id));
        seen |= bit;
        clear();
        g_keys[c.keys[id][0]] = true;
        CtlState k = controls_from_keys(&c, g_keys);
        CHECK(c.keys[id][0] && k.buttons == bit && k.lx == 0x80 && k.ly == 0x80, "%s: key %s gives 0x%X (0x%X)",
              controls_name((ControlId)id), SDL_GetScancodeName((SDL_Scancode)c.keys[id][0]), bit, k.buttons);
        clear();
        push_pad(&c, id, 32767);
        CtlState p = controls_from_gamepad(&c, g_buttons, g_axes);
        CHECK(c.pad[id][0].kind != PAD_NONE && p.buttons == bit, "%s: its gamepad input gives 0x%X (0x%X)",
              controls_name((ControlId)id), bit, p.buttons);
    }
    CHECK(seen == k_all, "all 12 PSP buttons are bound (0x%X)", seen);

    /* The defaults: the keys of before, positions on the pad as on a PlayStation pad. */
    CHECK(c.keys[CTL_CIRCLE][0] == SDL_SCANCODE_Z && c.keys[CTL_CIRCLE][1] == SDL_SCANCODE_RETURN &&
              c.keys[CTL_CROSS][0] == SDL_SCANCODE_X && c.keys[CTL_START][0] == SDL_SCANCODE_SPACE,
          "default keys: Z / Return circle, X cross, Space START");
    CHECK(c.pad[CTL_CIRCLE][0].code == SDL_GAMEPAD_BUTTON_EAST && c.pad[CTL_CROSS][0].code == SDL_GAMEPAD_BUTTON_SOUTH &&
              c.pad[CTL_TRIANGLE][0].code == SDL_GAMEPAD_BUTTON_NORTH &&
              c.pad[CTL_SQUARE][0].code == SDL_GAMEPAD_BUTTON_WEST,
          "default pad: east circle, south cross, north triangle, west square");

    /* The stick from keys: full tilt, and the D-pad with it. */
    clear();
    g_keys[SDL_SCANCODE_J] = true;
    g_keys[SDL_SCANCODE_K] = true;
    CtlState s = controls_from_keys(&c, g_keys);
    CHECK(s.lx == 0x00 && s.ly == 0xFF && s.buttons == (PSP_CTRL_LEFT | PSP_CTRL_DOWN),
          "keys J + K: stick left-down (%02X %02X), D-pad left + down (0x%X)", s.lx, s.ly, s.buttons);

    /* The gamepad's left stick. */
    clear();
    g_axes[SDL_GAMEPAD_AXIS_LEFTX] = 32767;
    g_axes[SDL_GAMEPAD_AXIS_LEFTY] = -32768;
    s = controls_from_gamepad(&c, g_buttons, g_axes);
    CHECK(s.lx == 0xFF && s.ly == 0x00 && s.buttons == (PSP_CTRL_RIGHT | PSP_CTRL_UP),
          "left stick right-up: %02X %02X, D-pad right + up (0x%X)", s.lx, s.ly, s.buttons);
    clear();
    g_axes[SDL_GAMEPAD_AXIS_LEFTX] = 8000; /* under the 50% deadzone */
    s = controls_from_gamepad(&c, g_buttons, g_axes);
    CHECK(s.lx > 0x80 && s.lx < 0xC0 && s.buttons == 0, "a small tilt moves the stick (%02X), not the D-pad", s.lx);
    clear();
    s = controls_from_gamepad(&c, g_buttons, g_axes);
    CHECK(s.lx == 0x80 && s.ly == 0x80 && s.buttons == 0, "nothing pushed: centred, no button");
    Controls no_dpad = c;
    no_dpad.stick_moves_dpad = false;
    g_axes[SDL_GAMEPAD_AXIS_LEFTX] = -32768;
    s = controls_from_gamepad(&no_dpad, g_buttons, g_axes);
    CHECK(s.lx == 0x00 && s.buttons == 0, "stick_moves_dpad = no: the stick alone (0x%X)", s.buttons);

    /* Triggers as L / R past the deadzone. */
    clear();
    g_axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 30000;
    g_axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 5000;
    s = controls_from_gamepad(&c, g_buttons, g_axes);
    CHECK(s.buttons == PSP_CTRL_LTRIGGER, "left trigger pulled: L; right touched: nothing (0x%X)", s.buttons);

    /* Merging: buttons of both, the moved stick. */
    CtlState a = {PSP_CTRL_CIRCLE, 0x80, 0x80}, b = {PSP_CTRL_UP, 0x10, 0x80};
    CtlState m = controls_merge(a, b);
    CHECK(m.buttons == (PSP_CTRL_CIRCLE | PSP_CTRL_UP) && m.lx == 0x10, "merge: both buttons, the moved stick");
    m = controls_merge(b, a);
    CHECK(m.lx == 0x10, "merge: a centred stick doesn't hide a moved one");

    /* The config file. */
    Controls r = c;
    char err[200];
    int bad = controls_parse(&r,
                             "# comment\n"
                             "[keyboard]\n"
                             "circle = Keypad 6, Return     # a comment after it\n"
                             "select = none\n"
                             "[gamepad]\n"
                             "cross = south, rightstick\n"
                             "l = lefttrigger\n"
                             "stick_left = rightx-\n"
                             "[options]\n"
                             "stick_moves_dpad = no\n"
                             "deadzone = 30\n",
                             err, sizeof err);
    CHECK(bad == 0, "a good config reads without complaint (%s)", err);
    CHECK(r.keys[CTL_CIRCLE][0] == SDL_SCANCODE_KP_6 && r.keys[CTL_CIRCLE][1] == SDL_SCANCODE_RETURN &&
              r.keys[CTL_CIRCLE][2] == 0,
          "circle rebound to Keypad 6, Return");
    clear();
    g_keys[SDL_SCANCODE_Z] = true;
    CHECK(controls_from_keys(&r, g_keys).buttons == 0, "Z isn't circle any more");
    CHECK(r.keys[CTL_SELECT][0] == 0, "select = none unbinds it");
    CHECK(r.pad[CTL_CROSS][1].kind == PAD_BUTTON && r.pad[CTL_CROSS][1].code == SDL_GAMEPAD_BUTTON_RIGHT_STICK,
          "cross on south and rightstick");
    CHECK(r.pad[CTL_L][0].kind == PAD_AXIS_POS && r.pad[CTL_L][0].code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER &&
              r.pad[CTL_L][1].kind == PAD_NONE,
          "L on the left trigger only");
    CHECK(r.pad[CTL_STICK_LEFT][0].kind == PAD_AXIS_NEG && r.pad[CTL_STICK_LEFT][0].code == SDL_GAMEPAD_AXIS_RIGHTX,
          "stick_left on rightx-");
    CHECK(!r.stick_moves_dpad && r.deadzone == 30, "options read");
    CHECK(r.keys[CTL_CROSS][0] == SDL_SCANCODE_X, "controls not named keep their defaults");

    Controls e = c;
    bad = controls_parse(&e,
                         "[keyboard]\n"
                         "circle = Nosuchkey\n"
                         "flap = Z\n"
                         "[gamepad]\n"
                         "square = q, x\n"
                         "[options]\n"
                         "deadzone = 150\n"
                         "[sound]\n",
                         err, sizeof err);
    CHECK(bad == 5 && strstr(err, "line 2") && strstr(err, "Nosuchkey"), "bad lines counted, the first named: %s",
          err);
    CHECK(!memcmp(&e, &c, sizeof c), "a bad line changes nothing");

    static char text[8192];
    controls_write(&r, text, sizeof text);
    Controls back;
    controls_defaults(&back);
    bad = controls_parse(&back, text, err, sizeof err);
    CHECK(bad == 0 && !memcmp(&back, &r, sizeof r), "the written config reads back as the same bindings (%s)", err);
    controls_write(&c, text, sizeof text);
    controls_defaults(&back);
    back.deadzone = 1;
    controls_parse(&back, text, err, sizeof err);
    CHECK(!memcmp(&back, &c, sizeof c), "the defaults written read back as the defaults");

    /* The help text, in the game's words: Shift-JIS symbols, lines broken anywhere. */
    static const struct {
        const char *text;
        uint32_t mask;
    } k_help[] = {
        {"Press the \x81\x9b button to cancel registration.", 1u << CTL_CIRCLE},
        {"Draw with a pencil. Keep the \x81\x7e button held down to", 1u << CTL_CROSS},
        {"Change the OKE using the L and R buttons.", (1u << CTL_L) | (1u << CTL_R)},
        {"Select a range. Use the directional\nbuttons to change",
         (1u << CTL_UP) | (1u << CTL_DOWN) | (1u << CTL_LEFT) | (1u << CTL_RIGHT)},
        {"Press the SELECT button to stop converting the", 1u << CTL_SELECT},
        {"Use the up and down buttons to select a color.", (1u << CTL_UP) | (1u << CTL_DOWN)},
        {"Use the left and right buttons to pick an emblem.", (1u << CTL_LEFT) | (1u << CTL_RIGHT)},
        {"Release the button to where you want to drag the chip to.", 0},
        {"Select a Team Card.", 0},
    };
    for (size_t i = 0; i < SDL_arraysize(k_help); i++) {
        uint32_t got = controls_help_buttons(k_help[i].text);
        CHECK(got == k_help[i].mask, "help \"%.40s\": 0x%X (0x%X)", k_help[i].text, k_help[i].mask, got);
    }
    char hint[256];
    controls_help_hint(&c, controls_help_buttons("Press the \x81\x9b button"), false, hint, sizeof hint);
    CHECK(!strcmp(hint, "\xe2\x97\x8b Z, Return"), "hint for the circle button: \"%s\"", hint);
    controls_help_hint(&c, controls_help_buttons("Use the directional buttons."), true, hint, sizeof hint);
    CHECK(!strcmp(hint, "D-pad Up/Down/Left/Right | pad D-pad or stick"), "hint for the D-pad: \"%s\"", hint);
    controls_help_hint(&c, 0, true, hint, sizeof hint);
    CHECK(!hint[0], "no buttons named: no hint");

    /* The config file's place. */
    SDL_setenv_unsafe("CHEXA_CONTROLS", "/tmp/x/controls.ini", 1);
    CHECK(!strcmp(controls_config_path(), "/tmp/x/controls.ini"), "CHEXA_CONTROLS names the file");
    SDL_unsetenv_unsafe("CHEXA_CONTROLS");
    const char *path = controls_config_path();
    CHECK(path && strstr(path, "chexa-pc") && strstr(path, "controls.ini"), "otherwise the user's config folder: %s",
          path ? path : "none");

    /* Hotplug, end to end: SDL's virtual gamepad through the platform's event pump. */
    SDL_setenv_unsafe("CHEXA_CONTROLS", "/nonexistent/controls.ini", 1); /* the defaults */
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        printf("skip  hotplug: no gamepad subsystem (%s)\n", SDL_GetError());
    } else {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "chexa test pad";
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        SDL_Joystick *joy = id ? SDL_OpenJoystick(id) : NULL;
        CHECK(joy != NULL, "a virtual gamepad attached (%s)", joy ? "" : SDL_GetError());
        if (joy) {
            psp_platform_pump(); /* SDL_EVENT_GAMEPAD_ADDED */
            CHECK(psp_ctrl_pad_count() == 1, "the pump opened it (%d)", psp_ctrl_pad_count());
            sceCtrlSetSamplingMode(1);
            SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_EAST, true);
            SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTX, -32768);
            SDL_UpdateJoysticks();
            psp_platform_pump();
            SceCtrlData d;
            sceCtrlPeekBufferPositive(&d, 1);
            CHECK((d.buttons & PSP_CTRL_CIRCLE) && (d.buttons & PSP_CTRL_LEFT) && d.lx == 0x00 && d.ly == 0x80,
                  "east button: circle; stick left: lx 00 and D-pad left (0x%X, %02X %02X)", d.buttons, d.lx, d.ly);
            SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_EAST, false);
            SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTX, 0);
            SDL_UpdateJoysticks();
            psp_platform_pump();
            sceCtrlPeekBufferPositive(&d, 1);
            CHECK(d.buttons == 0 && d.lx == 0x80, "released: nothing (0x%X, %02X)", d.buttons, d.lx);
            SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_START, true);
            SDL_UpdateJoysticks();
            SDL_CloseJoystick(joy);
            SDL_DetachVirtualJoystick(id);
            psp_platform_pump(); /* SDL_EVENT_GAMEPAD_REMOVED */
            sceCtrlPeekBufferPositive(&d, 1);
            CHECK(psp_ctrl_pad_count() == 0 && d.buttons == 0, "unplugged: closed, nothing held (0x%X)", d.buttons);
        }
        SDL_Quit();
    }

    printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
