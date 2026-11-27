/* See battle.h and docs/battle.md. */
#include "battle.h"
#include "battle_sim.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "font.h"
#include "gamedata.h"
#include "gu.h"
#include "heap.h"
#include "packfile.h"
#include "pad.h"
#include "pgchip.h"
#include "picture.h"
#include "psp_imports.h"
#include "psp_shim.h"
#include "render.h"

#define DEG (3.14159265f / 180.0f)

/* ---- the preset designs ---- */

typedef struct Preset {
    char path[48];
    char label[32];
} Preset;

#define MAX_PRESETS 128
static Preset g_mine[MAX_PRESETS], g_theirs[MAX_PRESETS];
static int g_mine_count, g_theirs_count;
static Pack g_pack;
static bool g_pack_open;

static void add_if_present(Preset *list, int *count, const char *path, const char *label)
{
    if (*count >= MAX_PRESETS || !pack_find(&g_pack, path))
        return;
    snprintf(list[*count].path, sizeof list[0].path, "%s", path);
    snprintf(list[*count].label, sizeof list[0].label, "%s", label);
    (*count)++;
}

/* The records' name fields are mostly empty, so they're labelled by file. Your side
 * offers the arena designs (complete fighting programs), the two test designs and
 * the tutorial model answers (lesson drills); the opponents are the arena's. */
static bool find_presets(void)
{
    if (!g_pack_open) {
        if (pack_open(&g_pack, "disc0:/PSP_GAME/USRDIR") < 0) {
            SDL_Log("[battle] can't open the pack index");
            return false;
        }
        g_pack_open = true;
    }
    char path[48], label[32];
    g_mine_count = g_theirs_count = 0;
    for (int i = 1; i <= 60; i++) {
        snprintf(path, sizeof path, "preset/arena/oke_b%02d_1.bin", i);
        snprintf(label, sizeof label, "Arena OKE %02d", i);
        add_if_present(g_mine, &g_mine_count, path, label);
    }
    add_if_present(g_mine, &g_mine_count, "preset/oke_t01_1.bin", "Test OKE 1");
    add_if_present(g_mine, &g_mine_count, "preset/oke_t01_2.bin", "Test OKE 2");
    for (int i = 0; i < 40; i++) {
        snprintf(path, sizeof path, "preset/lecture/oke_l%02d_a.bin", i);
        snprintf(label, sizeof label, "Lesson %02d answer", i);
        add_if_present(g_mine, &g_mine_count, path, label);
    }
    for (int i = 1; i <= 60; i++) {
        snprintf(path, sizeof path, "preset/arena/oke_b%02d_1.bin", i);
        snprintf(label, sizeof label, "Arena OKE %02d", i);
        add_if_present(g_theirs, &g_theirs_count, path, label);
    }
    SDL_Log("[battle] %d designs for you, %d arena opponents", g_mine_count, g_theirs_count);
    return g_mine_count > 0 && g_theirs_count > 0;
}

static bool load_record(const char *path, OkeDataBase *out)
{
    const PackEntry *e = pack_find(&g_pack, path);
    if (!e || pack_read(&g_pack, e, out, sizeof *out) != (long)sizeof *out) {
        SDL_Log("[battle] can't read %s", path);
        return false;
    }
    return true;
}

static Battle g_b;

/* ---- screens ---- */

typedef enum { SCR_PICK_MINE, SCR_PICK_THEIRS, SCR_BATTLE, SCR_RESULT } Screen;

static struct {
    Screen screen;
    int pick[2], scroll[2];
    float t;          /* seconds in this screen */
    int fileset;
    bool node_added;
    bool exit_request;
} g;

#define START_BANNER 1.5f /* seconds of BATTLE START before the fight */

static void enter(Screen s)
{
    g.screen = s;
    g.t = 0.0f;
}

static bool start_battle(void)
{
    memset(&g_b, 0, sizeof g_b);
    if (!load_record(g_mine[g.pick[0]].path, &g_b.oke[0].rec) ||
        !load_record(g_theirs[g.pick[1]].path, &g_b.oke[1].rec))
        return false;
    battle_sim_start(&g_b, 0x12345678u ^ (uint32_t)(g.pick[0] * 131 + g.pick[1]));
    SDL_Log("[battle] %s vs %s", g_mine[g.pick[0]].label, g_theirs[g.pick[1]].label);
    return true;
}

/* ---- drawing helpers ---- */

typedef struct ColorVertex {
    uint32_t color;
    int16_t x, y, z, pad;
} ColorVertex;

static void fill_rect(int x, int y, int w, int h, uint32_t abgr)
{
    ColorVertex *v = sceGuGetMemory(2 * (int)sizeof *v);
    if (!v)
        return;
    v[0] = (ColorVertex){abgr, (int16_t)x, (int16_t)y, 0, 0};
    v[1] = (ColorVertex){abgr, (int16_t)(x + w), (int16_t)(y + h), 0, 0};
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL, v);
}

static void fill_triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t abgr)
{
    ColorVertex *v = sceGuGetMemory(3 * (int)sizeof *v);
    if (!v)
        return;
    v[0] = (ColorVertex){abgr, (int16_t)x0, (int16_t)y0, 0, 0};
    v[1] = (ColorVertex){abgr, (int16_t)x1, (int16_t)y1, 0, 0};
    v[2] = (ColorVertex){abgr, (int16_t)x2, (int16_t)y2, 0, 0};
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_CULL_FACE);
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 3, NULL, v);
    sceGuEnable(GU_CULL_FACE);
}

static void text(const char *s, int x, int y, uint32_t color)
{
    TextStyle st = text_style(0, 6, 14, 14, 0, 4, color, 0);
    text_draw(s, &st, x, y, 0, NULL, NULL);
}

static void text_centered(const char *s, int cx, int y, uint32_t color)
{
    TextStyle st = text_style(0, 6, 14, 14, 0, 4, color, 0);
    int w, h;
    text_measure(s, &st, &w, &h);
    text_draw(s, &st, cx - w / 2, y, 0, NULL, NULL);
}

static Picture *pic(const char *name)
{
    return picture_load(name); /* cached after the first load */
}

/* ---- the selection screens (stand-ins, drawn with the title's art) ---- */

#define LIST_ROWS 7

static void draw_list(const char *title, const Preset *list, int count, int pick, int scroll)
{
    Picture *back = pic("title/ti_back.gim"), *button = pic("title/ti_menb0.gim"),
            *cursor = pic("title/ti_menl0.gim");
    picture_draw(back, 0, 0, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    fill_rect(0, 0, 480, 272, 0x60000000);
    text_centered(title, 240, 14, 0xFFFFFFFF);
    int x = 160, y0 = 44;
    for (int r = 0; r < LIST_ROWS && scroll + r < count; r++) {
        int i = scroll + r, y = y0 + r * 26;
        picture_draw(button, x, y, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
        if (i == pick) {
            float pulse = 0.65f + 0.35f * sinf(g.t * 5.0f);
            picture_draw(cursor, x, y, 0, BLEND_ADD, (uint32_t)(pulse * 255) << 24 | 0xFFFFFF, 0, 0);
        }
        text_centered(list[i].label, x + 80 - 4, y + 2, 0xFFFFFFFF);
    }
    char info[48];
    snprintf(info, sizeof info, "%d / %d", pick + 1, count);
    text_centered(info, 240, y0 + LIST_ROWS * 26 + 4, 0xFFC0C0C0);
    text_centered("O select   X back", 240, 248, 0xFFC0C0C0);
}

static void list_input(int side, int count)
{
    int *pick = &g.pick[side], *scroll = &g.scroll[side];
    if (pad_repeat(PSP_CTRL_UP))
        *pick = *pick > 0 ? *pick - 1 : count - 1;
    else if (pad_repeat(PSP_CTRL_DOWN))
        *pick = *pick < count - 1 ? *pick + 1 : 0;
    if (*pick < *scroll)
        *scroll = *pick;
    if (*pick >= *scroll + LIST_ROWS)
        *scroll = *pick - LIST_ROWS + 1;
}

/* ---- the battle screen ---- */

#define VIEW_X 112
#define VIEW_Y 8
#define VIEW_SIZE 256

static float sx(float x) { return VIEW_X + x * VIEW_SIZE / FIELD; }
static float sy(float y) { return VIEW_Y + y * VIEW_SIZE / FIELD; }

static void draw_oke(const Oke *o, uint32_t color)
{
    float x = sx(o->x), y = sy(o->y), h = o->heading * DEG, r = OKE_RADIUS * VIEW_SIZE / FIELD + 2;
    float fx = sinf(h), fy = -cosf(h);
    fill_triangle(x + fx * r * 1.6f, y + fy * r * 1.6f, x - fx * r + fy * r, y - fy * r - fx * r,
                  x - fx * r - fy * r, y - fy * r + fx * r, o->hp > 0 ? color : 0xFF404040);
    if (o->defend_left > 0.0f)
        fill_rect((int)x - 1, (int)y - 1, 3, 3, 0xFFFFFFFF);
}

static void draw_side_panel(const Oke *o, const char *label, int x, uint32_t color)
{
    fill_rect(x, 8, 100, 256, 0xA0000000);
    text(o->side == 0 ? "YOU" : "OPPONENT", x + 6, 12, color);
    text(label, x + 6, 30, 0xFFFFFFFF);
    char line[32];
    snprintf(line, sizeof line, "HP %3d%%", o->hp);
    text(line, x + 6, 56, 0xFFFFFFFF);
    fill_rect(x + 6, 74, 88, 6, 0xFF303030);
    fill_rect(x + 6, 74, 88 * o->hp / 100, 6, o->hp > 30 ? 0xFF40D040 : 0xFF2040E0);
    snprintf(line, sizeof line, "CPU %d", oke_cpu_part(&o->rec));
    text(line, x + 6, 90, 0xFFC0C0C0);
    snprintf(line, sizeof line, "%d chips/s", o->vm.speed);
    text(line, x + 6, 106, 0xFFC0C0C0);
    snprintf(line, sizeof line, "prog %d pc %d", o->vm.current, o->vm.prog[o->vm.current].current);
    text(line, x + 6, 122, 0xFFC0C0C0);
    snprintf(line, sizeof line, "shots %d", o->shots_fired);
    text(line, x + 6, 146, 0xFFC0C0C0);
    snprintf(line, sizeof line, "hits %d", o->hits);
    text(line, x + 6, 162, 0xFFC0C0C0);
}

static void draw_battle(void)
{
    fill_rect(0, 0, 480, 272, 0xFF201810);
    /* The field: a 6x6 grid. */
    fill_rect(VIEW_X, VIEW_Y, VIEW_SIZE, VIEW_SIZE, 0xFF304028);
    for (int i = 0; i <= 6; i++) {
        int p = VIEW_SIZE * i / 6;
        fill_rect(VIEW_X + p, VIEW_Y, 1, VIEW_SIZE, 0xFF486040);
        fill_rect(VIEW_X, VIEW_Y + p, VIEW_SIZE, 1, 0xFF486040);
    }
    for (int i = 0; i < MAX_SHOTS; i++)
        if (g_b.shots[i].live)
            fill_rect((int)sx(g_b.shots[i].x) - 1, (int)sy(g_b.shots[i].y) - 1, 3, 3,
                      g_b.shots[i].owner ? 0xFF4080FF : 0xFFFFC040);
    draw_oke(&g_b.oke[0], 0xFFFF9040);
    draw_oke(&g_b.oke[1], 0xFF4040FF);
    draw_side_panel(&g_b.oke[0], g_mine[g.pick[0]].label, 4, 0xFFFF9040);
    draw_side_panel(&g_b.oke[1], g_theirs[g.pick[1]].label, 376, 0xFF4040FF);
    char t[16];
    float left = TIME_LIMIT - g_b.time;
    snprintf(t, sizeof t, "%3d", left > 0 ? (int)ceilf(left) : 0);
    text_centered(t, 240, 12, 0xFFFFFFFF);

    if (g.screen == SCR_BATTLE && g.t < START_BANNER) {
        Picture *p = pic("parts/bs/bs_start.gim");
        picture_draw(p, 240 - picture_width(p) / 2, 120, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
    }
    if (g.screen == SCR_RESULT) {
        static const char *k_names[3] = {"parts/bs/rs_vict0.gim", "parts/bs/rs_lose0.gim",
                                         "parts/bs/rs_drow0.gim"};
        Picture *p = pic(k_names[g_b.result]);
        picture_draw(p, 240 - picture_width(p) / 2, 112, 0, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
        if (g.t > 1.0f)
            text_centered("O continue", 240, 236, 0xFFFFFFFF);
    }
}

static void battle_draw(void *ctx)
{
    (void)ctx;
    sceGuEnable(GU_BLEND);
    switch (g.screen) {
    case SCR_PICK_MINE:
        draw_list("Choose your OKE", g_mine, g_mine_count, g.pick[0], g.scroll[0]);
        break;
    case SCR_PICK_THEIRS:
        draw_list("Choose your opponent", g_theirs, g_theirs_count, g.pick[1], g.scroll[1]);
        break;
    case SCR_BATTLE:
    case SCR_RESULT:
        draw_battle();
        break;
    }
    sceGuEnable(GU_TEXTURE_2D);
}

void battle_mode_start(void)
{
    memset(&g, 0, sizeof g);
    if (!font_init())
        SDL_Log("[battle] fonts unavailable");
    g.fileset = fileset_open("battlescene", &g_default_heap, 0, 0);
    if (g.fileset < 0)
        SDL_Log("[battle] OpenFileSet(\"battlescene\") failed: no HUD art");
    if (!find_presets()) {
        g.exit_request = true;
        return;
    }
    render_add_node(render_instance(), 2, battle_draw, &g);
    g.node_added = true;
    enter(SCR_PICK_MINE);
    SDL_Log("[battle] Battle Mode: choose your OKE");
}

int battle_mode_update(float dt)
{
    if (g.exit_request) {
        if (g.node_added)
            render_remove_node(render_instance(), &g);
        g.node_added = false;
        return 0;
    }
    g.t += dt;
    switch (g.screen) {
    case SCR_PICK_MINE:
        list_input(0, g_mine_count);
        if (pad_pressed(PSP_CTRL_CIRCLE)) {
            SDL_Log("[battle] your OKE: %s", g_mine[g.pick[0]].label);
            enter(SCR_PICK_THEIRS);
        } else if (pad_pressed(PSP_CTRL_CROSS)) {
            g.exit_request = true;
        }
        break;
    case SCR_PICK_THEIRS:
        list_input(1, g_theirs_count);
        if (pad_pressed(PSP_CTRL_CIRCLE)) {
            SDL_Log("[battle] opponent: %s", g_theirs[g.pick[1]].label);
            if (start_battle())
                enter(SCR_BATTLE);
        } else if (pad_pressed(PSP_CTRL_CROSS)) {
            enter(SCR_PICK_MINE);
        }
        break;
    case SCR_BATTLE:
        if (g.t >= START_BANNER) {
            battle_sim_step(&g_b, dt);
            if (g_b.result >= 0) {
                static const char *k_res[3] = {"you win", "you lose", "draw"};
                SDL_Log("[battle] over after %.1f s: %s (HP %d vs %d, hits %d vs %d)", g_b.time,
                        k_res[g_b.result], g_b.oke[0].hp, g_b.oke[1].hp, g_b.oke[0].hits, g_b.oke[1].hits);
                enter(SCR_RESULT);
            }
        }
        break;
    case SCR_RESULT:
        if (g.t > 1.0f && pad_pressed(PSP_CTRL_CIRCLE))
            enter(SCR_PICK_THEIRS);
        break;
    }
    return 1;
}
