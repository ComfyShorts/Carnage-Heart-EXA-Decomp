/* See entrance.h. Written from the analysis of the functions named there; sound
 * calls, the save-data manager, network play, the demo timer's target and the
 * screens after the title are left out and logged instead. */
#include "entrance.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "font.h"
#include "gamedata.h"
#include "gu.h"
#include "heap.h"
#include "pad.h"
#include "picture.h"
#include "psp_imports.h"
#include "psp_shim.h"
#include "render.h"

/* ---- parts cache (FUN_0888763c) ----
 *
 * The game has 0x2F2 interface textures named in a table at 0x08c79468 and loads
 * them on first use (FUN_08887834). Names without a slash live in "parts/bf/". Only
 * the entries the title uses are listed. */

enum {
    PART_SD_WIND0 = 0x51,
    PART_TI_MENL0 = 0x5D, /* menu cursor */
    PART_TI_MENB0 = 0x5E, /* menu button */
    PART_TI_BACK = 0x5F,  /* title background, 480x272 */
    PART_TI_TITLE = 0x60, /* title logo, 352x160 */
    PART_LOGO_A = 0x71,   /* company logo */
    PART_COUNT = 0x2F2,
};

static const struct { int index; const char *name; } k_part_names[] = {
    {PART_TI_MENL0, "title/ti_menl0"}, {PART_TI_MENB0, "title/ti_menb0"},
    {PART_TI_BACK, "title/ti_back"},   {PART_TI_TITLE, "title/ti_title"},
    {PART_LOGO_A, "title/logo_a"},
};

static Picture *g_parts[PART_COUNT];

static Picture *part(int index)
{
    if (index < 0 || index >= PART_COUNT)
        return NULL;
    if (g_parts[index])
        return g_parts[index];
    const char *name = NULL;
    for (size_t i = 0; i < SDL_arraysize(k_part_names); i++)
        if (k_part_names[i].index == index)
            name = k_part_names[i].name;
    if (!name) {
        SDL_Log("[parts] texture 0x%X isn't listed in the port yet", index);
        return NULL;
    }
    char path[96];
    if (strchr(name, '/'))
        snprintf(path, sizeof path, "%s.gim", name);
    else
        snprintf(path, sizeof path, "parts/bf/%s.gim", name);
    g_parts[index] = picture_load(path);
    return g_parts[index];
}

/* ---- tweens (the CTimeValue members of the windows) ----
 *
 * An eased move from `from` to `to` over `duration` seconds:
 * value = (1 + e^-4 - e^(-4t/d)) * (to - from) + from. */

typedef struct Tween {
    float from, to, duration, t;
    bool active;
} Tween;

static void tween_start(Tween *tw, float from, float to, float duration)
{
    *tw = (Tween){from, to, duration, 0.0f, true};
}

static float tween_value(const Tween *tw)
{
    if (!tw->active || tw->from == tw->to || tw->t <= 0.0f)
        return tw->active && tw->t > 0.0f ? tw->to : tw->from;
    if (tw->t >= tw->duration)
        return tw->to;
    return (1.0f + expf(-4.0f) - expf(-4.0f * tw->t / tw->duration)) * (tw->to - tw->from) + tw->from;
}

static bool tween_done(const Tween *tw)
{
    return !tw->active || tw->t >= tw->duration;
}

static void tween_step(Tween *tw, float dt)
{
    if (tw->active && tw->t < tw->duration)
        tw->t += dt;
}

#define MENU_TWEEN_TIME (1.0f / 6.0f) /* DAT_08be7bc4 */

/* ---- windows (CWnd and the two kinds the title uses) ---- */

typedef enum { WND_COLOR, WND_MENU } WndKind;

typedef struct Wnd {
    WndKind kind;
    int x, y, w, h;   /* CWnd +0x16 .. +0x1C */
    bool visible;     /* +0x11 */
    bool active;      /* top of the stack: takes input */
    float opacity;    /* +0x20 */

    /* colour window */
    uint8_t rgba[4];  /* +0x3C */

    /* CEntranceWnd */
    int cursor;               /* +0x44 */
    int first, last;          /* +0x48 / +0x4C: string ids */
    int tex_button, tex_cursor; /* +0x50 / +0x54: part indices */
    Tween slide;              /* +0x58 */
    Tween fade;               /* +0x70 */
    TextStyle style;          /* +0x8C */
    float pulse_t;            /* +0xB0: cursor pulse */
    int home_x;               /* +0xF4 */
    int slide_dist;           /* +0xF8 */
    int spacing;              /* +0xFC */
    int text_dy;              /* +0x100 */
} Wnd;

#define MENU_ROW 21 /* item pitch in pixels */

/* CEntranceWnd::Create (FUN_088d7104) */
static void menu_create(Wnd *w, int tex_button, int tex_cursor, int first, int last, int x, int y)
{
    memset(w, 0, sizeof *w);
    w->kind = WND_MENU;
    w->x = x;
    w->y = y;
    w->visible = true;
    w->opacity = 1.0f;
    w->first = first;
    w->last = last;
    w->tex_button = tex_button;
    w->tex_cursor = tex_cursor;
    w->style = text_style(0, 6, 14, 14, 0, 4, 0xFFFFFFFF, 0);
    w->home_x = 0xA4;
    w->slide_dist = 200;
    w->spacing = 0;
    w->text_dy = -4;
}

static int menu_items(const Wnd *w)
{
    return w->last - w->first + 1;
}

/* FUN_088d7ce0: fade in and slide to home_x, from the left (from_right 0) or right. */
static void menu_open(Wnd *w, int from_right)
{
    tween_start(&w->fade, 0.0f, 1.0f, MENU_TWEEN_TIME);
    w->pulse_t = 0.0f; /* FUN_0888f474 */
    w->x = from_right ? w->home_x + w->slide_dist : w->home_x - w->slide_dist;
    tween_start(&w->slide, (float)w->x, (float)w->home_x, MENU_TWEEN_TIME);
}

/* FUN_088d7330: fade out and slide away to the left (to_right 0) or right. */
static void menu_close(Wnd *w, int to_right)
{
    tween_start(&w->fade, tween_value(&w->fade), 0.0f, MENU_TWEEN_TIME);
    w->pulse_t = 0.0f;
    w->x = (int)tween_value(&w->slide);
    int target = to_right ? w->home_x + w->slide_dist : w->home_x - w->slide_dist;
    tween_start(&w->slide, (float)w->x, (float)target, MENU_TWEEN_TIME);
}

/* FUN_088d7cac */
static bool menu_anim_done(const Wnd *w)
{
    return tween_done(&w->slide);
}

/* CEntranceWnd::Update (FUN_088d7778) */
static void menu_update(Wnd *w, float dt)
{
    tween_step(&w->slide, dt);
    tween_step(&w->fade, dt);
    if (w->slide.active)
        w->x = (int)tween_value(&w->slide);
    if (w->fade.active)
        w->opacity = tween_value(&w->fade);
    w->pulse_t += dt;
}

/* FUN_088d7ac0: up and down move the cursor; holding repeats, a fresh press past
 * either end wraps round. */
static void menu_input(Wnd *w)
{
    int old = w->cursor, n = menu_items(w);
    if (pad_repeat(PSP_CTRL_UP)) {
        if (w->cursor > 0)
            w->cursor--;
        else if (pad_trigger(PSP_CTRL_UP))
            w->cursor = n - 1;
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        if (w->cursor < n - 1)
            w->cursor++;
        else if (pad_trigger(PSP_CTRL_DOWN))
            w->cursor = 0;
    }
    if (w->cursor != old)
        SDL_Log("[entrance] menu cursor -> %d (sound 9 not played)", w->cursor);
}

/* FUN_0888f870, simplified: the cursor highlight breathes between 0.3 and 1. */
static float menu_pulse(const Wnd *w)
{
    float s = sinf(w->pulse_t * 2.0f * (float)M_PI / 1.2f - (float)M_PI / 2.0f);
    return 0.3f + 0.7f * (s + 1.0f) * 0.5f;
}

typedef struct QuadVertex {
    int16_t u, v;
    uint32_t color;
    int16_t x, y, z, pad;
} QuadVertex;

/* A textured 4-vertex strip in through mode (vertex type 0x80011E). */
static void draw_quad(int x, int y, int w, int h, uint32_t color)
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

/* CEntranceWnd::Draw (FUN_088d7efc) */
static void menu_draw(const Wnd *w)
{
    if (!w->visible)
        return;
    Picture *button = part(w->tex_button), *cursor = part(w->tex_cursor);
    int alpha = (int)(w->opacity * 255.0f);
    if (alpha <= 0 || !button)
        return;
    uint32_t color = (uint32_t)alpha << 24 | 0xFFFFFF;
    int bw = picture_width(button), bh = picture_height(button);
    sceGuDisable(GU_DITHER);
    for (int i = 0; i < menu_items(w); i++) {
        int y = w->y + MENU_ROW * i + w->spacing * i;
        sceGuEnable(GU_TEXTURE_2D);
        sceGuDisable(GU_CULL_FACE);
        picture_bind(button);
        draw_quad(w->x, y, bw, bh, color);
        sceGuEnable(GU_CULL_FACE);

        const char *s = design_string(w->first + i, 1);
        if (s) {
            TextStyle st = w->style;
            st.color = color;
            int tw, th;
            text_measure(s, &st, &tw, &th);
            text_draw(s, &st, w->x + (bw - tw) / 2 - 4, y + w->text_dy + (bh - th) / 2, 0, NULL, NULL);
        }
    }
    if (cursor) {
        int a = (int)(menu_pulse(w) * w->opacity * 255.0f);
        if (a > 0) {
            int y = w->y + MENU_ROW * w->cursor + w->spacing * w->cursor;
            sceGuEnable(GU_TEXTURE_2D);
            sceGuDisable(GU_CULL_FACE);
            sceGuBlendFunc(0, 2, 10, 0xFFFFFFFF, 0xFFFFFFFF); /* additive */
            picture_bind(cursor);
            draw_quad(w->x, y, picture_width(cursor), picture_height(cursor), (uint32_t)a << 24 | 0xFFFFFF);
            sceGuBlendFunc(0, 2, 3, 0xFFFFFFFF, 0xFFFFFFFF);
            sceGuEnable(GU_CULL_FACE);
        }
    }
    sceGuEnable(GU_DITHER);
}

typedef struct RectVertex {
    uint32_t color;
    int16_t x, y, z, pad;
} RectVertex;

/* FUN_0888f264 -> FUN_08ae3e94: the window's rectangle in its colour, alpha scaled
 * by the opacity (FUN_08ae31fc). */
static void color_draw(const Wnd *w)
{
    if (!w->visible)
        return;
    uint32_t a = (uint32_t)(w->opacity * 255.0f * (w->rgba[3] / 255.0f));
    uint32_t color = a << 24 | (uint32_t)w->rgba[2] << 16 | (uint32_t)w->rgba[1] << 8 | w->rgba[0];
    RectVertex *v = sceGuGetMemory(2 * (int)sizeof *v);
    if (!v)
        return;
    v[0] = (RectVertex){color, (int16_t)w->x, (int16_t)w->y, 0, 0};
    v[1] = (RectVertex){color, (int16_t)(w->x + w->w), (int16_t)(w->y + w->h), 0, 0};
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL, v);
    sceGuEnable(GU_TEXTURE_2D);
}

static void wnd_draw(const Wnd *w)
{
    if (w->kind == WND_MENU)
        menu_draw(w);
    else
        color_draw(w);
}

/* ---- CWndStack ---- */

typedef struct StackEntry {
    Wnd *wnd;
    int mode, arg;
} StackEntry;

typedef struct WndStack {
    StackEntry e[8];
    int count;
} WndStack;

/* FUN_08ae6234: the new top becomes active, the one below it inactive. */
static void stack_push(WndStack *s, Wnd *w, int mode, int arg)
{
    if (s->count >= (int)SDL_arraysize(s->e))
        return;
    s->e[s->count++] = (StackEntry){w, mode, arg};
    if (s->count > 1)
        s->e[s->count - 2].wnd->active = false;
    w->active = true;
}

/* FUN_08ae637c */
static void stack_pop(WndStack *s)
{
    if (s->count <= 0)
        return;
    s->e[--s->count].wnd->active = false;
    if (s->count > 0)
        s->e[s->count - 1].wnd->active = true;
}

/* ---- CEntranceMngr ---- */

enum {
    PHASE_NONE = -1,
    PHASE_IDLE = 0,
    PHASE_MODE_MENU = 2, /* Story Mode / Battle Mode */
    PHASE_LOGO = 3,
    PHASE_MOVIE = 4,
    PHASE_TOP_MENU = 5, /* Start Game / Continue / Network Play */
};

#define IDLE_VBLANKS 0xF3C /* about 65 seconds without input: phase 10 (attract) */

typedef struct EntranceMngr {
    bool initialised;   /* +0x20 */
    bool done;          /* +0x24 */
    int selection;      /* +0x28 */
    bool shown_once;    /* +0x2C */
    bool closing_mode;  /* +0x38: phase 2 is waiting for its menu to slide out */
    bool movie_done;    /* +0x3C */
    int logo_count;     /* +0x48 */
    int idle_vcount;    /* +0x4C */
    Wnd top_menu;       /* +0x58 */
    Wnd mode_menu;      /* +0x60 */
    WndStack stack;     /* +0x64 */
    Wnd black;          /* +0x68 */
    bool idle_warned, stop_warned;
} EntranceMngr;

static EntranceMngr g_mngr;

/* FUN_088d4438 */
static int mngr_phase(const EntranceMngr *m)
{
    return m->stack.count ? m->stack.e[m->stack.count - 1].mode : PHASE_NONE;
}

/* FUN_088d43a8: pops every entry of the top's phase. */
static void mngr_pop_phase(EntranceMngr *m)
{
    int phase = mngr_phase(m);
    while (m->stack.count && mngr_phase(m) == phase)
        stack_pop(&m->stack);
}

static void mngr_open(EntranceMngr *m, int phase, int arg);

/* FUN_088d6b40 */
static void mngr_init(EntranceMngr *m)
{
    memset(m, 0, sizeof *m);
    m->logo_count = -1;
    menu_create(&m->top_menu, PART_TI_MENB0, PART_TI_MENL0, 9, 0xB, 0, 0xA6);
    menu_create(&m->mode_menu, PART_TI_MENB0, PART_TI_MENL0, 0xD, 0xE, 0, 0xA6);
    m->black = (Wnd){.kind = WND_COLOR, .x = 0, .y = 0, .w = 0x1E0, .h = 0x110, .visible = true,
                     .opacity = 1.0f};
    m->initialised = true;
    mngr_open(m, PHASE_NONE, 0);
}

/* FUN_088d448c: enter a phase. */
static void mngr_open(EntranceMngr *m, int phase, int arg)
{
    switch (phase) {
    case PHASE_NONE: /* nothing to open: the next update picks the first phase */
        break;
    case PHASE_IDLE: /* FUN_088d45f4 */
        mngr_open(m, PHASE_TOP_MENU, arg);
        break;
    case PHASE_LOGO: /* FUN_088d4a10 */
        m->logo_count++;
        if (m->logo_count == 0) {
            stack_push(&m->stack, &m->black, PHASE_LOGO, arg);
            memcpy(m->black.rgba, (uint8_t[4]){0, 0, 0, 0xFF}, 4);
        }
        break;
    case PHASE_MOVIE: /* FUN_088d4ae0 */
        stack_push(&m->stack, &m->black, PHASE_MOVIE, arg);
        if (!m->movie_done) {
            /* The game switches the display off and resets CRender around the movie. */
            m->movie_done = true;
        }
        break;
    case PHASE_TOP_MENU: /* FUN_088d4624 */
        if (arg == 1) { /* back from the mode menu: slide in from the right */
            menu_open(&m->top_menu, 1);
        } else {
            stack_push(&m->stack, &m->top_menu, PHASE_TOP_MENU, arg);
            menu_open(&m->top_menu, 0);
        }
        m->idle_vcount = sceDisplayGetVcount();
        break;
    case PHASE_MODE_MENU: /* FUN_088d47d4 */
        stack_push(&m->stack, &m->mode_menu, PHASE_MODE_MENU, arg);
        menu_open(&m->mode_menu, 0);
        m->closing_mode = false;
        break;
    default:
        SDL_Log("[entrance] CEntranceMngr: phase %d isn't ported; staying on the current screen", phase);
        break;
    }
}

/* FUN_088d5090 */
static void phase_top_menu(EntranceMngr *m)
{
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        m->idle_vcount = sceDisplayGetVcount();
        if (m->top_menu.cursor == 0) {
            SDL_Log("[entrance] Start Game chosen: opening the mode menu");
            menu_close(&m->top_menu, 1);
            mngr_open(m, PHASE_MODE_MENU, 0);
        } else if (!m->stop_warned) {
            /* 1 Continue, 2 Network Play: pop and open phase 6 (the save list / net). */
            SDL_Log("[entrance] menu item %d chosen: phase 6 (load/network) isn't ported", m->top_menu.cursor);
            m->stop_warned = true;
        }
        return;
    }
    if (pad_held(0xFFFF))
        m->idle_vcount = sceDisplayGetVcount();
    else if (sceDisplayGetVcount() - m->idle_vcount > IDLE_VBLANKS && !m->idle_warned) {
        SDL_Log("[entrance] idle on the title: phase 10 (attract) isn't ported");
        m->idle_warned = true;
    }
}

/* FUN_088d5510 */
static void phase_mode_menu(EntranceMngr *m)
{
    if (m->closing_mode) {
        if (menu_anim_done(&m->mode_menu))
            mngr_pop_phase(m);
        return;
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        m->selection = m->mode_menu.cursor;
        if (!m->stop_warned) {
            SDL_Log("[entrance] %s chosen: phase %s isn't ported",
                    m->selection == 0 ? "Story Mode" : "Battle Mode", m->selection == 0 ? "0xC" : "7/9");
            m->stop_warned = true;
        }
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        menu_close(&m->mode_menu, 0);
        mngr_open(m, PHASE_TOP_MENU, 1);
        m->closing_mode = true;
        m->stop_warned = false;
    }
}

/* CEntranceMngr::Update (FUN_088d6360): the phase, then every window, then input for
 * the top one. */
static void mngr_update(EntranceMngr *m, float dt)
{
    switch (mngr_phase(m)) {
    case PHASE_NONE: /* FUN_088d502c */
        m->stack.count = 0;
        if (!m->shown_once) {
            mngr_open(m, PHASE_LOGO, 0);
            m->shown_once = true;
        } else {
            mngr_open(m, PHASE_IDLE, 0);
        }
        break;
    case PHASE_LOGO: /* FUN_088d5bec: one frame */
        mngr_pop_phase(m);
        mngr_open(m, PHASE_MOVIE, 0);
        break;
    case PHASE_MOVIE: /* FUN_088d5c20 */
        SDL_Log("[entrance] opening movie movie/che_op.pmf skipped (no MPEG player yet)");
        mngr_pop_phase(m);
        mngr_open(m, PHASE_IDLE, 0);
        break;
    case PHASE_TOP_MENU:
        phase_top_menu(m);
        break;
    case PHASE_MODE_MENU:
        phase_mode_menu(m);
        break;
    default:
        break;
    }
    for (int i = 0; i < m->stack.count; i++) /* FUN_08ae6608 */
        if (m->stack.e[i].wnd->kind == WND_MENU)
            menu_update(m->stack.e[i].wnd, dt);
    if (m->stack.count) { /* FUN_08ae657c */
        Wnd *top = m->stack.e[m->stack.count - 1].wnd;
        if (top->kind == WND_MENU && top->active && top->visible)
            menu_input(top);
    }
}

/* FUN_088d6524: the background in 4x2 tiles, then the title. */
static void draw_title(void)
{
    Picture *back = part(PART_TI_BACK);
    if (back) {
        int tw = picture_width(back) / 4, th = picture_height(back) / 2;
        for (int row = 0; row < 2; row++)
            for (int col = 0; col < 4; col++)
                picture_draw_rect(back, col * tw, row * th, 0, col * tw, row * th, tw, th, BLEND_ALPHA,
                                  0xFFFFFFFF, 0, 0);
    }
    picture_draw(part(PART_TI_TITLE), 0x46, 0x0C, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
}

/* CEntranceMngr::Draw (FUN_088d63b4) */
static void mngr_draw(void *ctx)
{
    EntranceMngr *m = ctx;
    if (!m->initialised)
        return;
    int phase = mngr_phase(m);
    if (phase == PHASE_LOGO && m->logo_count == 0)
        picture_draw(part(PART_LOGO_A), 0x70, 0x48, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    if (phase != PHASE_LOGO && phase != PHASE_MOVIE)
        draw_title(); /* phases 6, 7 and 0xB draw something else; not ported */
    for (int i = 0; i < m->stack.count; i++)
        wnd_draw(m->stack.e[i].wnd);
}

/* ---- the entrance screen (FUN_088d2d30) ---- */

static struct {
    int state;
    int fileset;
} g_entrance;

void entrance_start(void)
{
    g_entrance.state = 0;
    g_entrance.fileset = -1;
    SDL_Log("[entrance] screen created (root screen state 3)");
}

/* FUN_088d36e4 */
int entrance_update(float dt)
{
    switch (g_entrance.state) {
    case 0:
        /* FUN_088d3560: becomes a CRender node at priority 2. */
        render_add_node(render_instance(), 2, mngr_draw, &g_mngr);
        g_entrance.fileset = fileset_open("entrance", &g_default_heap, 0, 0);
        if (g_entrance.fileset < 0)
            SDL_Log("[entrance] OpenFileSet(\"entrance\") failed");
        if (!font_init())
            SDL_Log("[entrance] fonts unavailable: menu text won't be drawn");
        /* FUN_088d30d8: CSystemMngr in mode 0xD (CSaveMngr's start-up check), then
         * state 1 runs it until it's done. Not ported. */
        SDL_Log("[entrance] CSystemMngr (save-data check) skipped");
        g_entrance.state = 1;
        break;
    case 1:
        /* FUN_088d35a4: CEntranceMngr joins the layer list and initialises. */
        mngr_init(&g_mngr);
        g_entrance.state = 2;
        break;
    case 2:
        mngr_update(&g_mngr, dt);
        if (g_mngr.done) /* FUN_088d3674 -> FUN_088d317c: the next screens */
            return 0;
        break;
    }
    return 1;
}
