/* See scene.h. Hand-written from the functions named there. */
#include "scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "fileio.h"
#include "font.h"
#include "gamedata.h"
#include "gu.h"
#include "pad.h"
#include "picture.h"
#include "psp_shim.h"
#include "savedata.h"
#include "sound.h"
#include "ui.h"

/* ---- the script file (CEventScriptAnalysis) ---- */

#define CHS_MAX (512 * 1024)

static struct {
    uint8_t *data;
    int32_t size;
    uint32_t words, pool; /* +0x14, +0x1C */
    uint32_t pos;         /* the next word */
    bool bad;
} f;

static uint32_t rd32(uint32_t off)
{
    if (off + 4 > (uint32_t)f.size) {
        f.bad = true;
        return 0;
    }
    uint32_t v;
    memcpy(&v, f.data + off, 4);
    return v;
}

/* FUN_088df1dc / FUN_088df1f0: the next word. */
static uint32_t next_word(void)
{
    uint32_t v = rd32(f.pos);
    f.pos += 4;
    return v;
}
static int next_int(void) { return (int)next_word(); }

/* FUN_088df204: a string, as an offset into the pool. */
static const char *next_string(void)
{
    uint32_t off = next_word();
    uint32_t at = f.pool + off;
    if (at >= (uint32_t)f.size || !memchr(f.data + at, 0, (size_t)f.size - at)) {
        f.bad = true;
        return "";
    }
    return (const char *)f.data + at;
}

static void file_free(void)
{
    free(f.data);
    memset(&f, 0, sizeof f);
}

/* Load (FUN_088deef0) with the header check (FUN_088df10c: "CHS " and "1.10"), then
 * Seek (FUN_088df220): the script table's (name, first word) pairs. */
static bool file_open(const char *path, const char *id)
{
    file_free();
    int32_t size = fileio_file_size(path);
    if (size < 0x20 || size > CHS_MAX) {
        SDL_Log("[scene] %s: not on the disc", path);
        return false;
    }
    f.data = malloc((size_t)size + 1);
    if (!f.data || fileio_read_file(path, f.data, (uint32_t)size) != size) {
        SDL_Log("[scene] %s: can't be read", path);
        file_free();
        return false;
    }
    f.data[size] = 0;
    f.size = size;
    if (memcmp(f.data, "CHS ", 4) || memcmp(f.data + 4, "1.10", 4)) {
        SDL_Log("[scene] %s: not a CHS 1.10 file", path);
        file_free();
        return false;
    }
    int count = (int)rd32(8);
    uint32_t table = rd32(0x10);
    f.words = rd32(0x14);
    f.pool = rd32(0x1C);
    for (int i = 0; i < count && !f.bad; i++) {
        uint32_t name = f.pool + rd32(table + 8u * (uint32_t)i);
        if (name < (uint32_t)f.size && !strcmp((const char *)f.data + name, id)) {
            f.pos = f.words + 4u * rd32(table + 8u * (uint32_t)i + 4);
            return !f.bad;
        }
    }
    SDL_Log("[scene] %s: no script \"%s\"", path, id);
    file_free();
    return false;
}

/* ---- CEventScriptPicture ---- */

typedef struct ScenePic {
    Picture *pic;     /* +0x00 */
    int x, y;         /* +0x04, +0x08 */
    uint32_t color;   /* +0x10: the tint, alpha scaled by the fade */
    uint32_t back;    /* +0x14: a rectangle drawn under it (the colour faded from) */
    bool visible;     /* +0x18 */
    int shake;        /* +0x1C: amplitude, 0 none */
    int ox, oy;       /* +0x20, +0x24 */
    int shake_frames; /* +0x28: -1 for ever */
    int fade_state;   /* +0x2C: 0 none, 1 in, 2 out */
    int fade;         /* +0x30: 0..100 */
} ScenePic;

#define PORTRAITS 3

/* A translucent rectangle: alpha blending on for the untextured sprite. */
static void fill_alpha(int x, int y, int fw, int fh, uint32_t abgr)
{
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(0, 2, 3, 0xFFFFFFFF, 0xFFFFFFFF); /* add, src alpha, 1 - src alpha */
    ui_fill(x, y, fw, fh, abgr);
}

static void pic_free(ScenePic *p)
{
    if (p->pic)
        picture_release(p->pic);
    memset(p, 0, sizeof *p);
}

/* FUN_088e0a44 + FUN_088e0a88 + FUN_088e0cec: the picture, placed by `pos` (0x200 left,
 * 0x100 right, 0 centred) and on the bottom edge. */
static void pic_load(ScenePic *p, const char *path, int pos)
{
    pic_free(p);
    p->color = 0xFFFFFFFFu;
    p->visible = true;
    p->fade = 100;
    p->pic = path && *path ? picture_load(path) : NULL;
    if (!p->pic) {
        SDL_Log("[scene] picture %s missing", path ? path : "(none)");
        return;
    }
    int w = picture_width(p->pic), h = picture_height(p->pic);
    p->x = pos == 0x200 ? 0 : pos == 0x100 ? 480 - w : (480 - w) / 2;
    p->y = 272 - h;
}

static void pic_fade_in(ScenePic *p) /* FUN_088e0eb0 */
{
    if (p->pic) {
        p->fade = 0;
        p->visible = true;
        p->fade_state = 1;
    }
}

static void pic_fade_out(ScenePic *p) /* FUN_088e0ed0 */
{
    if (p->pic) {
        p->fade = 100;
        p->fade_state = 2;
    }
}

static bool pic_fading(const ScenePic *p) { return p->pic && (p->fade_state == 1 || p->fade_state == 2); }

/* FUN_088e0f28: the shake's offsets, then 5% of the fade a frame. */
static void pic_update(ScenePic *p)
{
    if (!p->pic)
        return;
    if (p->shake > 0 && p->shake_frames != 0) {
        p->ox = rand() % (p->shake + 1) - rand() % (p->shake + 1);
        p->oy = rand() % (p->shake + 1) - rand() % (p->shake + 1);
    } else {
        p->ox = p->oy = 0;
    }
    if (p->shake_frames != 0 && p->shake_frames != -1)
        p->shake_frames--;
    if (p->fade_state == 1) {
        if (p->fade < 100) {
            p->fade += 5;
        } else {
            p->fade = 100;
            p->fade_state = 0;
        }
    } else if (p->fade_state == 2) {
        if (p->fade < 1) {
            p->fade = 0;
            p->visible = false;
            p->fade_state = 0;
        } else {
            p->fade -= 5;
        }
    }
}

/* FUN_088e1064: the colour under it (FUN_08ae3e94), then the picture. */
static void pic_draw(const ScenePic *p)
{
    if (!p->pic)
        return;
    int w = picture_width(p->pic), h = picture_height(p->pic);
    if (p->back >> 24) {
        fill_alpha(p->x + p->ox, p->y + p->oy, w, h, p->back);
    }
    if (!p->visible)
        return;
    uint32_t a = ((p->color >> 24) * (uint32_t)p->fade) / 100u;
    picture_draw(p->pic, p->x + p->ox, p->y + p->oy, 0, BLEND_ALPHA, a << 24 | (p->color & 0xFFFFFFu), 0, 0);
}

/* ---- CEventScriptWnd ---- */

/* updateCommand's states (+0x40). */
enum {
    ST_DONE = 0,
    ST_RUN = 1,
    ST_PAGE = 2,   /* a page shown, waiting for the pad */
    ST_FADE = 3,   /* waiting for the pictures' fades */
    ST_HIDDEN = 4, /* the window removed with square */
    ST_NOTICE = 9, /* a notice ("Mission added.") */
    ST_WAIT = 10,  /* op 0x1C: frames */
};

/* The window styles (+0x44, op 0xD). */
enum { WND_NORMAL = 1, WND_NONE = 2, WND_FULL = 3 };

#define LINES_NORMAL 4
#define LINES_FULL 12
#define LINE_MAX 256
#define IF_MAX 16

#define COLOR_TABLE 0x004440D0u /* 0x08c480d0: three bytes a colour */
#define MSG_TABLE 0x004766B0u   /* 0x08c7a6b0 (FUN_088af824) */
#define MSG_HELP 0x2A           /* "#!kBacklog #sRemove Window #l#rSkip" (no backlog yet) */
#define MSG_SKIPPING 0x31       /* "#l#rSkipping" */
/* Op 0x10's tables (CEventScriptVariable): the battle missions, the parts stock and the
 * menu limits (pairs of group and item). */
#define VAR_BATTLES 0x00479280u /* 0x08c7d280, 0x46 words */
#define VAR_STOCK 0x00478AB0u   /* 0x08c7cab0, 0xDC pairs */
#define VAR_LIMITS 0x004784C0u  /* 0x08c7c4c0, 0xBE pairs */

static struct {
    bool active;
    int state;
    int wait;           /* +0x21B0 */
    int style;          /* +0x44 */
    char label[LINE_MAX];               /* +0x60C: the speaker */
    char lines[LINES_FULL][LINE_MAX];   /* +0x610 / +0x620 */
    int line_count;                     /* +0x650 */
    char shown_label[LINE_MAX];         /* +0x654 */
    char shown[LINES_FULL * LINE_MAX];  /* +0x854 */
    int if_depth;                       /* +0x88 */
    int if_state[IF_MAX + 1];           /* +0x44.. (0 none, 1 taking, 2 not yet, 3 done) */
    int locals[10];                     /* +0x21B4 */
    ScenePic bg;                        /* +0x217C */
    ScenePic chara[PORTRAITS];          /* +0x2180 */
    Picture *window;                    /* +0x11C: parts/bf/sd_wind1.gim */
    Picture *page;                      /* +0x120: parts/bf/sd_page0.gim */
    char notice[LINE_MAX];
    UiAsk ask;
    float t;
    bool voice_warned;
} w;

static const char *ptr_string(uint32_t ptr_addr)
{
    uint32_t p = gamedata_word(ptr_addr, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

/* FUN_08b168e0: the game's colour `index` (RGB bytes) with `alpha`, as ABGR. */
static uint32_t game_color(int index, int alpha)
{
    uint32_t a = COLOR_TABLE + 3u * (uint32_t)index;
    uint8_t b[3];
    for (int i = 0; i < 3; i++) {
        uint32_t word = gamedata_word((a + (uint32_t)i) & ~3u, 0);
        b[i] = (uint8_t)(word >> (8 * ((a + (uint32_t)i) & 3)));
    }
    return (uint32_t)alpha << 24 | (uint32_t)b[2] << 16 | (uint32_t)b[1] << 8 | b[0];
}

/* FUN_088e4bd0: L or R held skips fades, waits and pages. */
static bool skipping(void) { return pad_held(PSP_CTRL_LTRIGGER) || pad_held(PSP_CTRL_RTRIGGER); }

/* Commands only act while the if-stack takes them (FUN_088e6e30's states 0 and 1). */
static bool taking(void) { return w.if_state[w.if_depth] == 0 || w.if_state[w.if_depth] == 1; }

static void clear_lines(void) /* FUN_088e3fb4 / FUN_088e3fec / FUN_088e4090 */
{
    w.label[0] = 0;
    w.line_count = 0;
    memset(w.lines, 0, sizeof w.lines);
    w.shown_label[0] = 0;
    w.shown[0] = 0;
}

/* FUN_088e3d94 / FUN_088e3df4: a line into the page (4 or 12 at most). */
static void push_line(const char *s)
{
    int max = w.style == WND_FULL ? LINES_FULL : LINES_NORMAL;
    if (w.line_count < max)
        snprintf(w.lines[w.line_count++], LINE_MAX, "%s", s);
    else
        snprintf(w.lines[max - 1], LINE_MAX, "%s", s);
}

/* FUN_088e35d8: the "\\" line shows the page: the label and the lines joined. */
static void show_page(void)
{
    snprintf(w.shown_label, sizeof w.shown_label, "%s", w.style == WND_FULL ? "" : w.label);
    size_t n = 0;
    w.shown[0] = 0;
    for (int i = 0; i < w.line_count; i++) {
        size_t l = strlen(w.lines[i]);
        if (n + l >= sizeof w.shown)
            break;
        memcpy(w.shown + n, w.lines[i], l + 1);
        n += l;
    }
    w.state = ST_PAGE;
    w.t = 0.0f;
}

/* Op 0x10's flagged forms (FUN_088e4a20, FUN_088e4784, FUN_088e48a0): `op` 5 sets,
 * 6 ORs. */
static void var_battle(int op, int index, int value)
{
    if (index < 1 || index > 0x46)
        return;
    uint32_t m = gamedata_word(VAR_BATTLES + 4u * (uint32_t)(index - 1), 0xFFFFFFFFu);
    if (m == 0xFFFFFFFFu)
        return;
    if (value == 0) {
        if (op == 5)
            savedata_set_story_battle(-1);
    } else if (op == 5) {
        savedata_set_story_battle((int)m);
    } else if (op == 6) {
        savedata_set_story_battle(savedata_story_battle() | (int)m);
    }
    SDL_Log("[scene] battle mission now %d", savedata_story_battle());
}

static void var_stock(int op, int index, int value)
{
    if (index < 1 || index > 0xDC)
        return;
    int group = (int)gamedata_word(VAR_STOCK + 8u * (uint32_t)(index - 1), 0xFFFFFFFFu);
    int id = (int)gamedata_word(VAR_STOCK + 8u * (uint32_t)(index - 1) + 4, 0xFFFFFFFFu);
    if (group < 0 || id < 0)
        return;
    if (op == 5)
        savedata_set_stock(group, id, value);
    else if (op == 6)
        savedata_set_stock(group, id, savedata_stock(group, id) | value);
}

/* FUN_0895e1cc: a menu group's word base (FUN_0895e35c .. FUN_0895e9a4). */
static const uint8_t k_limit_base[9] = {0x00, 0x06, 0x13, 0x22, 0x30, 0x35, 0x5F, 0x67, 0x70};

static void var_limit(int op, int index, int value)
{
    if (index < 1 || index > 0xBE)
        return;
    int group = (int)gamedata_word(VAR_LIMITS + 8u * (uint32_t)(index - 1), 0xFFFFFFFFu);
    int item = (int)gamedata_word(VAR_LIMITS + 8u * (uint32_t)(index - 1) + 4, 0xFFFFFFFFu);
    if (group < 0 || group > 8 || item < 0)
        return;
    int word = k_limit_base[group] + item;
    if (op == 5)
        savedata_set_menu_limit(word, value);
    else if (op == 6)
        savedata_set_menu_limit(word, savedata_menu_limit(word) | value);
}

/* FUN_088e4b24: the script's own ten variables. */
static void var_local(int op, int index, int value)
{
    if (index < 1 || index > 10 || op < 1 || op > 6)
        return;
    int *v = &w.locals[index - 1];
    switch (op) {
    case 1: *v += value; break;
    case 2: *v -= value; break;
    case 3: (*v)++; break;
    case 4: (*v)--; break;
    case 5: *v = value; break;
    case 6: *v |= value; break;
    }
}

/* FUN_088df724: op 0x1F / 0x20's menu numbers to CEventScriptField's. */
static int field_menu(int menu)
{
    static const uint8_t k_menus[] = {10, 0x14, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x28, 0x29,
                                      0x2A, 0x32, 0x33, 0x34, 0x35, 0x3C, 0x46, 0x50, 0x5A};
    for (int i = 0; i < (int)sizeof k_menus; i++)
        if (k_menus[i] == menu)
            return i;
    return -1;
}

static void open_notice(const char *text)
{
    snprintf(w.notice, sizeof w.notice, "%s", text);
    size_t n = strlen(w.notice);
    while (n && (w.notice[n - 1] == '\n' || w.notice[n - 1] == ' '))
        w.notice[--n] = 0;
    w.state = ST_NOTICE;
    w.t = 0.0f;
}

/* FUN_088e6e30: commands until one waits. */
static void run_commands(void)
{
    for (int guard = 0; guard < 100000 && w.state == ST_RUN; guard++) {
        uint32_t op = next_word();
        if (f.bad) {
            SDL_Log("[scene] the script ran past its file");
            w.state = ST_DONE;
            return;
        }
        switch (op) {
        case 0x00: /* the end (+0x8C 0: done; 1 asks to read the tutorial: lessons) */
            w.state = ST_DONE;
            return;
        case 0x01: { /* a line; "\\" shows the page */
            const char *s = next_string();
            if (!taking() || !*s)
                break;
            if (!strcmp(s, "\\")) {
                show_page();
                return;
            }
            if (w.style == WND_NORMAL || w.style == WND_FULL)
                push_line(s);
            break;
        }
        case 0x02: { /* the speaker */
            const char *s = next_string();
            if (taking() && *s && w.style != WND_FULL)
                snprintf(w.label, sizeof w.label, "%s", s);
            break;
        }
        case 0x03: { /* a portrait: slot & 0xF, place & 0xF00, 0x4000 fades in */
            const char *path = next_string();
            uint32_t fl = next_word();
            int slot = (int)(fl & 0xF);
            if (!taking() || slot >= PORTRAITS)
                break;
            pic_load(&w.chara[slot], path, (int)(fl & 0xF00));
            if ((fl & 0xF000) == 0x4000 && !skipping()) {
                pic_fade_in(&w.chara[slot]);
                w.state = ST_FADE;
                return;
            }
            break;
        }
        case 0x04: { /* move a portrait */
            uint32_t fl = next_word();
            int slot = (int)(fl & 0xF);
            if (taking() && slot < PORTRAITS && w.chara[slot].pic) {
                ScenePic *p = &w.chara[slot];
                int pw = picture_width(p->pic);
                int pos = (int)(fl & 0xF00);
                p->x = pos == 0x200 ? 0 : pos == 0x100 ? 480 - pw : (480 - pw) / 2;
            }
            break;
        }
        case 0x05: { /* hide a portrait, 0x4000 fading */
            uint32_t fl = next_word();
            int slot = (int)(fl & 0xF);
            if (!taking() || slot >= PORTRAITS || !w.chara[slot].pic)
                break;
            if ((fl & 0xF000) == 0x4000 && !skipping()) {
                pic_fade_out(&w.chara[slot]);
                w.state = ST_FADE;
                return;
            }
            w.chara[slot].visible = false;
            break;
        }
        case 0x06: { /* a background: 0x4000 from black, 0x2000 from white */
            const char *path = next_string();
            uint32_t fl = next_word();
            if (!taking())
                break;
            pic_load(&w.bg, path, (int)(fl & 0xF00));
            uint32_t kind = fl & 0xF000;
            if (kind == 0x4000 || kind == 0x2000) {
                w.bg.back = kind == 0x2000 ? 0xFFFFFFFFu : 0;
                if (!skipping()) {
                    pic_fade_in(&w.bg);
                    w.state = ST_FADE;
                    return;
                }
            }
            break;
        }
        case 0x07: { /* hide the background */
            uint32_t fl = next_word();
            if (!taking() || !w.bg.pic)
                break;
            uint32_t kind = fl & 0xF000;
            if ((kind == 0x4000 || kind == 0x2000) && !skipping()) {
                w.bg.back = kind == 0x2000 ? 0xFFFFFFFFu : 0;
                pic_fade_out(&w.bg);
                w.state = ST_FADE;
                return;
            }
            w.bg.visible = false;
            break;
        }
        case 0x08: { /* 1 shakes the background (8 px, for ever), 2 stops */
            int v = next_int();
            if (taking() && w.bg.pic) {
                if (v == 1) {
                    w.bg.shake = 8;
                    w.bg.shake_frames = -1;
                } else if (v == 2) {
                    w.bg.shake = w.bg.shake_frames = 0;
                }
            }
            break;
        }
        case 0x09: { /* the background's tint */
            uint32_t c = next_word();
            if (taking() && w.bg.pic)
                w.bg.color = c;
            break;
        }
        case 0x0A:
            if (taking()) {
                show_page();
                return;
            }
            break;
        case 0x0B: { /* music (story.c / part 4) */
            int track = next_int();
            next_int();
            if (taking())
                SDL_Log("[scene] music track %d (not played yet)", track);
            break;
        }
        case 0x0C: /* stop the music */
            next_int();
            next_int();
            break;
        case 0x0D: { /* the window's style; a change clears the page */
            int style = next_int();
            if (!taking())
                break;
            if (style != w.style)
                clear_lines();
            w.style = style;
            break;
        }
        case 0x0E: { /* a voice */
            const char *s = next_string();
            if (taking() && !w.voice_warned) {
                SDL_Log("[scene] voices aren't played yet (first: %s)", s);
                w.voice_warned = true;
            }
            break;
        }
        case 0x0F: /* the end of a message: the voice stops */
            break;
        case 0x10: { /* a variable: op, flags | index, value */
            int vop = next_int();
            uint32_t fl = next_word();
            int value = next_int();
            if (!taking())
                break;
            int index = (int)(fl & 0xFFFF);
            if (fl & 0x10000000u)
                SDL_Log("[scene] lesson state %d op %d value %d: lessons aren't ported", index, vop, value);
            else if (fl & 0x20000000u)
                var_stock(vop, index, value);
            else if (fl & 0x40000000u)
                var_limit(vop, index, value);
            else if (fl & 0x80000000u)
                var_battle(vop, index, value);
            else
                var_local(vop, (int)fl, value);
            break;
        }
        case 0x12: /* if: the lesson-state test (FUN_088e41e4) isn't ported: not taken */
        case 0x13: {
            next_int();
            next_int();
            next_int();
            if (op == 0x12) {
                if (w.if_depth < IF_MAX)
                    w.if_depth++;
                w.if_state[w.if_depth] = taking() || w.if_depth == 1 ? 2 : 3;
            } else if (w.if_state[w.if_depth] == 1) {
                w.if_state[w.if_depth] = 3;
            }
            break;
        }
        case 0x14: /* else */
            if (w.if_depth > 0)
                w.if_state[w.if_depth] = w.if_state[w.if_depth] == 2 ? 1 : 3;
            break;
        case 0x15: /* end if */
            if (w.if_depth > 0)
                w.if_state[w.if_depth--] = 0;
            break;
        case 0x18:
            next_string();
            break;
        case 0x1A: { /* a sound effect: id, volume % in the high half */
            uint32_t v = next_word();
            if (taking() && !skipping())
                sound_se((int)(v & 0xFFFF), (v >> 16) ? (float)(v >> 16) / 100.0f : 1.0f);
            break;
        }
        case 0x1B: /* stop the sound effects */
            break;
        case 0x1C: { /* wait N frames */
            int n = next_int();
            if (taking() && !skipping() && n > 0) {
                w.wait = n;
                w.state = ST_WAIT;
                return;
            }
            break;
        }
        case 0x1D: { /* shake a portrait for N frames */
            int frames = next_int();
            uint32_t fl = next_word();
            int slot = (int)(fl & 0xF);
            if (taking() && slot < PORTRAITS && w.chara[slot].pic) {
                w.chara[slot].shake = 8;
                w.chara[slot].shake_frames = frames;
            }
            break;
        }
        case 0x1E:
            next_string();
            break;
        case 0x1F: { /* hide a script on a menu point (CEventScriptField) */
            int menu = next_int();
            int key = next_int();
            next_int();
            const char *id = next_string();
            if (taking())
                savedata_story_field_hide(field_menu(menu), key, id);
            break;
        }
        case 0x20: {
            int menu = next_int();
            int key = next_int();
            if (taking())
                savedata_story_field_clear(field_menu(menu), key);
            break;
        }
        case 0x21: { /* a notice; the number is a jingle (10 when none) */
            const char *s = next_string();
            int se = next_int();
            if (taking()) {
                sound_se(se < 1 ? 10 : se, 1.0f);
                open_notice(s);
                return;
            }
            break;
        }
        case 0x22:
        case 0x2B:
        case 0x2C:
        case 0x2D: {
            int v = next_int();
            if (!taking())
                break;
            if (op == 0x2B && v >= 1 && v <= 32)
                savedata_set_current_card(v - 1);
            else if (op == 0x2D && v >= 1 && v <= 0xBE)
                savedata_story_set_2b28(v - 1);
            break;
        }
        case 0x2E:
            if (taking())
                savedata_story_set_2b28(-1);
            break;
        case 0x23:
            next_int();
            next_int();
            next_int();
            break;
        case 0x25:
        case 0x27:
            next_string();
            next_int();
            break;
        case 0x29:
            next_int();
            next_int();
            next_int();
            next_int();
            break;
        case 0x24: case 0x26: case 0x28: case 0x2A: /* the lessons' pointers and labels */
            break;
        case 0x2F:
            if (taking()) {
                show_page();
                return;
            }
            break;
        default:
            SDL_Log("[scene] unknown command 0x%X", (unsigned)op);
            w.state = ST_DONE;
            return;
        }
    }
}

bool scene_start(const char *path, const char *id)
{
    scene_stop();
    if (!file_open(path, id))
        return false;
    w.active = true;
    w.style = WND_NORMAL;
    w.state = ST_RUN;
    if (!w.window)
        w.window = picture_load("parts/bf/sd_wind1.gim");
    if (!w.page)
        w.page = picture_load("parts/bf/sd_page0.gim");
    SDL_Log("[scene] %s: %s", path, id);
    return true;
}

void scene_stop(void)
{
    pic_free(&w.bg);
    for (int i = 0; i < PORTRAITS; i++)
        pic_free(&w.chara[i]);
    Picture *win = w.window, *page = w.page;
    memset(&w, 0, sizeof w);
    w.window = win;
    w.page = page;
    file_free();
}

int scene_update(float dt)
{
    if (!w.active)
        return 0;
    w.t += dt;
    switch (w.state) {
    case ST_WAIT:
        if (w.wait > 0 && !skipping())
            w.wait--;
        else
            w.state = ST_RUN;
        break;
    case ST_FADE: { /* until every picture has finished */
        bool busy = pic_fading(&w.bg);
        for (int i = 0; i < PORTRAITS; i++)
            busy |= pic_fading(&w.chara[i]);
        if (!busy || skipping())
            w.state = ST_RUN;
        break;
    }
    case ST_PAGE: /* FUN_088e9134: circle (or L/R held) next, square removes the window */
        if (pad_pressed(PSP_CTRL_SQUARE)) {
            sound_se(SE_CANCEL, 1.0f);
            w.state = ST_HIDDEN;
        } else if ((w.t > 0.1f && pad_pressed(PSP_CTRL_CIRCLE)) || skipping()) {
            clear_lines();
            w.state = ST_RUN;
        }
        break;
    case ST_HIDDEN: /* FUN_088e4d20: any button brings it back */
        if (pad_pressed(PSP_CTRL_CIRCLE | PSP_CTRL_CROSS | PSP_CTRL_SQUARE | PSP_CTRL_TRIANGLE))
            w.state = ST_PAGE;
        break;
    case ST_NOTICE: /* FUN_088e4f90: circle or cross */
        if (w.t > 0.1f && pad_pressed(PSP_CTRL_CIRCLE | PSP_CTRL_CROSS)) {
            sound_se(SE_CANCEL, 1.0f);
            w.state = ST_RUN;
        }
        break;
    default:
        break;
    }
    if (w.state == ST_RUN)
        run_commands();
    pic_update(&w.bg);
    for (int i = 0; i < PORTRAITS; i++)
        pic_update(&w.chara[i]);
    if (w.state == ST_DONE) {
        w.active = false;
        return 0;
    }
    return 1;
}

/* ---- drawing ---- */

static void slice(int x, int y, int dw, int dh, int u, int v, int sw, int sh)
{
    picture_draw_ex(w.window, x, y, 0, dw, dh, u, v, sw, sh, BLEND_ALPHA, 0xFFFFFFFFu, 0, 0, 0, 0);
}

/* FUN_088eb690, style 1: the window from parts/bf/sd_wind1.gim in four rows (the tab with
 * the name, then the body), the name at (7, 0xAC), the text at (7, 0xC3). */
static void draw_window(void)
{
    if (!w.window || (!w.shown_label[0] && !w.shown[0]))
        return;
    const int x = 0, y = 0;
    int yy = y + 0xA7;
    slice(x + 1, yy, 6, 0x1B, 0, 0, 6, 0x1B);
    slice(x + 7, yy, 0x78, 0x1B, 6, 0, 6, 0x1B);
    slice(x + 0x7F, yy, 0x1A, 0x1B, 0xC, 0, 0x1A, 0x1B);
    slice(x + 0x99, yy, 0x13B, 0x1B, 0x26, 0, 0xD, 0x1B);
    slice(x + 0x1D4, yy, 0xC, 0x1B, 0x33, 0, 0xC, 0x1B);
    static const struct { int dy, h, v, sh; } k_rows[3] = {{0xC2, 0x38, 0x1B, 8}, {0xFA, 0xC, 0x23, 8}, {0x106, 0xC, 0x2B, 0xC}};
    for (int r = 0; r < 3; r++) {
        yy = y + k_rows[r].dy;
        slice(x + 1, yy, 6, k_rows[r].h, 0, k_rows[r].v, 6, k_rows[r].sh);
        slice(x + 7, yy, 0x1CD, k_rows[r].h, 6, k_rows[r].v, 0x2D, k_rows[r].sh);
        slice(x + 0x1D4, yy, 0xC, k_rows[r].h, 0x33, k_rows[r].v, 0xC, k_rows[r].sh);
    }
    /* The styles of Initialize: font 0, 2 px between lines, colour 8. */
    TextStyle st = text_style(0, 6, 12, 12, 0, 2, game_color(8, 0xFF), 0);
    if (w.shown_label[0])
        text_draw(w.shown_label, &st, x + 7, y + 0xAC, 0, NULL, NULL);
    if (w.shown[0])
        text_draw(w.shown, &st, x + 7, y + 0xC3, 0, NULL, NULL);
    if (w.state == ST_PAGE && w.page) { /* the page cursor, blinking */
        float a = ui_pulse(w.t);
        uint32_t alpha = (uint32_t)(a * 255.0f) & 0xFF;
        picture_draw(w.page, 0x1CE, 0xFC, 0, BLEND_ALPHA, alpha << 24 | 0xFFFFFF, 0, 0);
    }
}

/* Style 3: the half-dark screen (the colour window at +0x404, alpha 0x80) and the text
 * at (6, 0x1C) in colour 0. */
static void draw_full(void)
{
    if (!w.shown[0])
        return;
    fill_alpha(0, 0, 480, 272, 0x80000000u);
    TextStyle st = text_style(0, 6, 12, 12, 0, 2, game_color(0, 0xFF), 0);
    text_draw(w.shown, &st, 6, 0x1C, 0, NULL, NULL);
}

/* The game's button guide with the buttons as words. */
static const char *guide(const char *s)
{
    static char out[160];
    size_t n = 0;
    for (; s && *s && n + 16 < sizeof out; s++) {
        if (*s != '#' || !s[1]) {
            out[n++] = *s;
            continue;
        }
        s++;
        bool off = *s == '!';
        if (off && s[1])
            s++;
        const char *b;
        switch (*s) {
        case 'k': b = off ? "" : "Triangle: "; break;
        case 's': b = "Square: "; break;
        case 'l': b = "L/"; break;
        case 'r': b = "R: "; break;
        case 'o': b = "Circle: "; break;
        case 'x': b = "Cross: "; break;
        default: b = ""; break;
        }
        if (off) { /* the greyed-out item: left out up to the next full-width space */
            while (s[1] && !((uint8_t)s[1] == 0x81 && (uint8_t)s[2] == 0x40))
                s++;
            continue;
        }
        size_t l = strlen(b);
        memcpy(out + n, b, l);
        n += l;
    }
    out[n] = 0;
    return out;
}

void scene_draw(void)
{
    if (!w.active)
        return;
    pic_draw(&w.bg);
    for (int i = PORTRAITS - 1; i >= 0; i--)
        pic_draw(&w.chara[i]);
    if (w.state != ST_HIDDEN) {
        if (w.style == WND_NORMAL)
            draw_window();
        else if (w.style == WND_FULL)
            draw_full();
    }
    if (w.state == ST_NOTICE) {
        ui_frame(ui_part(PART_BF_SWND4), 120, 110, 240, 40, 8, 1.0f);
        TextStyle st = text_style(0, 6, 12, 12, 0, 2, game_color(8, 0xFF), 0);
        int tw = ui_text_width(w.notice, &st);
        ui_text(w.notice, &st, 240 - tw / 2, 124, 1.0f);
    }
    /* The guide (FUN_0891a118 on the help line Start makes at (5, 0xF9), FUN_08919ec8):
     * on the window's last row; the port's help bar when there's no window. */
    if (w.state == ST_PAGE || w.state == ST_HIDDEN) {
        const char *g = guide(ptr_string(MSG_TABLE + 4u * (skipping() ? MSG_SKIPPING : MSG_HELP)));
        if (w.state == ST_PAGE && w.style == WND_NORMAL && w.window && (w.shown_label[0] || w.shown[0])) {
            TextStyle st = text_style(0, 6, 12, 12, 0, 2, 0xFF3F3F3Fu, 0);
            text_draw(g, &st, 5, 0xF9 + 4, 0, NULL, NULL);
        } else {
            ui_help_draw(g);
        }
    }
}

int scene_dry_run(const char *path, const char *id)
{
    if (!file_open(path, id))
        return -1;
    int n = 0;
    static const char k_args[0x30][5] = {
        [0x01] = "s", [0x02] = "s", [0x03] = "si", [0x04] = "i", [0x05] = "i", [0x06] = "si", [0x07] = "i",
        [0x08] = "i", [0x09] = "i", [0x0B] = "ii", [0x0C] = "ii", [0x0D] = "i", [0x0E] = "s", [0x10] = "iii",
        [0x12] = "iii", [0x13] = "iii", [0x18] = "s", [0x1A] = "i", [0x1C] = "i", [0x1D] = "ii", [0x1E] = "s",
        [0x1F] = "iiis", [0x20] = "ii", [0x21] = "si", [0x22] = "i", [0x23] = "iii", [0x25] = "si",
        [0x27] = "si", [0x29] = "iiii", [0x2B] = "i", [0x2C] = "i", [0x2D] = "i",
    };
    static const uint8_t k_known[0x30] = {
        [0x00] = 1, [0x01] = 1, [0x02] = 1, [0x03] = 1, [0x04] = 1, [0x05] = 1, [0x06] = 1, [0x07] = 1, [0x08] = 1,
        [0x09] = 1, [0x0A] = 1, [0x0B] = 1, [0x0C] = 1, [0x0D] = 1, [0x0E] = 1, [0x0F] = 1, [0x10] = 1, [0x12] = 1,
        [0x13] = 1, [0x14] = 1, [0x15] = 1, [0x18] = 1, [0x1A] = 1, [0x1B] = 1, [0x1C] = 1, [0x1D] = 1, [0x1E] = 1,
        [0x1F] = 1, [0x20] = 1, [0x21] = 1, [0x22] = 1, [0x23] = 1, [0x24] = 1, [0x25] = 1, [0x26] = 1, [0x27] = 1,
        [0x28] = 1, [0x29] = 1, [0x2A] = 1, [0x2B] = 1, [0x2C] = 1, [0x2D] = 1, [0x2E] = 1, [0x2F] = 1,
    };
    for (;;) {
        uint32_t op = next_word();
        if (f.bad || op >= 0x30 || !k_known[op]) {
            file_free();
            return -1;
        }
        n++;
        if (op == 0)
            break;
        for (const char *a = k_args[op]; *a; a++)
            if (*a == 's')
                next_string();
            else
                next_word();
        if (f.bad) {
            file_free();
            return -1;
        }
    }
    file_free();
    return n;
}
