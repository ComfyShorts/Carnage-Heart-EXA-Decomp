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

#include <math.h>
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

/* ---- parameters (the sub-menus' values) ---- */

#define T_SUB_MENUS 0x00429CFCu /* DAT_08c2dcfc: per type, a count and 8 sub-menu ids */

int pgchip_sub_menus(int type, int out[PGCHIP_SUB_ITEMS])
{
    if (type < 0 || type >= PGCHIP_TYPE_END)
        return 0;
    uint32_t a = T_SUB_MENUS + 0x24u * (uint32_t)type;
    int n = (int)gamedata_word(a, 0);
    if (n < 0 || n > PGCHIP_SUB_ITEMS)
        n = 0;
    for (int i = 0; i < n; i++)
        out[i] = (int)gamedata_word(a + 4u + 4u * (uint32_t)i, 0);
    return n;
}

/* Where each (type, sub-menu) value lives, from GetSubMenuValue (FUN_08a959c8) and
 * SetSubMenuValue (FUN_08a96284): a byte, a signed byte, a 16- or 32-bit word, a
 * nibble, or (the range's shape) bit 0 of byte 2, which only the range editor sets. */
enum { F_U8, F_S8, F_U16, F_S16, F_U32, F_LO4, F_HI4, F_LO3, F_HI5, F_BIT0 };

typedef struct Field {
    uint8_t type, sub, off, kind;
} Field;

static const Field k_fields[] = {
    {0x02, 0x0D, 2, F_U8},  {0x03, 0x49, 2, F_U8},  {0x04, 0x2C, 2, F_U8},
    {0x05, 0x31, 2, F_U8},  {0x05, 0x34, 3, F_U8},
    {0x06, 0x35, 2, F_U32}, {0x06, 0x31, 6, F_LO4}, {0x06, 0x36, 6, F_HI4}, {0x06, 0x37, 7, F_U8},
    {0x07, 0x31, 2, F_U8},  {0x07, 0x38, 3, F_U8},  {0x08, 0x31, 2, F_U8},  {0x08, 0x38, 3, F_U8},
    {0x09, 0x07, 2, F_U8},
    {0x0A, 0x39, 4, F_U8},  {0x0A, 0x1B, 2, F_U16}, {0x0A, 0x08, 5, F_U8},
    {0x0B, 0x02, 2, F_BIT0},
    {0x0C, 0x1D, 6, F_LO4}, {0x0C, 0x1F, 6, F_HI4}, {0x0C, 0x21, 7, F_LO4}, {0x0C, 0x08, 7, F_HI4},
    {0x0C, 0x02, 2, F_BIT0},
    {0x0D, 0x23, 6, F_U8},  {0x0D, 0x08, 7, F_U8},  {0x0D, 0x02, 2, F_BIT0},
    {0x0E, 0x18, 6, F_U8},  {0x0E, 0x19, 7, F_LO4}, {0x0E, 0x08, 7, F_HI4}, {0x0E, 0x02, 2, F_BIT0},
    {0x0F, 0x24, 2, F_U8},  {0x0F, 0x25, 3, F_U8},  {0x0F, 0x08, 4, F_U8},
    {0x10, 0x28, 2, F_U8},  {0x11, 0x26, 2, F_U8},
    {0x12, 0x2A, 2, F_U16}, {0x12, 0x2B, 4, F_U8},  {0x12, 0x08, 5, F_U8},
    {0x13, 0x02, 2, F_BIT0},
    {0x14, 0x02, 2, F_BIT0}, {0x14, 0x4B, 6, F_U8},
    {0x15, 0x29, 2, F_U8},  {0x16, 0x22, 2, F_S8},  {0x17, 0x2D, 2, F_U8},
    {0x18, 0x14, 2, F_U8},  {0x18, 0x15, 3, F_U8},  {0x19, 0x30, 2, F_U8},
    {0x1A, 0x35, 2, F_U32}, {0x1A, 0x31, 6, F_LO4}, {0x1A, 0x36, 6, F_HI4}, {0x1A, 0x08, 7, F_U8},
    {0x1B, 0x3F, 2, F_U16}, {0x1B, 0x40, 4, F_U8},
    {0x1C, 0x41, 2, F_U8},  {0x1C, 0x42, 3, F_S8},  {0x1C, 0x08, 4, F_U8},
    {0x1D, 0x3C, 2, F_U8},
    {0x1E, 0x09, 2, F_U8},  {0x1E, 0x07, 3, F_U8},  {0x1E, 0x3A, 4, F_U16},
    {0x1F, 0x0A, 2, F_U8},  {0x1F, 0x07, 3, F_U8},  {0x1F, 0x3B, 4, F_U16},
    {0x20, 0x0B, 2, F_U8},  {0x20, 0x07, 3, F_U8},  {0x21, 0x09, 2, F_U8},  {0x21, 0x07, 3, F_U8},
    {0x22, 0x0A, 2, F_U8},  {0x22, 0x07, 3, F_U8},  {0x23, 0x0F, 2, F_U8},  {0x23, 0x07, 3, F_U8},
    {0x24, 0x10, 2, F_U8},  {0x24, 0x0E, 3, F_U8},  {0x24, 0x07, 4, F_U8},
    {0x25, 0x11, 2, F_U8},  {0x25, 0x07, 3, F_U8},
    {0x26, 0x13, 6, F_LO3}, {0x26, 0x16, 6, F_HI5}, {0x26, 0x17, 7, F_LO4}, {0x26, 0x07, 7, F_HI4},
    {0x26, 0x02, 2, F_BIT0},
    {0x27, 0x13, 4, F_U8},  {0x27, 0x16, 5, F_U8},  {0x27, 0x07, 6, F_U8},
    {0x28, 0x44, 4, F_U8},  {0x28, 0x32, 2, F_U8},  {0x28, 0x33, 3, F_U8},
    {0x29, 0x43, 2, F_U8},
    {0x2A, 0x44, 2, F_U8},  {0x2A, 0x05, 4, F_U16}, {0x2A, 0x06, 6, F_S16},
    {0x2B, 0x45, 2, F_U8},  {0x2B, 0x17, 3, F_U8},  {0x2B, 0x16, 4, F_U8},  {0x2B, 0x07, 5, F_U8},
    {0x2C, 0x12, 2, F_U8},  {0x2C, 0x4C, 3, F_U8},
    {0x2D, 0x1A, 2, F_U8},
    {0x2E, 0x4A, 6, F_LO4}, {0x2E, 0x1E, 6, F_HI4}, {0x2E, 0x20, 7, F_U8},  {0x2E, 0x02, 2, F_BIT0},
    {0x2F, 0x2E, 6, F_U8},
    {0x30, 0x2F, 2, F_U8},  {0x31, 0x31, 2, F_U8},
    {0x33, 0x3D, 2, F_U8},  {0x33, 0x3E, 3, F_U8},
    {0x34, 0x47, 2, F_U8},  {0x34, 0x46, 3, F_U8},  {0x34, 0x48, 4, F_U8},
};

static const Field *find_field(int type, int sub)
{
    for (size_t i = 0; i < sizeof k_fields / sizeof k_fields[0]; i++)
        if (k_fields[i].type == type && k_fields[i].sub == sub)
            return &k_fields[i];
    return NULL;
}

/* Move (0x1E) and Rotate (0x1F) with a distance / angle of 0 (endless) always pass
 * through: their processing mode reads as 1 and is written back as 1. */
static bool endless(const PgChipCommand *c)
{
    int t = pgchip_type(c);
    return (t == 0x1E || t == 0x1F) && !(c->b[4] | c->b[5]);
}

bool pgchip_param_get(const PgChipCommand *c, int sub, int32_t *value)
{
    int t = pgchip_type(c);
    if (t == 0x2F && sub == PGCHIP_SUB_POLAR) {
        *value = 0;
        return true;
    }
    const Field *f = find_field(t, sub);
    if (!f)
        return false;
    const uint8_t *b = c->b + f->off;
    switch (f->kind) {
    case F_U8: *value = b[0]; break;
    case F_S8: *value = (int8_t)b[0]; break;
    case F_U16: *value = b[0] | b[1] << 8; break;
    case F_S16: *value = (int16_t)(b[0] | b[1] << 8); break;
    case F_U32: *value = (int32_t)((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24); break;
    case F_LO4: *value = b[0] & 0xF; break;
    case F_HI4: *value = b[0] >> 4; break;
    case F_LO3: *value = b[0] & 7; break;
    case F_HI5: *value = b[0] >> 3; break;
    default: *value = b[0] & 1; break;
    }
    if (sub == PGCHIP_SUB_PROCESSING && endless(c))
        *value = 1;
    return true;
}

bool pgchip_param_set(PgChipCommand *c, int sub, int32_t v)
{
    const Field *f = find_field(pgchip_type(c), sub);
    if (!f || f->kind == F_BIT0)
        return false;
    uint8_t *b = c->b + f->off;
    switch (f->kind) {
    case F_U8: case F_S8: b[0] = (uint8_t)v; break;
    case F_U16: case F_S16: b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); break;
    case F_U32:
        for (int i = 0; i < 4; i++)
            b[i] = (uint8_t)((uint32_t)v >> (8 * i));
        break;
    case F_LO4: b[0] = (uint8_t)((b[0] & 0xF0) | (v & 0xF)); break;
    case F_HI4: b[0] = (uint8_t)((b[0] & 0x0F) | (v & 0xF) << 4); break;
    case F_LO3: b[0] = (uint8_t)((b[0] & 0xF8) | (v & 7)); break;
    default: b[0] = (uint8_t)((b[0] & 7) | (v & 0x1F) << 3); break;
    }
    if (endless(c))
        c->b[3] = 1;
    return true;
}

/* ---- ranges (PGCHIP::tagRANGE, bytes 2-5) ---- */

bool pgchip_has_range(int type)
{
    switch (type) {
    case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x13: case 0x14: case 0x26: case 0x2E: case 0x2F:
        return true;
    default:
        return false;
    }
}

/* PGCHIP::CRange's scale (the tables battle_range_distance reads). */
static int scale(int table, int level)
{
    static const uint32_t k_table[3] = {0x00418B7Cu, 0x00418BD0u, 0x00418AB4u}; /* steps, lengths, factors */
    static const int k_fallback[3] = {25, 50, 2};
    return (int)gamedata_word(k_table[table] + 4u * (uint32_t)level,
                              (uint32_t)(table == 2 ? 2 : level * k_fallback[table]));
}

/* FUN_089fc2f0: a step's scale level (the game's order puts 4 before 3). */
static int scale_level(int step)
{
    if (step < scale(0, 1))
        return 0;
    if (step < scale(0, 2))
        return 1;
    if (step < scale(0, 3))
        return 2;
    return step < scale(0, 4) ? 3 : 4;
}

int pgchip_range_distance(int step)
{
    if (step < 0)
        return -pgchip_range_distance(-step);
    int l = scale_level(step);
    return scale(1, l) + scale(2, l) * (step - scale(0, l));
}

/* FUN_089fc410: a distance back to a step. */
static int range_step(int dist)
{
    if (dist < 0)
        return -range_step(-dist);
    for (int l = 4; l >= 0; l--)
        if (scale(1, l) <= dist)
            return scale(0, l) + (dist - scale(1, l)) / scale(2, l);
    return 0;
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* FUN_08bb42cc: into lo..hi, wrapping by `period`. */
static int wrap(int v, int lo, int hi, int period)
{
    if (v < lo) {
        int r = (lo - v) % period;
        return r ? lo + period - r : lo;
    }
    return v > hi ? (v - lo) % period + lo : v;
}

/* FUN_089fc4dc: degrees to the 2-degree units, rounding away from zero. */
static int half_deg(int deg)
{
    return deg < 1 ? (deg - 1) / 2 : (deg + 1) / 2;
}

#define RANGE_MAX 0x6E
#define PI_F 3.14159265f

/* FUN_089fbfe8 + FUN_089fc838: write a range and clamp it. */
static void range_put(uint8_t r[4], int shape, int b2, int b3, int b1, int inner)
{
    r[0] = (uint8_t)((shape & 1) | (inner << 1));
    r[2] = (uint8_t)b2;
    r[3] = (uint8_t)b3;
    r[1] = (uint8_t)b1;
    if (!shape) {
        r[1] = (uint8_t)clamp(r[1], 0, 0xB4);
        r[2] = (uint8_t)clamp(r[2], 0, RANGE_MAX);
        r[3] = (uint8_t)clamp(r[3], 0, 0xB4);
    } else {
        r[0] = (uint8_t)(1 | clamp(r[0] >> 1, 0, RANGE_MAX) << 1);
        r[1] = (uint8_t)clamp(r[1], 0, RANGE_MAX);
        r[2] = (uint8_t)clamp((int8_t)r[2], -RANGE_MAX, RANGE_MAX);
        r[3] = (uint8_t)clamp((int8_t)r[3], -RANGE_MAX, RANGE_MAX);
    }
}

void pgchip_range_set_shape(uint8_t r[4], int shape)
{
    shape &= 1;
    if ((r[0] & 1) == shape)
        return;
    if (shape == 0) { /* rectangle -> sector: the centre's distance and bearing */
        int x = pgchip_range_distance((int8_t)r[2]), y = pgchip_range_distance((int8_t)r[3]);
        int dist = (int)lroundf(sqrtf((float)(x * x + y * y)));
        int w = pgchip_range_distance(r[1]), d = pgchip_range_distance(r[0] >> 1);
        int far = pgchip_range_distance(RANGE_MAX), dir = 0;
        if (dist > 0) {
            dir = (int)lroundf(acosf((float)y / (float)dist) * (180.0f / PI_F));
            if (x < 0)
                dir = 360 - dir;
            dir = wrap(dir, 0, 359, 360);
        }
        range_put(r, 0, range_step(dist), half_deg(dir), half_deg((d + w) * 360 / (far * 2)), 0);
    } else { /* sector -> rectangle: the far point's place, a square the width's size */
        int dist = pgchip_range_distance(r[2]), deg = r[3] * 2;
        int x = (int)lroundf((float)dist * sinf((float)deg * (PI_F / 180.0f)));
        int y = (int)lroundf((float)dist * cosf((float)deg * (PI_F / 180.0f)));
        int side = r[1] * 2 * pgchip_range_distance(RANGE_MAX) / 360;
        range_put(r, 1, (int8_t)range_step(x), (int8_t)range_step(y), range_step(side), range_step(side));
    }
}

/* FUN_089fc65c: a step to the next scale-level boundary that way (L held). */
static int snap_step(int v, int dir, bool sign)
{
    bool neg = false;
    if (sign && v < 0) {
        v = -v;
        dir = -dir;
        neg = true;
    }
    int l = scale_level(v);
    if (v != scale(0, l))
        v = dir < 1 ? scale(0, l) : l < 4 ? scale(0, l + 1) : RANGE_MAX;
    return neg ? -v : v;
}

/* FUN_089fc70c: to a multiple of 45 (90 degrees) that way. */
static int snap45(int v, int dir)
{
    if (v % 45)
        v = (dir > 0 ? v + 44 : v) / 45 * 45;
    return v;
}

bool pgchip_range_edit(uint8_t r[4], int dx, int dy, bool alt, bool snap)
{
    uint8_t old[4];
    memcpy(old, r, 4);
    if (r[0] & 1) { /* rectangle: the centre, or (R) the width and depth */
        if (!alt) {
            if (dx) {
                int v = clamp((int8_t)r[2] + dx, -RANGE_MAX, RANGE_MAX);
                r[2] = (uint8_t)(snap ? clamp(snap_step(v, dx, true), -RANGE_MAX, RANGE_MAX) : v);
            }
            if (dy) {
                int v = clamp((int8_t)r[3] + dy, -RANGE_MAX, RANGE_MAX);
                r[3] = (uint8_t)(snap ? clamp(snap_step(v, dy, true), -RANGE_MAX, RANGE_MAX) : v);
            }
        } else {
            if (dx) {
                int v = clamp(r[1] + dx, 0, RANGE_MAX);
                r[1] = (uint8_t)(snap ? clamp(snap_step(v, dx, false), 0, RANGE_MAX) : v);
            }
            if (dy) {
                int v = clamp((r[0] >> 1) + dy, 0, RANGE_MAX);
                v = snap ? clamp(snap_step(v, dy, false), 0, RANGE_MAX) : v;
                r[0] = (uint8_t)((r[0] & 1) | v << 1);
            }
        }
    } else if (!alt) { /* sector: the bearing and the outer distance */
        if (dx) {
            int v = wrap(r[3] + dx, 0, 0xB3, 0xB4);
            r[3] = (uint8_t)(snap ? wrap(snap45(v, dx), 0, 0xB3, 0xB4) : v);
        }
        if (dy) {
            int v = clamp(r[2] + dy, 0, RANGE_MAX);
            r[2] = (uint8_t)(snap ? clamp(snap_step(v, dy, false), 0, RANGE_MAX) : v);
            int inner = r[0] >> 1;
            if (r[2] <= inner)
                inner = r[2] & 0x7F;
            r[0] = (uint8_t)((r[0] & 1) | inner << 1);
        }
    } else { /* (R) the half-width and the inner distance */
        if (dx) {
            int v = wrap(r[1] + dx, 0, 0xB4, 0xB5);
            r[1] = (uint8_t)(snap ? wrap(snap45(v, dx), 0, 0xB4, 0xB5) : v);
        }
        if (dy) {
            int v = (r[0] >> 1) + dy;
            if (snap)
                v = snap_step(v, dy, false);
            v = clamp(v, 0, r[2]);
            r[0] = (uint8_t)((r[0] & 1) | (v & 0x7F) << 1);
        }
    }
    return memcmp(old, r, 4) != 0;
}

/* ---- direction and elevation (Select and Fire 0x27, Turn Turret 0x2A) ---- */

bool pgchip_angle_get(const PgChipCommand *c, bool elevation, int *centideg)
{
    switch (pgchip_type(c)) {
    case 0x27: /* FUN_089fc4d4: 2-degree units */
        *centideg = (elevation ? (int8_t)c->b[3] : c->b[2]) * 200;
        return true;
    case 0x2A:
        *centideg = elevation ? (int16_t)(c->b[6] | c->b[7] << 8) : (c->b[4] | c->b[5] << 8);
        return true;
    default:
        return false;
    }
}

bool pgchip_angle_set(PgChipCommand *c, bool elevation, int centideg)
{
    switch (pgchip_type(c)) {
    case 0x27:
        c->b[elevation ? 3 : 2] = (uint8_t)half_deg(centideg / 100);
        return true;
    case 0x2A:
        c->b[elevation ? 6 : 4] = (uint8_t)centideg;
        c->b[elevation ? 7 : 5] = (uint8_t)(centideg >> 8);
        return true;
    default:
        return false;
    }
}

int pgchip_angle_step(const PgChipCommand *c)
{
    return pgchip_type(c) == 0x27 ? 200 : c->b[2] ? 10 : 100;
}

int pgchip_angle_edit(int centideg, bool elevation, int step, int d, bool fresh, bool snap)
{
    if (!elevation) { /* FUN_08a99954: wraps at 360 degrees; L to the quarter */
        int v = wrap(centideg + d * step, 0, 35999, 36000);
        if (snap && v % 9000)
            v = (d < 1 ? v / 9000 : (v + 8999) / 9000) * 9000;
        v = wrap(v, 0, 35999, 36000);
        return v / step * step;
    }
    /* FUN_08a9b490: -90..90; a fresh press wraps, a held one stops */
    if (!fresh)
        return clamp(centideg + d * step, -9000, 9000);
    int v = wrap(centideg + d * step, -9000, 9000, 18001);
    v = v < 1 ? v - step + 1 : v + step - 1;
    return v / step * step;
}
