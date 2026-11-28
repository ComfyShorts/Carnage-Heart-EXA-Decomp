/* See battle.h and docs/battle.md. */
#include "battle.h"
#include "battle_sim.h"
#include "battle_menu.h"
#include "savedata.h"

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
#include "music.h"
#include "sound.h"
#include "pgchip.h"
#include "picture.h"
#include "psp_imports.h"
#include "psp_shim.h"
#include "render.h"
#include "terrain.h"

#define DEG (3.14159265f / 180.0f)

/* ---- the records ---- */

static Pack g_pack;
static bool g_pack_open;

static bool open_pack(void)
{
    if (!g_pack_open) {
        if (pack_open(&g_pack, "disc0:/PSP_GAME/USRDIR") < 0) {
            SDL_Log("[battle] can't open the pack index");
            return false;
        }
        g_pack_open = true;
    }
    return true;
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

/* The 3D scene: the field and a body per OKE (see docs/battle.md). */
#define OKE_HW_BODY 0x240   /* s32 in the hardware block: body type (COkeFile id) */
#define OKE_HW_PAINT 0x244  /* s32: paint scheme 0-3 (assumed from the presets) */
static struct {
    int map;    /* the loaded land/mapNN, -1 none */
    Terrain *land;
    Gmo *sky;
    OkeModel *body[MAX_OKES];
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
    int map = battle_menu_setup()->map;
    if (!g_scene.land || g_scene.map != map) {
        terrain_free(g_scene.land);
        gmo_free(g_scene.sky);
        g_scene.land = terrain_load(map);
        char path[48];
        snprintf(path, sizeof path, "land/map%02d/map%02d_sky.gmo", map, map);
        g_scene.sky = gmo_load(path);
        g_scene.map = map;
    }
    for (int i = 0; i < MAX_OKES; i++) {
        okemodel_free(g_scene.body[i]);
        g_scene.body[i] = NULL;
        if (i < g_b.count) {
            const OkeDataBase *rec = &g_b.oke[i].rec;
            g_scene.body[i] = okemodel_load(hw_word(rec, OKE_HW_BODY), hw_word(rec, OKE_HW_PAINT));
        }
    }
    g_scene.cam_valid = false;
}

/* ---- screens ---- */

typedef enum { SCR_MENU, SCR_BATTLE, SCR_RESULT } Screen;

static struct {
    Screen screen;
    float t;          /* seconds in this screen */
    int fileset;
    bool node_added;
    bool exit_request;
    int watch;        /* the OKE the camera and the HUD follow (Battle.oke index) */
    const char *label[MAX_OKES];
} g;

#define START_BANNER 1.5f /* seconds of BATTLE START before the fight */

static void enter(Screen s)
{
    g.screen = s;
    g.t = 0.0f;
}

static int battle_team_count(int side)
{
    int n = 0;
    for (int i = 0; i < g_b.count; i++)
        n += g_b.oke[i].side == side;
    return n;
}

/* From the choices made on the Battle Mode screens (battle_menu.c). */
static void battle_sound(void *user, int se, float x, float y);

static bool start_battle(void)
{
    uint64_t perf_t0 = PERF_BEGIN();
    const BattleSetup *set = battle_menu_setup();
    memset(&g_b, 0, sizeof g_b);
    for (int side = 0; side < 2; side++)
        for (int m = 0; m < set->count[side] && m < SETUP_TEAM_MAX; m++) {
            const BattleOke *bo = &set->oke[side][m];
            Oke *o = &g_b.oke[g_b.count];
            const OkeDataBase *card = savedata_card(bo->card);
            if (card) /* a saved OKE card */
                o->rec = *card;
            else if (!open_pack() || !load_record(bo->path, &o->rec))
                return false;
            o->side = side;
            o->member = m;
            o->spot = bo->spot;
            g.label[g_b.count++] = bo->label;
        }
    if (battle_team_count(0) == 0 || battle_team_count(1) == 0)
        return false;
    g_b.time_limit = set->time_limit;
    g_b.map_size = set->map_size;
    const BattleOke *lead = &set->oke[1][0];
    uint32_t seed = 0x12345678u;
    for (const char *c = lead->path[0] ? lead->path : lead->label; *c; c++)
        seed = seed * 31u + (uint8_t)*c;
    g_b.sound = battle_sound;
    battle_sim_start(&g_b, seed ^ (uint32_t)set->map);
    /* the battle bank (FUN_08b26998(.., 1, "sound/se/seset_01bat.phd", ".pbd")) */
    sound_bank_open(SE_BANK_BATTLE);
    /* FUN_088754d8: a battle that names its own track plays it (FUN_08b34e88);
     * Battle Mode's don't, so the battle list (FUN_0888dfb0) */
    music_battle();
    g.watch = 0; /* your leader */
    uint64_t perf_t1 = PERF_BEGIN();
    scene_load();
    if (perf_t0)
        SDL_Log("[perf] battle loaded in %.1f ms (field and models %.1f ms)",
                (double)(psp_perf_now() - perf_t0) / 1e6, (double)(psp_perf_now() - perf_t1) / 1e6);
    SDL_Log("[battle] %s (%d) vs %s (%d) on map %02d, %.0f s", set->team_label[0], set->count[0],
            set->team_label[1], set->count[1], set->map, set->time_limit);
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

static void text_right(const char *s, int rx, int y, uint32_t color)
{
    TextStyle st = text_style(0, 6, 14, 14, 0, 4, color, 0);
    int w, h;
    text_measure(s, &st, &w, &h);
    text_draw(s, &st, rx - w, y, 0, NULL, NULL);
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

/* A bone with boxes or a weapon mount has armour at the start; at 0 it's off. */
static bool part_had_armour(const Oke *o, int bone)
{
    const OkeScripts *s = o->motion.s;
    for (int i = 0; i < s->n_boxes; i++)
        if (s->box[i].bone == bone)
            return true;
    for (int i = 0; i < OKE_MOUNTS; i++)
        if (s->mount_bone[i] == bone)
            return true;
    return false;
}

static void draw_oke3d(int i)
{
    const Oke *o = &g_b.oke[i];
    float p[3];
    world_pos(o->x, o->y, p);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 t = {p[0], p[1], p[2]};
    sceGumTranslate(&t);
    sceGumRotateY(3.14159265f - o->heading * DEG);
    if (o->hp <= 0) /* stand-in for the wreck: knocked over */
        sceGumRotateZ(1.4f);
    else if (o->down_left > 0.0f) /* stand-in for the knock-down ragdoll */
        sceGumRotateX(-1.2f);
    uint32_t broken = 0;
    for (int b = 0; b < OKE_BONES && b < 32; b++)
        if (o->motion.s && b < o->motion.s->n_bones && o->part_hp[b] <= 0 && part_had_armour(o, b))
            broken |= 1u << b;
    okemodel_draw_hiding(g_scene.body[i], o->motion.pose, broken);
}

/* Stand-in for CBattleSceneCamera: follows the watched OKE from behind and above,
 * eased. */
static void update_camera(float dt)
{
    const Oke *o = &g_b.oke[g.watch];
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
    sound_set_listener(g_scene.cam_eye, g_scene.cam_at);
}

/* The sim's sound events, placed at the field point (FUN_08b36cac). */
static void battle_sound(void *user, int se, float x, float y)
{
    (void)user;
    float p[3];
    world_pos(x, y, p);
    sound_se_3d(se, 1.0f, p);
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
        uint32_t c = s->owner ? 0xFF4080FF : 0xFFFFC040; /* by side */
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
    for (int i = 0; i < g_b.count; i++)
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

/* ---- HUD ----
 * CIndicatorBase windows (FUN_089011d8 / FUN_0890126c): a table of 12-byte entries
 * {x, y, w, h, ...} at 0x003ed78c; each window is the same rectangle cut from the
 * full-screen atlas bs_wind0. The status window (2) holds the gauges drawn by
 * FUN_089104fc (HP, bs_hpguage), FUN_089106ac (heat, bs_htguage) and FUN_08910860
 * (energy, bs_enguage): 130 pixels for 1000/1000, positions at 0x003f0514 and
 * heights at 0x003f0508, relative to the window. Numbers come from the EBOOT at run
 * time, with the values below as fall-backs. */
#define HUD_WINDOWS 0x003ED78Cu
#define HUD_GAUGE_POS 0x003F0514u
#define HUD_GAUGE_H 0x003F0508u

typedef struct HudRect {
    int x, y, w, h;
} HudRect;

static int16_t ed_short(uint32_t addr, int16_t fallback)
{
    uint32_t w = gamedata_word(addr & ~3u, 0xFFFFFFFFu);
    if (w == 0xFFFFFFFFu && !gamedata_available())
        return fallback;
    return (int16_t)((addr & 2) ? w >> 16 : w & 0xFFFF);
}

static HudRect hud_window(int i)
{
    static const HudRect k_fallback[11] = {{361, 0, 119, 19}, {0, 156, 89, 96}, {89, 223, 302, 29},
                                           {402, 180, 78, 71}, {0, 252, 480, 20}, {0, 0, 126, 84},
                                           {0, 85, 117, 69},   {365, 19, 115, 68}, {132, 0, 217, 84},
                                           {100, 195, 268, 28}, {0, 0, 0, 0}};
    uint32_t a = HUD_WINDOWS + (uint32_t)i * 12;
    HudRect f = k_fallback[i];
    return (HudRect){ed_short(a, (int16_t)f.x), ed_short(a + 2, (int16_t)f.y), ed_short(a + 4, (int16_t)f.w),
                     ed_short(a + 6, (int16_t)f.h)};
}

static void draw_hud_window(int i)
{
    Picture *atlas = pic("parts/bs/bs_wind0.gim");
    HudRect r = hud_window(i);
    if (atlas && r.w > 0)
        picture_draw_rect(atlas, r.x, r.y, 0, r.x, r.y, r.w, r.h, BLEND_ALPHA, 0xFFFFFFFF, 0, 0);
}

/* One gauge: width = value / 1000 of 130 pixels; the HP texture is 12 pixels
 * stretched, the others are cut to the width. */
static void draw_gauge(const char *name, int n, int permille, bool stretch)
{
    HudRect w = hud_window(2);
    static const int k_pos[3][2] = {{104, 20}, {104, 35}, {104, 43}}, k_h[3] = {12, 5, 3};
    int gx = ed_short(HUD_GAUGE_POS + (uint32_t)n * 4, (int16_t)k_pos[n][0]);
    int gy = ed_short(HUD_GAUGE_POS + (uint32_t)n * 4 + 2, (int16_t)k_pos[n][1]);
    int h = (int)gamedata_word(HUD_GAUGE_H + (uint32_t)n * 4, (uint32_t)k_h[n]);
    int len = SDL_clamp(permille * 0x82 / 1000, n == 0 ? 1 : 0, 0x82);
    Picture *p = pic(name);
    if (p && len > 0)
        picture_draw_ex(p, w.x + gx, w.y + gy, 0, len, h, 0, 0, stretch ? 12 : len, h, BLEND_ALPHA, 0xFFFFFFFF, 0,
                        0, 0, 0);
}

/* Screen position of a world point with the battle camera, for the gun sight. */
static bool project(const float p[3], float *sx, float *sy)
{
    const float *e = g_scene.cam_eye, *a = g_scene.cam_at;
    float f[3] = {a[0] - e[0], a[1] - e[1], a[2] - e[2]};
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (int i = 0; i < 3; i++)
        f[i] /= fl;
    float r[3] = {-f[2], 0, f[0]}; /* f x up */
    float rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
    r[0] /= rl;
    r[2] /= rl;
    float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    float d[3] = {p[0] - e[0], p[1] - e[1], p[2] - e[2]};
    float z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    if (z < 1.0f)
        return false;
    float t = 1.0f / tanf(55.0f * DEG / 2);
    *sx = 240.0f + (d[0] * r[0] + d[2] * r[2]) / z * t * 136.0f;
    *sy = 136.0f - (d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) / z * t * 136.0f;
    return true;
}

/* CIndicatorFriend (FUN_08904200): in window 4, the watched OKE's two team-mates,
 * the next member first. Each row (0x08bf28fe, plus 0x08bf28f2) has the member's
 * mark from bs_memark0 (16 x 18, cut at 0x08bf290a by member), an HP bar from
 * bs_hpguage 130 pixels for 100% and 12 high (FUN_08904178), and the OKE's name at
 * +0x18, +5. */
#define FRIEND_ROWS 0x003EE8FEu
#define FRIEND_OFFS 0x003EE8F2u
#define FRIEND_MARKS 0x003EE90Au

static void draw_friends(const Oke *me)
{
    const Oke *mate[TEAM_SIZE] = {0};
    int n = 0;
    for (int i = 0; i < g_b.count; i++)
        if (g_b.oke[i].side == me->side && g_b.oke[i].member < TEAM_SIZE)
            mate[g_b.oke[i].member] = &g_b.oke[i];
    for (int k = 0; k < TEAM_SIZE; k++)
        n += mate[k] != NULL;
    if (n < 2)
        return;
    draw_hud_window(4);
    HudRect w = hud_window(4);
    static const int16_t k_rows[2][2] = {{330, 0}, {0, 0}}, k_offs[2][2] = {{19, 5}, {19, 5}};
    static const int16_t k_marks[3][2] = {{0, 0}, {20, 0}, {40, 0}};
    Picture *marks = pic("parts/bs/bs_memark0.gim"), *bar = pic("parts/bs/bs_hpguage.gim");
    TextStyle st = text_style(0, 5, 10, 10, 0, 4, 0xFFFFFFFF, 0);
    int m = me->member;
    for (int row = 0; row < 2; row++) {
        m = (m + 1) % TEAM_SIZE;
        const Oke *o = mate[m];
        if (!o)
            continue;
        int rx = w.x + ed_short(FRIEND_ROWS + (uint32_t)row * 4, k_rows[row][0]);
        int ry = w.y + ed_short(FRIEND_ROWS + (uint32_t)row * 4 + 2, k_rows[row][1]);
        int ox = ed_short(FRIEND_OFFS + (uint32_t)row * 4, k_offs[row][0]);
        int oy = ed_short(FRIEND_OFFS + (uint32_t)row * 4 + 2, k_offs[row][1]);
        int mu = ed_short(FRIEND_MARKS + (uint32_t)m * 4, k_marks[m][0]);
        int mv = ed_short(FRIEND_MARKS + (uint32_t)m * 4 + 2, k_marks[m][1]);
        if (marks)
            picture_draw_ex(marks, rx, ry + 1, 0, 16, 18, mu, mv, 16, 18, BLEND_ALPHA,
                            o->hp > 0 ? 0xFFFFFFFF : 0xFF808080, 0, 0, 0, 0);
        int len = o->hp > 0 ? oke_hp_percent(o) * 0x82 / 100 : 0;
        if (bar && len > 0)
            picture_draw_ex(bar, rx + ox, ry + oy, 0, len, 12, 0, 0, 12, 12, BLEND_ALPHA, 0xFFFFFFFF, 0, 0, 0, 0);
        const char *name = g.label[o->index];
        text_draw(name ? name : "", &st, rx + ox + 0x18 - 19, ry + 5, 0, NULL, NULL);
    }
}

static void draw_hud(void)
{
    const Oke *me = &g_b.oke[g.watch];
    /* Status window with HP and heat; energy isn't simulated, so it shows full. */
    draw_hud_window(2);
    draw_gauge("parts/bs/bs_hpguage.gim", 0, me->hp_max ? me->hp * 1000 / me->hp_max : 0, true);
    draw_gauge("parts/bs/bs_htguage.gim", 1, me->heat_max ? me->heat * 1000 / me->heat_max : 0, false);
    draw_gauge("parts/bs/bs_enguage.gim", 2, 1000, false);
    draw_friends(me);
    /* Radar window (3) with the game's radar frame and a marker per OKE, centred on
     * the watched one. Their placement, scale and colours are stand-ins:
     * CIndicatorRadar (FUN_08909a98) isn't read yet. */
    draw_hud_window(3);
    HudRect rw = hud_window(3);
    Picture *frame = pic("parts/bs/bs_radf0.gim");
    int cx = rw.x + rw.w / 2, cy = rw.y + rw.h / 2;
    if (frame)
        picture_draw_ex(frame, rw.x + 2, rw.y + 2, 0, rw.w - 4, rw.h - 4, 0, 0, picture_width(frame),
                        picture_height(frame), BLEND_ALPHA, 0xFFFFFFFF, 0, 0, 0, 0);
    Picture *dot = pic("parts/bs/bs_radm2.gim");
    float scale = (float)(rw.h - 12) / FIELD;
    for (int i = 0; i < g_b.count; i++) {
        const Oke *o = &g_b.oke[i];
        int x = cx + (int)((o->x - me->x) * scale), y = cy + (int)((o->y - me->y) * scale);
        if (x < rw.x + 4 || x > rw.x + rw.w - 4 || y < rw.y + 4 || y > rw.y + rw.h - 4)
            continue;
        uint32_t c = o->hp <= 0 ? 0xFF606060 : i == g.watch ? 0xFFFFFFFF : o->side == me->side ? 0xFFFFC080 : 0xFF4040FF;
        if (dot)
            picture_draw(dot, x - 4, y - 4, 0, BLEND_ALPHA, c, 0, 0);
    }
    /* The gun sight: CBattleOkeGunSightController's HUD point (+0x70), which eases
     * after where the gun aims; the gunsite mark while locked on, else the dot. The
     * height is a stand-in (the sight's pitch isn't simulated). */
    float p[3], sx, sy;
    world_pos(me->mark_x, me->mark_y, p);
    p[1] += 4.0f;
    Picture *sight = pic(me->locked ? "parts/bs/gunsite.gim" : "parts/bs/gundot.gim");
    if (sight && me->hp > 0 && project(p, &sx, &sy) && sx > 0 && sx < 480 && sy > 0 && sy < 272)
        picture_draw(sight, (int)sx - picture_width(sight) / 2, (int)sy - picture_height(sight) / 2, 0, BLEND_ALPHA,
                     0xFFFFFFFF, 0, 0);
    /* Names: plain text until the game's top windows are read. The watched OKE's
     * name, and the other team with its OKEs left. */
    const BattleSetup *set = battle_menu_setup();
    int foe = me->side ^ 1;
    text(g.label[g.watch] ? g.label[g.watch] : "", 6, 6, me->side == 0 ? 0xFFFFC080 : 0xFF8080FF);
    text_right(set->team_label[foe], 474, 6, foe == 0 ? 0xFFFFC080 : 0xFF8080FF);
    char line[24];
    if (set->count[foe] > 1)
        snprintf(line, sizeof line, "%d/%d  %d%%", battle_team_alive(&g_b, foe), set->count[foe],
                 battle_team_percent(&g_b, foe));
    else
        snprintf(line, sizeof line, "%d%%", battle_team_percent(&g_b, foe));
    text_right(line, 474, 22, 0xFFFFFFFF);
}

static void draw_battle(void)
{
    update_camera(g.screen == SCR_BATTLE && g.t >= START_BANNER ? 1.0f / 60.0f : 1.0f);
    draw_scene3d();
    draw_hud();
    char t[16];
    float left = battle_time_limit(&g_b) - g_b.time;
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
    case SCR_MENU:
        battle_menu_draw();
        break;
    case SCR_BATTLE:
    case SCR_RESULT:
        draw_battle();
        break;
    }
    sceGuEnable(GU_TEXTURE_2D);
}

int g_battle_forced_result = -1;

static void mode_start(bool story)
{
    memset(&g, 0, sizeof g);
    g_scene.map = -1;
    if (!font_init())
        SDL_Log("[battle] fonts unavailable");
    music_lists_build(); /* FUN_088d3d80: the BGM Player's lists for this mode */
    g.fileset = fileset_open("battlescene", &g_default_heap, 0, 0);
    if (g.fileset < 0)
        SDL_Log("[battle] OpenFileSet(\"battlescene\") failed: no HUD art");
    render_add_node(render_instance(), 2, battle_draw, &g);
    g.node_added = true;
    if (story)
        battle_menu_start_story();
    else
        battle_menu_start();
    enter(SCR_MENU);
}

void battle_mode_start(void) { mode_start(false); }
void battle_mode_start_story(void) { mode_start(true); }

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
    case SCR_MENU: {
        int r = battle_menu_update(dt);
        if (r == 0)
            g.exit_request = true;
        else if (r == 2 && start_battle())
            enter(SCR_BATTLE);
        break;
    }
    case SCR_BATTLE:
        /* The watched OKE: L and R step through every OKE on the field (the port's
         * buttons; CBattleSceneCamera's own switching isn't read yet). */
        if (pad_pressed(PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER) && g_b.count > 1) {
            int d = pad_pressed(PSP_CTRL_RTRIGGER) ? 1 : g_b.count - 1;
            g.watch = (g.watch + d) % g_b.count;
            g_scene.cam_valid = false;
            SDL_Log("[battle] watching %s", g.label[g.watch] ? g.label[g.watch] : "?");
        }
        if (g_battle_forced_result >= 0 && g_battle_forced_result <= 2 && g.t >= START_BANNER + 1.0f &&
            g_b.result < 0) {
            g_b.result = g_battle_forced_result;
            SDL_Log("[battle] ended as result %d (--battle-result)", g_b.result);
            enter(SCR_RESULT);
            break;
        }
        if (g.t >= START_BANNER) {
            battle_sim_step(&g_b, dt);
            if (g_b.result >= 0) {
                static const char *k_res[3] = {"you win", "you lose", "draw"};
                SDL_Log("[battle] over after %.1f s: %s (OKEs left %d vs %d, durability %d%% vs %d%%)", g_b.time,
                        k_res[g_b.result], battle_team_alive(&g_b, 0), battle_team_alive(&g_b, 1),
                        battle_team_percent(&g_b, 0), battle_team_percent(&g_b, 1));
                enter(SCR_RESULT);
            }
        }
        break;
    case SCR_RESULT:
        if (g.t > 1.0f && pad_pressed(PSP_CTRL_CIRCLE)) {
            sound_se(SE_DECIDE, 1.0f);
            sound_bank_close(SE_BANK_BATTLE);
            battle_menu_battle_over(g_b.result);
            battle_menu_resume();
            enter(SCR_MENU);
        }
        break;
    }
    return 1;
}
