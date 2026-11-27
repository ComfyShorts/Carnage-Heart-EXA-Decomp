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
    int cap = 0, n = 0, script = -1;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        if (line[0] == '!') {
            if (!strcmp(line, "!SCRIPT") && script + 1 < OKE_SCRIPTS) {
                script++;
                s->first[script] = n;
            } else if (strcmp(line, "!SCRIPT")) {
                if (script >= 0 && !strcmp(line, "!END"))
                    break;
            }
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
    SDL_Log("[motion] %s: %d scripts, %d commands", path, script + 1, n);
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
