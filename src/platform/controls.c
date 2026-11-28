/* See controls.h and docs/controls.md. */
#include "controls.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "psp_shim.h"

static const struct {
    const char *name, *symbol;
    uint32_t bit;
} k_controls[CTL_COUNT] = {
    [CTL_UP] = {"up", "D-pad up", PSP_CTRL_UP},
    [CTL_DOWN] = {"down", "D-pad down", PSP_CTRL_DOWN},
    [CTL_LEFT] = {"left", "D-pad left", PSP_CTRL_LEFT},
    [CTL_RIGHT] = {"right", "D-pad right", PSP_CTRL_RIGHT},
    [CTL_CIRCLE] = {"circle", "\xe2\x97\x8b", PSP_CTRL_CIRCLE},
    [CTL_CROSS] = {"cross", "\xc3\x97", PSP_CTRL_CROSS},
    [CTL_TRIANGLE] = {"triangle", "\xe2\x96\xb3", PSP_CTRL_TRIANGLE},
    [CTL_SQUARE] = {"square", "\xe2\x96\xa1", PSP_CTRL_SQUARE},
    [CTL_L] = {"l", "L", PSP_CTRL_LTRIGGER},
    [CTL_R] = {"r", "R", PSP_CTRL_RTRIGGER},
    [CTL_START] = {"start", "START", PSP_CTRL_START},
    [CTL_SELECT] = {"select", "SELECT", PSP_CTRL_SELECT},
    [CTL_STICK_UP] = {"stick_up", "stick up", 0},
    [CTL_STICK_DOWN] = {"stick_down", "stick down", 0},
    [CTL_STICK_LEFT] = {"stick_left", "stick left", 0},
    [CTL_STICK_RIGHT] = {"stick_right", "stick right", 0},
};

const char *controls_name(ControlId id) { return id >= 0 && id < CTL_COUNT ? k_controls[id].name : "?"; }
const char *controls_symbol(ControlId id) { return id >= 0 && id < CTL_COUNT ? k_controls[id].symbol : "?"; }
uint32_t controls_psp_bit(ControlId id) { return id >= 0 && id < CTL_COUNT ? k_controls[id].bit : 0; }

/* ---- defaults ---- */

static void set_keys(Controls *c, ControlId id, int a, int b)
{
    c->keys[id][0] = a;
    c->keys[id][1] = b;
}

static void set_pad(Controls *c, ControlId id, int kind, int code, int kind2, int code2)
{
    c->pad[id][0] = (PadSource){kind, code};
    c->pad[id][1] = (PadSource){kind2, code2};
}

void controls_defaults(Controls *c)
{
    memset(c, 0, sizeof *c);
    set_keys(c, CTL_UP, SDL_SCANCODE_UP, 0);
    set_keys(c, CTL_DOWN, SDL_SCANCODE_DOWN, 0);
    set_keys(c, CTL_LEFT, SDL_SCANCODE_LEFT, 0);
    set_keys(c, CTL_RIGHT, SDL_SCANCODE_RIGHT, 0);
    set_keys(c, CTL_CIRCLE, SDL_SCANCODE_Z, SDL_SCANCODE_RETURN);
    set_keys(c, CTL_CROSS, SDL_SCANCODE_X, SDL_SCANCODE_BACKSPACE);
    set_keys(c, CTL_TRIANGLE, SDL_SCANCODE_S, 0);
    set_keys(c, CTL_SQUARE, SDL_SCANCODE_A, 0);
    set_keys(c, CTL_L, SDL_SCANCODE_Q, 0);
    set_keys(c, CTL_R, SDL_SCANCODE_W, 0);
    set_keys(c, CTL_START, SDL_SCANCODE_SPACE, 0);
    set_keys(c, CTL_SELECT, SDL_SCANCODE_RSHIFT, 0);
    set_keys(c, CTL_STICK_UP, SDL_SCANCODE_I, 0);
    set_keys(c, CTL_STICK_DOWN, SDL_SCANCODE_K, 0);
    set_keys(c, CTL_STICK_LEFT, SDL_SCANCODE_J, 0);
    set_keys(c, CTL_STICK_RIGHT, SDL_SCANCODE_L, 0);

    /* Buttons by position, as on a PlayStation pad: east is Circle, south Cross. */
    set_pad(c, CTL_UP, PAD_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_NONE, 0);
    set_pad(c, CTL_DOWN, PAD_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_NONE, 0);
    set_pad(c, CTL_LEFT, PAD_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_NONE, 0);
    set_pad(c, CTL_RIGHT, PAD_BUTTON, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_NONE, 0);
    set_pad(c, CTL_CIRCLE, PAD_BUTTON, SDL_GAMEPAD_BUTTON_EAST, PAD_NONE, 0);
    set_pad(c, CTL_CROSS, PAD_BUTTON, SDL_GAMEPAD_BUTTON_SOUTH, PAD_NONE, 0);
    set_pad(c, CTL_TRIANGLE, PAD_BUTTON, SDL_GAMEPAD_BUTTON_NORTH, PAD_NONE, 0);
    set_pad(c, CTL_SQUARE, PAD_BUTTON, SDL_GAMEPAD_BUTTON_WEST, PAD_NONE, 0);
    set_pad(c, CTL_L, PAD_BUTTON, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PAD_AXIS_POS, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    set_pad(c, CTL_R, PAD_BUTTON, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_AXIS_POS, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    set_pad(c, CTL_START, PAD_BUTTON, SDL_GAMEPAD_BUTTON_START, PAD_NONE, 0);
    set_pad(c, CTL_SELECT, PAD_BUTTON, SDL_GAMEPAD_BUTTON_BACK, PAD_NONE, 0);
    set_pad(c, CTL_STICK_UP, PAD_AXIS_NEG, SDL_GAMEPAD_AXIS_LEFTY, PAD_NONE, 0);
    set_pad(c, CTL_STICK_DOWN, PAD_AXIS_POS, SDL_GAMEPAD_AXIS_LEFTY, PAD_NONE, 0);
    set_pad(c, CTL_STICK_LEFT, PAD_AXIS_NEG, SDL_GAMEPAD_AXIS_LEFTX, PAD_NONE, 0);
    set_pad(c, CTL_STICK_RIGHT, PAD_AXIS_POS, SDL_GAMEPAD_AXIS_LEFTX, PAD_NONE, 0);
    c->stick_moves_dpad = true;
    c->deadzone = 50; /* the game's own: a stick under 0x40 of 0x80 counts as centred */
}

/* ---- names ---- */

static const struct {
    const char *alias;
    int button;
} k_pad_aliases[] = {
    {"south", SDL_GAMEPAD_BUTTON_SOUTH}, {"east", SDL_GAMEPAD_BUTTON_EAST},
    {"west", SDL_GAMEPAD_BUTTON_WEST},   {"north", SDL_GAMEPAD_BUTTON_NORTH},
    {"select", SDL_GAMEPAD_BUTTON_BACK}, {"l1", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"r1", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
};

static bool parse_pad(const char *s, PadSource *out)
{
    for (size_t i = 0; i < SDL_arraysize(k_pad_aliases); i++)
        if (!SDL_strcasecmp(s, k_pad_aliases[i].alias)) {
            *out = (PadSource){PAD_BUTTON, k_pad_aliases[i].button};
            return true;
        }
    SDL_GamepadButton b = SDL_GetGamepadButtonFromString(s);
    if (b != SDL_GAMEPAD_BUTTON_INVALID && (int)b < CTL_PAD_BUTTONS) {
        *out = (PadSource){PAD_BUTTON, (int)b};
        return true;
    }
    char axis[32];
    size_t n = strlen(s);
    int kind = PAD_AXIS_POS;
    if (n == 0 || n >= sizeof axis)
        return false;
    memcpy(axis, s, n + 1);
    if (axis[n - 1] == '+' || axis[n - 1] == '-') {
        kind = axis[n - 1] == '-' ? PAD_AXIS_NEG : PAD_AXIS_POS;
        axis[n - 1] = 0;
    }
    SDL_GamepadAxis a = SDL_GetGamepadAxisFromString(axis);
    if (a == SDL_GAMEPAD_AXIS_INVALID || (int)a >= CTL_PAD_AXES)
        return false;
    *out = (PadSource){kind, (int)a};
    return true;
}

static const char *face_position(int button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return " (south)";
    case SDL_GAMEPAD_BUTTON_EAST: return " (east)";
    case SDL_GAMEPAD_BUTTON_WEST: return " (west)";
    case SDL_GAMEPAD_BUTTON_NORTH: return " (north)";
    default: return "";
    }
}

/* `positions` adds "(east)" and so on after the face buttons, for people. */
static void pad_name(PadSource p, bool positions, char *buf, size_t size)
{
    const char *n = NULL;
    if (p.kind == PAD_BUTTON)
        n = SDL_GetGamepadStringForButton((SDL_GamepadButton)p.code);
    else if (p.kind == PAD_AXIS_POS || p.kind == PAD_AXIS_NEG)
        n = SDL_GetGamepadStringForAxis((SDL_GamepadAxis)p.code);
    if (!n) {
        snprintf(buf, size, "?");
        return;
    }
    bool trigger = p.kind == PAD_AXIS_POS &&
                   (p.code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || p.code == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    snprintf(buf, size, "%s%s%s", n, p.kind == PAD_AXIS_NEG ? "-" : p.kind == PAD_AXIS_POS && !trigger ? "+" : "",
             p.kind == PAD_BUTTON && positions ? face_position(p.code) : "");
}

static int find_control(const char *name)
{
    for (int i = 0; i < CTL_COUNT; i++)
        if (!SDL_strcasecmp(name, k_controls[i].name))
            return i;
    return -1;
}

/* ---- the config file ---- */

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

static bool parse_bool(const char *s, bool *out)
{
    if (!SDL_strcasecmp(s, "yes") || !SDL_strcasecmp(s, "true") || !strcmp(s, "1") || !SDL_strcasecmp(s, "on"))
        *out = true;
    else if (!SDL_strcasecmp(s, "no") || !SDL_strcasecmp(s, "false") || !strcmp(s, "0") || !SDL_strcasecmp(s, "off"))
        *out = false;
    else
        return false;
    return true;
}

int controls_parse(Controls *c, const char *text, char *err, size_t err_size)
{
    enum { SEC_NONE, SEC_KEYS, SEC_PAD, SEC_OPTIONS } sec = SEC_NONE;
    int errors = 0, line_no = 0;
    if (err && err_size)
        err[0] = 0;
#define BAD(...)                                                                 \
    do {                                                                         \
        if (!errors++ && err && err_size) {                                      \
            int k_ = snprintf(err, err_size, "line %d: ", line_no);              \
            if (k_ >= 0 && (size_t)k_ < err_size)                                \
                snprintf(err + k_, err_size - (size_t)k_, __VA_ARGS__);          \
        }                                                                        \
    } while (0)
    const char *p = text;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[512];
        line_no++;
        if (len >= sizeof line) {
            BAD("too long");
            p = nl ? nl + 1 : NULL;
            continue;
        }
        memcpy(line, p, len);
        line[len] = 0;
        p = nl ? nl + 1 : NULL;
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';')
            continue;
        if (*s == '[') {
            char *e = strchr(s, ']');
            if (!e) {
                BAD("unfinished section name");
                continue;
            }
            *e = 0;
            s = trim(s + 1);
            sec = !SDL_strcasecmp(s, "keyboard") ? SEC_KEYS
                  : !SDL_strcasecmp(s, "gamepad") ? SEC_PAD
                  : !SDL_strcasecmp(s, "options") ? SEC_OPTIONS
                                                  : SEC_NONE;
            if (sec == SEC_NONE)
                BAD("unknown section [%s]", s);
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) {
            BAD("no '='");
            continue;
        }
        *eq = 0;
        char *hash = strstr(eq + 1, " #"); /* a comment after the value */
        if (hash)
            *hash = 0;
        char *key = trim(s), *value = trim(eq + 1);
        if (sec == SEC_OPTIONS) {
            bool b;
            char *end;
            if (!SDL_strcasecmp(key, "stick_moves_dpad")) {
                if (parse_bool(value, &b))
                    c->stick_moves_dpad = b;
                else
                    BAD("stick_moves_dpad: yes or no, not \"%s\"", value);
            } else if (!SDL_strcasecmp(key, "deadzone")) {
                long v = strtol(value, &end, 10);
                if (*value && !*end && v >= 1 && v <= 99)
                    c->deadzone = (int)v;
                else
                    BAD("deadzone: a percentage from 1 to 99, not \"%s\"", value);
            } else {
                BAD("unknown option %s", key);
            }
            continue;
        }
        if (sec == SEC_NONE) {
            BAD("%s outside [keyboard] or [gamepad]", key);
            continue;
        }
        int id = find_control(key);
        if (id < 0) {
            BAD("unknown control %s", key);
            continue;
        }
        /* The list replaces the defaults; it is applied only if every name is known. */
        int keys[CTL_MAX_BINDS] = {0};
        PadSource pads[CTL_MAX_BINDS] = {{0}};
        int n = 0;
        bool ok = true;
        char *save = NULL;
        for (char *tok = SDL_strtok_r(value, ",", &save); tok; tok = SDL_strtok_r(NULL, ",", &save)) {
            tok = trim(tok);
            if (!SDL_strcasecmp(tok, "none") || !*tok)
                continue;
            if (n == CTL_MAX_BINDS) {
                BAD("%s: at most %d inputs", key, CTL_MAX_BINDS);
                ok = false;
                break;
            }
            if (sec == SEC_KEYS) {
                SDL_Scancode sc = SDL_GetScancodeFromName(tok);
                if (sc == SDL_SCANCODE_UNKNOWN || (int)sc >= CTL_SCANCODES) {
                    BAD("%s: unknown key \"%s\"", key, tok);
                    ok = false;
                    break;
                }
                keys[n++] = (int)sc;
            } else if (parse_pad(tok, &pads[n])) {
                n++;
            } else {
                BAD("%s: unknown gamepad input \"%s\"", key, tok);
                ok = false;
                break;
            }
        }
        if (!ok)
            continue;
        if (sec == SEC_KEYS)
            memcpy(c->keys[id], keys, sizeof keys);
        else
            memcpy(c->pad[id], pads, sizeof pads);
    }
#undef BAD
    return errors;
}

/* snprintf that keeps a running length and never writes past `size`. */
static void append(char *buf, size_t size, size_t *len, const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(4);
static void append(char *buf, size_t size, size_t *len, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(*len < size ? buf + *len : NULL, *len < size ? size - *len : 0, fmt, ap);
    va_end(ap);
    if (n > 0)
        *len += (size_t)n;
}

static void key_list(const Controls *c, ControlId id, char *buf, size_t size)
{
    size_t len = 0;
    buf[0] = 0;
    for (int k = 0; k < CTL_MAX_BINDS; k++)
        if (c->keys[id][k])
            append(buf, size, &len, "%s%s", len ? ", " : "", SDL_GetScancodeName((SDL_Scancode)c->keys[id][k]));
    if (!len)
        snprintf(buf, size, "none");
}

static void pad_list(const Controls *c, ControlId id, bool positions, char *buf, size_t size)
{
    size_t len = 0;
    buf[0] = 0;
    for (int k = 0; k < CTL_MAX_BINDS; k++)
        if (c->pad[id][k].kind != PAD_NONE) {
            char n[48];
            pad_name(c->pad[id][k], positions, n, sizeof n);
            append(buf, size, &len, "%s%s", len ? ", " : "", n);
        }
    if (!len)
        snprintf(buf, size, "none");
}

size_t controls_write(const Controls *c, char *buf, size_t size)
{
    size_t len = 0;
    char list[160];
    if (size)
        buf[0] = 0;
    append(buf, size, &len,
           "# Carnage Heart EXA PC port: controls (docs/controls.md).\n"
           "# A control listed here replaces its default inputs; \"none\" unbinds it.\n"
           "# Keys are SDL scancode names (where the key is on a US layout): Z, Return,\n"
           "# Right Shift, Keypad 8, ... Gamepad inputs are SDL's names: a b x y (or south\n"
           "# east west north), dpup dpdown dpleft dpright, leftshoulder rightshoulder,\n"
           "# start back, leftstick rightstick, and axes with a direction: leftx- leftx+\n"
           "# lefty- lefty+ rightx- rightx+ righty- righty+ lefttrigger righttrigger.\n"
           "# Up to %d inputs a control.\n\n[keyboard]\n",
           CTL_MAX_BINDS);
    for (int i = 0; i < CTL_COUNT; i++) {
        key_list(c, (ControlId)i, list, sizeof list);
        append(buf, size, &len, "%s = %s\n", k_controls[i].name, list);
    }
    append(buf, size, &len, "\n[gamepad]\n");
    for (int i = 0; i < CTL_COUNT; i++) {
        pad_list(c, (ControlId)i, false, list, sizeof list);
        append(buf, size, &len, "%s = %s\n", k_controls[i].name, list);
    }
    append(buf, size, &len,
           "\n[options]\n"
           "# The stick also presses the D-pad (the PSP's stick doesn't; the menus only\n"
           "# read the D-pad).\nstick_moves_dpad = %s\n"
           "# How far (percent) a stick or trigger must go to count as pressed.\ndeadzone = %d\n",
           c->stick_moves_dpad ? "yes" : "no", c->deadzone);
    return len;
}

/* ---- the live state ---- */

static uint8_t stick_byte(int toward_pos, int toward_neg)
{
    int d = toward_pos - toward_neg; /* -32767 .. 32767 */
    if (d == 0)
        return 0x80;
    int v = d > 0 ? 0x80 + d * 0x7F / 32767 : 0x80 + d * 0x80 / 32767;
    return (uint8_t)(v < 0 ? 0 : v > 0xFF ? 0xFF : v);
}

/* `tilt[id]`: how far each control is pushed, 0..32767. */
static CtlState from_tilt(const Controls *c, const int tilt[CTL_COUNT])
{
    CtlState s = {0, 0x80, 0x80};
    int threshold = c->deadzone * 32767 / 100;
    for (int i = 0; i < CTL_COUNT; i++)
        if (k_controls[i].bit && tilt[i] > threshold)
            s.buttons |= k_controls[i].bit;
    s.lx = stick_byte(tilt[CTL_STICK_RIGHT], tilt[CTL_STICK_LEFT]);
    s.ly = stick_byte(tilt[CTL_STICK_DOWN], tilt[CTL_STICK_UP]);
    if (c->stick_moves_dpad) {
        static const ControlId k_from[4] = {CTL_STICK_UP, CTL_STICK_DOWN, CTL_STICK_LEFT, CTL_STICK_RIGHT};
        static const uint32_t k_to[4] = {PSP_CTRL_UP, PSP_CTRL_DOWN, PSP_CTRL_LEFT, PSP_CTRL_RIGHT};
        for (int k = 0; k < 4; k++)
            if (tilt[k_from[k]] > threshold)
                s.buttons |= k_to[k];
    }
    return s;
}

CtlState controls_from_keys(const Controls *c, const bool *down)
{
    int tilt[CTL_COUNT] = {0};
    for (int i = 0; i < CTL_COUNT; i++)
        for (int k = 0; k < CTL_MAX_BINDS; k++) {
            int sc = c->keys[i][k];
            if (sc > 0 && sc < CTL_SCANCODES && down[sc])
                tilt[i] = 32767;
        }
    return from_tilt(c, tilt);
}

CtlState controls_from_gamepad(const Controls *c, const bool buttons[CTL_PAD_BUTTONS],
                               const int16_t axes[CTL_PAD_AXES])
{
    int tilt[CTL_COUNT] = {0};
    for (int i = 0; i < CTL_COUNT; i++)
        for (int k = 0; k < CTL_MAX_BINDS; k++) {
            PadSource p = c->pad[i][k];
            int t = 0;
            if (p.kind == PAD_BUTTON && p.code >= 0 && p.code < CTL_PAD_BUTTONS && buttons[p.code])
                t = 32767;
            else if ((p.kind == PAD_AXIS_POS || p.kind == PAD_AXIS_NEG) && p.code >= 0 && p.code < CTL_PAD_AXES) {
                int v = axes[p.code];
                t = p.kind == PAD_AXIS_POS ? v : -v;
                t = t < 0 ? 0 : t > 32767 ? 32767 : t;
            }
            if (t > tilt[i])
                tilt[i] = t;
        }
    return from_tilt(c, tilt);
}

CtlState controls_merge(CtlState a, CtlState b)
{
    CtlState s = a;
    s.buttons |= b.buttons;
    if (b.lx != 0x80 || b.ly != 0x80) {
        s.lx = b.lx;
        s.ly = b.ly;
    }
    return s;
}

size_t controls_describe(const Controls *c, char *buf, size_t size)
{
    size_t len = 0;
    char keys[160], pads[160];
    if (size)
        buf[0] = 0;
    append(buf, size, &len, "  %-12s %-12s %-26s %s\n", "PSP", "", "keyboard", "gamepad");
    for (int i = 0; i < CTL_COUNT; i++) {
        key_list(c, (ControlId)i, keys, sizeof keys);
        pad_list(c, (ControlId)i, true, pads, sizeof pads);
        /* the symbols are UTF-8: pad by characters, not bytes */
        const char *sym = k_controls[i].symbol;
        int chars = 0;
        for (const char *q = sym; *q; q++)
            chars += ((unsigned char)*q & 0xC0) != 0x80;
        append(buf, size, &len, "  %-12s %s%*s %-26s %s\n", k_controls[i].name, sym, chars < 12 ? 12 - chars : 0, "",
               keys, pads);
    }
    append(buf, size, &len, "  The stick %s the D-pad; deadzone %d%%. Escape quits.\n",
           c->stick_moves_dpad ? "also presses" : "doesn't press", c->deadzone);
    return len;
}

/* ---- the help text ---- */

/* Shift-JIS and UTF-8 spellings of the face-button symbols the game's help uses. */
static const struct {
    const char *text;
    uint32_t mask;
} k_help_words[] = {
    {"\x81\x9b", 1u << CTL_CIRCLE},   {"\xe2\x97\x8b", 1u << CTL_CIRCLE},
    {"\x81\x7e", 1u << CTL_CROSS},    {"\xc3\x97", 1u << CTL_CROSS},
    {"\x81\xa2", 1u << CTL_TRIANGLE}, {"\xe2\x96\xb3", 1u << CTL_TRIANGLE},
    {"\x81\xa0", 1u << CTL_SQUARE},   {"\xe2\x96\xa1", 1u << CTL_SQUARE},
    {"L and R button", (1u << CTL_L) | (1u << CTL_R)},
    {"L button", 1u << CTL_L},
    {"R button", 1u << CTL_R},
    {"START button", 1u << CTL_START},
    {"SELECT button", 1u << CTL_SELECT},
    {"directional button", (1u << CTL_UP) | (1u << CTL_DOWN) | (1u << CTL_LEFT) | (1u << CTL_RIGHT)},
    {"up and down button", (1u << CTL_UP) | (1u << CTL_DOWN)},
    {"left and right button", (1u << CTL_LEFT) | (1u << CTL_RIGHT)},
    {"up button", 1u << CTL_UP},
    {"down button", 1u << CTL_DOWN},
    {"left button", 1u << CTL_LEFT},
    {"right button", 1u << CTL_RIGHT},
};

uint32_t controls_help_buttons(const char *help)
{
    if (!help)
        return 0;
    /* The help window breaks lines anywhere ("directional\nbuttons"): join them. */
    char text[512];
    size_t n = 0;
    for (const char *p = help; *p && n + 1 < sizeof text; p++) {
        char ch = *p == '\n' || *p == '\r' ? ' ' : *p;
        if (ch == ' ' && n && text[n - 1] == ' ')
            continue;
        text[n++] = ch;
    }
    text[n] = 0;
    uint32_t mask = 0;
    for (size_t i = 0; i < SDL_arraysize(k_help_words); i++)
        if (strstr(text, k_help_words[i].text))
            mask |= k_help_words[i].mask;
    return mask;
}

size_t controls_help_hint(const Controls *c, uint32_t mask, bool pad, char *buf, size_t size)
{
    size_t len = 0;
    char keys[160], pads[160];
    if (size)
        buf[0] = 0;
    const uint32_t dpad = (1u << CTL_UP) | (1u << CTL_DOWN) | (1u << CTL_LEFT) | (1u << CTL_RIGHT);
    if ((mask & dpad) == dpad) {
        /* the four together, as "directional buttons": the first key of each */
        append(buf, size, &len, "D-pad");
        for (int i = CTL_UP; i <= CTL_RIGHT; i++) {
            key_list(c, (ControlId)i, keys, sizeof keys);
            char *comma = strchr(keys, ',');
            if (comma)
                *comma = 0;
            append(buf, size, &len, "%s%s", i == CTL_UP ? " " : "/", keys);
        }
        if (pad)
            append(buf, size, &len, " | pad D-pad%s", c->stick_moves_dpad ? " or stick" : "");
        mask &= ~dpad;
    }
    for (int i = 0; i < CTL_COUNT; i++) {
        if (!(mask & (1u << i)))
            continue;
        key_list(c, (ControlId)i, keys, sizeof keys);
        append(buf, size, &len, "%s%s %s", len ? "   " : "", k_controls[i].symbol, keys);
        if (pad) {
            pad_list(c, (ControlId)i, false, pads, sizeof pads);
            append(buf, size, &len, " | pad %s", pads);
        }
    }
    return len;
}

/* ---- where the config file lives ---- */

const char *controls_config_path(void)
{
    static char path[1024];
    const char *env = SDL_getenv("CHEXA_CONTROLS");
    if (env && *env)
        return env;
#if defined(_WIN32)
    const char *base = SDL_getenv("APPDATA");
    if (!base || !*base)
        return NULL;
    snprintf(path, sizeof path, "%s\\chexa-pc\\controls.ini", base);
#elif defined(__APPLE__)
    const char *home = SDL_getenv("HOME");
    if (!home || !*home)
        return NULL;
    snprintf(path, sizeof path, "%s/Library/Application Support/chexa-pc/controls.ini", home);
#else
    const char *xdg = SDL_getenv("XDG_CONFIG_HOME"), *home = SDL_getenv("HOME");
    if (xdg && *xdg)
        snprintf(path, sizeof path, "%s/chexa-pc/controls.ini", xdg);
    else if (home && *home)
        snprintf(path, sizeof path, "%s/.config/chexa-pc/controls.ini", home);
    else
        return NULL;
#endif
    return path;
}
