/* See pgchip_edit.h. Hand-written from:
 *   PGCHIP::CCommand: Init FUN_089fcd54 and the per-type parameter defaults behind the
 *     table at 0x08C1D0B0; the copy FUN_089fce18; the exits FUN_089fe01c / FUN_089fe054;
 *     the processing mode behind the table at 0x08C1D188 (FUN_089fdfe4).
 *   CPgChipArea: the incoming count FUN_089f77f8, the range check FUN_089f7e24.
 *   CPgChipWnd: UpdateCursorPgChipReturnDir FUN_08a1dff4, UpdateCursorPgChipConnectedDir
 *     FUN_08a1e2ac, UpdateCursorPgChipEnableParam FUN_08a1e578, GetCursorPgChipErrorType
 *     FUN_08a1e710, NormalizeArea FUN_08a190dc, ClearArea FUN_08a18ce4, the placement
 *     FUN_08a194c4, the exit turns FUN_08a197b8 / FUN_08a1a008 / FUN_08a19ce0 /
 *     FUN_08a1a530 and the direction pick FUN_08a1ec84. */
#include "pgchip_edit.h"

#include <string.h>

#include "gamedata.h"

#define T_CHIP_KIND 0x00418BE4u /* DAT_08c1cbe4: a signed byte per chip type */

const signed char pgchip_dir_step[PGCHIP_DIRS][2] = {
    {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
};

int pgchip_kind(int type)
{
    if (!pgchip_valid_type(type))
        return -1;
    uint32_t a = T_CHIP_KIND + (uint32_t)type;
    return (int8_t)(gamedata_word(a & ~3u, 0xFFFFFFFFu) >> ((a & 3) * 8));
}

bool pgchip_is_chip(const PgChipCommand *c)
{
    int t = pgchip_type(c);
    return pgchip_valid_type(t) && t != PGCHIP_RETURN;
}

bool pgchip_is_return(const PgChipCommand *c)
{
    return pgchip_type(c) == PGCHIP_RETURN;
}

int pgchip_exit_dir(const PgChipCommand *c)
{
    if (!pgchip_valid_type(pgchip_type(c)) || pgchip_is_return(c))
        return -1;
    int d = c->b[1] & 0x0F;
    return d < PGCHIP_DIRS ? d : -1;
}

int pgchip_branch_dir(const PgChipCommand *c)
{
    if (!pgchip_valid_type(pgchip_type(c)) || pgchip_kind(pgchip_type(c)) != 1)
        return -1;
    int d = c->b[1] >> 4;
    return d < PGCHIP_DIRS ? d : -1;
}

static int u16_at(const PgChipCommand *c, int i) { return c->b[i] | c->b[i + 1] << 8; }

/* The table at 0x08C1D188: most types are always 1 (passthrough); a few read the
 * byte that holds their "wait for completion" choice. */
int pgchip_attribute(const PgChipCommand *c)
{
    switch (pgchip_type(c)) {
    case 0x02: case 0x03: case 0x1D: return 0;
    case 0x09: return c->b[2];
    case 0x1E: case 0x1F: return u16_at(c, 4) ? c->b[3] : 1;
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x25: return c->b[3];
    case 0x24: return c->b[4];
    case 0x26: return (c->b[7] & 0xF0) >> 4;
    case 0x27: return c->b[6];
    case 0x2B: return c->b[5];
    default: return 1;
    }
}

/* FUN_089fbfe8 (then the clamps of FUN_089fc838, which these values pass): a polar
 * range, 0x2D wide, 0x32 long. */
static void default_range(PgChipCommand *c)
{
    c->b[2] = 0x00;
    c->b[3] = 0x2D;
    c->b[4] = 0x32;
    c->b[5] = 0x00;
}

/* The per-type functions behind 0x08C1D0B0, after the parameters were zeroed. */
static void default_params(PgChipCommand *c)
{
    uint8_t *b = c->b;
    switch (pgchip_type(c)) {
    case 0x02: case 0x17: b[2] = 1; break;
    case 0x0A: case 0x12: b[2] = 1; break; /* u16 1 at byte 2 */
    case 0x0B: case 0x13: default_range(c); break;
    case 0x0C: default_range(c); b[6] = 0x50; b[7] = 0x01; break;
    case 0x0D: default_range(c); break;
    case 0x0E: default_range(c); b[6] = 0x0B; b[7] = 0x01; break;
    case 0x0F: b[3] = 0x32; break;
    case 0x11: case 0x2C: b[2] = 0x32; break;
    case 0x14: default_range(c); break;
    case 0x16: b[2] = 0xFF; break;
    case 0x1E: b[2] = 2; b[3] = 1; break;
    case 0x1F: b[3] = 1; break;
    case 0x20: case 0x21: b[2] = 2; break;
    case 0x23: b[2] = 3; break;
    case 0x24: b[3] = 5; break;
    case 0x26: default_range(c); b[6] = 0x08; break;
    case 0x27: b[5] = 1; break; /* bytes 2-3 are (0 + 1) / 2 of a zero: 0 */
    case 0x2B: b[4] = 1; break;
    case 0x2E: default_range(c); b[7] = 6; break;
    case 0x2F: default_range(c); b[6] = 1; break;
    case 0x34: b[3] = 4; b[4] = 1; break;
    default: break; /* all zero */
    }
}

void pgchip_default(PgChipCommand *c, int type)
{
    memset(c->b, 0, sizeof c->b);
    c->b[0] = (uint8_t)(type & 0x3F);
    c->b[1] = 0x40; /* exit 0 (up), branch 4 (down) */
    if (pgchip_valid_type(type))
        default_params(c);
}

void pgchip_copy(PgChipCommand *dst, const PgChipCommand *src)
{
    if (pgchip_is_return(src)) { /* the type and bit 7 set, byte 2 cleared, the rest kept */
        dst->b[0] = (uint8_t)((dst->b[0] & 0xC0) | PGCHIP_RETURN | 0x80);
        dst->b[2] = 0;
        return;
    }
    *dst = *src;
}

/* ---- areas ---- */

PgChipArea pgchip_area(SoftwareDataBase *sw, int program)
{
    PgChipArea a = {0};
    if (program < 0 || program >= PGCHIP_PROGRAM_MAX)
        return a;
    const int cap = program ? PGCHIP_SUB_CAP : PGCHIP_MAIN_CAP;
    a.chips = program == 0 ? sw->main : program == 1 ? sw->sub1 : sw->sub2;
    a.width = sw->width[program] > 0 && sw->width[program] <= cap ? sw->width[program] : 0;
    a.start = &sw->start[program];
    return a;
}

bool pgchip_in_area(const PgChipArea *a, int x, int y)
{
    return x >= 0 && y >= 0 && x < a->width && y < a->width;
}

PgChipCommand *pgchip_at(const PgChipArea *a, int x, int y)
{
    return pgchip_in_area(a, x, y) ? &a->chips[y * a->width + x] : NULL;
}

int pgchip_incoming(const PgChipArea *a, int x, int y)
{
    if (!pgchip_in_area(a, x, y))
        return -1;
    int n = y == 0 && x == *a->start;
    for (int d = 0; d < PGCHIP_DIRS; d++) {
        const PgChipCommand *c = pgchip_at(a, x + pgchip_dir_step[d][0], y + pgchip_dir_step[d][1]);
        if (!c || !pgchip_is_chip(c))
            continue;
        int back = (d + 4) & 7, e = pgchip_exit_dir(c), b = pgchip_branch_dir(c);
        n += (e >= 0 && e == back) || (b >= 0 && b == back);
    }
    return n;
}

int pgchip_error(const PgChipArea *a, int program, bool subs, int x, int y)
{
    const PgChipCommand *c = pgchip_at(a, x, y);
    if (!c || !pgchip_is_chip(c))
        return PGCHIP_ERR_NONE;
    if (pgchip_type(c) == PGCHIP_CALL && program == 0 && !subs)
        return PGCHIP_ERR_NO_SUB;
    return pgchip_incoming(a, x, y) < 1 ? PGCHIP_ERR_UNCONNECTED : PGCHIP_ERR_NONE;
}

bool pgchip_update_return(const PgChipArea *a, int x, int y)
{
    PgChipCommand *c = pgchip_at(a, x, y);
    if (!c)
        return false;
    if (pgchip_incoming(a, x, y) < 1) {
        if (!pgchip_is_return(c))
            return false;
        memset(c->b, 0, sizeof c->b); /* FUN_089fcddc: a blank chip */
        c->b[1] = 0x40;
        return true;
    }
    if (pgchip_valid_type(pgchip_type(c)))
        return false;
    pgchip_default(c, PGCHIP_RETURN); /* GetDefaultPgChip(0x35) */
    return true;
}

bool pgchip_update_connected(const PgChipArea *a, int x, int y)
{
    PgChipCommand *c = pgchip_at(a, x, y);
    if (!c || !pgchip_is_chip(c))
        return false;
    bool in = pgchip_incoming(a, x, y) >= 1, set = (c->b[0] & 0x40) != 0;
    if (in == set)
        return false;
    c->b[0] ^= 0x40;
    return true;
}

bool pgchip_update_enabled(const PgChipArea *a, int program, bool subs, int x, int y)
{
    PgChipCommand *c = pgchip_at(a, x, y);
    if (!c)
        return false;
    int err = pgchip_error(a, program, subs, x, y);
    bool ok = err == PGCHIP_ERR_NONE || err == PGCHIP_ERR_UNCONNECTED, set = (c->b[0] & 0x80) != 0;
    if (ok == set)
        return false;
    c->b[0] ^= 0x80;
    return true;
}

void pgchip_update_around(const PgChipArea *a, int program, bool subs, int x, int y)
{
    pgchip_update_return(a, x, y);
    for (int d = 0; d < PGCHIP_DIRS; d++)
        pgchip_update_return(a, x + pgchip_dir_step[d][0], y + pgchip_dir_step[d][1]);
    pgchip_update_connected(a, x, y);
    for (int d = 0; d < PGCHIP_DIRS; d++)
        pgchip_update_connected(a, x + pgchip_dir_step[d][0], y + pgchip_dir_step[d][1]);
    pgchip_update_enabled(a, program, subs, x, y);
}

bool pgchip_has_subs(const SoftwareDataBase *sw)
{
    return sw->width[1] > 0 && sw->width[2] > 0;
}

void pgchip_normalize(SoftwareDataBase *sw)
{
    bool subs = pgchip_has_subs(sw);
    for (int p = 0; p < PGCHIP_PROGRAM_MAX; p++) {
        PgChipArea a = pgchip_area(sw, p);
        if (!a.width)
            continue;
        if (*a.start < 0 || *a.start >= a.width)
            *a.start = 0;
        for (int y = 0; y < a.width; y++)
            for (int x = 0; x < a.width; x++) {
                pgchip_update_return(&a, x, y);
                pgchip_update_connected(&a, x, y);
                pgchip_update_enabled(&a, p, subs, x, y);
            }
    }
}

/* FUN_08a1ec84: the first direction after `from` (clockwise, or back for step -1)
 * that isn't `avoid`; every direction can be connected (FUN_089f7a20 marks all
 * eight). */
static int next_dir(int from, int step, int avoid)
{
    for (int i = 1; i < PGCHIP_DIRS; i++) {
        int d = ((from + step * i) % PGCHIP_DIRS + PGCHIP_DIRS) % PGCHIP_DIRS;
        if (d != avoid)
            return d;
    }
    return from;
}

static void set_nibble(PgChipCommand *c, bool branch, int dir)
{
    if (branch)
        c->b[1] = (uint8_t)((c->b[1] & 0x0F) | dir << 4);
    else
        c->b[1] = (uint8_t)((c->b[1] & 0xF0) | (dir & 0x0F));
}

bool pgchip_place(SoftwareDataBase *sw, int program, int x, int y, const PgChipCommand *chip)
{
    PgChipArea a = pgchip_area(sw, program);
    PgChipCommand *c = pgchip_at(&a, x, y);
    if (!c || !pgchip_is_chip(chip))
        return false;
    pgchip_copy(c, chip);
    if (pgchip_exit_dir(c) < 0)
        set_nibble(c, false, next_dir(0, 1, pgchip_branch_dir(c)));
    if (pgchip_kind(pgchip_type(c)) == 1 && pgchip_branch_dir(c) < 0)
        set_nibble(c, true, next_dir(0, 1, pgchip_exit_dir(c)));
    pgchip_update_around(&a, program, pgchip_has_subs(sw), x, y);
    return true;
}

void pgchip_remove(SoftwareDataBase *sw, int program, int x, int y)
{
    PgChipArea a = pgchip_area(sw, program);
    PgChipCommand *c = pgchip_at(&a, x, y);
    if (!c)
        return;
    memset(c->b, 0, sizeof c->b);
    c->b[1] = 0x40;
    /* Its exits only reached neighbours, so their Returns and bit 6 are all redone. */
    pgchip_update_around(&a, program, pgchip_has_subs(sw), x, y);
}

/* The exit turn and set: the old and the new target cells get their Return and bit 6
 * redone (FUN_08a1e128 / FUN_08a1e3f4 around the old and new direction). */
static void retarget(SoftwareDataBase *sw, int program, int x, int y, int old_dir, int new_dir)
{
    PgChipArea a = pgchip_area(sw, program);
    const int dirs[2] = {old_dir, new_dir};
    for (int i = 0; i < 2; i++) {
        if (dirs[i] < 0)
            continue;
        int tx = x + pgchip_dir_step[dirs[i]][0], ty = y + pgchip_dir_step[dirs[i]][1];
        pgchip_update_return(&a, tx, ty);
    }
    for (int i = 0; i < 2; i++) {
        if (dirs[i] < 0)
            continue;
        int tx = x + pgchip_dir_step[dirs[i]][0], ty = y + pgchip_dir_step[dirs[i]][1];
        pgchip_update_connected(&a, tx, ty);
    }
}

static PgChipCommand *edit_target(SoftwareDataBase *sw, int program, int x, int y, bool branch)
{
    PgChipArea a = pgchip_area(sw, program);
    PgChipCommand *c = pgchip_at(&a, x, y);
    if (!c || !pgchip_is_chip(c) || (branch && pgchip_kind(pgchip_type(c)) != 1))
        return NULL;
    return c;
}

bool pgchip_turn_exit(SoftwareDataBase *sw, int program, int x, int y, bool branch, int step)
{
    PgChipCommand *c = edit_target(sw, program, x, y, branch);
    if (!c)
        return false;
    int old = branch ? pgchip_branch_dir(c) : pgchip_exit_dir(c);
    int other = branch ? pgchip_exit_dir(c) : pgchip_branch_dir(c);
    int d = next_dir(old < 0 ? 0 : old, step < 0 ? -1 : 1, other);
    if (d == old)
        return false;
    set_nibble(c, branch, d);
    retarget(sw, program, x, y, old, d);
    return true;
}

/* FUN_08a1ed6c: the requested direction unless the other exit has it. */
bool pgchip_set_exit(SoftwareDataBase *sw, int program, int x, int y, bool branch, int dir)
{
    PgChipCommand *c = edit_target(sw, program, x, y, branch);
    if (!c || dir < 0 || dir >= PGCHIP_DIRS)
        return false;
    int old = branch ? pgchip_branch_dir(c) : pgchip_exit_dir(c);
    int other = branch ? pgchip_exit_dir(c) : pgchip_branch_dir(c);
    if (dir == old || dir == other)
        return false;
    set_nibble(c, branch, dir);
    retarget(sw, program, x, y, old, dir);
    return true;
}

void pgchip_clear(SoftwareDataBase *sw)
{
    for (int p = 0; p < PGCHIP_PROGRAM_MAX; p++) {
        PgChipArea a = pgchip_area(sw, p);
        for (int i = 0; i < a.width * a.width; i++) {
            memset(a.chips[i].b, 0, sizeof a.chips[i].b);
            a.chips[i].b[1] = 0x40;
        }
        if (a.width)
            pgchip_update_return(&a, *a.start, 0);
    }
}

bool pgchip_move_start(SoftwareDataBase *sw, int program, int step)
{
    PgChipArea a = pgchip_area(sw, program);
    int to = *a.start + step;
    if (!a.width || to < 0 || to >= a.width)
        return false;
    int from = *a.start;
    *a.start = to;
    bool subs = pgchip_has_subs(sw);
    pgchip_update_around(&a, program, subs, from, 0);
    pgchip_update_around(&a, program, subs, to, 0);
    return true;
}

/* ---- rectangles ---- */

static void order(int *a, int *b)
{
    if (*a > *b) {
        int t = *a;
        *a = *b;
        *b = t;
    }
}

void pgchip_clip_take(PgChipClip *clip, const PgChipArea *a, int x0, int y0, int x1, int y1)
{
    order(&x0, &x1);
    order(&y0, &y1);
    clip->w = x1 - x0 + 1;
    clip->h = y1 - y0 + 1;
    for (int y = 0; y < clip->h; y++)
        for (int x = 0; x < clip->w; x++) {
            const PgChipCommand *c = pgchip_at(a, x0 + x, y0 + y);
            PgChipCommand *d = &clip->chips[y * clip->w + x];
            memset(d->b, 0, sizeof d->b);
            if (c && pgchip_is_chip(c))
                *d = *c;
        }
}

void pgchip_erase_rect(SoftwareDataBase *sw, int program, int x0, int y0, int x1, int y1)
{
    order(&x0, &x1);
    order(&y0, &y1);
    PgChipArea a = pgchip_area(sw, program);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            PgChipCommand *c = pgchip_at(&a, x, y);
            if (c && pgchip_is_chip(c)) {
                memset(c->b, 0, sizeof c->b);
                c->b[1] = 0x40;
            }
        }
    pgchip_normalize(sw);
}

void pgchip_clip_paste(SoftwareDataBase *sw, int program, const PgChipClip *clip, int x, int y)
{
    PgChipArea a = pgchip_area(sw, program);
    for (int cy = 0; cy < clip->h; cy++)
        for (int cx = 0; cx < clip->w; cx++) {
            const PgChipCommand *s = &clip->chips[cy * clip->w + cx];
            PgChipCommand *d = pgchip_at(&a, x + cx, y + cy);
            if (d && pgchip_is_chip(s))
                *d = *s;
        }
    pgchip_normalize(sw);
}

bool pgchip_copy_software(SoftwareDataBase *dst, const SoftwareDataBase *src)
{
    bool lost = false;
    for (int p = 0; p < PGCHIP_PROGRAM_MAX; p++) {
        PgChipArea d = pgchip_area(dst, p);
        PgChipArea s = pgchip_area((SoftwareDataBase *)src, p);
        for (int y = 0; y < d.width; y++)
            for (int x = 0; x < d.width; x++) {
                PgChipCommand *c = pgchip_at(&d, x, y);
                const PgChipCommand *o = pgchip_at(&s, x, y);
                memset(c->b, 0, sizeof c->b);
                c->b[1] = 0x40;
                if (o && pgchip_is_chip(o))
                    *c = *o;
            }
        for (int y = 0; y < s.width; y++)
            for (int x = 0; x < s.width; x++)
                if ((x >= d.width || y >= d.width) && pgchip_is_chip(pgchip_at(&s, x, y)))
                    lost = true;
        if (d.width)
            *d.start = s.width && *s.start < d.width ? *s.start : 0;
    }
    memcpy(dst->counter_name, src->counter_name, sizeof dst->counter_name);
    pgchip_normalize(dst);
    return lost;
}
