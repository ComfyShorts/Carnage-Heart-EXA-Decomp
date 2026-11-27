/* See oke_script.h. */
#include "oke_script.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gamedata.h"
#include "packfile.h"

#define DEG (3.14159265f / 180.0f)

#define OKE_FILE_TABLE 0x0041666Cu /* COkeFile: {txt, gmo, paints, count} per body */
#define OKE_ID_MAX 0x24

#define PUSH_SCALE 67.5f         /* 0x08bd6930 */
#define SPIN_SCALE 2.6179938f    /* 0x08cce2d0 (0x40278d36), radians */
#define GROUND_BRAKE 12.0f       /* 0.4 x 29.999998 in FUN_0884df08 */
#define DRAG 0.3f                /* +0xb6f4 default (0x3e99999a) */
#define SPIN_DRAG 1.0f           /* +0xb714 default */

static const OkeScripts *g_cache[OKE_ID_MAX + 1];
static bool g_tried[OKE_ID_MAX + 1];

static char *read_text(const char *path, long *size)
{
    static Pack pack;
    static int state; /* 0 untried, 1 open, -1 failed */
    if (state == 0)
        state = pack_open(&pack, "disc0:/PSP_GAME/USRDIR") < 0 ? -1 : 1;
    if (state < 0)
        return NULL;
    const PackEntry *e = pack_find(&pack, path);
    if (!e || e->is_dir)
        return NULL;
    char *buf = SDL_malloc((size_t)e->size + 1);
    long n = buf ? pack_read(&pack, e, buf, (size_t)e->size) : -1;
    if (n <= 0) {
        SDL_free(buf);
        return NULL;
    }
    buf[n] = 0;
    *size = n;
    return buf;
}

#define MAX_POSES 512

/* One keyword line of !BODY or !POSE; the values are on the next line. */
static void body_line(OkeScripts *s, int section, const char *key, int *rot, int *mounts)
{
    if (section == 1 && !strcmp(key, "PARTS2")) {
        char *v = strtok(NULL, "\r\n");
        int parent, part;
        float p[3];
        if (v && s->n_bones < OKE_BONES && sscanf(v, "%d %d %f %f %f", &parent, &part, &p[0], &p[1], &p[2]) == 5) {
            s->parent[s->n_bones] = parent;
            memcpy(s->pivot[s->n_bones], p, sizeof p);
            s->n_bones++;
        }
    } else if (section == 1 && !strcmp(key, "BBOX2")) {
        char *v = strtok(NULL, "\r\n");
        OkeBox b;
        if (v && s->n_boxes < OKE_BOXES &&
            sscanf(v, "%d %d %f %f %f %f %f %f", &b.bone, &b.cls, &b.c[0], &b.c[1], &b.c[2], &b.s[0], &b.s[1],
                   &b.s[2]) == 8)
            s->box[s->n_boxes++] = b;
    } else if (section == 1 && !strcmp(key, "WEAPON2")) {
        char *v = strtok(NULL, "\r\n");
        int bone;
        if (v && *mounts < OKE_MOUNTS && sscanf(v, "%d", &bone) == 1)
            s->mount_bone[(*mounts)++] = bone;
    } else if (section == 1 && !strcmp(key, "EARTH")) {
        char *v = strtok(NULL, "\r\n");
        int bone;
        float x, y, z;
        if (v && sscanf(v, "%d %f %f %f", &bone, &x, &y, &z) == 4 && bone >= 0 && y < s->ground)
            s->ground = y;
    } else if (section == 2 && s->n_poses > 0 && !strcmp(key, "TRANSLATE")) {
        char *v = strtok(NULL, "\r\n");
        float *r = s->pose_root[s->n_poses - 1];
        if (v)
            sscanf(v, "%f %f %f", &r[0], &r[1], &r[2]);
    } else if (section == 2 && s->n_poses > 0 && !strcmp(key, "ROTATE")) {
        char *v = strtok(NULL, "\r\n");
        if (v && *rot < OKE_BONES) {
            float *r = s->pose_rot[s->n_poses - 1][*rot];
            sscanf(v, "%f %f %f", &r[0], &r[1], &r[2]);
        }
        (*rot)++;
    }
}

/* Bone frames as okemodel_draw builds them: translate to the pivot, then rotate
 * Y, X, Z (libgum post-multiplies). m[0..8] rotation (column-major), m[9..11]
 * translation. */
static void mat_mul(const float a[12], const float b[12], float out[12])
{
    float r[12];
    for (int c = 0; c < 3; c++)
        for (int row = 0; row < 3; row++)
            r[c * 3 + row] = a[0 * 3 + row] * b[c * 3 + 0] + a[1 * 3 + row] * b[c * 3 + 1] + a[2 * 3 + row] * b[c * 3 + 2];
    for (int row = 0; row < 3; row++)
        r[9 + row] = a[0 * 3 + row] * b[9] + a[1 * 3 + row] * b[10] + a[2 * 3 + row] * b[11] + a[9 + row];
    memcpy(out, r, sizeof r);
}

static void mat_rot(int axis, float a, float out[12])
{
    float c = cosf(a), s = sinf(a);
    memset(out, 0, 12 * sizeof(float));
    if (axis == 0) { /* X */
        out[0] = 1; out[4] = c; out[5] = s; out[7] = -s; out[8] = c;
    } else if (axis == 1) { /* Y */
        out[0] = c; out[2] = -s; out[4] = 1; out[6] = s; out[8] = c;
    } else { /* Z */
        out[0] = c; out[1] = s; out[3] = -s; out[4] = c; out[8] = 1;
    }
}

static void bone_frames(const OkeScripts *s, int pose, float frame[OKE_BONES][12])
{
    bool has = pose >= 0 && pose < s->n_poses;
    float root[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    if (has)
        memcpy(&root[9], s->pose_root[pose], 3 * sizeof(float));
    else
        root[10] = -s->ground;
    bool done[OKE_BONES] = {0};
    for (int pass = 0; pass < OKE_BONES; pass++)
        for (int b = 0; b < s->n_bones; b++) {
            int p = s->parent[b];
            if (done[b] || (p >= 0 && (p >= s->n_bones || !done[p])))
                continue;
            float local[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, s->pivot[b][0], s->pivot[b][1], s->pivot[b][2]};
            if (has) {
                float r[12];
                const float *a = s->pose_rot[pose][b];
                mat_rot(1, a[1], r); mat_mul(local, r, local);
                mat_rot(0, a[0], r); mat_mul(local, r, local);
                mat_rot(2, a[2], r); mat_mul(local, r, local);
            }
            mat_mul(p >= 0 ? frame[p] : root, local, frame[b]);
            done[b] = true;
        }
}

int oke_body_box_at(const OkeScripts *s, int pose, float x, float z, float centre[3])
{
    if (!s || s->n_boxes == 0)
        return -1;
    float frame[OKE_BONES][12];
    bone_frames(s, pose, frame);
    for (int i = 0; i < s->n_boxes; i++) {
        const OkeBox *b = &s->box[i];
        if (b->bone < 0 || b->bone >= s->n_bones)
            continue;
        const float *m = frame[b->bone];
        float w[3];
        for (int r = 0; r < 3; r++)
            w[r] = m[0 * 3 + r] * b->c[0] + m[1 * 3 + r] * b->c[1] + m[2 * 3 + r] * b->c[2] + m[9 + r];
        float d[3] = {x - w[0], 0.0f, z - w[2]}; /* tested at the box's own height */
        bool in = true;
        for (int axis = 0; axis < 3 && in; axis++) {
            float l = m[axis * 3 + 0] * d[0] + m[axis * 3 + 1] * d[1] + m[axis * 3 + 2] * d[2];
            in = fabsf(l) <= b->s[axis] * 0.5f;
        }
        if (in) {
            if (centre)
                memcpy(centre, w, sizeof w);
            return i;
        }
    }
    return -1;
}

static const OkeScripts *load(int body)
{
    uint32_t p = gamedata_word(OKE_FILE_TABLE + (uint32_t)body * 16, 0);
    const char *path = p ? gamedata_string(p) : NULL;
    if (!path)
        return NULL;
    long size;
    char *text = read_text(path, &size);
    if (!text) {
        SDL_Log("[motion] %s not found", path);
        return NULL;
    }
    OkeScripts *s = SDL_calloc(1, sizeof *s);
    s->body = body;
    s->pose_root = SDL_calloc(MAX_POSES, sizeof *s->pose_root);
    s->pose_rot = SDL_calloc(MAX_POSES, sizeof *s->pose_rot);
    for (int i = 0; i < OKE_MOUNTS; i++)
        s->mount_bone[i] = -1;
    int cap = 0, n = 0, script = -1, section = 0, rot = 0, mounts = 0;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        if (line[0] == '!') {
            section = !strcmp(line, "!BODY") ? 1 : !strcmp(line, "!POSE") ? 2 : 0;
            if (section == 2 && s->n_poses < MAX_POSES) {
                s->n_poses++;
                rot = 0;
            }
            if (!strcmp(line, "!SCRIPT") && script + 1 < OKE_SCRIPTS) {
                script++;
                s->first[script] = n;
            } else if (strcmp(line, "!SCRIPT")) {
                if (script >= 0 && !strcmp(line, "!END"))
                    break;
            }
            continue;
        }
        if (section == 1 || section == 2) {
            body_line(s, section, line, &rot, &mounts);
            continue;
        }
        if (script < 0 || strcmp(line, "COMMAND"))
            continue;
        char *v = strtok(NULL, "\r\n");
        int c, m, val, fr;
        if (!v || sscanf(v, "%d %d %d %d", &c, &m, &val, &fr) != 4)
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 1024;
            s->cmds = SDL_realloc(s->cmds, sizeof *s->cmds * (size_t)cap);
        }
        s->cmds[n++] = (OkeScriptCmd){(int16_t)c, (int16_t)m, (uint16_t)val, (uint16_t)fr};
        s->count[script]++;
    }
    SDL_free(text);
    /* Poses without their own data (the short "!POSE END" entries) reuse the first,
     * as okemodel.c draws them. */
    for (int i = 1; i < s->n_poses; i++)
        if (s->pose_root[i][1] == 0.0f && s->pose_root[0][1] != 0.0f) {
            memcpy(s->pose_root[i], s->pose_root[0], sizeof s->pose_root[0]);
            memcpy(s->pose_rot[i], s->pose_rot[0], sizeof s->pose_rot[0]);
        }
    SDL_Log("[motion] %s: %d scripts, %d commands, %d bones, %d boxes, %d poses", path, script + 1, n,
            s->n_bones, s->n_boxes, s->n_poses);
    return s;
}

const OkeScripts *oke_scripts_get(int body)
{
    if (body < 0 || body > OKE_ID_MAX)
        return NULL;
    if (!g_tried[body]) {
        g_tried[body] = true;
        g_cache[body] = load(body);
    }
    return g_cache[body];
}

void oke_motion_reset(OkeMotion *m, const OkeScripts *s)
{
    memset(m, 0, sizeof *m);
    m->s = s;
    m->script = -1;
}

void oke_motion_start(OkeMotion *m, int script)
{
    if (!m->s || script < 0 || script >= OKE_SCRIPTS)
        return;
    /* SetScript keeps a running script (FUN_0884321c returns early for the same id). */
    if (m->script == script)
        return;
    m->script = script;
    m->pc = 0;
    m->wait = 0.0f;
    m->loop_count = -1; /* *(oke + 0x20) = -1 in the chip handlers */
    memset(m->trigger, 0, sizeof m->trigger);
    m->loop_key = 0;
}

void oke_motion_stop(OkeMotion *m)
{
    if (m->script > 0) {
        m->script = 0;
        m->pc = 0;
        m->wait = 0.0f;
    }
}

bool oke_motion_busy(const OkeMotion *m)
{
    return m->s && m->script > 0 && m->pc < m->s->count[m->script];
}

/* FUN_08858c8c / FUN_08858d44 with the OKE's performance at 100%: unchanged. */
static float seconds(const OkeScriptCmd *c)
{
    return c->frames / 30.0f;
}

static void run_script(OkeMotion *m, float dt, float heading)
{
    if (!m->s || m->script < 0)
        return;
    const OkeScriptCmd *base = m->s->cmds + m->s->first[m->script];
    int count = m->s->count[m->script];
    float h = heading * DEG;
    float fwd[2] = {sinf(h), -cosf(h)}, right[2] = {cosf(h), sinf(h)};
    if (m->wait > 0.0f) {
        m->wait -= dt;
        if (m->wait > 0.0f)
            return;
        m->pc++;
    }
    for (int guard = 0; m->pc < count && guard < 64; guard++) {
        const OkeScriptCmd *c = &base[m->pc];
        float t = seconds(c);
        switch (c->cmd) {
        case 0x200: case 0x201: case 0x202: case 0x203: {
            int ch = c->cmd - 0x200;
            const float *axis = ch < 2 ? right : fwd;
            float a = (ch & 1 ? -1.0f : 1.0f) * c->value * PUSH_SCALE;
            if (t != 0.0f) {
                m->push[ch][0] = axis[0] * a;
                m->push[ch][1] = axis[1] * a;
            }
            m->push_t[ch] = t;
            m->pc++;
            break;
        }
        case 0x204: case 0x205: {
            int ch = c->cmd - 0x204;
            if (t != 0.0f)
                m->spin[ch] = (ch ? -1.0f : 1.0f) * c->value * SPIN_SCALE / DEG;
            m->spin_t[ch] = t;
            m->pc++;
            break;
        }
        case 0x209: /* FUN_08858fd8 */
            if (c->mod == 4) {
                if (m->loop_key != c->frames) {
                    m->loop_key = c->frames;
                    m->loop_count = c->frames;
                }
                if (m->loop_count != 0) {
                    if (m->loop_count > 0)
                        m->loop_count--;
                    m->pc -= c->value;
                    if (m->pc < 0)
                        m->pc = 0;
                    break;
                }
            }
            m->pc++;
            break;
        case 0x20b:
            if (c->value >= 1 && c->value <= 5)
                m->trigger[c->value - 1]++;
            m->pc++;
            break;
        case 0x211: /* holds */
            return;
        default:
            if (c->cmd < 0x200) {
                /* A pose: the game waits until the body has blended to it. The
                 * blend isn't modelled; the command's frame count stands in. */
                m->pose = c->cmd;
                m->wait = t > 0.0f ? t : 1.0f / 30.0f;
                return;
            }
            m->pc++; /* 0x206 pose speed, 0x207/0x208/0x20a-0x210: not modelled */
            break;
        }
    }
}

void oke_motion_step(OkeMotion *m, float dt, float *x, float *y, float *heading)
{
    run_script(m, dt, *heading);
    /* FUN_0884df08, in 1/60 s steps. */
    for (float left = dt; left > 1e-6f; left -= 1.0f / 60.0f) {
        float st = left < 1.0f / 60.0f ? left : 1.0f / 60.0f;
        float ax = 0, ay = 0, alpha = 0;
        bool pushed = false;
        for (int i = 0; i < 6; i++)
            if (m->push_t[i] > 0.0f) {
                ax += m->push[i][0];
                ay += m->push[i][1];
                pushed = true;
                m->push_t[i] -= st;
                if (m->push_t[i] < 1e-5f)
                    m->push_t[i] = 0.0f;
            }
        for (int i = 0; i < 2; i++)
            if (m->spin_t[i] > 0.0f) {
                alpha += m->spin[i];
                pushed = true;
                m->spin_t[i] -= st;
                if (m->spin_t[i] < 1e-5f)
                    m->spin_t[i] = 0.0f;
            }
        if (!pushed) {
            ax -= GROUND_BRAKE * m->vx;
            ay -= GROUND_BRAKE * m->vy;
            alpha -= GROUND_BRAKE * m->omega;
        }
        m->vx += ax * st;
        m->vy += ay * st;
        float k = expf(-DRAG * st);
        m->vx *= k;
        m->vy *= k;
        m->omega += alpha * st;
        m->omega *= expf(-SPIN_DRAG * st);
        *x += m->vx * st;
        *y += m->vy * st;
        *heading += m->omega * st;
    }
    while (*heading > 180.0f)
        *heading -= 360.0f;
    while (*heading < -180.0f)
        *heading += 360.0f;
}
