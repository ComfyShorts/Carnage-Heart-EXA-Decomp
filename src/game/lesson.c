/* See lesson.h. Hand-written from the functions named there; the cards' 3D drum
 * (the rows past two from the middle, drawn receding on a cylinder) is left out and the
 * near rows are drawn flat (docs/story.md, "Lessons"). */
#include "lesson.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "font.h"
#include "gamedata.h"
#include "gu.h"
#include "pad.h"
#include "psp_shim.h"
#include "savedata.h"
#include "scene.h"
#include "sound.h"
#include "storydb.h"
#include "ui.h"

/* ---- data in the EBOOT (splat addresses) ---- */

#define CREDITS 0x0047CF00u      /* 0x08c80f00: CLectureCredit, 49 records */
#define CREDIT_SIZE 0x24u
#define LESSON_SLOTS 0x00479190u /* 0x08c7d190: lesson number - 1 -> card, 0x3C words */
#define STR_LESSON 0x004766B0u   /* CBattleModeMngr's strings (FUN_088af824) */
#define STR_TEST 0x00466D50u     /* "No" */
#define KEY_ICONS 0x003F2698u    /* 0x08bf6698: CKeyPicture's symbols on cm_btn_p, u v w h */

/* A record: +0 the lesson number, +4 its title, +8 the bits a done card has, +0xC a
 * battle, +0x10 the state byte (filled in when read), +0x14 the script file, +0x18 the
 * first script, +0x1C the last, +0x20 the file set. */
enum { C_NUMBER = 0x0, C_TITLE = 0x4, C_REQUIRED = 0x8, C_FILE = 0x14, C_FIRST = 0x18, C_LAST = 0x1C };

/* The parts (CLectureCard::Initialize FUN_08933744, CLectureMngr::Initialize). */
enum {
    PART_LC_CUR = 0xB6,
    PART_LC_WND_B = 0xB7, /* a card not done */
    PART_LC_WND_L = 0xB8, /* a card done */
    PART_LC_ICO_1 = 0xB9, /* the lesson icon, not done / done */
    PART_LC_ICO_2 = 0xBA,
    PART_LC_ICO_3 = 0xBB, /* the battle icon, not done / done */
    PART_LC_ICO_4 = 0xBC,
    PART_LC_STR_N = 0xBD, /* "not done" / "done" beside them */
    PART_LC_STR_C = 0xBE,
    PART_CM_BTN_P = 0xE0, /* the Stop Lesson button */
};

static uint32_t pointer(uint32_t p) { return p >= 0x08804000u ? p - 0x08804000u : p; }

static uint32_t credit_word(int card, uint32_t off)
{
    return gamedata_word(CREDITS + CREDIT_SIZE * (uint32_t)card + off, 0);
}

static const char *credit_string(int card, uint32_t off)
{
    uint32_t p = pointer(credit_word(card, off));
    return p ? gamedata_string(p) : NULL;
}

static const char *table_string(uint32_t table, int id)
{
    uint32_t p = pointer(gamedata_word(table + 4u * (uint32_t)id, 0));
    return p ? gamedata_string(p) : "";
}

/* ---- CLectureCredit ---- */

int lesson_card_of(int number)
{
    if (number < 1 || number > 0x3C)
        return -1;
    int card = (int)gamedata_word(LESSON_SLOTS + 4u * (uint32_t)(number - 1), 0xFFFFFFFFu);
    return card >= 0 && card < LESSON_CARDS ? card : -1;
}

const char *lesson_title(int card)
{
    const char *s = card >= 0 && card < LESSON_CARDS ? credit_string(card, C_TITLE) : NULL;
    return s ? s : "";
}

bool lesson_done(int card)
{
    uint32_t need = credit_word(card, C_REQUIRED);
    return ((uint32_t)savedata_lesson_state(card) & need) == need;
}

bool lesson_listed(int card) { return savedata_lesson_offered(card) != 0; }

/* FUN_08934254 / FUN_08934094: the listed cards in table order. */
static int listed_count(void)
{
    int n = 0;
    for (int c = 0; c < LESSON_CARDS; c++)
        n += lesson_listed(c);
    return n;
}

static int listed_card(int index)
{
    if (index < 0)
        return -1;
    for (int c = 0; c < LESSON_CARDS; c++)
        if (lesson_listed(c) && index-- == 0)
            return c;
    return -1;
}

void lesson_var(int op, int number, int value)
{
    int card = lesson_card_of(number);
    if (card < 0)
        return;
    uint32_t offered = (uint32_t)(value & 0xF0) >> 4;
    int state = value & 0xF;
    if (op == 5) {
        savedata_set_lesson_offered(card, offered);
        savedata_set_lesson_state(card, state);
    } else if (op == 6) {
        savedata_set_lesson_offered(card, offered | savedata_lesson_offered(card));
        savedata_set_lesson_state(card, state | savedata_lesson_state(card));
    }
}

bool lesson_completed_text(int number, int value, char *out, size_t n)
{
    int card = lesson_card_of(number);
    if (card < 0 || value != 1 || !lesson_done(card))
        return false;
    const char *fmt = table_string(STR_LESSON, 0x3A); /* "Completed Lesson: %s" */
    const char *pct = strstr(fmt, "%s");
    if (!pct)
        snprintf(out, n, "%s", lesson_title(card));
    else
        snprintf(out, n, "%.*s%s%s", (int)(pct - fmt), fmt, lesson_title(card), pct + 2);
    return true;
}

int lesson_test(int test, int number, int value)
{
    if (number < 1 || number > 0x3C)
        return 0;
    uint32_t offered = savedata_lesson_offered(number - 1);
    uint32_t state = (uint32_t)savedata_lesson_state(number - 1);
    uint32_t v = (uint32_t)value;
    bool same = (offered & 0xF0) == (v & 0xF0) && (state & 0xF) == (v & 0xF);
    switch (test) {
    case 1: return same ? 1 : 2;
    case 2: return same ? 2 : 1;
    case 7: return (offered & v & 0xF0) == 0 && (state & v & 0xF) == 0 ? 2 : 1;
    default: return 0;
    }
}

static bool g_day_done;

/* FUN_089348f0: the day's cards (those of the step's script file) are all done. */
static bool day_done(void)
{
    int step = savedata_story_step();
    const char *file = storydb_lecture_part(step) ? storydb_script(step) : NULL;
    if (!file)
        return false;
    int cards = 0;
    for (int c = 0; c < LESSON_CARDS; c++) {
        const char *f = credit_string(c, C_FILE);
        if (!f || strcmp(f, file))
            continue;
        cards++;
        if (!lesson_done(c))
            return false;
    }
    return cards > 0;
}

/* The lesson's end (ReportEnd and the Stop Lesson answer, FUN_0893621c): no lesson
 * taken, and on a mission step the mission and its briefing script back. */
static void lesson_clear(void)
{
    savedata_set_lesson_mode(0);
    savedata_set_lesson_current(-1);
    int step = savedata_story_step();
    if (!storydb_lecture_part(step)) {
        savedata_set_story_battle(savedata_story_saved_battle());
        savedata_set_story_saved_battle(-1);
        savedata_story_field_reset();
        if (!savedata_story_briefed() && storydb_script(step) && storydb_script_id(step)) {
            savedata_set_story_script(storydb_script(step));
            savedata_story_field_hide(13, 0, storydb_script_id(step));
        }
    }
}

void lesson_report_end(const char *id)
{
    if (!id)
        return;
    for (int c = 0; c < LESSON_CARDS; c++) {
        const char *last = credit_string(c, C_LAST);
        if (!lesson_listed(c) || !last || strcmp(last, id))
            continue;
        lesson_clear();
        if (day_done()) {
            g_day_done = true;
            SDL_Log("[lesson] %s ends the day's lessons", id);
        } else {
            SDL_Log("[lesson] %s ends lesson %d (%s)", id, (int)credit_word(c, C_NUMBER), lesson_title(c));
        }
    }
    /* FUN_08934748: a mission's briefing script has ended. */
    if (!strncmp(id, "mission", 7) && strlen(id) == 9 && id[7] >= '0' && id[7] <= '1')
        savedata_set_story_briefed(true);
}

bool lesson_take_day_done(void)
{
    bool d = g_day_done;
    g_day_done = false;
    return d;
}

/* ---- CLectureMngr ---- */

enum { LS_LIST, LS_ASK_START, LS_ASK_STOP, LS_SCRIPT };

static struct {
    int state;
    int top, column, row; /* +0x23F4 the middle row, +0x23F8 the column, +0x23FC the cursor's row from it */
    Tween scroll;         /* +0x2400: the rows' slide, 51 or -51 to 0 */
    float pulse_t;
    UiAsk ask;
} m;

static int cursor_index(void) { return (m.top + m.row) * 2 + m.column; }

/* A hidden script on the screen (menu 0x14) plays once (FUN_08ac27cc). */
static bool play_field(void)
{
    const char *hidden = savedata_story_field_seek(1, 0);
    if (!hidden || !*hidden)
        return false;
    char id[0x41];
    snprintf(id, sizeof id, "%s", hidden);
    savedata_story_field_clear(1, 0);
    if (!scene_start(savedata_story_script(), id))
        return false;
    m.state = LS_SCRIPT;
    return true;
}

void lesson_screen_open(void)
{
    memset(&m, 0, sizeof m);
    int count = listed_count();
    int current = savedata_lesson_current();
    /* The cursor on the lesson taken, else on the first card not done. */
    for (int i = 0; i < count; i++) {
        int c = listed_card(i);
        bool here = current == -1 ? !lesson_done(c) : (int)credit_word(c, C_NUMBER) == current;
        if (here) {
            m.top = i / 2;
            m.column = i & 1;
            break;
        }
    }
    SDL_Log("[lesson] the Lesson card screen: %d cards, lesson %d taken", count, current);
    play_field();
}

/* FUN_08936730: the card's first script on this screen. */
static void start_card(void)
{
    int card = listed_card(cursor_index());
    if (card < 0)
        return;
    const char *file = credit_string(card, C_FILE), *first = credit_string(card, C_FIRST);
    savedata_set_lesson_mode(savedata_lesson_state(card) == 0 || credit_word(card, C_REQUIRED) == 0 ? 2 : 1);
    if (!file || !first || !scene_start(file, first)) {
        savedata_set_lesson_mode(0);
        return;
    }
    savedata_story_set_2b28(-1);
    if (!storydb_lecture_part(savedata_story_step()) && savedata_story_saved_battle() == -1)
        savedata_set_story_saved_battle(savedata_story_battle());
    savedata_set_story_battle(-1);
    savedata_set_story_script(file);
    savedata_set_lesson_current((int)credit_word(card, C_NUMBER));
    savedata_story_field_reset();
    m.state = LS_SCRIPT;
    SDL_Log("[lesson] lesson %d (%s) from %s, %s", savedata_lesson_current(), lesson_title(card), first,
            savedata_lesson_mode() == 1 ? "a review" : "the first time");
}

static void stop_lesson(void)
{
    lesson_clear();
    savedata_story_field_reset();
    if (storydb_lecture_part(savedata_story_step()))
        savedata_set_story_battle(-1);
    savedata_story_set_2b28(-1);
    SDL_Log("[lesson] the lesson was stopped");
}

static void move_rows(int top)
{
    tween_start(&m.scroll, top > m.top ? 51.0f : -51.0f, 0.0f, 0.3f);
    m.top = top;
}

/* FUN_0893834c */
static int update_list(void)
{
    int count = listed_count();
    int before = cursor_index();
    if (pad_pressed(PSP_CTRL_CIRCLE)) {
        int card = listed_card(cursor_index());
        if (card < 0)
            return 1;
        int mode = savedata_lesson_mode();
        if (mode == 1 || mode == 2) {
            bool same = (int)credit_word(card, C_NUMBER) == savedata_lesson_current();
            ui_ask_open(&m.ask, table_string(STR_LESSON, same ? 0x1B : 0x19), design_string(5, 1),
                        table_string(STR_TEST, 32), 0);
            sound_se(SE_DECIDE, 1.0f);
            m.state = LS_ASK_START;
        } else {
            sound_se(SE_DECIDE, 1.0f);
            start_card();
        }
        return 1;
    }
    if (pad_pressed(PSP_CTRL_CROSS)) {
        sound_se(SE_CANCEL, 1.0f);
        return 0;
    }
    if (pad_pressed(PSP_CTRL_SQUARE)) {
        if (savedata_lesson_current() != -1) {
            ui_ask_open(&m.ask, table_string(STR_LESSON, 0x1C), design_string(5, 1), table_string(STR_TEST, 32), 1);
            sound_se(SE_DECIDE, 1.0f);
            m.state = LS_ASK_STOP;
        }
        return 1;
    }
    if (pad_pressed(PSP_CTRL_LEFT) || pad_pressed(PSP_CTRL_RIGHT)) {
        if (m.column == 0) {
            if ((m.top + m.row) * 2 + 1 <= count - 1)
                m.column = 1;
        } else {
            m.column = 0;
        }
    } else if (pad_repeat(PSP_CTRL_UP)) {
        if (m.top + m.row < 1) { /* past the top: round to the bottom on a fresh press */
            if (pad_pressed(PSP_CTRL_UP) && count >= 3) {
                int last = (count - 1) / 2;
                m.row = 0;
                move_rows(last);
                if (cursor_index() > count - 1)
                    m.column = 0;
            }
        } else if (m.row < 0) {
            move_rows(m.top - 1);
        } else {
            m.row--;
        }
    } else if (pad_repeat(PSP_CTRL_DOWN)) {
        if (count - 1 < cursor_index() + 2) { /* nothing below: round to the top on a fresh press */
            if (pad_pressed(PSP_CTRL_DOWN) && count >= 3) {
                m.row = 0;
                move_rows(0);
            }
        } else if (m.row > 0) {
            move_rows(m.top + 1);
        } else {
            m.row++;
        }
    }
    if (cursor_index() != before) {
        sound_se(9, 1.0f);
        m.pulse_t = 0.0f;
    }
    return 1;
}

int lesson_screen_update(float dt)
{
    tween_step(&m.scroll, dt);
    m.pulse_t += dt;
    switch (m.state) {
    case LS_LIST:
        return update_list();
    case LS_ASK_START:
    case LS_ASK_STOP: {
        int r = ui_ask_update(&m.ask, dt);
        if (r < 0)
            return 1;
        int was = m.state;
        m.state = LS_LIST;
        if (r == 0) {
            sound_se(SE_CANCEL, 1.0f);
        } else if (was == LS_ASK_START) {
            sound_se(SE_DECIDE, 1.0f);
            start_card();
        } else {
            sound_se(SE_DECIDE, 1.0f);
            stop_lesson();
        }
        return 1;
    }
    case LS_SCRIPT:
        if (!scene_update(dt)) {
            scene_stop();
            m.state = LS_LIST;
            if (lesson_take_day_done()) { /* the day is over: back to the briefing */
                SDL_Log("[lesson] the day's lessons are done");
                g_day_done = true;
                return 0;
            }
            play_field();
        }
        return 1;
    }
    return 1;
}

/* CLectureCard::Draw (FUN_08933a90): the card's title and its icon with "done" or
 * "not done" beside it (FUN_08933d0c). */
static void draw_icon(int icon, int label, int x, int y)
{
    Picture *pi = ui_part(icon), *pl = ui_part(label);
    if (!pi)
        return;
    picture_draw(pi, x, y, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    if (pl) {
        int iw = picture_width(pi), ih = picture_height(pi);
        picture_draw(pl, x + iw + (iw - picture_width(pl)) / 2, y + (ih - picture_height(pl)) / 2, 0, BLEND_ALPHA,
                     0xFFFFFFFF, 0, 0);
    }
}

static void draw_card(int card, int x, int y)
{
    bool done = lesson_done(card);
    Picture *wnd = ui_part(done ? PART_LC_WND_L : PART_LC_WND_B);
    if (wnd)
        picture_draw(wnd, x, y, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    TextStyle st = text_style(0, 7, 12, 11, 0, 4, 0xFFFFFFFF, 0);
    ui_text(lesson_title(card), &st, x + 4, y + 6, 1.0f);
    uint32_t need = credit_word(card, C_REQUIRED), state = (uint32_t)savedata_lesson_state(card);
    int shift = 0;
    if (need & 2) {
        draw_icon(state & 2 ? PART_LC_ICO_4 : PART_LC_ICO_3, state & 2 ? PART_LC_STR_C : PART_LC_STR_N, x + 118,
                  y + 25);
        shift = 46;
    }
    if (need & 1)
        draw_icon(state & 1 ? PART_LC_ICO_2 : PART_LC_ICO_1, state & 1 ? PART_LC_STR_C : PART_LC_STR_N,
                  x - shift + 118, y + 25);
}

void lesson_screen_draw(void)
{
    sceGuEnable(GU_BLEND);
    Picture *back = ui_part(PART_BF_BACK);
    if (back)
        picture_draw(back, 0, 0, 0, BLEND_REPLACE, 0xFFFFFFFF, 0, 0);
    else
        ui_fill(0, 0, 480, 272, 0xFF404040);
    Picture *wnd = ui_part(PART_LC_WND_B);
    int cw = wnd ? picture_width(wnd) : 162, ch = wnd ? picture_height(wnd) : 51;
    int count = listed_count();
    int slide = (int)tween_value(&m.scroll);
    /* The rows two either side of the middle one (FUN_08938d80's 2D pass). */
    for (int i = 0; i < count; i++) {
        int row = i / 2;
        if (row < m.top - 2 || row > m.top + 2)
            continue;
        int x = 480 - 2 * cw + cw * (i & 1);
        int y = 0x88 - ch / 2 + ch * (row - m.top) + slide;
        draw_card(listed_card(i), x, y);
    }
    /* The cursor (CCursorWnd on lc_cur, 0x9F x 0x30). */
    Picture *cur = ui_part(PART_LC_CUR);
    if (cur && m.state != LS_SCRIPT)
        ui_frame(cur, (2 - m.column) * -0xA2 + 0x1E0, m.row * 0x33 + 0x6F, 0x9F, 0x30, 8, ui_pulse(m.pulse_t));
    /* The Stop Lesson button while a lesson is taken (CBtnWnd at (0xCE, 0)): "#s", the
     * square from cm_btn_p (CKeyPicture's rectangle 3), then the text. */
    if (savedata_lesson_current() != -1) {
        Picture *sheet = ui_part(PART_CM_BTN_P);
        uint32_t r = gamedata_word(KEY_ICONS + 4u * 3u, 0);
        int iw = (int)(r >> 16 & 0xFF);
        if (sheet && iw)
            picture_draw_rect(sheet, 0xCE, 0, 0, (int)(r & 0xFF), (int)(r >> 8 & 0xFF), iw, (int)(r >> 24), BLEND_ALPHA,
                              0xFFFFFFFF, 0, 0);
        TextStyle st = text_style(0, 6, 12, 12, 0, 0, 0xFFFFFFFF, 0);
        const char *s = table_string(STR_LESSON, 0x17); /* "#sStop Lesson." */
        ui_text(!strncmp(s, "#s", 2) ? s + 2 : s, &st, 0xCE + iw + 2, 5, 1.0f);
    }
    if (m.state == LS_ASK_START || m.state == LS_ASK_STOP)
        ui_ask_draw(&m.ask);
    if (m.state == LS_SCRIPT)
        scene_draw();
    else
        ui_help_draw(table_string(STR_LESSON, 0x18));
    sceGuEnable(GU_TEXTURE_2D);
}
