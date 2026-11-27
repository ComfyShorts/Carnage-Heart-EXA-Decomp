/* See modelview.h. */
#include "modelview.h"

#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gmo.h"
#include "gu.h"
#include "okemodel.h"
#include "terrain.h"
#include "render.h"

const char *g_modelview_path;
const char *g_modelview_textures;
float g_modelview_angle;
int g_modelview_oke = -1;
int g_modelview_pose;
int g_modelview_map = -1;

static struct {
    Gmo *model, *tex;
    OkeModel *oke;
    Terrain *land;
    float angle, bounds[6];
} g_mv;

static void draw(void *ctx)
{
    (void)ctx;
    if (g_mv.land) {
        sceGuViewport(2048, 2048, 480, 272);
        sceGumMatrixMode(GU_PROJECTION);
        sceGumLoadIdentity();
        sceGumPerspective(50.0f, 480.0f / 272.0f, 2.0f, 3000.0f);
        sceGumMatrixMode(GU_VIEW);
        sceGumLoadIdentity();
        float a = g_mv.angle * 3.14159265f / 180.0f;
        ScePspFVector3 eye = {sinf(a) * 600.0f, 260.0f, cosf(a) * 600.0f}, at = {0, 0, 0}, up = {0, 1, 0};
        sceGumLookAt(&eye, &at, &up);
        sceGuDisable(GU_CULL_FACE);
        sceGuEnable(GU_DEPTH_TEST);
        sceGuDepthFunc(7);
        sceGuShadeModel(1);
        sceGuTexFilter(1, 1);
        terrain_draw(g_mv.land);
        if (g_mv.oke) {
            sceGumMatrixMode(GU_MODEL);
            sceGumLoadIdentity();
            ScePspFVector3 p = {0, terrain_height(g_mv.land, 0, 0), 0}, s = {6, 6, 6};
            sceGumTranslate(&p);
            sceGumScale(&s);
            okemodel_draw(g_mv.oke, g_modelview_pose);
        }
        sceGuEnable(GU_CULL_FACE);
        return;
    }
    if (!g_mv.model && !g_mv.oke)
        return;
    const float *b = g_mv.bounds;
    float cx = (b[0] + b[3]) / 2, cy = (b[1] + b[4]) / 2, cz = (b[2] + b[5]) / 2;
    float r = fmaxf(fmaxf(b[3] - b[0], b[4] - b[1]), b[5] - b[2]);
    if (r <= 0.0f)
        r = 10.0f;
    sceGuViewport(2048, 2048, 480, 272);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(45.0f, 480.0f / 272.0f, r * 0.05f, r * 20.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    ScePspFVector3 eye = {cx + r * 0.9f, cy + r * 0.5f, cz + r * 1.4f}, at = {cx, cy, cz}, up = {0, 1, 0};
    sceGumLookAt(&eye, &at, &up);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 c = {cx, cy, cz}, mc = {-cx, -cy, -cz};
    sceGumTranslate(&c);
    sceGumRotateY(g_mv.angle * 3.14159265f / 180.0f);
    sceGumTranslate(&mc);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(7);
    sceGuShadeModel(1);
    sceGuTexFunc(0, 1);
    sceGuTexFilter(1, 1);
    if (g_mv.oke)
        okemodel_draw(g_mv.oke, g_modelview_pose);
    else
        gmo_draw(g_mv.model);
    sceGuEnable(GU_CULL_FACE);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
}

void modelview_start(void)
{
    if (g_modelview_map >= 0)
        g_mv.land = terrain_load(g_modelview_map);
    if (g_modelview_oke >= 0) {
        g_mv.oke = okemodel_load(g_modelview_oke, 0);
        float b[6] = {-3, 0, -3, 3, 8, 3};
        memcpy(g_mv.bounds, b, sizeof b);
        goto done;
    }
    g_mv.model = gmo_load(g_modelview_path);
    if (g_modelview_textures) {
        g_mv.tex = gmo_load(g_modelview_textures);
        gmo_use_textures_from(g_mv.model, g_mv.tex);
    }
    if (g_mv.model) {
        gmo_bounds(g_mv.model, g_mv.bounds);
        SDL_Log("[modelview] bounds %.1f %.1f %.1f .. %.1f %.1f %.1f", g_mv.bounds[0], g_mv.bounds[1],
                g_mv.bounds[2], g_mv.bounds[3], g_mv.bounds[4], g_mv.bounds[5]);
        for (int i = 0; i < gmo_part_count(g_mv.model) && i < 64; i++)
            SDL_Log("[modelview] part %d: %s", i, gmo_part_name(g_mv.model, i));
    }
done:
    g_mv.angle = g_modelview_angle;
    render_add_node(render_instance(), 2, draw, &g_mv);
}

void modelview_update(float dt)
{
    g_mv.angle += dt * 30.0f;
}
