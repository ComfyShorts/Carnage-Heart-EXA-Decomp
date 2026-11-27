/* See ui.h. */
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gamedata.h"
#include "gu.h"
#include "pad.h"
#include "psp_imports.h"
#include "psp_shim.h"

/* ---- parts cache (FUN_0888763c, FUN_08887834) ----
 *
 * The names come from the pointer table at 0x08c79468 (splat 0x00475468). Names
 * without a slash live in "parts/bf/". Without the EBOOT only the title's entries
 * are known. */

#define PARTS_NAME_TABLE 0x00475468u

static const struct { int index; const char *name; } k_title_parts[] = {
    {PART_TI_MENL0, "title/ti_menl0"}, {PART_TI_MENB0, "title/ti_menb0"},
    {PART_TI_BACK, "title/ti_back"},   {PART_TI_TITLE, "title/ti_title"},
    {PART_LOGO_A, "title/logo_a"},
};

static Picture *g_parts[PART_COUNT];
static bool g_parts_failed[PART_COUNT];

static const char *part_name(int index)
{
    uint32_t p = gamedata_word(PARTS_NAME_TABLE + 4u * (uint32_t)index, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    const char *name = p ? gamedata_string(p) : NULL;
    if (name && *name)
        return name;
    for (size_t i = 0; i < SDL_arraysize(k_title_parts); i++)
        if (k_title_parts[i].index == index)
            return k_title_parts[i].name;
    return NULL;
}

Picture *ui_part(int index)
{
    if (index < 0 || index >= PART_COUNT || g_parts_failed[index])
        return NULL;
    if (g_parts[index])
        return g_parts[index];
    const char *name = part_name(index);
    if (!name) {
        SDL_Log("[parts] no name for texture 0x%X (EBOOT not loaded?)", index);
        g_parts_failed[index] = true;
        return NULL;
    }
    char path[96];
    if (strchr(name, '/'))
        snprintf(path, sizeof path, "%s.gim", name);
    else
        snprintf(path, sizeof path, "parts/bf/%s.gim", name);
    g_parts[index] = picture_load(path);
    if (!g_parts[index])
        g_parts_failed[index] = true;
    return g_parts[index];
}

/* ---- tweens ---- */

void tween_start(Tween *tw, float from, float to, float duration)
{
    *tw = (Tween){from, to, duration, 0.0f, true};
}

float tween_value(const Tween *tw)
{
    if (!tw->active || tw->from == tw->to || tw->t <= 0.0f)
        return tw->active && tw->t > 0.0f ? tw->to : tw->from;
    if (tw->t >= tw->duration)
        return tw->to;
    return (1.0f + expf(-4.0f) - expf(-4.0f * tw->t / tw->duration)) * (tw->to - tw->from) + tw->from;
}

bool tween_done(const Tween *tw)
{
    return !tw->active || tw->t >= tw->duration;
}

void tween_step(Tween *tw, float dt)
{
    if (tw->active && tw->t < tw->duration)
        tw->t += dt;
}

/* ---- drawing ---- */

typedef struct QuadVertex {
    int16_t u, v;
    uint32_t color;
    int16_t x, y, z, pad;
} QuadVertex;

void ui_quad(int x, int y, int w, int h, uint32_t color)
{
    QuadVertex *v = sceGuGetMemory(4 * (int)sizeof *v);
    if (!v)
        return;
    v[0] = (QuadVertex){0, 0, color, (int16_t)x, (int16_t)y, 0, 0};
    v[1] = (QuadVertex){(int16_t)w, 0, color, (int16_t)(x + w), (int16_t)y, 0, 0};
    v[2] = (QuadVertex){0, (int16_t)h, color, (int16_t)x, (int16_t)(y + h), 0, 0};
    v[3] = (QuadVertex){(int16_t)w, (int16_t)h, color, (int16_t)(x + w), (int16_t)(y + h), 0, 0};
    sceGuDrawArray(GU_TRIANGLE_STRIP,
                   GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 4, NULL, v);
}

static uint32_t alpha_white(float alpha)
{
    int a = (int)(SDL_clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return (uint32_t)a << 24 | 0xFFFFFF;
}

void ui_pic(Picture *p, int x, int y, float alpha, bool additive)
{
    if (p && alpha > 0.0f)
        picture_draw(p, x, y, 0, additive ? BLEND_ADD : BLEND_ALPHA, alpha_white(alpha), 0, 0);
}

void ui_frame(Picture *p, int x, int y, int w, int h, int b, float alpha)
{
    if (!p || alpha <= 0.0f)
        return;
    int pw = picture_width(p), ph = picture_height(p);
    uint32_t c = alpha_white(alpha);
    int sx[3] = {0, b, pw - b}, sw[3] = {b, pw - 2 * b, b};
    int sy[3] = {0, b, ph - b}, sh[3] = {b, ph - 2 * b, b};
    int dx[3] = {x, x + b, x + w - b}, dw[3] = {b, w - 2 * b, b};
    int dy[3] = {y, y + b, y + h - b}, dh[3] = {b, h - 2 * b, b};
    for (int r = 0; r < 3; r++)
        for (int col = 0; col < 3; col++)
            if (dw[col] > 0 && dh[r] > 0)
                picture_draw_ex(p, dx[col], dy[r], 0, dw[col], dh[r], sx[col], sy[r], sw[col], sh[r], BLEND_ALPHA,
                                c, 0, 0, 0, 0);
}

typedef struct ColorVertex {
    uint32_t color;
    int16_t x, y, z, pad;
} ColorVertex;

void ui_fill(int x, int y, int w, int h, uint32_t abgr)
{
    ColorVertex *v = sceGuGetMemory(2 * (int)sizeof *v);
    if (!v)
        return;
    v[0] = (ColorVertex){abgr, (int16_t)x, (int16_t)y, 0, 0};
    v[1] = (ColorVertex){abgr, (int16_t)(x + w), (int16_t)(y + h), 0, 0};
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL, v);
    sceGuEnable(GU_TEXTURE_2D);
}

int ui_text(const char *s, const TextStyle *st, int x, int y, float alpha)
{
    if (!s || alpha <= 0.0f)
        return 0;
    TextStyle t = *st;
    uint32_t a = (uint32_t)((float)(t.color >> 24) * SDL_clamp(alpha, 0.0f, 1.0f));
    t.color = (t.color & 0xFFFFFF) | a << 24;
    int w = 0, h = 0;
    text_draw(s, &t, x, y, 0, &w, &h);
    return w;
}

int ui_text_width(const char *s, const TextStyle *st)
{
    int w = 0, h = 0;
    if (s)
        text_measure(s, st, &w, &h);
    return w;
}

float ui_pulse(float t)
{
    float s = sinf(t * 2.0f * (float)M_PI / 1.2f - (float)M_PI / 2.0f);
    return 0.3f + 0.7f * (s + 1.0f) * 0.5f;
}

/* ---- CTopMenuWnd ---- */

void ui_menu_init(UiMenu *m, int x, int y, int pitch, int text_x, TextStyle style)
{
    memset(m, 0, sizeof *m);
    m->x = x;
    m->y = y;
    m->pitch = pitch;
    m->text_x = text_x;
    m->style = style;
    m->selected_color = 0xFFFFFFFF;
    m->fade.from = 1.0f; /* drawn fully until opened */
}

UiMenuItem *ui_menu_add(UiMenu *m, const char *text, int id, int help, int button, int cursor)
{
    if (m->count >= UI_MENU_MAX)
        return NULL;
    UiMenuItem *it = &m->item[m->count++];
    *it = (UiMenuItem){text, id, help, button, cursor, false, false, NULL};
    return it;
}

/* SetMenuStatus: the column fades in while sliding from 32 pixels to the left. The
 * distance is the port's; the time is the windows' usual 1/6 s. */
void ui_menu_open(UiMenu *m)
{
    tween_start(&m->fade, 0.0f, 1.0f, UI_TWEEN_TIME);
    tween_start(&m->slide, -32.0f, 0.0f, UI_TWEEN_TIME);
    m->pulse_t = 0.0f;
    m->active = true;
}

void ui_menu_close(UiMenu *m)
{
    tween_start(&m->fade, tween_value(&m->fade), 0.0f, UI_TWEEN_TIME);
    tween_start(&m->slide, tween_value(&m->slide), -32.0f, UI_TWEEN_TIME);
    m->active = false;
}

void ui_menu_update(UiMenu *m, float dt)
{
    tween_step(&m->fade, dt);
    tween_step(&m->slide, dt);
    m->pulse_t += dt;
}

/* FUN_08ad75ec */
bool ui_menu_input(UiMenu *m)
{
    if (!m->active || m->count <= 0)
        return false;
    int old = m->cursor, n = m->count;
    if (pad_repeat(PSP_CTRL_UP)) {
        if (m->cursor > 0)
            m->cursor--;
        else if (pad_trigger(PSP_CTRL_UP))
            m->cursor = n - 1;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        if (m->cursor < n - 1)
            m->cursor++;
        else if (pad_trigger(PSP_CTRL_DOWN))
            m->cursor = 0;
    }
    if (m->cursor != old)
        m->pulse_t = 0.0f;
    return m->cursor != old;
}

int ui_menu_id(const UiMenu *m)
{
    return m->count ? m->item[m->cursor].id : -1;
}

/* FUN_08ad77ec draws every button but the cursor's, then that one on top. A CBtnWnd
 * (FUN_08897248) draws its first picture with its text, tinted 0xCFCFCF when
 * disabled; once selected (FUN_088967c4 sets +0x3C) it draws the second picture
 * instead, growing over 0.1 s to 1/10 wider and 2 pixels taller about its centre. */
#define UI_SELECT_TIME 0.1f

void ui_menu_draw(const UiMenu *m)
{
    float alpha = tween_value(&m->fade);
    if (alpha <= 0.0f)
        return;
    int x0 = m->x + (int)tween_value(&m->slide);
    float grow = SDL_min(m->pulse_t / UI_SELECT_TIME, 1.0f);
    sceGuDisable(GU_DITHER);
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < m->count; i++) {
            if ((i == m->cursor) != (pass == 1))
                continue;
            const UiMenuItem *it = &m->item[i];
            bool selected = i == m->cursor && m->active;
            Picture *button = ui_part(it->button), *sel = ui_part(it->cursor);
            int y = m->y + i * m->pitch;
            int bh = button ? picture_height(button) : m->pitch;
            int bw = button ? picture_width(button) : 128;
            if (selected && sel) {
                int sw = picture_width(sel), sh = picture_height(sel);
                int dx = (int)(grow * (float)sw / 20.0f), dy = (int)grow;
                picture_draw_ex(sel, x0 - dx, y - dy + (bh - sh) / 2, 0, sw + 2 * dx, sh + 2 * dy, 0, 0, sw, sh,
                                BLEND_ALPHA, alpha_white(alpha), 0, 0, 0, 0);
            } else if (button) {
                picture_draw(button, x0, y, 0, BLEND_ALPHA,
                             it->disabled ? (uint32_t)(alpha * 255.0f) << 24 | 0xCFCFCF : alpha_white(alpha), 0, 0);
            }
            TextStyle st = m->style;
            if (selected)
                st.color = m->selected_color;
            if (it->disabled)
                st.color = 0xFF808080;
            int th = st.height;
            ui_text(it->text, &st, x0 + m->text_x, y + (bh - th) / 2, alpha);
            if (it->value && m->value_x) {
                TextStyle vs = m->style;
                ui_text(it->value, &vs, x0 + m->value_x, y + (bh - th) / 2, alpha);
            } else if (it->value) {
                int w = ui_text_width(it->value, &st);
                ui_text(it->value, &st, x0 + bw - 10 - w, y + (bh - th) / 2, alpha);
            }
            if (it->is_new)
                ui_pic(ui_part(PART_BF_NEW00), x0 + bw - 28, y - 4, alpha, false);
        }
    sceGuEnable(GU_DITHER);
}

/* ---- help window ----
 * FUN_088ffcac makes a full-screen window holding sd_wind0 and the text; its draw
 * (FUN_088ffec0) puts the text at the window's offset (8, 5) less 2. The panel's
 * place at the bottom of the screen is the port's. */

#define HELP_Y 226

void ui_help_draw(const char *text)
{
    ui_frame(ui_part(PART_SD_WIND0), 0, HELP_Y, 480, 272 - HELP_Y, 8, 0.9f);
    if (!text)
        return;
    TextStyle st = text_style(0, 6, 12, 12, 0, 4, 0xFF3F3F3F, 0); /* FUN_08b15d98(.., 4, 0xff3f3f3f) */
    ui_text(text, &st, 8 - 2, HELP_Y + 5, 1.0f);
}

/* ---- Yes/No question ---- */

void ui_ask_open(UiAsk *a, const char *question, const char *yes, const char *no, int cursor)
{
    memset(a, 0, sizeof *a);
    a->question = question;
    a->yes = yes;
    a->no = no;
    a->cursor = cursor;
    a->open = true;
    tween_start(&a->fade, 0.0f, 1.0f, UI_TWEEN_TIME);
}

int ui_ask_update(UiAsk *a, float dt)
{
    if (!a->open)
        return -1;
    tween_step(&a->fade, dt);
    a->pulse_t += dt;
    if (pad_repeat(PSP_CTRL_LEFT | PSP_CTRL_RIGHT | PSP_CTRL_UP | PSP_CTRL_DOWN)) {
        a->cursor ^= 1;
        a->pulse_t = 0.0f;
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        a->open = false;
        return a->cursor == 0;
    }
    if (pad_pressed(PSP_CTRL_CROSS)) {
        a->open = false;
        return 0;
    }
    return -1;
}

void ui_ask_draw(const UiAsk *a)
{
    if (!a->open)
        return;
    float alpha = tween_value(&a->fade);
    TextStyle st = text_style(0, 6, 12, 12, 0, 4, 0xFFFFFFFF, 0);
    int w = 240, h = 84, x = (480 - w) / 2, y = 80;
    ui_fill(0, 0, 480, 272, (uint32_t)(alpha * 0x60) << 24);
    ui_frame(ui_part(PART_SD_WIND0), x, y, w, h, 8, alpha);
    int tw = ui_text_width(a->question, &st);
    ui_text(a->question, &st, x + (w - tw) / 2, y + 14, alpha);
    Picture *button = ui_part(PART_BF_MENB4), *cursor = ui_part(PART_BF_MENL4);
    int bw = button ? picture_width(button) : 96;
    const char *label[2] = {a->yes, a->no};
    for (int i = 0; i < 2; i++) {
        int bx = x + w / 2 - bw / 2 - 52 + i * 104, by = y + 46;
        bool sel = i == a->cursor;
        Picture *p = sel && cursor ? cursor : button;
        if (p)
            picture_draw_ex(p, bx + 26, by, 0, bw - 52, picture_height(p), 0, 0, picture_width(p), picture_height(p),
                            BLEND_ALPHA, alpha_white(alpha), 0, 0, 0, 0);
        TextStyle bs = st;
        bs.color = sel ? 0xFFFFFFFF : 0xFF303030;
        int lw = ui_text_width(label[i], &bs);
        ui_text(label[i], &bs, bx + bw / 2 - lw / 2, by + 5, alpha);
    }
}
