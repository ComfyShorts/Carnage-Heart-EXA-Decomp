/* See story.h. Hand-written from:
 *   FUN_088d3d80 modes 0 and 3 (the scene manager starting Story Mode);
 *   CTutorialFlow: ctor FUN_08ae0df0, setType FUN_08ae0f64, setChild FUN_08ae0f6c,
 *     loadPreset FUN_08ae1090, moveAdventurePart FUN_08ae1464,
 *     hideEventScriptBeforeBriefing FUN_08ae1668, the briefing FUN_08ae1024,
 *     Update FUN_08ae1938;
 *   CStoryMngr: Initialize FUN_08ab1dcc, update FUN_08ab2278 (states 0-3: the save
 *     question FUN_08ab26b8, the save FUN_08ab23f8, the title card FUN_08ab2894 with
 *     its fades FUN_08ab296c), draw FUN_08ab29b8 / FUN_08ab2ad0;
 *   the clear data: CTutorialFlow::Update state 3 and CScenarioClearSaveFlow. */
#include "story.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "font.h"
#include "heap.h"
#include "gamedata.h"
#include "pad.h"
#include "picture.h"
#include "psp_shim.h"
#include "render.h"
#include "savedata.h"
#include "scene.h"
#include "music.h"
#include "battle.h"
#include "battle_menu.h"
#include "sound.h"
#include "storydb.h"
#include "ui.h"

/* The message strings (FUN_088af824, the table at 0x08c7a6b0). */
#define MSG_TABLE 0x004766B0u
#define MSG_SAVE_PROGRESS 0x38 /* "Save your progress?" */
#define MSG_SAVE_CLEARED 0x39  /* "Save cleared data?" */
/* The question's Yes / No, as the entrance takes them (CDesignString 5,
 * CBattleTestString 32 at 0x08c6ad50). */
#define NO_STRING_PTR (0x00466D50u + 4u * 32u)

/* The file Story Data is saved to. The game's save (CSaveMngr type 1, exec 5) lets the
 * player pick or name the file; the port saves to the one loaded, else this. */
#define STORY_NEW_FILE "NEWDATA.CHE"

int g_story_step = -1;

static const char *ptr_string(uint32_t ptr_addr)
{
    uint32_t p = gamedata_word(ptr_addr, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

static const char *message(int id) { return ptr_string(MSG_TABLE + 4u * (uint32_t)id); }

/* ---- CTimeValue: a value moving from `from` to `to` over `length` seconds ---- */

typedef struct Fade {
    float from, to, length, t;
} Fade;

static void fade_start(Fade *f, float from, float to, float length) { *f = (Fade){from, to, length, 0.0f}; }
static bool fade_running(const Fade *f) { return f->t < f->length; }
static void fade_step(Fade *f, float dt)
{
    if (fade_running(f))
        f->t += dt;
}
static float fade_value(const Fade *f)
{
    if (f->from == f->to || f->t <= 0.0f)
        return f->t <= 0.0f ? f->from : f->to;
    if (f->t >= f->length)
        return f->to;
    return (f->to - f->from) * f->t / f->length + f->from;
}

/* ---- the children ---- */

typedef enum {
    CHILD_NONE,
    CHILD_ADVENTURE, /* slot 1: CStoryFlow */
    CHILD_BRIEFING,  /* slot 2: the briefing flow (CBriefingMngr) */
    CHILD_ENDING,    /* slot 3: the ending (FUN_0896864c) */
    CHILD_CLEAR,     /* slot 4: CScenarioClearSaveFlow */
} Child;

/* CStoryMngr's states (+0x20). */
enum { MNGR_ASK = 0, MNGR_SAVE = 1, MNGR_TITLE = 2, MNGR_SCRIPT = 3 };

static struct {
    bool running;
    bool node_added;
    bool in_hub;     /* the briefing hub (battle.c) draws itself */
    int type;      /* CTutorialFlow +0x18: 1 new, 2 continued */
    bool started;  /* the first child was made (Update's default case) */
    Child child;   /* +0x14 */
    bool child_done;

    /* CStoryMngr */
    int mngr;
    bool title;        /* IsTitleMode: a chapter card */
    Picture *chapter;  /* +0x3130 */
    Fade fade;         /* +0x3134.. */
    bool fading_out;   /* +0x28: ended, fading */
    UiAsk ask;         /* the dialog at +0x2FAC */
    bool saving;
    char script[0x40];
    char script_id[0x41];
    const char *note;  /* the stand-in screens' line */
    float t;
} g;

static void child_finish(void) { g.child_done = true; }

static void release_chapter(void)
{
    if (g.chapter)
        picture_release(g.chapter);
    g.chapter = NULL;
}

/* Saves Story Data (CSaveMngr type 1): to the file loaded or saved last. */
static void begin_save(void)
{
    savedata_save_start(SAVE_STORY, g_save.story_file[0] ? g_save.story_file : STORY_NEW_FILE);
    g.saving = true;
}

static void open_question(int msg)
{
    ui_ask_open(&g.ask, message(msg), design_string(5, 1), ptr_string(NO_STRING_PTR), 0);
}

/* FUN_08ab296c: the card fades in over half a second. */
static void title_show(void)
{
    fade_start(&g.fade, 0.0f, 1.0f, 0.5f);
    g.fading_out = false;
    g.mngr = MNGR_TITLE;
}

/* CTutorialFlow::moveAdventurePart (FUN_08ae1464) with CStoryMngr::Initialize: the
 * step's script, script id, chapter picture and title mode; `after_step` (the second
 * argument, 1 when a step has just ended) asks to save before a chapter card. */
static void move_adventure_part(bool after_step)
{
    int step = savedata_story_step();
    const char *script = storydb_script(step);
    const char *id = storydb_script_id(step);
    snprintf(g.script, sizeof g.script, "%s", script ? script : "");
    snprintf(g.script_id, sizeof g.script_id, "%s", id ? id : "");
    g.title = storydb_title(step);
    g.child = CHILD_ADVENTURE;
    g.child_done = false;
    g.t = 0.0f;
    release_chapter();
    if (!g.title) {
        /* CStoryMngr::Initialize: the scene player (CEventScriptWnd) on the step's
         * script, state 3. */
        g.mngr = MNGR_SCRIPT;
        g.note = "Scene";
        SDL_Log("[story] step %d: scene %s (%s), %s", step, g.script_id, g.script,
                storydb_progress(step, false) ? storydb_progress(step, false) : "");
        if (!scene_start(g.script, g.script_id))
            SDL_Log("[story] the scene couldn't start: circle goes on");
        return;
    }
    const char *pic = storydb_chapter_picture(step);
    g.chapter = pic ? picture_load(pic) : NULL;
    if (!g.chapter)
        SDL_Log("[story] step %d: chapter picture %s missing", step, pic ? pic : "(none)");
    SDL_Log("[story] step %d: chapter title %s", step, pic ? pic : "?");
    if (after_step) {
        g.mngr = MNGR_ASK;
        open_question(MSG_SAVE_PROGRESS);
    } else {
        title_show();
    }
}

/* The lesson and mission steps: hideEventScriptBeforeBriefing (FUN_08ae1668) keeps
 * the step's script in the save as the briefing's, then the briefing flow
 * (FUN_08ae1024 -> FUN_088926d0). */
static void move_briefing(void)
{
    int step = savedata_story_step();
    savedata_set_story_script(storydb_script(step));
    g.child = CHILD_BRIEFING;
    g.child_done = false;
    g.t = 0.0f;
    g.note = "Briefing";
    snprintf(g.script_id, sizeof g.script_id, "%s", storydb_script_id(step) ? storydb_script_id(step) : "");
    /* A lesson's first script is hidden on the hub (FUN_088df4b0 on menu 0, key 0). */
    if (storydb_lecture_part(step) && g.script_id[0])
        savedata_story_field_hide(0, 0, g.script_id);
    SDL_Log("[story] step %d: briefing for %s (%s)", step, g.script_id, savedata_story_script());
    /* CBriefingMngr: the Battle Mode screens with the hub's items (battle_menu.c). */
    battle_mode_start_story();
    g.in_hub = true;
}

/* After the last step: the ending, then the clear data (Update state 3). */
static void move_ending(void)
{
    g.child = CHILD_ENDING;
    g.child_done = false;
    g.t = 0.0f;
    g.note = "Ending";
    SDL_Log("[story] the ending");
}

static void move_clear_save(void)
{
    /* Update state 3: Story Mode completed in the system data, the story put back on
     * step 0x53 with mission 11's script as the briefing and battle 0x30. */
    savedata_set_story_completed(true);
    savedata_set_story_step(0x53);
    savedata_set_story_script("adv/mission11.chs");
    savedata_story_field_hide(0xD, 0, "mission11");
    savedata_set_story_battle(0x30);
    g.child = CHILD_CLEAR;
    g.child_done = false;
    g.saving = false;
    open_question(MSG_SAVE_CLEARED);
}

/* CTutorialFlow::Update after a child has ended: go on to the next step. */
static void next_step(void)
{
    if (savedata_story_step() >= STORY_STEP_LAST) {
        move_ending();
        return;
    }
    savedata_set_story_step(savedata_story_step() + 1); /* GoNextStep FUN_08a69700 */
    if (storydb_briefing(savedata_story_step()))
        move_briefing();
    else
        move_adventure_part(true);
}

/* ---- CStoryMngr::Update ---- */

static void adventure_update(float dt)
{
    fade_step(&g.fade, dt);
    switch (g.mngr) {
    case MNGR_ASK: { /* FUN_08ab26b8: Yes saves, No goes straight to the card */
        int r = ui_ask_update(&g.ask, dt);
        if (r == 1) {
            begin_save();
            g.mngr = MNGR_SAVE;
        } else if (r == 0) {
            title_show();
        }
        break;
    }
    case MNGR_SAVE: /* FUN_08ab23f8 */
        if (!savedata_io_update()) {
            g.saving = false;
            if (savedata_result() != 0)
                SDL_Log("[story] the save failed: 0x%08X", (unsigned)savedata_result());
            sound_se(SE_CANCEL, 1.0f); /* SE 0xB, as the game plays after the save */
            title_show();
        }
        break;
    case MNGR_TITLE: /* FUN_08ab2894: circle once the fade-in is over fades out over a second */
        if (g.fading_out) {
            if (!fade_running(&g.fade))
                child_finish();
        } else if (!fade_running(&g.fade) && pad_pressed(PSP_CTRL_CIRCLE)) {
            fade_start(&g.fade, 1.0f, 0.0f, 1.0f);
            g.fading_out = true;
        }
        break;
    case MNGR_SCRIPT: /* FUN_08ab24c8: until the script ends */
        g.t += dt;
        if (!scene_update(dt)) {
            scene_stop();
            music_stop(); /* FUN_08b34f44 when the script has ended */
            child_finish();
        }
        break;
    }
}

static void stand_in_update(float dt)
{
    g.t += dt;
    if (g.t > 0.25f && pad_pressed(PSP_CTRL_CIRCLE))
        child_finish();
}

static void clear_update(float dt)
{
    if (g.saving) {
        if (!savedata_io_update()) {
            g.saving = false;
            child_finish();
        }
        return;
    }
    int r = ui_ask_update(&g.ask, dt);
    if (r == 1)
        begin_save();
    else if (r == 0)
        child_finish();
}

/* ---- drawing ---- */

static TextStyle note_style(uint32_t color) { return text_style(0, 6, 12, 12, 0, 0, color, 0); }

static void draw(void *ctx)
{
    (void)ctx;
    if (!g.running || g.in_hub)
        return;
    ui_fill(0, 0, 480, 272, 0xFF000000);
    if (g.child == CHILD_ADVENTURE && g.title) {
        /* FUN_08ab2ad0: the picture at (0, 0), alpha from the fade. */
        if (g.chapter && g.mngr == MNGR_TITLE) {
            uint32_t a = (uint32_t)(fade_value(&g.fade) * 255.0f) & 0xFF;
            picture_draw(g.chapter, 0, 0, 0, BLEND_ALPHA, (a << 24) | 0xFFFFFF, 0, 0);
        }
    } else if (g.child == CHILD_ADVENTURE) {
        scene_draw(); /* FUN_08ab2c58 */
    } else if (g.child != CHILD_NONE && g.child != CHILD_CLEAR) {
        /* The stand-in until the ending is written. */
        TextStyle st = note_style(0xFFFFFFFF);
        char line[160];
        int step = savedata_story_step();
        const char *prog = storydb_progress(step, false);
        snprintf(line, sizeof line, "%s  (step %d)", prog ? prog : "", step);
        ui_text(line, &st, 24, 100, 1.0f);
        snprintf(line, sizeof line, "%s: %s", g.note ? g.note : "", g.script_id);
        ui_text(line, &st, 24, 120, 1.0f);
        TextStyle dim = note_style(0xFF909090);
        ui_text("Not ported yet. Circle: go on.", &dim, 24, 150, 1.0f);
    }
    ui_ask_draw(&g.ask);
}

/* ---- the flow ---- */

static void begin(int type)
{
    memset(&g.ask, 0, sizeof g.ask);
    release_chapter();
    g.running = true;
    g.type = type;
    g.started = false;
    g.child = CHILD_NONE;
    g.child_done = false;
    g.saving = false;
    /* moveAdventurePart's sound bank (FUN_08ae179c): the story effects. */
    if (!sound_bank_open(SE_BANK_ADV))
        SDL_Log("[story] the story sound bank couldn't be opened");
    if (!g.node_added)
        render_add_node(render_instance(), 2, draw, &g);
    g.node_added = true;
}

void story_start(bool loaded)
{
    if (!loaded) {
        /* CCarnageHeart::Reinitialize, then FUN_088d3d80 mode 0. */
        savedata_new_game();
        savedata_story_new();
    }
    SDL_Log("[story] Story Mode: %s (step %d)", loaded ? "continued" : "a new game", savedata_story_step());
    begin(loaded ? 2 : 1);
}

void story_start_at(int step)
{
    /* Straight from start-up there is no entrance: open its file set for the fonts
     * and interface parts, as the entrance does. */
    if (fileset_open("entrance", &g_default_heap, 0, 0) < 0)
        SDL_Log("[story] OpenFileSet(\"entrance\") failed");
    font_init();
    story_start(false);
    savedata_story_preset();
    /* The scenes before it, for their effects only (the mission, hidden scripts,
     * stock and menus), as if they had been played. */
    int applied = 0;
    for (int i = 0; i < step && i < STORY_STEPS; i++) {
        if (storydb_title(i) || !storydb_script(i) || !storydb_script_id(i))
            continue;
        if (!storydb_briefing(i)) {
            applied += scene_apply(storydb_script(i), storydb_script_id(i));
        } else if (storydb_lecture_part(i)) { /* as the lesson stand-in does */
            applied += scene_apply_file(storydb_script(i));
            savedata_story_field_clear(0, 0);
        }
    }
    if (applied)
        SDL_Log("[story] the %d scenes before step %d applied (mission %d)", applied, step,
                savedata_story_battle());
    savedata_set_story_step(step);
    g.started = true;
    if (storydb_briefing(step))
        move_briefing();
    else
        move_adventure_part(false);
}

int story_update(float dt)
{
    if (!g.running)
        return 0;
    if (!g.started) {
        /* Update's first pass: a new game loads the preset cards and goes to step 0;
         * a loaded one resumes its step, the briefing for lessons and missions. */
        g.started = true;
        if (g.type == 1 || g.type == 3) {
            if (!savedata_story_preset())
                SDL_Log("[story] the preset OKEs couldn't all be read");
            savedata_set_story_step(savedata_story_step() + 1);
            move_adventure_part(false);
        } else if (storydb_briefing(savedata_story_step())) {
            move_briefing();
        } else {
            move_adventure_part(false);
        }
        return 1;
    }
    switch (g.child) {
    case CHILD_ADVENTURE:
        adventure_update(dt);
        break;
    case CHILD_BRIEFING: /* FUN_08892c1c: until the step is done */
        if (!battle_mode_update(dt)) {
            g.in_hub = false;
            if (battle_menu_story_advanced())
                child_finish();
            else
                g.running = false; /* the hub can only end with the step done */
        }
        break;
    case CHILD_ENDING:
        stand_in_update(dt);
        break;
    case CHILD_CLEAR:
        clear_update(dt);
        break;
    case CHILD_NONE:
        break;
    }
    if (g.child_done) {
        g.child_done = false;
        if (g.child == CHILD_ENDING) {
            move_clear_save();
        } else if (g.child == CHILD_CLEAR) {
            g.running = false;
        } else {
            next_step();
        }
    }
    if (!g.running) {
        release_chapter();
        scene_stop();
        sound_bank_close(SE_BANK_ADV);
        if (g.node_added)
            render_remove_node(render_instance(), &g);
        g.node_added = false;
        SDL_Log("[story] Story Mode ended");
        return 0;
    }
    return 1;
}
