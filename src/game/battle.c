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
#include "gmo.h"
#include "heap.h"
#include "okemodel.h"
#include "packfile.h"
#include "pad.h"
#include "pgchip.h"
#include "picture.h"
#include "psp_imports.h"
#include "psp_shim.h"
#include "render.h"
#include "terrain.h"

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

/* The 3D scene: the field and the two bodies (see docs/battle.md). */
#define BATTLE_MAP 1        /* stand-in: the arena's map choice isn't read yet */
#define OKE_HW_BODY 0x240   /* s32 in the hardware block: body type (COkeFile id) */
#define OKE_HW_PAINT 0x244  /* s32: paint scheme 0-3 (assumed from the presets) */
static struct {
    Terrain *land;
    Gmo *sky;
    OkeModel *body[2];
    float cam_eye[3], cam_at[3];
    bool cam_valid;
} g_scene;

static int hw_word(const OkeDataBase *rec, int off)
{
    int32_t v;
    memcpy(&v, rec->hardware + off, sizeof v);
    return v;
}

static void scene_load(void)
{
    if (!g_scene.land) {
        g_scene.land = terrain_load(BATTLE_MAP);
        char path[48];
        snprintf(path, sizeof path, "land/map%02d/map%02d_sky.gmo", BATTLE_MAP, BATTLE_MAP);
        g_scene.sky = gmo_load(path);
    }
    for (int i = 0; i < 2; i++) {
        okemodel_free(g_scene.body[i]);
        const OkeDataBase *rec = &g_b.oke[i].rec;
        g_scene.body[i] = okemodel_load(hw_word(rec, OKE_HW_BODY), hw_word(rec, OKE_HW_PAINT));
    }
    g_scene.cam_valid = false;
}

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
    scene_load();
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

/* The simulation's 800x800 field maps onto the terrain's extent; its heading 0 is
 * "up" (-z) and turns clockwise seen from above. */
static void world_pos(float sx, float sy, float out[3])
{
    float e[4] = {-360, -360, 720, 720};
    if (g_scene.land)
        terrain_extent(g_scene.land, e);
    out[0] = e[0] + sx / FIELD * e[2];
    out[2] = e[1] + sy / FIELD * e[3];
    out[1] = g_scene.land ? terrain_height(g_scene.land, out[0], out[2]) : 0.0f;
}

static void draw_oke3d(int side)
{
    const Oke *o = &g_b.oke[side];
    float p[3];
    world_pos(o->x, o->y, p);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 t = {p[0], p[1], p[2]};
    sceGumTranslate(&t);
    sceGumRotateY(3.14159265f - o->heading * DEG);
    if (o->hp <= 0) /* stand-in for the wreck: knocked over */
        sceGumRotateZ(1.4f);
    okemodel_draw(g_scene.body[side], 0);
}

/* Stand-in for CBattleSceneCamera: follows your OKE from behind and above, eased. */
static void update_camera(float dt)
{
    const Oke *o = &g_b.oke[0];
    float p[3];
    world_pos(o->x, o->y, p);
    float h = o->heading * DEG, fx = sinf(h), fz = -cosf(h);
    float eye[3] = {p[0] - fx * 34.0f, p[1] + 16.0f, p[2] - fz * 34.0f};
    float at[3] = {p[0] + fx * 30.0f, p[1] + 4.0f, p[2] + fz * 30.0f};
    if (g_scene.land) {
        float ground = terrain_height(g_scene.land, eye[0], eye[2]) + 4.0f;
        if (eye[1] < ground)
            eye[1] = ground;
    }
    float k = g_scene.cam_valid ? 1.0f - expf(-dt * 8.0f) : 1.0f;
    for (int i = 0; i < 3; i++) {
        g_scene.cam_eye[i] += (eye[i] - g_scene.cam_eye[i]) * k;
        g_scene.cam_at[i] += (at[i] - g_scene.cam_at[i]) * k;
    }
    g_scene.cam_valid = true;
}

typedef struct ShotVertex {
    uint32_t color;
    float x, y, z;
} ShotVertex;

static void draw_shots3d(void)
{
    ShotVertex *v = sceGuGetMemory(sizeof(ShotVertex) * MAX_SHOTS * 2);
    int n = 0;
    for (int i = 0; i < MAX_SHOTS; i++) {
        const Shot *s = &g_b.shots[i];
        if (!s->live)
            continue;
        float a[3], b[3];
        world_pos(s->x, s->y, a);
        world_pos(s->x - s->vx * 0.05f, s->y - s->vy * 0.05f, b);
        uint32_t c = s->owner ? 0xFF4080FF : 0xFFFFC040;
        v[n++] = (ShotVertex){c, a[0], a[1] + 4.0f, a[2]};
        v[n++] = (ShotVertex){c, b[0], b[1] + 4.0f, b[2]};
    }
    if (!n)
        return;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_LINES, GU_COLOR_8888 | GU_VERTEX_32BITF, n, NULL, v);
}

static void draw_scene3d(void)
{
    sceGuViewport(2048, 2048, 480, 272);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(55.0f, 480.0f / 272.0f, 1.0f, 2000.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    ScePspFVector3 eye = {g_scene.cam_eye[0], g_scene.cam_eye[1], g_scene.cam_eye[2]};
    ScePspFVector3 at = {g_scene.cam_at[0], g_scene.cam_at[1], g_scene.cam_at[2]}, up = {0, 1, 0};
    sceGumLookAt(&eye, &at, &up);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(1);
    sceGuTexFilter(1, 1);
    sceGuTexFunc(0, 1);
    /* The sky model around the camera, behind everything. */
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(1);
    fill_rect(0, 0, 480, 272, 0xFF000000);
    if (g_scene.sky) {
        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();
        sceGumTranslate(&eye);
        gmo_draw(g_scene.sky);
    }
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(0);
    sceGuDepthFunc(7);
    terrain_draw(g_scene.land);
    for (int i = 0; i < 2; i++)
        draw_oke3d(i);
    draw_shots3d();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexFilter(7, 1); /* back to the frame's reset state (render.c) */
}

/* Stand-in HUD (the game's CBattleDisplayMngr isn't ported yet): name, HP bar and
 * the chip interpreter's state for each side. */
static void draw_side_panel(const Oke *o, const char *label, int x, uint32_t color)
{
    fill_rect(x, 4, 118, 52, 0x90000000);
    text(o->side == 0 ? "YOU" : "OPPONENT", x + 4, 6, color);
    text(label, x + 4, 20, 0xFFFFFFFF);
    fill_rect(x + 4, 36, 110, 5, 0xFF303030);
    fill_rect(x + 4, 36, 110 * o->hp / 100, 5, o->hp > 30 ? 0xFF40D040 : 0xFF2040E0);
    char line[40];
    snprintf(line, sizeof line, "HP %d%% hits %d", o->hp, o->hits);
    text(line, x + 4, 42, 0xFFC0C0C0);
}

/* A small top-down radar, standing in for CBattleRadarMngr. */
static void draw_radar(void)
{
    const int rx = 404, ry = 196, rs = 70;
    fill_rect(rx, ry, rs, rs, 0x90000000);
    for (int i = 0; i < 2; i++) {
        const Oke *o = &g_b.oke[i];
        int x = rx + (int)(o->x * rs / FIELD), y = ry + (int)(o->y * rs / FIELD);
        fill_rect(x - 2, y - 2, 4, 4, o->hp <= 0 ? 0xFF404040 : i == 0 ? 0xFFFF9040 : 0xFF4040FF);
    }
}

static void draw_battle(void)
{
    update_camera(g.screen == SCR_BATTLE && g.t >= START_BANNER ? 1.0f / 60.0f : 1.0f);
    draw_scene3d();
    draw_side_panel(&g_b.oke[0], g_mine[g.pick[0]].label, 4, 0xFFFF9040);
    draw_side_panel(&g_b.oke[1], g_theirs[g.pick[1]].label, 358, 0xFF4040FF);
    draw_radar();
    char t[16];
    float left = TIME_LIMIT - g_b.time;
    snprintf(t, sizeof t, "%3d", left > 0 ? (int)ceilf(left) : 0);
    text_centered(t, 240, 8, 0xFFFFFFFF);

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
