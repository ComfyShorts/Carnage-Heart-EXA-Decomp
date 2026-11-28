/* Software Setup's parameter sub-menus. Hand-written from CSoftDesignMngr:
 *   SetupSubMenu FUN_08a8ddbc (the list of sub-menus, labels through 0x08C2E494),
 *     UpdateModeSubMenu FUN_08a854fc, its circle FUN_08a87894;
 *   the value window (mode 0x14) FUN_08a7e600, its confirm FUN_08a92db0, the windows
 *     FUN_08a8b290 (a list), FUN_08a8b56c (a list with only the first n usable), the
 *     dials with GetSubMenuMin / Max FUN_08a8a4b8 / FUN_08a8a7ac and their unit
 *     pictures (sd_tani0, rectangles at 0x08C40C60);
 *   mode 0x15 FUN_08a93f28 / FUN_08a87b10 (the number after "Value" or "Specify
 *     Altitude");
 *   the range editor (mode 0x17) FUN_08a87ea0 / FUN_08a87fc8 with its input
 *     FUN_08a9fa58, the direction / elevation editors (modes 0x18 / 0x19)
 *     FUN_08a88584 / FUN_08a88894 with FUN_08a99954 / FUN_08a9b490;
 *   the "no assign button" warning (mode 0x2E, CSoftString 0x18E).
 * The values' places in the chip and the editors' rules are in pgchip_edit.c. */
#include "oke_param.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "font.h"
#include "gamedata.h"
#include "oke_parts.h"
#include "pad.h"
#include "psp_shim.h"
#include "pgchip_edit.h"
#include "picture.h"
#include "sound.h"
#include "ui.h"

#define ADDR(a) ((a) - 0x08804000u)
#define T_SOFT_STRING 0x00486BA0u /* CSoftString */
#define T_CHIP_NAME 0x00481100u
#define T_COUNTER_NAME 0x004810B8u
#define T_SUB_LABEL ADDR(0x08C2E494u)     /* sub-menu -> CSoftString */
#define T_UNIT ADDR(0x08C40C60u)          /* sd_tani0 rectangles: u8 u, v, w, h */
#define T_ABILITY ADDR(0x08C7B3B0u)       /* CEquipmentName: weapon abilities */
#define T_OKE_TYPE ADDR(0x08C8508Cu)      /* COkeName: Two Legged, ... */
#define T_CPU_CLASS ADDR(0x08BD5F68u)     /* FUN_088566ec: per CPU part */
#define T_WEAPON_OPTION ADDR(0x08C3560Cu) /* 0x39's 10 entries */
#define SOFT_STRINGS 0x2A3
#define PIC_UNIT "parts/soft/sd_tani0.gim" /* CSoftPicture 44 */
#define PART_BF_SWND2 0x99

/* ---- the game's text ---- */

static const char *ptr_string(uint32_t table, int i)
{
    uint32_t p = gamedata_word(table + 4u * (uint32_t)i, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

static const char *sstr(int id)
{
    const char *s = id >= 0 && id < SOFT_STRINGS ? ptr_string(T_SOFT_STRING, id) : NULL;
    return s ? s : "";
}

static int32_t rd32(uint32_t a)
{
    return (int32_t)gamedata_word(a, 0);
}

static const TextStyle *style(int size, uint32_t color)
{
    static TextStyle st[4];
    static int n;
    TextStyle *s = &st[n++ & 3];
    *s = text_style(0, size / 2, size, size, 0, 4, color, 0);
    return s;
}

/* ---- what each sub-menu's window is ---- */

/* A dial: GetSubMenuMin / Max and the step and unit its window is made with. */
typedef struct Dial {
    uint8_t sub;
    int32_t min, max;
    int step;
    int8_t unit; /* sd_tani0 rectangle, -1 none */
    int8_t shown; /* added for display (0x22's codes are stored one lower) */
} Dial;

static const Dial k_dials[] = {
    {0x0D, 1, 120, 1, 0x12, 0},   {0x0E, 5, 60, 1, 0x12, 0},   {0x12, 20, 100, 10, 8, 0},
    {0x16, 1, 16, 1, 9, 0},       {0x19, 1, 8, 1, 9, 0},       {0x1B, 1, 990, 1, 9, 0},
    {0x21, 1, 3, 1, 0x10, 0},     {0x22, -1, 41, 1, -1, 1},    {0x23, 0, 30, 1, 8, 0},
    {0x25, 0, 100, 1, 6, 0},      {0x26, 1, 99, 1, 6, 0},      {0x2A, 1, 300, 1, 0xB, 0},
    {0x2D, 1, 3, 1, 0x10, 0},     {0x35, -9999999, 9999999, 1, -1, 0},
    {0x3A, 0, 500, 1, 8, 0},      {0x3B, 0, 360, 1, 0x11, 0},  {0x42, -128, 127, 1, -1, 0},
    {0x48, 1, 5, 1, 0xB, 0},
};

static const Dial *find_dial(int sub)
{
    for (size_t i = 0; i < sizeof k_dials / sizeof k_dials[0]; i++)
        if (k_dials[i].sub == sub)
            return &k_dials[i];
    return NULL;
}

/* A list of CSoftString ids (FUN_08a8b290 / FUN_08a8b56c's text tables). */
typedef struct List {
    uint8_t sub, count;
    uint32_t table;
} List;

static const List k_lists[] = {
    {0x02, 2, 0x08C35634}, {0x09, 8, 0x08C352D0}, {0x0A, 2, 0x08C352D0}, {0x0B, 9, 0x08C352D0},
    {0x0C, 8, 0x08C352D0}, {0x0F, 4, 0x08C352F8}, {0x10, 2, 0x08C35308}, {0x11, 3, 0x08C35310},
    {0x13, 6, 0x08C3531C}, {0x14, 5, 0x08C35334}, {0x17, 3, 0x08C35348}, {0x18, 12, 0x08C3538C},
    {0x1A, 4, 0x08C35360}, {0x1C, 4, 0x08C3537C}, {0x1D, 4, 0x08C3537C}, {0x1E, 3, 0x08C3537C},
    {0x24, 3, 0x08C353BC}, {0x27, 21, 0x08C353C8}, {0x28, 21, 0x08C353C8}, {0x29, 12, 0x08C3541C},
    {0x2B, 2, 0x08C35458}, {0x2C, 2, 0x08C35468}, {0x2E, 2, 0x08C35470}, {0x2F, 6, 0x08C35478},
    {0x30, 4, 0x08C35490}, {0x34, 44, 0x08C354A0}, {0x37, 19, 0x08C3559C}, {0x38, 8, 0x08C355EC},
    {0x39, 10, 0x08C3560C}, {0x3C, 4, 0x08C3563C}, {0x3D, 8, 0x08C3564C}, {0x3E, 3, 0x08C3566C},
    {0x3F, 10, 0x08C35678}, {0x40, 5, 0x08C356A0}, {0x41, 2, 0x08C356B4}, {0x43, 7, 0x08C356BC},
    {0x44, 2, 0x08C356F0}, {0x45, 6, 0x08C356D8}, {0x46, 6, 0x08C356F8}, {0x47, 6, 0x08C35710},
    {0x49, 4, 0x08C3563C}, {0x4A, 3, 0x08C3544C}, {0x4B, 2, 0x08C35460}, {0x4C, 3, 0x08C35354},
};

static const List *find_list(int sub)
{
    for (size_t i = 0; i < sizeof k_lists / sizeof k_lists[0]; i++)
        if (k_lists[i].sub == sub)
            return &k_lists[i];
    return NULL;
}

/* ---- state ---- */

typedef enum {
    W_MENU,  /* mode 0x13 */
    W_LIST,  /* mode 0x14, a list */
    W_DIAL,  /* mode 0x14 / 0x15, a number */
    W_CHECK, /* mode 0x14, Assign Button's checklist */
    W_RANGE, /* mode 0x17 */
    W_ANGLE, /* modes 0x18 / 0x19 */
    W_WARN,  /* mode 0x2E */
} Window;

#define LIST_MAX 48
#define LIST_SHOWN 10

static struct {
    PgChipCommand chip;
    const SoftwareDataBase *sw;
    int weapons, options, cpu_class;
    int subs[PGCHIP_SUB_ITEMS], count, cursor;
    Window win;
    int sub; /* the value window's sub-menu */
    /* list / checklist */
    int n, sel, top;
    bool usable[LIST_MAX];
    uint16_t mask;
    /* dial */
    const Dial *dial;
    int32_t value;
    int digit; /* the step's power of ten (a stand-in for the number pad) */
    int then;  /* mode 0x15: the sub-menu the dial is for, written with `then_value` */
    int then_sub, then_value;
    /* range / angle */
    uint8_t range[4];
    bool elevation;
    int angle, angle_step;
    float t;
} p;

/* ---- the chip's parameters ---- */

static int type(void)
{
    return pgchip_type(&p.chip);
}

static int32_t get(int sub)
{
    int32_t v = 0;
    pgchip_param_get(&p.chip, sub, &v);
    return v;
}

static int label(int sub)
{
    return rd32(T_SUB_LABEL + 4u * (uint32_t)sub);
}

static void counter_name(int i, char *out, size_t n)
{
    char name[7] = {0};
    if (p.sw)
        memcpy(name, p.sw->counter_name[i], 6);
    const char *d = name[0] ? name : ptr_string(T_COUNTER_NAME, i);
    snprintf(out, n, "%s", d ? d : "");
}

/* The entries of a list window and their text. */
static int entry_count(int sub)
{
    switch (sub) {
    case 0x07: return 2;
    case 0x08: return type() == 0x1A || type() == 0x1C ? 3 : 2;
    case 0x15: return 17;
    case 0x1F: return 6;
    case 0x20: return 7;
    case 0x31: case 0x32: case 0x33: return 8;
    case 0x36: return 9;
    default: {
        const List *l = find_list(sub);
        return l ? l->count : 0;
    }
    }
}

static void entry_text(int sub, int i, char *out, size_t n)
{
    const char *s = NULL;
    switch (sub) {
    case 0x07: /* Halt / Passthrough; Wait (9) says Halt Until Changed */
        s = sstr(rd32(ADDR(type() == 0x09 ? 0x08C352C8u : 0x08C352C0u) + 4u * (uint32_t)i));
        break;
    case 0x08: s = sstr(rd32(ADDR(0x08C35370u) + 4u * (uint32_t)i)); break;
    case 0x15: /* FUN_08aa1644: the weapon abilities, then two of its own */
        s = i < 15 ? ptr_string(T_ABILITY, i) : sstr(0x179 + i - 15);
        break;
    case 0x1F: s = ptr_string(T_OKE_TYPE, i); break;
    case 0x20: s = i < 5 ? ptr_string(T_OKE_TYPE, i) : i == 5 ? sstr(0x178) : ptr_string(T_OKE_TYPE, 5); break;
    case 0x31: case 0x32: case 0x33: counter_name(i, out, n); return;
    case 0x36:
        if (i < 8) {
            counter_name(i, out, n);
            return;
        }
        s = sstr(0x132); /* Value */
        break;
    default: {
        const List *l = find_list(sub);
        if (l && i >= 0 && i < l->count)
            s = sstr(rd32(ADDR(l->table) + 4u * (uint32_t)i));
    }
    }
    snprintf(out, n, "%s", s ? s : "");
}

/* FUN_08a8b56c's usable entries: the body's weapons / options, a CPU of class 3 for
 * the sub-programs; 0x39 (FUN_08aa2640) both. */
static bool entry_usable(int sub, int i)
{
    switch (sub) {
    case 0x13: case 0x45: return i < p.weapons + 1;
    case 0x1A: return i < p.options;
    case 0x2C: return p.cpu_class == 3;
    case 0x43: return i < p.weapons;
    case 0x39: return i < 6 ? i <= p.weapons : i - 6 < p.options;
    default: return true;
    }
}

/* The value as the reference panel and the dials show it. */
static void value_text(int sub, char *out, size_t n)
{
    if (sub == PGCHIP_SUB_DIRECTION || sub == PGCHIP_SUB_ELEVATION) {
        int a = 0;
        pgchip_angle_get(&p.chip, sub == PGCHIP_SUB_ELEVATION, &a);
        snprintf(out, n, "%d.%02d", a / 100, (a < 0 ? -a : a) % 100);
        return;
    }
    if (sub == PGCHIP_SUB_POLAR || sub == PGCHIP_SUB_CARTESIAN) {
        out[0] = 0;
        return;
    }
    int32_t v = get(sub);
    if (sub == 0x3F) { /* the buttons, by name */
        out[0] = 0;
        for (int i = 0; i < 10; i++)
            if (v >> i & 1) {
                char e[32];
                entry_text(sub, i, e, sizeof e);
                size_t l = strlen(out);
                snprintf(out + l, n - l, "%s%s", l ? ", " : "", e);
            }
        return;
    }
    const Dial *d = find_dial(sub);
    if (d) {
        snprintf(out, n, "%d", (int)(v + d->shown));
        return;
    }
    entry_text(sub, v, out, n);
    int then = sub == 0x36 && v == 8 ? 0x35 : sub == 0x4C && v == 2 ? 0x12 : -1; /* mode 0x15's number */
    if (then >= 0) {
        size_t l = strlen(out);
        snprintf(out + l, n - l, " %d", (int)get(then));
    }
}

/* ---- opening ---- */

bool param_has(int type)
{
    int subs[PGCHIP_SUB_ITEMS];
    return pgchip_sub_menus(type, subs) > 0;
}

void param_open(const PgChipCommand *chip, const SoftwareDataBase *sw, const OkeDataBase *card)
{
    memset(&p, 0, sizeof p);
    p.chip = *chip;
    p.sw = sw;
    if (card) {
        int body = hw_get(card, HW_BODY), cpu = oke_cpu_part(card);
        p.weapons = body_weapons(body);
        p.options = body_options(body);
        p.cpu_class = cpu >= 0 && cpu < 8 ? rd32(T_CPU_CLASS + 4u * (uint32_t)cpu) : 0;
    }
    p.count = pgchip_sub_menus(type(), p.subs);
    p.win = W_MENU;
    SDL_Log("[soft] %s: %d sub-menus", ptr_string(T_CHIP_NAME, type()), p.count);
}

const PgChipCommand *param_chip(void)
{
    return &p.chip;
}

static void open_list(int sub, int cur)
{
    p.win = W_LIST;
    p.sub = sub;
    p.n = entry_count(sub);
    if (p.n > LIST_MAX)
        p.n = LIST_MAX;
    for (int i = 0; i < p.n; i++)
        p.usable[i] = entry_usable(sub, i);
    p.sel = cur >= 0 && cur < p.n ? cur : 0;
    p.top = p.sel >= LIST_SHOWN ? p.sel - LIST_SHOWN + 1 : 0;
}

static void open_dial(const Dial *d, int32_t v, int then_sub, int then_value)
{
    p.win = W_DIAL;
    p.dial = d;
    p.value = v < d->min ? d->min : v > d->max ? d->max : v;
    p.digit = 0;
    p.then_sub = then_sub;
    p.then_value = then_value;
}

/* FUN_08a87894: circle on a sub-menu item. */
static void choose(void)
{
    int sub = p.subs[p.cursor];
    if (sub == PGCHIP_SUB_CONFIRM)
        return; /* handled by the caller */
    if (sub == PGCHIP_SUB_POLAR || sub == PGCHIP_SUB_CARTESIAN) {
        memcpy(p.range, p.chip.b + 2, 4);
        pgchip_range_set_shape(p.range, sub == PGCHIP_SUB_CARTESIAN);
        p.win = W_RANGE;
        sound_se(SE_DECIDE, 1.0f);
        return;
    }
    if (sub == PGCHIP_SUB_DIRECTION || sub == PGCHIP_SUB_ELEVATION) {
        p.elevation = sub == PGCHIP_SUB_ELEVATION;
        pgchip_angle_get(&p.chip, p.elevation, &p.angle);
        p.angle_step = pgchip_angle_step(&p.chip);
        p.win = W_ANGLE;
        sound_se(SE_DECIDE, 1.0f);
        return;
    }
    if (sub == PGCHIP_SUB_PROCESSING && (type() == 0x1E || type() == 0x1F) && !(p.chip.b[4] | p.chip.b[5])) {
        sound_se(SE_ERROR, 1.0f); /* an endless move / turn always passes through */
        return;
    }
    p.sub = sub;
    const Dial *d = find_dial(sub);
    if (d)
        open_dial(d, get(sub) + d->shown, -1, 0);
    else if (sub == 0x3F) {
        p.win = W_CHECK;
        p.n = 10;
        p.sel = 0;
        p.top = 0;
        p.mask = (uint16_t)get(sub);
    } else
        open_list(sub, get(sub));
    sound_se(SE_DECIDE, 1.0f);
}

/* ---- input ---- */

static int wrap_index(int i, int n)
{
    return (i % n + n) % n;
}

static void update_menu(void)
{
    int old = p.cursor;
    if (pad_repeat(PSP_CTRL_UP))
        p.cursor = wrap_index(p.cursor - 1, p.count);
    else if (pad_repeat(PSP_CTRL_DOWN))
        p.cursor = wrap_index(p.cursor + 1, p.count);
    else if (pad_pressed(PSP_CTRL_LEFT) && p.cursor != p.count - 1) /* to Confirm */
        p.cursor = p.count - 1;
    if (p.cursor != old)
        sound_se(SE_CURSOR, 1.0f);
}

static void back_to_menu(int se)
{
    p.win = W_MENU;
    sound_se(se, 1.0f);
}

/* FUN_08a92db0: the list's choice. */
static void list_chosen(void)
{
    int sub = p.sub, v = p.sel;
    if (sub == PGCHIP_SUB_RANGE) { /* the shape, then the range itself */
        memcpy(p.range, p.chip.b + 2, 4);
        pgchip_range_set_shape(p.range, v);
        p.win = W_RANGE;
        sound_se(SE_DECIDE, 1.0f);
        return;
    }
    if (sub == 0x36 && v == 8) { /* Value: a number for 0x35 */
        open_dial(find_dial(0x35), get(0x35), 0x36, 8);
        sound_se(SE_DECIDE, 1.0f);
        return;
    }
    if (sub == 0x4C && v == 2) { /* Specify Altitude: a number for 0x12 */
        open_dial(find_dial(0x12), get(0x12), 0x4C, 2);
        sound_se(SE_DECIDE, 1.0f);
        return;
    }
    pgchip_param_set(&p.chip, sub, v);
    back_to_menu(SE_DECIDE);
}

static void update_list(void)
{
    if (pad_pressed(PSP_CTRL_CROSS)) {
        back_to_menu(SE_CANCEL);
        return;
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        if (p.win == W_CHECK) {
            pgchip_param_set(&p.chip, p.sub, p.mask);
            back_to_menu(SE_DECIDE);
        } else if (p.usable[p.sel])
            list_chosen();
        else
            sound_se(SE_ERROR, 1.0f);
        return;
    }
    if (p.win == W_CHECK && pad_pressed(PSP_CTRL_TRIANGLE)) {
        p.mask ^= (uint16_t)(1u << p.sel);
        sound_se(p.mask >> p.sel & 1 ? SE_DECIDE : 0xE, 1.0f);
        return;
    }
    if (p.win == W_LIST && p.sub == 0x47 && pad_pressed(PSP_CTRL_TRIANGLE)) {
        SDL_Log("[soft] alert sound %d (not played: the battle's alert sounds aren't ported)", p.sel + 1);
        return;
    }
    int old = p.sel;
    if (pad_repeat(PSP_CTRL_UP))
        p.sel = wrap_index(p.sel - 1, p.n);
    else if (pad_repeat(PSP_CTRL_DOWN))
        p.sel = wrap_index(p.sel + 1, p.n);
    if (p.sel != old) {
        sound_se(SE_CURSOR, 1.0f);
        if (p.sel < p.top)
            p.top = p.sel;
        if (p.sel >= p.top + LIST_SHOWN)
            p.top = p.sel - LIST_SHOWN + 1;
    }
}

static int pow10i(int e)
{
    int v = 1;
    while (e-- > 0)
        v *= 10;
    return v;
}

/* CNumListWnd: up / down by the step; a fresh press past an end goes round, a held
 * one stops there. Left / right pick the digit: a stand-in for the number pad
 * (mode 0x16) that triangle opens in the game. */
static void update_dial(void)
{
    const Dial *d = p.dial;
    if (pad_pressed(PSP_CTRL_CROSS)) {
        back_to_menu(SE_CANCEL);
        return;
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        if (p.then_sub >= 0) { /* mode 0x15: both values */
            pgchip_param_set(&p.chip, d->sub, p.value);
            pgchip_param_set(&p.chip, p.then_sub, p.then_value);
        } else
            pgchip_param_set(&p.chip, d->sub, p.value - d->shown);
        back_to_menu(SE_DECIDE);
        return;
    }
    int span = d->max - d->min, digits = 0;
    while (pow10i(digits + 1) * d->step <= span && digits < 7)
        digits++;
    if (pad_repeat(PSP_CTRL_LEFT) && p.digit < digits) {
        p.digit++;
        sound_se(SE_CURSOR, 1.0f);
    } else if (pad_repeat(PSP_CTRL_RIGHT) && p.digit > 0) {
        p.digit--;
        sound_se(SE_CURSOR, 1.0f);
    }
    int dir = 0;
    bool fresh = false;
    if (pad_repeat(PSP_CTRL_UP)) {
        dir = 1;
        fresh = pad_trigger(PSP_CTRL_UP);
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        dir = -1;
        fresh = pad_trigger(PSP_CTRL_DOWN);
    }
    if (!dir)
        return;
    int32_t lo = d->min + d->shown, hi = d->max + d->shown, old = p.value;
    int64_t v = (int64_t)p.value + (int64_t)dir * d->step * pow10i(p.digit);
    if (v > hi)
        v = fresh && p.value == hi ? lo : hi;
    else if (v < lo)
        v = fresh && p.value == lo ? hi : lo;
    p.value = (int32_t)v;
    if (p.value != old)
        sound_se(0x10, 1.0f);
}

/* FUN_08a9fa58 (the rules in pgchip_range_edit): circle keeps it (FUN_08a87fc8). */
static void update_range(void)
{
    if (pad_pressed(PSP_CTRL_CROSS)) {
        back_to_menu(SE_CANCEL);
        return;
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        memcpy(p.chip.b + 2, p.range, 4);
        back_to_menu(SE_DECIDE);
        return;
    }
    if (pad_pressed(PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER))
        sound_se(0x11, 1.0f);
    int dx = 0, dy = 0;
    if (pad_repeat(PSP_CTRL_LEFT))
        dx--;
    if (pad_repeat(PSP_CTRL_RIGHT))
        dx++;
    if (pad_repeat(PSP_CTRL_UP))
        dy++;
    if (pad_repeat(PSP_CTRL_DOWN))
        dy--;
    if ((dx || dy) && pgchip_range_edit(p.range, dx, dy, pad_held(PSP_CTRL_RTRIGGER), pad_held(PSP_CTRL_LTRIGGER)))
        sound_se(0x10, 1.0f);
}

static void update_angle(void)
{
    if (pad_pressed(PSP_CTRL_CROSS)) {
        back_to_menu(SE_CANCEL);
        return;
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) { /* FUN_08a88698 / FUN_08a889a8 */
        pgchip_angle_set(&p.chip, p.elevation, p.angle);
        back_to_menu(SE_DECIDE);
        return;
    }
    int old = p.angle;
    if (!p.elevation) {
        int d = (pad_repeat(PSP_CTRL_RIGHT) ? 1 : 0) - (pad_repeat(PSP_CTRL_LEFT) ? 1 : 0);
        if (d)
            p.angle = pgchip_angle_edit(p.angle, false, p.angle_step, d, false, pad_held(PSP_CTRL_LTRIGGER));
        if (pad_pressed(PSP_CTRL_LTRIGGER))
            sound_se(0x11, 1.0f);
    } else {
        int fresh = (pad_trigger(PSP_CTRL_UP) ? 1 : 0) - (pad_trigger(PSP_CTRL_DOWN) ? 1 : 0);
        int held = (pad_repeat(PSP_CTRL_UP) ? 1 : 0) - (pad_repeat(PSP_CTRL_DOWN) ? 1 : 0);
        if (fresh)
            p.angle = pgchip_angle_edit(p.angle, true, p.angle_step, fresh, true, false);
        else if (held)
            p.angle = pgchip_angle_edit(p.angle, true, p.angle_step, held, false, false);
    }
    if (p.angle != old)
        sound_se(0x10, 1.0f);
}

/* FUN_08a87e20: Button Input (0x1B) needs at least one button. */
static bool buttons_ok(void)
{
    return type() != 0x1B || (get(0x3F) & 0x3FF);
}

ParamResult param_update(float dt)
{
    p.t += dt;
    switch (p.win) {
    case W_MENU:
        if (pad_pressed(PSP_CTRL_CROSS)) {
            if (!buttons_ok()) {
                sound_se(SE_ERROR, 1.0f);
                p.win = W_WARN;
                return PARAM_OPEN;
            }
            sound_se(SE_CANCEL, 1.0f);
            return PARAM_CANCEL;
        }
        if (pad_pressed(PSP_CTRL_CIRCLE)) {
            if (p.subs[p.cursor] == PGCHIP_SUB_CONFIRM) {
                if (!buttons_ok()) {
                    sound_se(SE_ERROR, 1.0f);
                    p.win = W_WARN;
                    return PARAM_OPEN;
                }
                sound_se(0x11, 1.0f);
                return PARAM_CONFIRM;
            }
            choose();
            return PARAM_OPEN;
        }
        update_menu();
        break;
    case W_LIST:
    case W_CHECK: update_list(); break;
    case W_DIAL: update_dial(); break;
    case W_RANGE: update_range(); break;
    case W_ANGLE: update_angle(); break;
    case W_WARN: /* FUN_08a7fc04 */
        if (pad_pressed(PSP_CTRL_CIRCLE | PSP_CTRL_CROSS))
            back_to_menu(SE_CANCEL);
        break;
    }
    return PARAM_OPEN;
}

/* ---- drawing ---- */

#define MENU_X 8
#define MENU_Y 40
#define MENU_W 150
#define WIN_X 164
#define ROW 14

static void draw_cursor_box(int x, int y, int w, int h)
{
    ui_frame(ui_part(PART_BF_CUR0), x, y, w, h, 6, ui_pulse(p.t));
}

static void draw_unit(int unit, int x, int y)
{
    static Picture *pic;
    static bool tried;
    if (!tried) {
        tried = true;
        pic = picture_load(PIC_UNIT);
    }
    if (!pic || unit < 0 || unit >= 0x17)
        return;
    uint32_t r = gamedata_word(T_UNIT + 4u * (uint32_t)unit, 0);
    int u = r & 0xFF, v = r >> 8 & 0xFF, w = r >> 16 & 0xFF, h = r >> 24 & 0xFF;
    picture_draw_ex(pic, x, y, 0, w, h, u, v, w, h, BLEND_ALPHA, 0xFFFFFFFF, 0, 0, 0, 0);
}

static void draw_menu(void)
{
    int h = p.count * ROW + 12;
    ui_frame(ui_part(PART_BF_SWND2), MENU_X, MENU_Y, MENU_W, h, 8, 1.0f);
    for (int i = 0; i < p.count; i++)
        ui_text(sstr(label(p.subs[i])), style(12, 0xFF303030), MENU_X + 14, MENU_Y + 6 + i * ROW, 1.0f);
    draw_cursor_box(MENU_X + 8, MENU_Y + 5 + p.cursor * ROW, MENU_W - 16, ROW);
}

/* The chip's values beside the menu: a stand-in for CPgChipRefWnd (FUN_08a13b08). */
static void draw_reference(void)
{
    const int x = 300, y = 78, w = 172;
    int rows = 0;
    for (int i = 0; i < p.count; i++)
        rows += p.subs[i] != PGCHIP_SUB_CONFIRM;
    ui_fill(x, y, w, rows * 22 + 6, 0xC0202020);
    int r = 0;
    for (int i = 0; i < p.count; i++) {
        int sub = p.subs[i];
        if (sub == PGCHIP_SUB_CONFIRM)
            continue;
        char v[96];
        value_text(sub, v, sizeof v);
        ui_text(sstr(label(sub)), style(10, 0xFFC0C0C0), x + 6, y + 3 + r * 22, 1.0f);
        ui_text(v, style(10, 0xFFFFFFFF), x + 14, y + 13 + r * 22, 1.0f);
        r++;
    }
}

static void draw_list(void)
{
    int shown = p.n < LIST_SHOWN ? p.n : LIST_SHOWN;
    const int y = MENU_Y + p.cursor * ROW;
    int w = 130;
    for (int i = 0; i < p.n; i++) {
        char e[64];
        entry_text(p.sub, i, e, sizeof e);
        int tw = ui_text_width(e, style(12, 0)) + (p.win == W_CHECK ? 10 : 0) + 36;
        if (tw > w)
            w = tw;
    }
    int yy = y + shown * ROW + 30 > 222 ? 222 - shown * ROW - 30 : y;
    ui_frame(ui_part(PART_BF_SWND2), WIN_X, yy, w, shown * ROW + 30, 8, 1.0f);
    ui_text(sstr(label(p.sub)), style(10, 0xFF606060), WIN_X + 10, yy + 5, 1.0f);
    for (int k = 0; k < shown; k++) {
        int i = p.top + k;
        char e[64];
        entry_text(p.sub, i, e, sizeof e);
        uint32_t c = p.usable[i] ? 0xFF303030 : 0xFFA0A0A0;
        int tx = WIN_X + 14;
        if (p.win == W_CHECK) {
            ui_text(p.mask >> i & 1 ? "*" : "-", style(12, c), tx, yy + 19 + k * ROW, 1.0f);
            tx += 10;
        }
        ui_text(e, style(12, c), tx, yy + 19 + k * ROW, 1.0f);
    }
    if (p.top > 0)
        ui_text("^", style(10, 0xFF606060), WIN_X + w - 16, yy + 18, 1.0f);
    if (p.top + shown < p.n)
        ui_text("v", style(10, 0xFF606060), WIN_X + w - 16, yy + 16 + shown * ROW, 1.0f);
    draw_cursor_box(WIN_X + 8, yy + 18 + (p.sel - p.top) * ROW, w - 16, ROW);
}

static void draw_dial(void)
{
    const int y = MENU_Y + p.cursor * ROW, w = 130;
    ui_frame(ui_part(PART_BF_SWND2), WIN_X, y, w, 46, 8, 1.0f);
    ui_text(sstr(label(p.then_sub >= 0 ? p.then_sub : p.dial->sub)), style(10, 0xFF606060), WIN_X + 10, y + 5, 1.0f);
    char s[24];
    snprintf(s, sizeof s, "%d", (int)p.value);
    const TextStyle *st = style(14, 0xFF303030);
    int tw = ui_text_width(s, st), right = WIN_X + w - 30;
    ui_text(s, st, right - tw, y + 21, 1.0f);
    draw_unit(p.dial->unit, right + 3, y + 23);
    /* the digit up / down moves (stand-in) */
    int cw = ui_text_width("0", st);
    ui_fill(right - (p.digit + 1) * cw, y + 37, cw, 2, 0xFF3060C0);
}

/* A dot, for the stand-in drawings. */
static void dot(float x, float y, uint32_t c)
{
    ui_fill((int)x - 1, (int)y - 1, 2, 2, c);
}

static void line(float x0, float y0, float x1, float y1, uint32_t c)
{
    float dx = x1 - x0, dy = y1 - y0;
    int n = (int)(SDL_sqrtf(dx * dx + dy * dy) / 2) + 1;
    for (int i = 0; i <= n; i++)
        dot(x0 + dx * i / n, y0 + dy * i / n, c);
}

/* The range from above, the OKE in the middle facing up: a stand-in for the game's
 * 3D view (CPgChipRangeWnd, FUN_08a9ec84), with the camera the analog pad turns. */
static void draw_range(void)
{
    const int x = WIN_X, y = 30, w = 306, h = 192;
    ui_frame(ui_part(PART_BF_SWND2), x, y, w, h, 8, 1.0f);
    const float cx = x + w / 2.0f, cy = y + h / 2.0f + 6, rmax = 76.0f;
    const uint8_t *r = p.range;
    /* zoomed to fit the range: the view's radius is the first of 50, 100, 200, 500, ... past it */
    int extent;
    if (r[0] & 1) {
        int ex = SDL_abs(pgchip_range_distance((int8_t)r[2])) + pgchip_range_distance(r[1]) / 2;
        int ey = SDL_abs(pgchip_range_distance((int8_t)r[3])) + pgchip_range_distance(r[0] >> 1) / 2;
        extent = ex > ey ? ex : ey;
    } else
        extent = pgchip_range_distance(r[2]);
    int view = 50;
    for (int i = 0; view < extent; i++)
        view = view * (i % 3 == 1 ? 5 : 2) / (i % 3 == 1 ? 2 : 1);
    float k = rmax / (float)view;
    for (int ring = 1; ring <= 4; ring++) /* every quarter of the view */
        for (int a = 0; a < 360; a += 6) {
            float rr = rmax * ring / 4, t = a * 3.14159265f / 180.0f;
            dot(cx + rr * SDL_sinf(t), cy - rr * SDL_cosf(t), 0xFFC8C8C8);
        }
    char vs[16];
    snprintf(vs, sizeof vs, "%d", view);
    ui_text(vs, style(10, 0xFF909090), (int)cx + 2, (int)(cy - rmax) - 12, 1.0f);
    const uint32_t col = 0xFFE05020;
    char s[96];
    if (r[0] & 1) {
        float rx = (float)pgchip_range_distance((int8_t)r[2]) * k, ry = (float)pgchip_range_distance((int8_t)r[3]) * k;
        float hw = (float)pgchip_range_distance(r[1]) * k / 2, hd = (float)pgchip_range_distance(r[0] >> 1) * k / 2;
        float l = cx + rx - hw, rt = cx + rx + hw, t = cy - ry - hd, b = cy - ry + hd;
        line(l, t, rt, t, col);
        line(rt, t, rt, b, col);
        line(rt, b, l, b, col);
        line(l, b, l, t, col);
        snprintf(s, sizeof s, "X %d  Y %d  W %d  D %d", pgchip_range_distance((int8_t)r[2]),
                 pgchip_range_distance((int8_t)r[3]), pgchip_range_distance(r[1]), pgchip_range_distance(r[0] >> 1));
    } else {
        float outer = (float)pgchip_range_distance(r[2]) * k, inner = (float)pgchip_range_distance(r[0] >> 1) * k;
        int dir = r[3] * 2, half = r[1] * 2;
        for (int a = dir - half; a <= dir + half; a += 2) {
            float t = a * 3.14159265f / 180.0f;
            dot(cx + outer * SDL_sinf(t), cy - outer * SDL_cosf(t), col);
            dot(cx + inner * SDL_sinf(t), cy - inner * SDL_cosf(t), col);
        }
        if (half < 180)
            for (int e = -1; e <= 1; e += 2) {
                float t = (dir + e * half) * 3.14159265f / 180.0f;
                line(cx + inner * SDL_sinf(t), cy - inner * SDL_cosf(t), cx + outer * SDL_sinf(t),
                     cy - outer * SDL_cosf(t), col);
            }
        snprintf(s, sizeof s, "%d-%d  %d +/- %d", pgchip_range_distance(r[0] >> 1), pgchip_range_distance(r[2]), dir,
                 half);
    }
    ui_fill((int)cx - 3, (int)cy - 3, 6, 6, 0xFF303030); /* the OKE */
    ui_fill((int)cx - 1, (int)cy - 9, 2, 6, 0xFF303030);
    char shape[64];
    entry_text(PGCHIP_SUB_RANGE, r[0] & 1, shape, sizeof shape); /* the shape's name */
    ui_text(shape, style(10, 0xFF606060), x + 10, y + 5, 1.0f);
    ui_text(s, style(10, 0xFF303030), x + 10, y + h - 17, 1.0f);
}

static void draw_angle(void)
{
    const int x = WIN_X, y = 40, w = 200, h = 170;
    ui_frame(ui_part(PART_BF_SWND2), x, y, w, h, 8, 1.0f);
    ui_text(sstr(label(p.elevation ? PGCHIP_SUB_ELEVATION : PGCHIP_SUB_DIRECTION)), style(10, 0xFF606060), x + 10,
            y + 5, 1.0f);
    const float cx = x + w / 2.0f, cy = y + h / 2.0f + 4, rr = 60.0f;
    float t = p.angle / 100.0f * 3.14159265f / 180.0f;
    if (!p.elevation) { /* from above, 0 ahead, clockwise */
        for (int a = 0; a < 360; a += 5) {
            float u = a * 3.14159265f / 180.0f;
            dot(cx + rr * SDL_sinf(u), cy - rr * SDL_cosf(u), a % 90 ? 0xFFC8C8C8 : 0xFF808080);
        }
        line(cx, cy, cx + rr * SDL_sinf(t), cy - rr * SDL_cosf(t), 0xFFE05020);
    } else { /* from the side, the OKE facing right */
        for (int a = -90; a <= 90; a += 5) {
            float u = a * 3.14159265f / 180.0f;
            dot(cx - 30 + rr * SDL_cosf(u), cy - rr * SDL_sinf(u), a % 45 ? 0xFFC8C8C8 : 0xFF808080);
        }
        line(cx - 30, cy, cx - 30 + rr * SDL_cosf(t), cy - rr * SDL_sinf(t), 0xFFE05020);
    }
    char s[24];
    snprintf(s, sizeof s, "%s%d.%02d", p.angle < 0 ? "-" : "", (p.angle < 0 ? -p.angle : p.angle) / 100,
             (p.angle < 0 ? -p.angle : p.angle) % 100);
    ui_text(s, style(12, 0xFF303030), x + 10, y + h - 20, 1.0f);
    draw_unit(0x11, x + 12 + ui_text_width(s, style(12, 0xFF303030)), y + h - 18);
}

void param_draw(void)
{
    if (p.win != W_RANGE && p.win != W_ANGLE && p.win != W_WARN)
        draw_reference();
    draw_menu();
    switch (p.win) {
    case W_LIST:
    case W_CHECK: draw_list(); break;
    case W_DIAL: draw_dial(); break;
    case W_RANGE: draw_range(); break;
    case W_ANGLE: draw_angle(); break;
    case W_WARN: {
        ui_frame(ui_part(PART_BF_SWND2), 90, 100, 300, 50, 8, 1.0f);
        ui_text(sstr(0x18E), style(12, 0xFF303030), 104, 108, 1.0f);
        break;
    }
    default: break;
    }
}

/* The game's button guides (CSoftString 0x1A4-0x1B5) with the buttons as text, on
 * two lines when long. */
static const char *guide(int id)
{
    static char out[160];
    const char *s = sstr(id);
    size_t n = 0, half = strlen(s) / 2;
    bool broke = strlen(s) < 48;
    for (const char *s0 = s; *s && n + 12 < sizeof out; s++) {
        if (*s != '#' || !s[1]) {
            out[n++] = *s;
            continue;
        }
        if (!broke && (size_t)(s - s0) >= half && n && out[n - 1] == ' ') {
            out[n - 1] = '\n';
            broke = true;
        }
        const char *b;
        switch (*++s) {
        case 'a': b = "Stick: "; break;
        case 'd': b = "D-pad: "; break;
        case 'l': b = "L: "; break;
        case 'r': b = "R: "; break;
        case 't': b = "Triangle: "; break;
        case 's': b = "Square: "; break;
        case 'o': b = "Circle: "; break;
        case 'x': b = "Cross: "; break;
        default: b = ""; break;
        }
        size_t l = strlen(b);
        if (n + l >= sizeof out - 1)
            break;
        memcpy(out + n, b, l);
        n += l;
    }
    out[n] = 0;
    return out;
}

const char *param_help(void)
{
    switch (p.win) {
    case W_RANGE:
        if (p.range[0] & 1)
            return guide(pad_held(PSP_CTRL_RTRIGGER) ? 0x1B4 : 0x1B2);
        return guide(pad_held(PSP_CTRL_RTRIGGER) ? 0x1B0 : 0x1AE);
    case W_ANGLE: return guide(p.elevation ? 0x1AD : 0x1AB);
    case W_CHECK: return guide(p.mask >> p.sel & 1 ? 0x1A7 : 0x1A6);
    case W_LIST: return p.sub == 0x47 ? guide(0x1AA) : NULL;
    default: return NULL;
    }
}
