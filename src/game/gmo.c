/* See gmo.h and docs/battle.md ("GMO notes").
 *
 * Chunks: u16 type, u16 header size, u32 chunk size, then for the structural types
 * (2 file, 3 model, 4 bone, 5 part, 6 mesh, 7 arrays, 8 material, 9 texture,
 * 0x0A image file, 0x0B motion) u32 child offset, u32 data offset and a name.
 * Data chunks (types 0x8000 and up) hold their values after the 8-byte header.
 * References are (type << 16) | flags | index. */
#include "gmo.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "gu.h"
#include "heap.h"
#include "picture.h"

typedef struct Arrays {
    uint32_t fmt;
    int count, stride;
    const uint8_t *verts;
} Arrays;

typedef struct Draw {
    int arrays; /* global index */
    int prim, n, count;
    const uint8_t *idx; /* n * count u16 indices */
} Draw;

typedef struct Mesh {
    int material;
    int first_draw, draws;
} Mesh;

typedef struct Part {
    char name[32];
    int first_mesh, meshes;
} Part;

typedef struct Bone {
    int parent, part;
    bool has_matrix;
    float m[16];
} Bone;

typedef struct Material {
    uint32_t diffuse; /* ABGR */
    int file;         /* image index, or -1 */
} Material;

struct Gmo {
    uint8_t *data;
    float bounds[6];
    bool has_offset;
    float offset[3], scale[3];
    Arrays *arrays; int n_arrays;
    Draw *draws; int n_draws;
    Mesh *meshes; int n_meshes;
    Part *parts; int n_parts;
    Bone *bones; int n_bones;
    Material *materials; int n_materials;
    Picture **files; int n_files;
    const Gmo *tex_src;
};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static float rdf(const uint8_t *p)
{
    uint32_t u = rd32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

#define PUSH(arr, n, item) \
    do { (arr) = SDL_realloc((arr), (size_t)((n) + 1) * sizeof *(arr)); (arr)[(n)++] = (item); } while (0)

static uint8_t to8(float f)
{
    return f <= 0.0f ? 0 : f >= 1.0f ? 255 : (uint8_t)(f * 255.0f + 0.5f);
}

typedef struct Ctx {
    Gmo *g;
    int part_arrays_base; /* global index of the current part's first arrays */
    int cur_mesh, cur_material, cur_bone;
    const char *path;
} Ctx;

static void parse(Ctx *c, const uint8_t *p, const uint8_t *end, int parent_type);

static void structural(Ctx *c, int type, const uint8_t *p, uint32_t size)
{
    Gmo *g = c->g;
    uint32_t child = rd32(p + 8);
    const char *name = (const char *)(p + 16);
    switch (type) {
    case 4: { /* bone */
        Bone b = {.parent = -1, .part = -1};
        PUSH(g->bones, g->n_bones, b);
        c->cur_bone = g->n_bones - 1;
        break;
    }
    case 5: { /* part */
        Part pt = {.first_mesh = g->n_meshes};
        SDL_strlcpy(pt.name, name, sizeof pt.name);
        PUSH(g->parts, g->n_parts, pt);
        /* The part's arrays come after its meshes, so they start at the current count;
         * the meshes' references are local to the part. */
        c->part_arrays_base = g->n_arrays;
        break;
    }
    case 6: { /* mesh */
        Mesh m = {.material = -1, .first_draw = g->n_draws};
        PUSH(g->meshes, g->n_meshes, m);
        g->parts[g->n_parts - 1].meshes++;
        c->cur_mesh = g->n_meshes - 1;
        break;
    }
    case 7: { /* arrays: format, count, morphs, flags, then the vertices */
        uint32_t data = rd32(p + 12);
        Arrays a = {rd32(p + data - 16), (int)rd32(p + data - 12), 0, p + data};
        if (a.count > 0)
            a.stride = (int)((size - data) / (uint32_t)a.count);
        PUSH(g->arrays, g->n_arrays, a);
        return;
    }
    case 8: { /* material */
        Material m = {0xFFFFFFFF, -1};
        PUSH(g->materials, g->n_materials, m);
        c->cur_material = g->n_materials - 1;
        break;
    }
    case 0x0A: { /* image file: 0x8012 name, 0x8013 u32 size + GIM */
        const uint8_t *q = p + child, *e = p + size;
        Picture *pic = NULL;
        while (q + 8 <= e) {
            uint32_t sz = rd32(q + 4);
            if (sz < 8 || q + sz > e)
                break;
            if (rd16(q) == 0x8013 && sz > 12)
                pic = picture_from_memory(c->path, q + 12, (int32_t)rd32(q + 8));
            q += sz;
        }
        PUSH(g->files, g->n_files, pic);
        return;
    }
    case 0x0B: /* motion: not used yet */
        return;
    default:
        break;
    }
    parse(c, p + child, p + size, type);
}

static void value(Ctx *c, int type, const uint8_t *v, uint32_t len, int parent_type)
{
    Gmo *g = c->g;
    switch (type) {
    case 0x8014: /* bounding box */
        if (parent_type == 3 && len >= 24)
            for (int i = 0; i < 6; i++)
                g->bounds[i] = rdf(v + 4 * i);
        break;
    case 0x8015: /* vertex offset: mask, offset xyz, scale xyz */
        if (len >= 28 && (rd32(v) & 0x180)) {
            g->has_offset = true;
            for (int i = 0; i < 3; i++) {
                g->offset[i] = rdf(v + 4 + 4 * i);
                g->scale[i] = rdf(v + 16 + 4 * i);
            }
        }
        break;
    case 0x8041: /* bone: parent */
        if (parent_type == 4)
            g->bones[c->cur_bone].parent = (int)(rd32(v) & 0xFFF);
        break;
    case 0x8047: /* bone: local matrix */
        if (parent_type == 4 && len >= 64) {
            Bone *b = &g->bones[c->cur_bone];
            for (int i = 0; i < 16; i++)
                b->m[i] = rdf(v + 4 * i);
            b->has_matrix = true;
        }
        break;
    case 0x804E: /* bone: draws a part */
        if (parent_type == 4)
            g->bones[c->cur_bone].part = (int)(rd32(v) & 0xFFF);
        break;
    case 0x8061: /* mesh: material */
        if (parent_type == 6)
            g->meshes[c->cur_mesh].material = (int)(rd32(v) & 0xFFF);
        break;
    case 0x8066: { /* mesh: draw arrays (arrays ref, primitive, n, count, indices) */
        if (parent_type != 6 || len < 16)
            break;
        Draw d = {c->part_arrays_base + (int)(rd32(v) & 0xFFF), (int)rd32(v + 4), (int)rd32(v + 8),
                  (int)rd32(v + 12), v + 16};
        if ((uint32_t)(d.n * d.count * 2) + 16 > len)
            break;
        PUSH(g->draws, g->n_draws, d);
        g->meshes[c->cur_mesh].draws++;
        break;
    }
    case 0x8082: /* material: diffuse RGBA */
        if (parent_type == 8 && len >= 16)
            g->materials[c->cur_material].diffuse = (uint32_t)to8(rdf(v + 12)) << 24 |
                                                    (uint32_t)to8(rdf(v + 8)) << 16 |
                                                    (uint32_t)to8(rdf(v + 4)) << 8 | to8(rdf(v));
        break;
    case 0x8091: /* texture: image file */
        if (parent_type == 9 && c->cur_material >= 0)
            g->materials[c->cur_material].file = (int)(rd32(v) & 0xFFF);
        break;
    default:
        break;
    }
}

static void parse(Ctx *c, const uint8_t *p, const uint8_t *end, int parent_type)
{
    while (p + 8 <= end) {
        int type = rd16(p);
        uint32_t size = rd32(p + 4);
        if (size < 8 || p + size > end)
            return;
        if (type & 0x8000)
            value(c, type, p + 8, size - 8, parent_type);
        else if (type >= 2 && type <= 0x0B)
            structural(c, type, p, size);
        p += size;
    }
}

Gmo *gmo_load(const char *path)
{
    int32_t size = fileio_file_size(path);
    if (size < 32) {
        SDL_Log("[gmo] %s not found", path);
        return NULL;
    }
    Gmo *g = SDL_calloc(1, sizeof *g);
    g->data = heap_alloc(&g_default_heap, (uint32_t)size, 16, "model", 0);
    if (!g->data || fileio_read_file(path, g->data, (uint32_t)size) != size ||
        memcmp(g->data, "OMG.00.1PSP", 11) != 0) {
        SDL_Log("[gmo] %s: can't read or not a GMO file", path);
        gmo_free(g);
        return NULL;
    }
    Ctx c = {g, 0, -1, -1, -1, path};
    parse(&c, g->data + 16, g->data + size, 0);
    SDL_Log("[gmo] %s: %d bones, %d parts, %d meshes, %d arrays, %d materials, %d images", path, g->n_bones,
            g->n_parts, g->n_meshes, g->n_arrays, g->n_materials, g->n_files);
    return g;
}

void gmo_free(Gmo *g)
{
    if (!g)
        return;
    for (int i = 0; i < g->n_files; i++)
        picture_release(g->files[i]);
    SDL_free(g->arrays);
    SDL_free(g->draws);
    SDL_free(g->meshes);
    SDL_free(g->parts);
    SDL_free(g->bones);
    SDL_free(g->materials);
    SDL_free(g->files);
    if (g->data)
        heap_free(&g_default_heap, g->data);
    SDL_free(g);
}

void gmo_use_textures_from(Gmo *g, const Gmo *textures)
{
    if (g)
        g->tex_src = textures;
}

int gmo_part_count(const Gmo *g)
{
    return g ? g->n_parts : 0;
}

const char *gmo_part_name(const Gmo *g, int part)
{
    return g && part >= 0 && part < g->n_parts ? g->parts[part].name : "";
}

int gmo_find_part(const Gmo *g, const char *name)
{
    for (int i = 0; g && i < g->n_parts; i++)
        if (!SDL_strcasecmp(g->parts[i].name, name))
            return i;
    return -1;
}

void gmo_bounds(const Gmo *g, float out[6])
{
    memcpy(out, g->bounds, sizeof g->bounds);
}

static Picture *material_picture(const Gmo *g, const Material *m)
{
    if (m->file < 0)
        return NULL;
    if (m->file < g->n_files && g->files[m->file])
        return g->files[m->file];
    const Gmo *t = g->tex_src;
    if (t && m->file < t->n_files)
        return t->files[m->file];
    return NULL;
}

static void draw_part_now(const Gmo *g, int part)
{
    const Part *pt = &g->parts[part];
    for (int mi = 0; mi < pt->meshes; mi++) {
        const Mesh *m = &g->meshes[pt->first_mesh + mi];
        uint32_t color = 0xFFFFFFFF;
        Picture *pic = NULL;
        if (m->material >= 0 && m->material < g->n_materials) {
            color = g->materials[m->material].diffuse;
            pic = material_picture(g, &g->materials[m->material]);
        }
        if (pic) {
            sceGuEnable(GU_TEXTURE_2D);
            picture_bind(pic);
            sceGuTexScale(1.0f, 1.0f);
            sceGuTexOffset(0.0f, 0.0f);
            sceGuTexWrap(0, 0);
        } else {
            sceGuDisable(GU_TEXTURE_2D);
        }
        sceGuColor(color);
        for (int di = 0; di < m->draws; di++) {
            const Draw *d = &g->draws[m->first_draw + di];
            if (d->arrays < 0 || d->arrays >= g->n_arrays)
                continue;
            const Arrays *a = &g->arrays[d->arrays];
            int vtype = (int)(a->fmt & 0x007FFFFF);
            for (int s = 0; s < d->count; s++)
                sceGuDrawArray(d->prim, vtype, d->n, d->idx + (size_t)s * (size_t)d->n * 2, a->verts);
        }
    }
}

static void with_offset(const Gmo *g)
{
    if (!g->has_offset)
        return;
    ScePspFVector3 t = {g->offset[0], g->offset[1], g->offset[2]};
    ScePspFVector3 s = {g->scale[0], g->scale[1], g->scale[2]};
    sceGumTranslate(&t);
    sceGumScale(&s);
}

void gmo_draw_part(const Gmo *g, int part)
{
    if (!g || part < 0 || part >= g->n_parts)
        return;
    sceGumPushMatrix();
    with_offset(g);
    sceGumUpdateMatrix();
    draw_part_now(g, part);
    sceGumPopMatrix();
}

static void draw_bone(const Gmo *g, int bone, int depth)
{
    const Bone *b = &g->bones[bone];
    sceGumPushMatrix();
    if (b->has_matrix)
        sceGumMultMatrix((const ScePspFMatrix4 *)b->m);
    if (b->part >= 0 && b->part < g->n_parts)
        gmo_draw_part(g, b->part);
    for (int i = 0; i < g->n_bones && depth < 32; i++)
        if (g->bones[i].parent == bone && i != bone)
            draw_bone(g, i, depth + 1);
    sceGumPopMatrix();
}

void gmo_draw(const Gmo *g)
{
    if (!g)
        return;
    if (g->n_bones == 0) {
        for (int i = 0; i < g->n_parts; i++)
            gmo_draw_part(g, i);
        return;
    }
    for (int i = 0; i < g->n_bones; i++)
        if (g->bones[i].parent < 0)
            draw_bone(g, i, 0);
}
