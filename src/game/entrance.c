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
#include "sound.h"
#include "music.h"
#include "movie.h"
#include "picture.h"
#include "psp_imports.h"
#include "psp_shim.h"
#include "render.h"
#include "savedata.h"
#include "ui.h"

#define MENU_TWEEN_TIME UI_TWEEN_TIME
#define part ui_part

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
        sound_se(SE_CURSOR, 1.0f);
}

/* FUN_0888f870, simplified: the cursor highlight breathes between 0.3 and 1. */
static float menu_pulse(const Wnd *w)
{
    float s = sinf(w->pulse_t * 2.0f * (float)M_PI / 1.2f - (float)M_PI / 2.0f);
    return 0.3f + 0.7f * (s + 1.0f) * 0.5f;
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
        ui_quad(w->x, y, bw, bh, color);
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
            ui_quad(w->x, y, picture_width(cursor), picture_height(cursor), (uint32_t)a << 24 | 0xFFFFFF);
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
    PHASE_LOAD = 6,     /* Continue: the save list (CSystemMngr type 3); stand-in, see below */
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
    UiAsk ask;          /* +0x74: phase 9's question */
    bool idle_warned, stop_warned;
    /* Phase 6, the port's stand-in for CSystemMngr's load screens. */
    Wnd load_wnd;       /* an empty stack entry for the phase */
    UiMenu load_menu;
    int load_state;
    const char *load_msg;
    bool loaded;        /* the entrance ended on a loaded Battle Data save */
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
    m->load_wnd = (Wnd){.kind = WND_COLOR, .visible = false};
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
        music_title(); /* FUN_0888c718 */
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
    case PHASE_LOAD: /* FUN_088d438c(6) + CSystemMngr::Initialize(3) */
        stack_push(&m->stack, &m->load_wnd, PHASE_LOAD, arg);
        m->load_state = 0;
        m->load_msg = NULL;
        savedata_list_start();
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
        sound_se(SE_DECIDE, 1.0f);
        m->idle_vcount = sceDisplayGetVcount();
        if (m->top_menu.cursor == 0) {
            SDL_Log("[entrance] Start Game chosen: opening the mode menu");
            menu_close(&m->top_menu, 1);
            mngr_open(m, PHASE_MODE_MENU, 0);
        } else if (m->top_menu.cursor == 1) {
            SDL_Log("[entrance] Continue chosen: the Battle Data list");
            mngr_open(m, PHASE_LOAD, 0);
        } else if (!m->stop_warned) {
            /* 2 Network Play: phase 6 with the network screens. */
            SDL_Log("[entrance] Network Play chosen: isn't ported");
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

/* Phase 6, Continue. The game opens CSystemMngr type 3: a menu of the kinds of save
 * (mode 4), then CSaveMngr's own list of that kind's files (exec type 1, drawn by
 * UpdateSelectFileList FUN_08a676f0). The port lists the Battle Data files (Story Mode
 * isn't ported) in a plain menu, loads the chosen one (state 0x2B) and opens Battle
 * Mode on it. The texts here are the port's. */
#define LOAD_X 150
#define LOAD_Y 64

static void load_build(EntranceMngr *m)
{
    const SaveListEntry *list;
    int n = savedata_list(&list);
    ui_menu_init(&m->load_menu, LOAD_X + 10, LOAD_Y + 24, 24 - 5, 12,
                 text_style(0, 6, 12, 12, 0, 0, 0xFF303030, 0));
    static char names[UI_MENU_MAX][SAVE_FILE_MAX];
    for (int i = 0; i < n && i < UI_MENU_MAX; i++) {
        snprintf(names[i], sizeof names[i], "%.*s", (int)strlen(list[i].file) - 4, list[i].file);
        UiMenuItem *it = ui_menu_add(&m->load_menu, names[i], i, 0, PART_BF_MENB4, PART_BF_MENL4);
        it->disabled = list[i].broken;
    }
    if (n > 0) {
        ui_menu_open(&m->load_menu);
        m->load_msg = "Choose the Battle Data to load.";
    } else {
        m->load_msg = savedata_result() ? "The save data couldn't be read." : "There is no Battle Data.";
    }
}

static void phase_load(EntranceMngr *m, float dt)
{
    switch (m->load_state) {
    case 0: /* the file list */
        if (!savedata_list_update()) {
            load_build(m);
            m->load_state = 1;
        }
        break;
    case 1: /* choosing */
        ui_menu_update(&m->load_menu, dt);
        if (m->load_menu.count)
            ui_menu_input(&m->load_menu);
        if (pad_pressed(PSP_CTRL_CROSS)) {
            sound_se(SE_CANCEL, 1.0f);
            ui_menu_close(&m->load_menu);
            mngr_pop_phase(m);
            m->top_menu.active = true;
        } else if (pad_pressed(PSP_CTRL_CIRCLE) && m->load_menu.count) {
            sound_se(SE_DECIDE, 1.0f);
            const SaveListEntry *list;
            savedata_list(&list);
            const SaveListEntry *e = &list[m->load_menu.cursor];
            if (e->broken) {
                m->load_msg = "This data is damaged.";
            } else {
                savedata_load_start(e->file);
                m->load_msg = "Loading...";
                m->load_state = 2;
            }
        }
        break;
    case 2: /* loading */
        ui_menu_update(&m->load_menu, dt);
        if (savedata_io_update())
            break;
        if (savedata_result() == 0) {
            /* The load leaves game mode 2: straight to Battle Mode. */
            m->selection = 1;
            m->loaded = true;
            m->done = true;
        } else {
            m->load_msg = "The save data couldn't be read.";
            m->load_state = 1;
        }
        break;
    }
}

/* The dialog class (FUN_088c0ba0) isn't in the export; its "No" is taken from
 * CBattleTestString's table (0x08c6ad50, string 32), "Yes" from CDesignString 5. */
static const char *k_no(void)
{
    uint32_t p = gamedata_word(0x00466D50u + 4u * 32u, 0);
    return p ? gamedata_string(p >= 0x08804000u ? p - 0x08804000u : p) : NULL;
}

/* FUN_088d5510 */
static void phase_mode_menu(EntranceMngr *m)
{
    if (m->closing_mode) {
        if (menu_anim_done(&m->mode_menu))
            mngr_pop_phase(m);
        return;
    }
    if (m->ask.open) {
        /* Phase 9 (FUN_088d5eb8): circle on Yes goes on to phase 7, the player-name
         * keyboard (sceUtility OSK); No or cross stays on the mode menu. */
        int r = ui_ask_update(&m->ask, 1.0f / 60.0f);
        if (r == 1) {
            /* Phase 7 (FUN_088d5a70) saves the name, sets game mode 2 and fades out
             * (phase 0xB). There's no on-screen keyboard here: the name isn't taken. */
            SDL_Log("[entrance] Battle Mode: player-name entry (OSK) skipped; opening Battle Mode");
            m->done = true;
        }
        if (r >= 0)
            m->mode_menu.active = true;
        return;
    }
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        m->selection = m->mode_menu.cursor;
        sound_se(m->selection == 1 ? SE_START : SE_DECIDE, 1.0f);
        if (m->selection == 1 && savedata_story_completed()) {
            /* FUN_0889c400: the system data's +0x74, Story Mode completed. Then
             * there's no question: straight to phase 7. */
            SDL_Log("[entrance] Battle Mode: Story Mode completed, no question; player-name entry (OSK) skipped");
            m->done = true;
            return;
        }
        if (m->selection == 1) {
            /* Not completed: the game asks first (phase 9, FUN_088d4c84). */
            ui_ask_open(&m->ask, design_string(0x11, 1), design_string(0x5, 1), k_no(), 0);
            m->mode_menu.active = false;
            return;
        }
        if (!m->stop_warned) {
            SDL_Log("[entrance] %s chosen: phase %s isn't ported",
                    m->selection == 0 ? "Story Mode" : "Battle Mode", m->selection == 0 ? "0xC" : "7/9");
            m->stop_warned = true;
        }
    } else if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
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
    case PHASE_MOVIE: /* FUN_088d5c20: plays it here, holding the game thread */
        movie_play("movie/che_op.pmf", MOVIE_SKIP_ANY);
        mngr_pop_phase(m);
        mngr_open(m, PHASE_IDLE, 0);
        break;
    case PHASE_TOP_MENU:
        phase_top_menu(m);
        break;
    case PHASE_MODE_MENU:
        phase_mode_menu(m);
        break;
    case PHASE_LOAD:
        phase_load(m, dt);
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
    ui_ask_draw(&m->ask);
    if (phase == PHASE_LOAD) {
        int rows = m->load_menu.count > 0 ? m->load_menu.count : 1;
        ui_frame(ui_part(PART_BF_SWND4), LOAD_X, LOAD_Y, 180, 34 + 19 * rows, 8, 1.0f);
        const char *caption = gamedata_string(0x00437D64u); /* "Battle Data" (FUN_08ab82f0) */
        if (caption)
            ui_text(caption, &m->load_menu.style, LOAD_X + 12, LOAD_Y + 7, 1.0f);
        ui_menu_draw(&m->load_menu);
        if (m->load_msg)
            ui_help_draw(m->load_msg);
    }
}

/* ---- the entrance screen (FUN_088d2d30) ---- */

static struct {
    int state;
    int fileset;
    bool fileset_open;
    bool checked;
} g_entrance;

void entrance_start(void)
{
    g_entrance.state = 0;
    SDL_Log("[entrance] screen created (root screen state 3)");
}

/* FUN_088d36e4 */
int entrance_selection(void)
{
    return g_mngr.selection;
}

bool entrance_loaded(void)
{
    return g_mngr.loaded;
}

int entrance_update(float dt)
{
    switch (g_entrance.state) {
    case 0:
        /* FUN_088d3560: becomes a CRender node at priority 2. */
        render_add_node(render_instance(), 2, mngr_draw, &g_mngr);
        if (!g_entrance.fileset_open) { /* kept open: the battle setup uses its art too */
            g_entrance.fileset = fileset_open("entrance", &g_default_heap, 0, 0);
            g_entrance.fileset_open = g_entrance.fileset >= 0;
            if (!g_entrance.fileset_open)
                SDL_Log("[entrance] OpenFileSet(\"entrance\") failed");
        }
        if (!font_init())
            SDL_Log("[entrance] fonts unavailable: menu text won't be drawn");
        /* FUN_088d30d8: CSystemMngr in mode 0xD, CSaveMngr's start-up check (type 4,
         * exec 3), then state 1 runs it until it's done. Only the first time: back
         * from Battle Mode the system data is already in. */
        if (!g_entrance.checked)
            savedata_check_start();
        g_entrance.state = 1;
        break;
    case 1:
        if (savedata_check_update())
            break;
        g_entrance.checked = true;
        /* FUN_088d35a4: CEntranceMngr joins the layer list and initialises. */
        mngr_init(&g_mngr);
        g_entrance.state = 2;
        break;
    case 2:
        mngr_update(&g_mngr, dt);
        if (g_mngr.done) { /* FUN_088d3674 -> FUN_088d317c: the next screens */
            render_remove_node(render_instance(), &g_mngr);
            return 0;
        }
        break;
    }
    return 1;
}
