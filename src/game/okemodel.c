/* See okemodel.h. */
#include "okemodel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "gamedata.h"
#include "gmo.h"
#include "gu.h"
#include "heap.h"

#define MAX_POSES 512

/* COkeFile's table: 16-byte entries {txt path, gmo path, paint path list, paint count}. */
#define OKE_FILE_TABLE 0x0041666Cu
/* GetOkeFileSetName3's table (0x08c8486c): five set names per body. */
#define OKE_SET_TABLE 0x0048086Cu

typedef struct OkeBone {
    int parent, part; /* part: index among the model's intact parts */
    float pivot[3];
} OkeBone;

typedef struct OkePose {
    float root[3];
    float rot[OKE_MAX_BONES][3];
} OkePose;

struct OkeModel {
    Gmo *parts, *paint;
    int part_of[64]; /* intact part index -> model part */
    int n_intact;
    OkeBone bones[OKE_MAX_BONES];
    int n_bones;
    OkePose *poses;
    int n_poses;
    float ground; /* lowest EARTH point below the root */
};

static const char *table_string(int oke_id, int field)
{
    if (oke_id < 0 || oke_id > OKE_ID_MAX)
        return NULL;
    uint32_t p = gamedata_word(OKE_FILE_TABLE + (uint32_t)oke_id * 16 + (uint32_t)field * 4, 0);
    return p ? gamedata_string(p) : NULL;
}

const char *okemodel_type_name(int oke_id)
{
    static char name[16];
    const char *gmo = table_string(oke_id, 1); /* "battle/xxx/part.gmo" */
    if (!gmo || strncmp(gmo, "battle/", 7))
        return "";
    const char *e = strchr(gmo + 7, '/');
    size_t n = e ? (size_t)(e - gmo - 7) : 0;
    if (n >= sizeof name)
        n = sizeof name - 1;
    memcpy(name, gmo + 7, n);
    name[n] = 0;
    return name;
}

/* A line-oriented reader over the text file. */
typedef struct Lines {
    char *p, *end;
} Lines;

static char *next_line(Lines *l)
{
    while (l->p < l->end) {
        char *s = l->p;
        char *e = memchr(s, '\n', (size_t)(l->end - s));
        if (!e)
            e = l->end;
        l->p = e < l->end ? e + 1 : l->end;
        *e = 0;
        if (e > s && e[-1] == '\r')
            e[-1] = 0;
        if (*s)
            return s;
    }
    return NULL;
}

static void parse_description(OkeModel *m, char *text, int32_t size)
{
    Lines l = {text, text + size};
    char *s;
    int section = 0; /* 1 body, 2 pose */
    OkePose *pose = NULL;
    int rot = 0;
    m->ground = 0.0f;
    while ((s = next_line(&l))) {
        if (s[0] == '!') {
            section = !strcmp(s, "!BODY") ? 1 : !strcmp(s, "!POSE") ? 2 : 0;
            pose = NULL;
            if (section == 2 && m->n_poses < MAX_POSES) {
                pose = &m->poses[m->n_poses++];
                memset(pose, 0, sizeof *pose);
                rot = 0;
            }
            continue;
        }
        if (section == 1 && !strcmp(s, "PARTS2") && m->n_bones < OKE_MAX_BONES) {
            char *v = next_line(&l);
            OkeBone *b = &m->bones[m->n_bones];
            if (v && sscanf(v, "%d %d %f %f %f", &b->parent, &b->part, &b->pivot[0], &b->pivot[1],
                            &b->pivot[2]) == 5)
                m->n_bones++;
        } else if (section == 1 && !strcmp(s, "EARTH")) {
            char *v = next_line(&l);
            int bone;
            float x, y, z;
            if (v && sscanf(v, "%d %f %f %f", &bone, &x, &y, &z) == 4 && bone >= 0 && y < m->ground)
                m->ground = y;
        } else if (pose && !strcmp(s, "TRANSLATE")) {
            char *v = next_line(&l);
            if (v)
                sscanf(v, "%f %f %f", &pose->root[0], &pose->root[1], &pose->root[2]);
        } else if (pose && !strcmp(s, "ROTATE")) {
            char *v = next_line(&l);
            if (v && rot < OKE_MAX_BONES)
                sscanf(v, "%f %f %f", &pose->rot[rot][0], &pose->rot[rot][1], &pose->rot[rot][2]);
            rot++;
        }
    }
    /* Poses without their own data (the short "!POSE END" entries) reuse the first. */
    for (int i = 1; i < m->n_poses; i++)
        if (m->poses[i].root[1] == 0.0f && m->poses[0].root[1] != 0.0f)
            m->poses[i] = m->poses[0];
}

static void *read_whole(const char *path, int32_t *size)
{
    *size = fileio_file_size(path);
    if (*size <= 0)
        return NULL;
    char *buf = heap_alloc(&g_default_heap, (uint32_t)*size + 1, 16, "okefile", 0);
    if (!buf)
        return NULL;
    if (fileio_read_file(path, buf, (uint32_t)*size) != *size) {
        heap_free(&g_default_heap, buf);
        return NULL;
    }
    buf[*size] = 0;
    return buf;
}

const char *okemodel_set_name(int oke_id, int paint)
{
    if (oke_id < 0 || oke_id > OKE_ID_MAX || paint < 0 || paint > 4)
        return NULL;
    uint32_t p = gamedata_word(OKE_SET_TABLE + (uint32_t)(oke_id * 5 + paint) * 4, 0);
    return p ? gamedata_string(p) : NULL;
}

OkeModel *okemodel_load(int oke_id, int paint)
{
    const char *txt = table_string(oke_id, 0), *gmo = table_string(oke_id, 1);
    if (!txt || !gmo) {
        SDL_Log("[okemodel] no files for OKE id %d", oke_id);
        return NULL;
    }
    int n_paint = (int)gamedata_word(OKE_FILE_TABLE + (uint32_t)oke_id * 16 + 12, 0);
    uint32_t paint_list = gamedata_word(OKE_FILE_TABLE + (uint32_t)oke_id * 16 + 8, 0);
    if (paint < 0 || paint >= n_paint)
        paint = 0;
    const char *paint_path = paint_list ? gamedata_string(gamedata_word(paint_list + (uint32_t)paint * 4, 0)) : NULL;

    /* FUN_089f3064: the body's set for this paint (from the top of the default heap,
     * as the game's call); the models are copied out of it, so it is closed after. */
    const char *set_name = okemodel_set_name(oke_id, paint);
    int set = set_name ? fileset_open(set_name, &g_default_heap, 1, 0) : -1;
    OkeModel *m = SDL_calloc(1, sizeof *m);
    m->poses = SDL_calloc(MAX_POSES, sizeof *m->poses);
    m->parts = gmo_load(gmo);
    if (paint_path)
        m->paint = gmo_load(paint_path);
    if (set >= 0)
        fileset_close(set);
    if (!m->parts) {
        okemodel_free(m);
        return NULL;
    }
    gmo_use_textures_from(m->parts, m->paint);
    /* The PARTS2 part numbers count the intact parts; each has a "_b" (broken) twin. */
    for (int i = 0; i < gmo_part_count(m->parts) && m->n_intact < 64; i++) {
        const char *n = gmo_part_name(m->parts, i);
        size_t len = strlen(n);
        if (len < 2 || strcmp(n + len - 2, "_b"))
            m->part_of[m->n_intact++] = i;
    }
    int32_t size;
    char *text = read_whole(txt, &size);
    if (text) {
        parse_description(m, text, size);
        heap_free(&g_default_heap, text);
    }
    SDL_Log("[okemodel] id %d (%s): %d parts, %d bones, %d poses, paint %s", oke_id, okemodel_type_name(oke_id),
            m->n_intact, m->n_bones, m->n_poses, paint_path ? paint_path : "-");
    return m;
}

void okemodel_free(OkeModel *m)
{
    if (!m)
        return;
    gmo_free(m->parts);
    gmo_free(m->paint);
    SDL_free(m->poses);
    SDL_free(m);
}

int okemodel_pose_count(const OkeModel *m)
{
    return m ? m->n_poses : 0;
}

static void draw_bone(const OkeModel *m, const OkePose *p, int bone, int depth, uint32_t hidden)
{
    if (bone < 32 && (hidden >> bone) & 1)
        return;
    const OkeBone *b = &m->bones[bone];
    sceGumPushMatrix();
    ScePspFVector3 t = {b->pivot[0], b->pivot[1], b->pivot[2]};
    sceGumTranslate(&t);
    if (p) {
        sceGumRotateY(p->rot[bone][1]);
        sceGumRotateX(p->rot[bone][0]);
        sceGumRotateZ(p->rot[bone][2]);
    }
    if (b->part >= 0 && b->part < m->n_intact)
        gmo_draw_part(m->parts, m->part_of[b->part]);
    for (int i = 0; i < m->n_bones && depth < OKE_MAX_BONES; i++)
        if (m->bones[i].parent == bone && i != bone)
            draw_bone(m, p, i, depth + 1, hidden);
    sceGumPopMatrix();
}

void okemodel_draw(const OkeModel *m, int pose)
{
    okemodel_draw_hiding(m, pose, 0);
}

void okemodel_draw_hiding(const OkeModel *m, int pose, uint32_t hidden)
{
    if (!m)
        return;
    const OkePose *p = pose >= 0 && pose < m->n_poses ? &m->poses[pose] : NULL;
    sceGumPushMatrix();
    ScePspFVector3 root = {p ? p->root[0] : 0, p ? p->root[1] : -m->ground, p ? p->root[2] : 0};
    sceGumTranslate(&root);
    for (int i = 0; i < m->n_bones; i++)
        if (m->bones[i].parent < 0)
            draw_bone(m, p, i, 0, hidden);
    sceGumPopMatrix();
}
