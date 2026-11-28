/* Interface pieces shared by the title and the Battle Mode screens.
 *
 *   - The parts cache (FUN_0888763c / FUN_088878e4): the game's 0x2F2 interface
 *     textures, named by the table at 0x08c79468 in the EBOOT (read at run time).
 *   - Tweens: the windows' CTimeValue members.
 *   - CTopMenuWnd (FUN_08ad7248 update, FUN_08ad75ec input, FUN_08ad77ec draw): a
 *     column of CBtnWnd buttons with a cursor, used by every Battle Mode menu.
 *   - The help window (FUN_088ffcac / FUN_088ffec0): one or two lines of text on
 *     sd_wind0.
 *   - A Yes/No question (stand-in layout, see docs/screens.md). */
#ifndef GAME_UI_H
#define GAME_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "font.h"
#include "picture.h"

/* ---- parts ---- */

enum {
    PART_BF_BACK = 0x00,
    PART_BF_NOENTRY0 = 0x05,
    PART_BF_MENB4 = 0x0C, /* CTopMenuWnd button */
    PART_BF_MENL4 = 0x0D, /* its cursor */
    PART_BF_TTL0 = 0x12,  /* title banners bf_ttl0..6 */
    PART_BF_MPI00 = 0x19, /* map previews bf_mpi00.. */
    PART_SD_WIND0 = 0x51,
    PART_BF_MPWIND = 0x58,
    PART_BM_SELEC = 0x5A,
    PART_TI_MENL0 = 0x5D,
    PART_TI_MENB0 = 0x5E,
    PART_TI_BACK = 0x5F,
    PART_TI_TITLE = 0x60,
    PART_LOGO_A = 0x71,
    PART_BF_CUR0 = 0x81,
    PART_BF_NEW00 = 0x85,
    PART_BF_SCMB1 = 0x87, /* Battle Mode top menu buttons bf_scmb1..6 */
    PART_BF_SCML1 = 0x8D, /* and their cursors bf_scml1..6 */
    PART_BF_TTL1_PIC = 0x93,
    PART_BF_SWND4 = 0x9B,
    PART_OP_WIND2 = 0x42,
    PART_COUNT = 0x2F2,
};

/* The texture for a parts index, loaded on first use; NULL if it can't be. */
Picture *ui_part(int index);

/* ---- tweens ----
 * An eased move from `from` to `to` over `duration` seconds:
 * value = (1 + e^-4 - e^(-4t/d)) * (to - from) + from. */

typedef struct Tween {
    float from, to, duration, t;
    bool active;
} Tween;

#define UI_TWEEN_TIME (1.0f / 6.0f) /* DAT_08be7bc4 */

void tween_start(Tween *tw, float from, float to, float duration);
float tween_value(const Tween *tw);
bool tween_done(const Tween *tw);
void tween_step(Tween *tw, float dt);

/* ---- drawing ---- */

/* A textured 4-vertex strip in through mode (vertex type 0x80011E), the whole
 * texture bound before. */
void ui_quad(int x, int y, int w, int h, uint32_t color);

/* A picture at (x, y), alpha 0-1, additive or alpha blended. */
void ui_pic(Picture *p, int x, int y, float alpha, bool additive);

/* A picture stretched to w x h as a nine-slice with `border` pixels kept. */
void ui_frame(Picture *p, int x, int y, int w, int h, int border, float alpha);

void ui_fill(int x, int y, int w, int h, uint32_t abgr);

/* Text in the given style with its colour's alpha scaled; returns the width. */
int ui_text(const char *s, const TextStyle *st, int x, int y, float alpha);
int ui_text_width(const char *s, const TextStyle *st);

/* The cursor's breathing highlight (FUN_0888f870, simplified): 0.3 .. 1. */
float ui_pulse(float t);

/* ---- CTopMenuWnd ---- */

#define UI_MENU_MAX 12

typedef struct UiMenuItem {
    const char *text;
    int id;          /* CBtnWnd user param 0 */
    int help;        /* user param 1: the help string's id */
    int button, cursor; /* parts indices */
    bool disabled, is_new;
    const char *value; /* drawn right-aligned in the button, or NULL */
} UiMenuItem;

typedef struct UiMenu {
    int x, y, pitch;
    int text_x;      /* text offset in the button */
    TextStyle style;
    uint32_t selected_color; /* text colour on the selected button */
    int value_x;     /* where item values go, from the button's left; 0: right-aligned inside */
    UiMenuItem item[UI_MENU_MAX];
    int count, cursor;
    bool active;     /* takes input (top of the window stack) */
    Tween fade, slide;
    float pulse_t;   /* seconds since the cursor moved */
} UiMenu;

void ui_menu_init(UiMenu *m, int x, int y, int pitch, int text_x, TextStyle style);
UiMenuItem *ui_menu_add(UiMenu *m, const char *text, int id, int help, int button, int cursor);
void ui_menu_open(UiMenu *m);
void ui_menu_close(UiMenu *m);
void ui_menu_update(UiMenu *m, float dt);
/* Up and down: hold repeats, a fresh press past either end wraps. Returns true when
 * the cursor moved (the game plays sound 9). */
bool ui_menu_input(UiMenu *m);
void ui_menu_draw(const UiMenu *m);
int ui_menu_id(const UiMenu *m); /* the id under the cursor */

/* ---- help window ---- */

void ui_help_draw(const char *text);

/* ---- Yes/No question ---- */

typedef struct UiAsk {
    const char *question, *yes, *no;
    int cursor; /* 0 yes, 1 no */
    bool open;
    Tween fade;
    float pulse_t;
} UiAsk;

void ui_ask_open(UiAsk *a, const char *question, const char *yes, const char *no, int cursor);
/* Returns 1 for yes, 0 for no (circle on No or cross), -1 while undecided. */
int ui_ask_update(UiAsk *a, float dt);
void ui_ask_draw(const UiAsk *a);

#endif
