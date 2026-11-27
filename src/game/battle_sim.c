/* See battle_sim.h. */
#include "battle_sim.h"

#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gamedata.h"

#define DEG (3.14159265f / 180.0f)

/* ---- numbers from the executable (read at run time, see gamedata.h) ---- */

/* Chips per second by CPU part (FUN_08856704, table at 0x08bd5f88). */
int battle_chip_speed(int cpu_part)
{
    if (cpu_part < 0 || cpu_part > 7)
        cpu_part = 1;
    return (int)gamedata_word(0x003D1F88u + 4u * (uint32_t)cpu_part, 120);
}

/* PGCHIP::CRange step -> distance (FUN_089fc37c): five scale levels, each a base
 * step (0x08c1cb7c), base length (0x08c1cbd0) and magnification (0x08c1cab4). */
int battle_range_distance(int step)
{
    if (step < 0)
        return -battle_range_distance(-step);
    int base_step[5], base_len[5], mag[5];
    for (int i = 0; i < 5; i++) {
        base_step[i] = (int)gamedata_word(0x00418B7Cu + 4u * (uint32_t)i, (uint32_t)(i * 25));
        base_len[i] = (int)gamedata_word(0x00418BD0u + 4u * (uint32_t)i, (uint32_t)(i * 50));
        mag[i] = (int)gamedata_word(0x00418AB4u + 4u * (uint32_t)i, 2);
    }
    /* FUN_089fc2f0 picks the level; note the game's order puts level 4 before 3. */
    int lvl = 0;
    if (step >= base_step[1]) {
        lvl = 1;
        if (step >= base_step[2]) {
            lvl = 2;
            if (step >= base_step[3])
                lvl = step < base_step[4] ? 3 : 4;
        }
    }
    return base_len[lvl] + mag[lvl] * (step - base_step[lvl]);
}



#define O(t) ((Oke *)(t))
#define ENEMY(o) (&(o)->battle->oke[(o)->side ^ 1])

static void setup_parts(Oke *o);
static bool weapon_lost(const Oke *o, int slot);
static bool reacting(const Oke *o);

/* FUN_08843b74 refuses new orders while a hit reaction plays. */
#define REFUSE_IF_REACTING(t) \
    do { \
        if (reacting(O(t))) \
            return 0x10; \
    } while (0)

static uint32_t rnd(Battle *b)
{
    b->rng = b->rng * 1103515245u + 12345u;
    return b->rng >> 8;
}

static float wrap180(float a)
{
    while (a > 180.0f)
        a -= 360.0f;
    while (a < -180.0f)
        a += 360.0f;
    return a;
}

/* Bearing of (px, py) from the OKE, relative to its heading, in degrees. */
static float bearing(const Oke *o, float px, float py)
{
    float a = atan2f(px - o->x, -(py - o->y)) / DEG;
    return wrap180(a - o->heading);
}

/* PGCHIP::tagRANGE, sector mode (b2 bit 0 clear): b2 bits 1-7 inner step, b3
 * half-width, b4 outer step, b5 the arc direction in 2-degree units (values above
 * 180 degrees wrap negative). */
static void range_bounds(PgChipRange r, float *inner, float *outer)
{
    float a = (float)battle_range_distance(r.b[0] >> 1), b = (float)battle_range_distance(r.b[2]);
    /* Some chips store the two the other way round (the inner step above the outer). */
    *inner = a < b ? a : b;
    *outer = a < b ? b : a;
}

static bool in_range(const Oke *o, PgChipRange r, float px, float py)
{
    if (r.b[0] & 1) {
        /* Rectangle mode (FUN_08841bd8 -> FUN_08860408 -> FUN_0887eab8): a box in
         * the OKE's frame centred dist(b4) to the right and dist(b5) ahead (both
         * signed), dist(b3) wide and dist(b2 bits 1-7) deep. */
        float cx = (float)battle_range_distance((int8_t)r.b[2]);
        float cz = (float)battle_range_distance((int8_t)r.b[3]);
        float w = (float)battle_range_distance(r.b[1]);
        float dpt = (float)battle_range_distance((r.b[0] & 0xFE) >> 1);
        float h = o->heading * DEG, dx = px - o->x, dy = py - o->y;
        float right = dx * cosf(h) + dy * sinf(h), ahead = dx * sinf(h) - dy * cosf(h);
        return fabsf(right - cx) <= w * 0.5f && fabsf(ahead - cz) <= dpt * 0.5f;
    }
    float inner, outer;
    range_bounds(r, &inner, &outer);
    float half = (float)r.b[1], dir = (float)(r.b[3] * 2);
    if (dir > 180.0f)
        dir -= 360.0f;
    float d = hypotf(px - o->x, py - o->y);
    if (d < inner || d > outer)
        return false;
    return half >= 180.0f || fabsf(wrap180(bearing(o, px, py) - dir)) <= half;
}

static bool outside_field(float x, float y)
{
    return x < 0 || y < 0 || x > FIELD || y > FIELD;
}

/* Is any part of the arena wall inside the range? Sampled along the arc. */
static bool wall_in_range(const Oke *o, PgChipRange r)
{
    float inner, outer;
    range_bounds(r, &inner, &outer);
    float half = r.b[1] >= 180 ? 180.0f : (float)r.b[1], dir = (float)(r.b[3] * 2);
    if (dir > 180.0f)
        dir -= 360.0f;
    for (float a = -half; a <= half; a += 5.0f)
        for (float d = inner; d <= outer; d += 10.0f) {
            float h = (o->heading + dir + a) * DEG;
            if (outside_field(o->x + sinf(h) * d, o->y - cosf(h) * d))
                return true;
        }
    return false;
}

static void start_action(Oke *o, ActKind k, float seconds)
{
    if (o->scripted)
        oke_motion_stop(&o->motion);
    o->scripted = false;
    o->act = k;
    o->act_left = seconds;
}

/* ---- CPgChipTarget ---- */

static int t_timestamp(PgChipTarget *t) { return O(t)->battle->tick; }
static int t_halt_start(PgChipTarget *t, int frames) { O(t)->halt_left = frames / 30.0f; return 0; }
static int t_halt_finished(PgChipTarget *t) { return O(t)->halt_left <= 0.0f; }
static int t_action_finished(PgChipTarget *t, int category)
{
    (void)category;
    return O(t)->act == ACT_NONE && O(t)->firing < 0;
}
static int t_fire_finished(PgChipTarget *t) { return O(t)->firing < 0 && O(t)->fire_left <= 0.0f; }
static int t_halt_action(PgChipTarget *t, int category)
{
    (void)category;
    O(t)->act = ACT_NONE;
    O(t)->scripted = false;
    oke_motion_stop(&O(t)->motion);
    O(t)->vx = O(t)->vy = O(t)->turn_rate = 0.0f;
    return 0;
}

/* Move directions in the order of the game's action names (EBOOT strings at
 * 0x003ed040): forward, backward, right, left, forward right, forward left, back
 * right, back left. */
static const float k_dir_angle[8] = {0.0f, 180.0f, 90.0f, -90.0f, 45.0f, -45.0f, 135.0f, -135.0f};

static void set_velocity(Oke *o, int dir, float speed)
{
    float h = (o->heading + k_dir_angle[dir & 7]) * DEG;
    o->vx = sinf(h) * speed;
    o->vy = -cosf(h) * speed;
}

/* CBattleOke's Move / Rotate (FUN_08844450 / FUN_088445b0): start the motion
 * script; the observer (FUN_0883aaec, at +0xb430) watches for `amount - 0.5`. */
static const int k_move_script[8] = {4, 3, 1, 2, 8, 7, 10, 9}; /* 0x08bd2414 */

static void observe(Oke *o, ActKind k, int amount, float fallback)
{
    if (o->firing >= 0 && !o->armed)
        o->firing = -1; /* SetScript replaced the weapon script before it fired */
    o->scripted = true;
    o->act = k;
    o->act_left = 1e9f;
    o->obs_amount = (amount > 0 ? (float)amount : fallback) - 0.5f;
    o->obs_x = o->x;
    o->obs_y = o->y;
    o->obs_turned = 0.0f;
    o->obs_last_heading = o->heading;
}

static int t_move(PgChipTarget *t, int dir, int amount)
{
    REFUSE_IF_REACTING(t);
    Oke *o = O(t);
    if (o->motion.s) {
        oke_motion_start(&o->motion, k_move_script[dir & 7]);
        observe(o, ACT_MOVE, amount, 40.0f);
        return 0;
    }
    float dist = amount > 0 ? (float)amount : 40.0f;
    set_velocity(o, dir, MOVE_SPEED);
    start_action(o, ACT_MOVE, dist / MOVE_SPEED);
    return 0;
}

static int t_rotate(PgChipTarget *t, int dir, int amount)
{
    REFUSE_IF_REACTING(t);
    Oke *o = O(t);
    if (o->motion.s) {
        oke_motion_start(&o->motion, dir ? 5 : 6); /* 0 clockwise: script 6 (0x204) */
        observe(o, ACT_TURN, amount, 45.0f);
        return 0;
    }
    float deg = amount > 0 ? (float)amount : 45.0f;
    o->turn_rate = dir ? -TURN_SPEED : TURN_SPEED; /* 0 clockwise, 1 counter-clockwise */
    start_action(o, ACT_TURN, deg / TURN_SPEED);
    return 0;
}

static int t_jump(PgChipTarget *t, int dir)
{
    REFUSE_IF_REACTING(t);
    Oke *o = O(t);
    if (dir >= 8)
        o->vx = o->vy = 0.0f;
    else
        set_velocity(o, dir, MOVE_SPEED * 1.5f);
    start_action(o, ACT_JUMP, 0.6f);
    return 0;
}

/* FUN_08844940 / FUN_08844a00: one pass of a script, no observer. */
static const int k_quick_script[8] = {31, 30, 28, 29, 25, 24, 27, 26}; /* 0x08bd2568 */

static bool start_quick(Oke *o, int script, ActKind k)
{
    if (!o->motion.s || o->motion.s->count[script] == 0)
        return false;
    oke_motion_start(&o->motion, script);
    observe(o, k, 0, 1e9f);
    return true;
}

static int t_quick_move(PgChipTarget *t, int dir)
{
    REFUSE_IF_REACTING(t);
    if (start_quick(O(t), k_quick_script[dir & 7], ACT_MOVE))
        return 0;
    set_velocity(O(t), dir, QUICK_SPEED);
    start_action(O(t), ACT_MOVE, 0.35f);
    return 0;
}

static int t_quick_rotate(PgChipTarget *t, int dir)
{
    REFUSE_IF_REACTING(t);
    if (start_quick(O(t), dir ? 0x20 : 0x21, ACT_TURN))
        return 0;
    O(t)->turn_rate = dir ? -TURN_SPEED * 2.0f : TURN_SPEED * 2.0f;
    start_action(O(t), ACT_TURN, 0.25f);
    return 0;
}

static int t_attack(PgChipTarget *t, int kind)
{
    REFUSE_IF_REACTING(t);
    (void)kind;
    Oke *o = O(t), *e = ENEMY(o);
    o->vx = o->vy = o->turn_rate = 0.0f;
    start_action(o, ACT_ATTACK, 0.5f);
    if (hypotf(e->x - o->x, e->y - o->y) <= MELEE_RANGE && fabsf(bearing(o, e->x, e->y)) < 60.0f &&
        e->defend_left <= 0.0f) {
        e->hp -= e->hp_max * MELEE_DAMAGE / 100;
        o->hits++;
    }
    return 0;
}

static int t_defense(PgChipTarget *t, int kind, int b3)
{
    REFUSE_IF_REACTING(t);
    (void)kind;
    O(t)->defend_left = (b3 > 0 ? b3 : 15) / 30.0f;
    O(t)->vx = O(t)->vy = O(t)->turn_rate = 0.0f;
    start_action(O(t), ACT_DEFEND, O(t)->defend_left);
    return 0;
}

static int t_special_move(PgChipTarget *t, int kind) { return t_quick_move(t, kind ? 2 : 6); }

/* CBattleOkeEquipment::GetBulletSpeed (FUN_08856528): the weapon table at
 * 0x08bd5574 ({type, ability, bullet, dispenser, category}, 20 bytes per weapon)
 * names a bullet (speed at +0x0c of the 0x34-byte entries at 0x08c69c74; 77 = none)
 * and a dispenser (missile launchers, speed at +0x0c of 0x08bcb8fc; 10 = none). */
static float weapon_speed(int id)
{
    if (id <= 0 || id >= 60)
        return 100.0f;
    uint32_t bullet = gamedata_word(0x003D157Cu + (uint32_t)id * 20, 77);
    uint32_t disp = gamedata_word(0x003D1580u + (uint32_t)id * 20, 10);
    uint32_t bits = 0x42c80000u; /* 100.0 */
    if (bullet != 77 && bullet < 78)
        bits = gamedata_word(0x00465C80u + bullet * 0x34, bits);
    if (disp != 10 && disp < 32)
        bits = gamedata_word(0x003C7908u + disp * 0x34, bits);
    float v;
    memcpy(&v, &bits, sizeof v);
    return v > 1.0f && v < 10000.0f ? v : 100.0f;
}

static int word_at(uint32_t splat, int fallback)
{
    return (int)gamedata_word(splat, (uint32_t)fallback);
}

static float float_at(uint32_t splat, float fallback)
{
    uint32_t bits;
    memcpy(&bits, &fallback, sizeof bits);
    bits = gamedata_word(splat, bits);
    float v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

/* The bullet and dispenser tables, as CBattleOkeEquipment reads them:
 *   bullets     0x08bcb1a4, 0x14 each: attack, hit heat, flags, ?, kind
 *   sub-bullets 0x08c69410, 0x1c each: child bullet, child count (a cluster
 *               reports its child's numbers: FUN_08810584 / FUN_08810500)
 *   dispensers  0x08bcb8dc, 0x34 each: +0x0c child bullet, +0x10/+0x14 counts
 *               (both non-zero: the child's numbers), +0x24 attack, +0x28 heat
 *               (FUN_08815f90 / FUN_08815f30) */
#define NO_BULLET 77
#define NO_DISPENSER 10

static int bullet_stat(int bullet, int field)
{
    if (word_at(0x00465414u + (uint32_t)bullet * 0x1c, 0) != 0)
        bullet = word_at(0x00465410u + (uint32_t)bullet * 0x1c, bullet);
    if (bullet < 0 || bullet >= NO_BULLET)
        return 0;
    return word_at(0x003C71A4u + (uint32_t)bullet * 0x14 + (uint32_t)field * 4, field ? 0 : 50);
}

static int dispenser_stat(int disp, int field)
{
    uint32_t e = 0x003C78DCu + (uint32_t)disp * 0x34;
    if (word_at(e + 0x10, 0) * word_at(e + 0x14, 0) != 0)
        return bullet_stat(word_at(e + 0x0c, NO_BULLET), field);
    return field < 2 ? word_at(e + 0x24 + (uint32_t)field * 4, field ? 0 : 100) : 0x20;
}

/* GetWeaponAttack (FUN_08856480) and friends: field 0 attack, 1 hit heat, 2 flags. */
static int weapon_stat(int id, int field)
{
    int bullet = word_at(0x003D157Cu + (uint32_t)id * 20, NO_BULLET);
    int disp = word_at(0x003D1580u + (uint32_t)id * 20, NO_DISPENSER);
    int v = field == 0 ? 7 : 0;
    if (bullet != NO_BULLET && bullet >= 0 && bullet < NO_BULLET)
        v = bullet_stat(bullet, field);
    if (disp != NO_DISPENSER && disp >= 0 && disp < 32)
        v = dispenser_stat(disp, field);
    return v;
}

/* FUN_0883d960: heat rises, up to 120% of the limit. */
static void add_heat(Oke *o, int amount)
{
    o->heat += amount;
    if (o->heat > o->heat_max * 120 / 100)
        o->heat = o->heat_max * 120 / 100;
}

static void fire_at(Oke *o, float tx, float ty, float speed, int weapon, float spread_max)
{
    Battle *b = o->battle;
    for (int i = 0; i < MAX_SHOTS; i++)
        if (!b->shots[i].live) {
            float d = hypotf(tx - o->x, ty - o->y);
            if (d < 1.0f)
                d = 1.0f;
            float spread = ((float)(rnd(b) % 100) - 50.0f) / 50.0f * spread_max;
            float ux = (tx - o->x) / d, uy = (ty - o->y) / d;
            float c = cosf(spread), s = sinf(spread);
            int bullet = word_at(0x003D157Cu + (uint32_t)weapon * 20, NO_BULLET);
            /* FUN_08811940: speed + rand x the bullet's +0x10 (-20 to -40 for some). */
            if (bullet >= 0 && bullet < NO_BULLET) {
                float jitter = float_at(0x00465C84u + (uint32_t)bullet * 0x34, 0.0f);
                if (jitter < 0.0f && jitter > -1000.0f)
                    speed += (float)(rnd(b) % 10000) / 10000.0f * jitter;
            }
            float falloff = bullet >= 0 && bullet < NO_BULLET
                                ? float_at(0x00465CA0u + (uint32_t)bullet * 0x34, 3.4e38f) /* +0x2c */
                                : 3.4e38f;
            /* +0x28 finite: a ballistic bullet (the bullet update, FUN_08812xxx,
             * sets its flag from that alone). It slows by +0x18 a second down to
             * half its base speed (+0xc) and falls by +0x30 x gravity (no height
             * here), and homes by +0x24 (0 for every bullet). */
            float drag = 0.0f, min_speed = 0.0f;
            if (bullet >= 0 && bullet < NO_BULLET &&
                float_at(0x00465C9Cu + (uint32_t)bullet * 0x34, 3.4e38f) < 3.4e38f) {
                drag = float_at(0x00465C8Cu + (uint32_t)bullet * 0x34, 0.0f);
                min_speed = float_at(0x00465C80u + (uint32_t)bullet * 0x34, 0.0f) * 0.5f;
                if (!(drag > 0.0f && drag < 1000.0f))
                    drag = 0.0f;
            }
            b->shots[i] = (Shot){true, o->side, o->x, o->y, (ux * c - uy * s) * speed,
                                 (ux * s + uy * c) * speed, drag, min_speed, 0.0f, falloff,
                                 weapon_stat(weapon, 0), weapon_stat(weapon, 1), weapon_stat(weapon, 2)};
            o->shots_fired++;
            return;
        }
}

/* Select and Fire / Firing Direction (FUN_08845038 / FUN_088452c8): slot 0 means
 * the current weapon, 1-5 that slot. The weapon's script (0x2a + slot,
 * SCRIPT_ID_WEAPON_1..5) aims, and its 0x20b command pulls the trigger
 * (FUN_08848a68); `count` shots go out (+0xa308, FUN_0884299c). */
static int pick_weapon(const Oke *o, int slot)
{
    if (slot > 0)
        return slot <= 5 ? slot - 1 : -1;
    return o->readied; /* FUN_0883fbb0: the weapon Start Reticle readied */
}

static int start_fire(Oke *o, int slot, int count)
{
    int w = pick_weapon(o, slot);
    if (w < 0)
        return 0x11; /* no weapon */
    if (reacting(o))
        return 0x10;
    if (o->weapon[w] <= 0 || o->ammo[w] <= 0 || weapon_lost(o, w))
        return 0x12; /* empty or broken off */
    if (o->scripted) {
        o->scripted = false;
        o->act = ACT_NONE;
    }
    o->firing = w;
    o->burst_left = count > 0 ? count : 1;
    o->burst_timer = 0.0f;
    o->armed = true;
    if (o->motion.s && o->motion.s->count[0x2a + w] > 0) {
        oke_motion_start(&o->motion, 0x2a + w);
        o->armed = false;
    }
    return 0;
}

/* FUN_0883bf74 / _bfd0 / _c034: the fire control goes to state 3 in that mode.
 * `spread` indexes the per-shot table at 0x08cce2c0 (filled at start-up: entries
 * 0, 1 and 4 are 0); only a 0 entry lets the shot lead (FUN_0884299c). */
static void aim_as(Oke *o, int mode, int spread)
{
    o->aiming = true;
    o->aim_mode = mode;
    o->lead_ok = spread == 0 || spread == 1 || spread == 4;
}

/* Start Reticle (FUN_088481b0 -> FUN_0883be40): 0-4 readies that weapon (8 if it
 * has broken off, 9 if the slot is empty, FUN_08843bf8), 5 puts it away and resets
 * the sight (FUN_0883b1d0), 6 readies the next usable one after the current
 * (FUN_0883b084), putting it away after the last. */
static int t_start_reticle(PgChipTarget *t, int which)
{
    Oke *o = O(t);
    if (which == 6) {
        int i = o->readied;
        do
            i = i < 0 ? 0 : i + 1;
        while (i < 5 && (o->weapon[i] <= 0 || weapon_lost(o, i)));
        which = i < 5 ? i : 5;
    }
    if (which == 5 || which < 0 || which > 4) {
        o->readied = -1;
        o->aiming = false;
        o->aim_mode = 0;
        o->sight_yaw = o->sight_goal = o->sight_goal_prev = o->sight_step = o->sight_bonus = 0.0f;
        o->mark_bonus = 0.0f;
        return 0;
    }
    if (reacting(o))
        return 0x10;
    if (weapon_lost(o, which))
        return 8;
    if (o->weapon[which] <= 0)
        return 9;
    o->readied = which;
    return 0;
}

/* Move Reticle (FUN_088481d8 -> FUN_0883c0d4): needs a readied weapon; ignored
 * while firing in mode 2 or 3. Sets the sight's goal to yaw/100 degrees (pitch
 * isn't simulated), relative to where it points when b2 is 1, and holds the
 * tracker off for 2 seconds. */
static int t_move_reticle(PgChipTarget *t, int rel, int yaw, int pitch)
{
    (void)pitch;
    Oke *o = O(t);
    if (o->readied < 0)
        return 0x11;
    if (o->aiming && (o->aim_mode == 2 || o->aim_mode == 3))
        return 0;
    int v = yaw & 0xffff;
    if (v > 18000)
        v -= 36000;
    float g = (float)v * 0.01f;
    g = g < -180.0f ? -180.0f : g > 180.0f ? 180.0f : g;
    if (rel == 1)
        g = wrap180(g + o->sight_yaw);
    o->sight_goal = g;
    o->sight_hold = 2.0f;
    return 0;
}

/* Shoot Reticle (FUN_08848288): fires weapon b2 (0: the readied one) b4 times at
 * the sight's mark, mode 1; b3 is the spread setting. */
static int t_shoot_reticle(PgChipTarget *t, int which, int spread, int count, int one)
{
    (void)one;
    Oke *o = O(t);
    if (o->readied < 0)
        return 0x11;
    int res = start_fire(o, which, count);
    if (res == 0)
        aim_as(o, 1, spread);
    return res;
}

static int t_select_fire(PgChipTarget *t, PgChipRange r, int a, int b, int c, int one)
{
    (void)c; (void)one;
    Oke *o = O(t), *e = ENEMY(o);
    if (!in_range(o, r, e->x, e->y))
        return 0xd; /* no target (FUN_08841bd8 == -1) */
    int res = start_fire(o, a, b);
    if (res == 0) /* FUN_0883bfd0 */
        aim_as(o, 2, c);
    return res;
}

static int t_firing_direction(PgChipTarget *t, int b2, int b3, int b4, int b5, int one)
{
    (void)b2; (void)one;
    Oke *o = O(t);
    int res = start_fire(o, b4, b5);
    if (res == 0)
        aim_as(o, 3, 0);
    o->aim_angle = (float)b3 * 2.0f; /* stand-in for FUN_089fc4d4 */
    return res;
}

/* FUN_0883e2c4: where to aim at the target, its body; with a bullet speed, moved
 * on by the target's velocity over the flight time (one pass, no iteration). */
static void aim_point(const Oke *o, const Oke *e, float speed, float *tx, float *ty)
{
    *tx = e->x;
    *ty = e->y;
    if (speed > 0.0f) {
        float t = hypotf(*tx - o->x, *ty - o->y) / speed;
        *tx += e->vel_x * t;
        *ty += e->vel_y * t;
    }
}

/* One 30 Hz frame of the gun sight. FUN_0883baa4 aims at the locked target's
 * body (lead only applies to the reticle chips, mode 1: not used here) and gives
 * the distance, 200 without a lock; FUN_0884a1c8 turns the sight half the way per
 * frame, at most 30 degrees (body table +0x50), with up to 50% extra while it keeps
 * turning the same way; FUN_0883b300 / FUN_0883b214 keep it within a turn. */
static void sight_tick(Oke *o)
{
    Oke *e = ENEMY(o);
    o->vel_x = (o->x - o->last_x) * 30.0f;
    o->vel_y = (o->y - o->last_y) * 30.0f;
    o->last_x = o->x;
    o->last_y = o->y;
    bool tracking = o->locked && e->hp > 0;
    if (tracking) {
        /* Shoot Reticle with the gun fully raised leads the target. */
        float speed = 0.0f;
        if (o->aiming && o->aim_mode == 1 && o->gun_raise >= 1.0f && o->lead_ok)
            speed = weapon_speed(o->weapon[o->firing >= 0 ? o->firing : o->readied]);
        float tx, ty;
        aim_point(o, e, speed, &tx, &ty);
        o->sight_dist = hypotf(tx - o->x, ty - o->y);
        if (o->sight_hold > 0.0f) {
            o->sight_hold -= 1.0f / 30.0f;
        } else {
            o->sight_goal_prev = o->sight_goal;
            o->sight_goal = wrap180(atan2f(tx - o->x, -(ty - o->y)) / DEG - o->heading);
        }
    } else {
        o->sight_hold = 0.0f;
        o->sight_dist = 200.0f;
    }
    float goal = o->sight_goal;
    if (tracking && o->sight_hold <= 0.0f) /* one frame ahead */
        goal += wrap180(o->sight_goal - o->sight_goal_prev);
    float best = goal;
    for (int k = -1; k <= 1; k += 2)
        if (fabsf(goal + 360.0f * k - o->sight_yaw) < fabsf(best - o->sight_yaw))
            best = goal + 360.0f * k;
    goal = best;
    float limit = o->aim_speed > 0.0f ? o->aim_speed : 30.0f;
    float step = (goal - o->sight_yaw) * 0.5f;
    if (step > limit) step = limit;
    if (step < -limit) step = -limit;
    if (fabsf(step) < 0.0017453292f) {
        o->sight_yaw = goal;
        o->sight_step = 0.0f;
        o->sight_bonus = 0.0f;
    } else {
        if (o->sight_step * step > 0.0f) {
            o->sight_bonus = o->sight_bonus + 0.05f > 0.5f ? 0.5f : o->sight_bonus + 0.05f;
            step += (goal - o->sight_yaw) * o->sight_bonus;
        } else {
            o->sight_bonus = 0.0f;
        }
        o->sight_yaw += step;
        o->sight_step = step;
    }
    o->sight_yaw = wrap180(o->sight_yaw);

    /* FUN_0884a4d8: the HUD mark follows the aim point at (0.5 + momentum) of the
     * way, at most 20 units a frame per axis (body table +0x54). */
    float h = (o->heading + o->sight_yaw) * DEG;
    float ax = o->x + sinf(h) * o->sight_dist, ay = o->y - cosf(h) * o->sight_dist;
    float dx = ax - o->mark_x, dy = ay - o->mark_y, f;
    if (hypotf(dx, dy) < 1.0f) {
        o->mark_bonus = 0.0f;
        f = 0.5f;
    } else {
        o->mark_bonus = o->mark_bonus + 0.05f > 0.5f ? 0.5f : o->mark_bonus + 0.05f;
        f = o->mark_bonus + 0.5f;
    }
    float ml = o->mark_speed > 0.0f ? o->mark_speed : 20.0f;
    dx *= f;
    dy *= f;
    o->mark_x += dx > ml ? ml : dx < -ml ? -ml : dx;
    o->mark_y += dy > ml ? ml : dy < -ml ? -ml : dy;
}

static void fire_step(Oke *o, float dt)
{
    if (o->firing < 0)
        return;
    int w = o->firing;
    if (!o->armed) {
        if (o->motion.trigger[w] > 0) {
            o->motion.trigger[w]--;
            o->armed = true;
        } else if (!oke_motion_busy(&o->motion)) {
            o->armed = true; /* the script ended without a trigger command */
        } else {
            return;
        }
    }
    o->burst_timer -= dt;
    while (o->burst_timer <= 0.0f && o->burst_left > 0 && o->ammo[w] > 0) {
        Oke *e = ENEMY(o);
        int id = o->weapon[w];
        float speed = weapon_speed(id);
        /* GetWeaponNum (0x08bd5a24): pellets per round, e.g. 8-12 for shotguns. */
        int pellets = word_at(0x003D1A24u + (uint32_t)id * 20, 1);
        if (pellets < 1 || pellets > 16)
            pellets = 1;
        float tx, ty;
        /* FUN_0883c298 picks the point the shot flies at (+0x40): Select and
         * Fire aims at the target itself, leading it once the gun is fully raised;
         * Shoot Reticle at the sight's mark (+0x70); Firing Direction along its
         * angle. */
        if (o->aim_mode == 2) {
            aim_point(o, e, o->gun_raise >= 1.0f && o->lead_ok ? speed : 0.0f, &tx, &ty);
        } else if (o->aim_mode == 1) {
            tx = o->mark_x;
            ty = o->mark_y;
        } else {
            float h = (o->heading + o->aim_angle) * DEG;
            tx = o->x + sinf(h) * 100.0f;
            ty = o->y - cosf(h) * 100.0f;
        }
        for (int p = 0; p < pellets; p++)
            fire_at(o, tx, ty, speed, id, pellets > 1 ? 0.08f : 0.0f); /* pellet spread: stand-in */
        /* GetWeaponShotCalorie (0x08bd5a30) heats the shooter (FUN_0883d960). */
        add_heat(o, word_at(0x003D1A30u + (uint32_t)id * 20, 0));
        o->ammo[w]--;
        o->burst_left--;
        o->burst_timer += BURST_GAP;
    }
    if (o->burst_left <= 0 || o->ammo[w] <= 0)
        o->firing = -1;
}

static int t_scan_oke(PgChipTarget *t, PgChipRange r, int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    Oke *o = O(t), *e = ENEMY(o);
    return e->hp > 0 && in_range(o, r, e->x, e->y);
}

static int t_judge_area(PgChipTarget *t, PgChipRange r) { return wall_in_range(O(t), r); }
static int t_scan_obstacles(PgChipTarget *t, PgChipRange r, int a, int b)
{
    (void)a; (void)b;
    return wall_in_range(O(t), r);
}

static int t_scan_projectiles(PgChipTarget *t, PgChipRange r, int a, int b, int c)
{
    (void)a; (void)b; (void)c;
    Oke *o = O(t);
    for (int i = 0; i < MAX_SHOTS; i++) {
        const Shot *s = &o->battle->shots[i];
        if (s->live && s->owner != o->side && in_range(o, r, s->x, s->y))
            return 1;
    }
    return 0;
}

/* Guess: b2 0 = remaining armour, b3 a percentage, b4 0 "at or below" / 1 "above". */
/* FUN_088462a4: HP (0), fuel (1) or heat (2) as a percentage, then FUN_08a184f0. */
static int t_judge_status(PgChipTarget *t, int b2, int b3, int b4)
{
    int v = b2 == 0 ? oke_hp_percent(O(t)) : b2 == 2 ? oke_heat_percent(O(t)) : 100;
    if (b4 == 2)
        return v == b3;
    return b4 == 0 ? v >= b3 : v <= b3;
}

/* Action Assessment, b2 in the order of the action names: 0 still, 1-4 forward /
 * backward / right / left, 5-6 clockwise / counter-clockwise, 7-10 the diagonals.
 * The interpreter also asks for 8 as its interrupt check, so that one stays false. */
static int t_judge_action(PgChipTarget *t, int b2)
{
    const Oke *o = O(t);
    if (b2 == 0)
        return o->act == ACT_NONE;
    if (o->motion.s && b2 <= 10) /* the action numbers are the motion script ids */
        return o->scripted && o->motion.script == b2;
    if (b2 == 5 || b2 == 6)
        return o->act == ACT_TURN && (b2 == 5) == (o->turn_rate > 0);
    if (b2 == 8 || o->act != ACT_MOVE)
        return 0;
    static const int k_dir_for[11] = {-1, 0, 1, 2, 3, -1, -1, 4, 5, 6, 7};
    if (b2 > 10 || k_dir_for[b2] < 0)
        return 0;
    float h = atan2f(o->vx, -o->vy) / DEG - o->heading;
    return fabsf(wrap180(h - k_dir_angle[k_dir_for[b2]])) < 1.0f;
}

static int t_judge_random(PgChipTarget *t, int pct) { return (int)(rnd(O(t)->battle) % 100) < pct; }

/* Guess: b2 seconds, b4 0 "elapsed at least" / 1 "less than". */
static int t_judge_time(PgChipTarget *t, int secs, int b4, int b5)
{
    (void)b5;
    float time = O(t)->battle->time;
    return b4 ? time < (float)secs : time >= (float)secs;
}

static int t_lock_on(PgChipTarget *t, PgChipRange r, int a, int b, int c)
{
    (void)a; (void)b; (void)c;
    Oke *o = O(t), *e = ENEMY(o);
    /* FUN_088464b0: a target found in range becomes the lock; otherwise the old
     * lock stays. Always accepted. */
    if (e->hp > 0 && in_range(o, r, e->x, e->y))
        o->locked = true;
    return 0;
}

static int t_cancel_lock_on(PgChipTarget *t) { O(t)->locked = false; return 0; }

static int t_judge_target_location(PgChipTarget *t, PgChipRange r)
{
    Oke *o = O(t), *e = ENEMY(o);
    return o->locked && in_range(o, r, e->x, e->y);
}

static int t_judge_location_from_target(PgChipTarget *t, PgChipRange r, int b6)
{
    (void)b6;
    Oke *o = O(t), *e = ENEMY(o);
    return o->locked && in_range(e, r, o->x, o->y);
}

static int t_auto_rotate(PgChipTarget *t, PgChipRange r, int b6)
{
    (void)b6;
    Oke *o = O(t), *e = ENEMY(o);
    if (!in_range(o, r, e->x, e->y))
        return 2;
    float turn = bearing(o, e->x, e->y);
    o->turn_rate = turn >= 0 ? TURN_SPEED : -TURN_SPEED;
    o->vx = o->vy = 0.0f;
    start_action(o, ACT_TURN, fabsf(turn) / TURN_SPEED);
    return 0;
}

static int t_zero1(PgChipTarget *t, int a) { (void)t; (void)a; return 0; }
static int t_zero2(PgChipTarget *t, int a, int b) { (void)t; (void)a; (void)b; return 0; }
static int t_zero3(PgChipTarget *t, int a, int b, int c) { (void)t; (void)a; (void)b; (void)c; return 0; }
static int t_zero5(PgChipTarget *t, int a, int b, int c, int d, int e)
{
    (void)t; (void)a; (void)b; (void)c; (void)d; (void)e;
    return 0;
}
static int t_button_input(PgChipTarget *t, int wait, int *pressed)
{
    (void)t; (void)wait;
    if (pressed)
        *pressed = 0;
    return 0;
}

static const PgChipTargetOps k_oke_ops = {
    .timestamp = t_timestamp,
    .halt_start = t_halt_start,
    .halt_finished = t_halt_finished,
    .action_finished = t_action_finished,
    .fire_finished = t_fire_finished,
    .halt_action = t_halt_action,
    .move = t_move,
    .rotate = t_rotate,
    .jump = t_jump,
    .quick_move = t_quick_move,
    .quick_rotate = t_quick_rotate,
    .attack = t_attack,
    .defense = t_defense,
    .special_move = t_special_move,
    .set_altitude = t_zero2,
    .select_fire = t_select_fire,
    .firing_direction = t_firing_direction,
    .activate_option = t_zero1,
    .judge_remaining = t_zero3,
    .judge_area = t_judge_area,
    .scan_oke = t_scan_oke,
    .scan_obstacles = t_scan_obstacles,
    .scan_projectiles = t_scan_projectiles,
    .judge_status = t_judge_status,
    .judge_action = t_judge_action,
    .judge_random = t_judge_random,
    .judge_time = t_judge_time,
    .lock_on = t_lock_on,
    .cancel_lock_on = t_cancel_lock_on,
    .judge_target_location = t_judge_target_location,
    .judge_location_from_target = t_judge_location_from_target,
    .judge_target_movement = t_zero1,
    .judge_target_code = t_zero1,
    .judge_target_alert = t_zero1,
    .judge_target_weapon = t_zero2,
    .auto_rotate = t_auto_rotate,
    .sight_location = t_zero1,
    .judge_line_of_fire = t_zero1,
    .input_counter = t_zero2,
    .control_counter = t_zero5,
    .counter_lock_on = t_zero1,
    .counter_reticle = t_zero3,
    .judge_counter = t_zero5,
    .send_counter = t_zero2,
    .receive_counter = t_zero2,
    .camera_view = t_zero2,
    .judge_button_assign = t_zero2,
    .button_input = t_button_input,
    .judge_analog = t_zero3,
    .start_reticle = t_start_reticle,
    .move_reticle = t_move_reticle,
    .shoot_reticle = t_shoot_reticle,
    .alert = t_zero3,
};

static void oke_init(Oke *o, Battle *b, int side, float x, float y, float heading)
{
    o->base.ops = &k_oke_ops;
    o->battle = b;
    o->side = side;
    o->x = x;
    o->y = y;
    o->heading = heading;
    int32_t body, hw_heat, armour_part;
    memcpy(&body, o->rec.hardware + 0x240, sizeof body); /* FUN_089ed210 */
    memcpy(&hw_heat, o->rec.hardware + 0x29c, sizeof hw_heat);
    memcpy(&armour_part, o->rec.hardware + 0x280, sizeof armour_part);
    memcpy(&o->armour_type, o->rec.hardware + 0x284, sizeof o->armour_type);
    /* The body table at 0x08c14d84 (0x68 per type): +0 max HP, +0x10 heat limit. */
    uint32_t bt = 0x00410D84u + (uint32_t)(body >= 0 && body < 64 ? body : 0) * 0x68;
    o->hp_max = word_at(bt, 800);
    if (o->hp_max <= 0)
        o->hp_max = 800;
    o->hp = o->hp_max;
    o->heat = 0;
    o->heat_max = word_at(bt + 0x10, 1000) * (200 - hw_heat) / 100; /* FUN_089ed718 */
    if (o->heat_max <= 0)
        o->heat_max = 1000;
    o->armour = armour_part >= 0 && armour_part < 12 ? word_at(0x003D1EDCu + (uint32_t)armour_part * 12, 0) : 0;
    o->stagger = word_at(bt + 0x30, 40);
    o->react_left = o->down_left = 0.0f;
    o->push_x = o->push_y = o->push_spin = 0.0f;
    float as = float_at(bt + 0x50, 30.0f), ms = float_at(bt + 0x54, 20.0f); /* FUN_089ed9c0 / _9e8 */
    o->aim_speed = as > 0.0f && as < 360.0f ? as : 30.0f;
    o->mark_speed = ms > 0.0f && ms < 1000.0f ? ms : 20.0f;
    o->sight_yaw = o->sight_goal = o->sight_goal_prev = o->sight_step = o->sight_bonus = 0.0f;
    o->sight_dist = 200.0f;
    o->readied = -1;
    o->aiming = false;
    o->aim_mode = 0;
    o->lead_ok = false;
    o->gun_raise = o->sight_hold = 0.0f;
    o->raise_rate = 2.0f * (float)word_at(bt + 0x38, 160) / 100.0f; /* FUN_089ed880 */
    o->last_x = x;
    o->last_y = y;
    o->vel_x = o->vel_y = 0.0f;
    o->sight_clock = o->mark_bonus = 0.0f;
    o->mark_x = x + sinf(heading * DEG) * 200.0f;
    o->mark_y = y - cosf(heading * DEG) * 200.0f;
    oke_motion_reset(&o->motion, oke_scripts_get(body));
    o->scripted = false;
    for (int i = 0; i < 5; i++) {
        memcpy(&o->weapon[i], o->rec.hardware + 0x258 + 4 * i, sizeof(int32_t));
        memcpy(&o->ammo[i], o->rec.hardware + 0x26c + 4 * i, sizeof(int32_t));
        if (o->weapon[i] < 0 || o->weapon[i] >= 60)
            o->weapon[i] = 0;
    }
    o->firing = -1;
    setup_parts(o);
    int cpu = oke_cpu_part(&o->rec);
    pgchip_setup(&o->vm, &o->rec, battle_chip_speed(cpu));
    SDL_Log("[battle] side %d: CPU part %d, %d chips/s, main program %dx%d", side, cpu, o->vm.speed,
            o->vm.prog[0].width, o->vm.prog[0].width);
}

static void oke_step(Oke *o, float dt)
{
    if (o->halt_left > 0.0f)
        o->halt_left -= dt;
    if (o->fire_left > 0.0f)
        o->fire_left -= dt;
    if (o->defend_left > 0.0f)
        o->defend_left -= dt;
    if (o->react_left > 0.0f)
        o->react_left -= dt;
    if (o->down_left > 0.0f)
        o->down_left -= dt;
    /* Knock-back acts for one 30 Hz physics step (FUN_0884df08 adds +0xb040 /
     * +0xb050 to the accelerations, then they're cleared). */
    o->motion.vx += o->push_x / 30.0f;
    o->motion.vy += o->push_y / 30.0f;
    o->motion.omega += o->push_spin / 30.0f / DEG;
    o->push_x = o->push_y = o->push_spin = 0.0f;
    /* FUN_088509bc: the gun rises while the fire control aims and drops at once
     * otherwise. */
    o->gun_raise = o->aiming ? fminf(1.0f, o->gun_raise + dt * o->raise_rate) : 0.0f;
    for (o->sight_clock += dt; o->sight_clock >= 1.0f / 30.0f; o->sight_clock -= 1.0f / 30.0f)
        sight_tick(o);
    fire_step(o, dt);
    if (o->motion.s && (o->act == ACT_NONE || o->scripted)) {
        oke_motion_step(&o->motion, dt, &o->x, &o->y, &o->heading);
        if (o->scripted) {
            o->obs_turned += fabsf(wrap180(o->heading - o->obs_last_heading));
            o->obs_last_heading = o->heading;
            float done = o->act == ACT_MOVE ? hypotf(o->x - o->obs_x, o->y - o->obs_y) : o->obs_turned;
            if (done >= o->obs_amount || !oke_motion_busy(&o->motion)) {
                oke_motion_stop(&o->motion);
                o->scripted = false;
                o->act = ACT_NONE;
            }
        }
    } else if (o->act != ACT_NONE) {
        o->x += o->vx * dt;
        o->y += o->vy * dt;
        o->heading = wrap180(o->heading + o->turn_rate * dt);
        o->act_left -= dt;
        if (o->act_left <= 0.0f) {
            o->act = ACT_NONE;
            o->vx = o->vy = o->turn_rate = 0.0f;
        }
    }
    if (o->x < OKE_RADIUS) o->x = OKE_RADIUS;
    if (o->y < OKE_RADIUS) o->y = OKE_RADIUS;
    if (o->x > FIELD - OKE_RADIUS) o->x = FIELD - OKE_RADIUS;
    if (o->y > FIELD - OKE_RADIUS) o->y = FIELD - OKE_RADIUS;
}

static int percent(int v, int max) /* FUN_0883fc04 */
{
    float p = max > 0 ? (float)v / (float)max * 100.0f : 100.0f;
    if (p <= 0.01f)
        return 0;
    if (p >= 99.99f)
        return 100;
    int i = (int)(p + 0.5f);
    return i < 1 ? 1 : i > 99 ? 99 : i;
}

int oke_hp_percent(const Oke *o) { return percent(o->hp, o->hp_max); }
int oke_heat_percent(const Oke *o) { return percent(o->heat, o->heat_max); }

static const float k_class_factor[3] = {1.0f, 0.7f, 0.1f}; /* 0x08bd1814 */
static const float k_class_weight[3] = {1.5f, 3.0f, 5.0f}; /* 0x08bd1820 */

static int box_class(int cls) { return cls < 0 || cls > 2 ? 0 : cls; }

/* Part armour at battle start (the loop after FUN_0883d4bc's weapon setup): each
 * bone's share of the class-weighted box volume, times twice the HP, plus 180 per
 * weapon mount on it, plus 5x the armour value once non-zero. */
static void setup_parts(Oke *o)
{
    memset(o->part_hp, 0, sizeof o->part_hp);
    const OkeScripts *s = o->motion.s;
    if (!s)
        return;
    float share[OKE_BONES] = {0}, total = 0.0f;
    for (int i = 0; i < s->n_boxes; i++) {
        const OkeBox *b = &s->box[i];
        if (b->bone < 0 || b->bone >= OKE_BONES)
            continue;
        float v = b->s[0] * b->s[1] * b->s[2] * k_class_weight[box_class(b->cls)];
        share[b->bone] += v;
        total += v;
    }
    for (int bone = 0; bone < OKE_BONES; bone++) {
        float sh = total > 0.0f ? share[bone] / total : 0.0f;
        int hp = (int)(o->hp * sh) * 2;
        if (sh > 0.0f && hp == 0)
            hp = 1;
        for (int m = 0; m < OKE_MOUNTS; m++)
            if (s->mount_bone[m] == bone)
                hp += 180;
        o->part_hp[bone] = hp > 0 ? hp + o->armour * 5 : hp;
    }
}

/* FUN_0883dd78: a weapon whose mount bone has broken off can't be used. */
static bool weapon_lost(const Oke *o, int slot)
{
    const OkeScripts *s = o->motion.s;
    if (!s || slot < 0 || slot >= OKE_MOUNTS)
        return false;
    int bone = s->mount_bone[slot];
    return bone >= 0 && bone < OKE_BONES && o->part_hp[bone] < 1;
}

static bool reacting(const Oke *o) { return o->down_left > 0.0f || o->react_left > 0.0f; }

/* FUN_08843c90 / FUN_08843ce8: flinch scripts by side, knock-down script 0x5b. */
static void react(Oke *o, int script, float fallback)
{
    o->firing = -1;
    o->scripted = false;
    o->act = ACT_NONE;
    float t = fallback;
    if (o->motion.s && o->motion.s->count[script] > 0) {
        o->motion.script = -1; /* restart even if it's already playing */
        oke_motion_start(&o->motion, script);
        t = 0.0f;
        const OkeScripts *s = o->motion.s;
        for (int i = 0; i < s->count[script]; i++)
            t += s->cmds[s->first[script] + i].frames / 30.0f;
        if (t < fallback)
            t = fallback;
    }
    o->react_left = t;
}

/* CBattleOke's hit receiver (FUN_0884d284). `bone`/`cls` is the box that was hit
 * (-1: no geometry, treated as an armoured class-0 part); hx, hy the hit point. */
static void receive_hit(Oke *e, const Shot *s, int bone, int cls, float hx, float hy)
{
    int attack = s->attack, heat = s->heat;
    /* CBattleBulletManager::ResolveCollisionRecords: old shots weaken. */
    if (s->age > s->falloff_after) {
        float over = s->age - s->falloff_after;
        int a = attack - (int)(over * 60.0f), h = heat - (int)(over * 120.0f);
        attack = a < (int)(attack * 0.2f) ? (int)(attack * 0.2f) : a;
        heat = h < (int)(heat * 0.2f) ? (int)(heat * 0.2f) : h;
    }
    float k = k_class_factor[box_class(cls)];
    bool armoured = bone < 0 || e->part_hp[bone] > 0; /* +0xa010 */
    float hf = (float)heat;
    if (armoured) {
        if (e->armour_type == 4)
            hf *= 0.7f;
        hf *= k;
    }
    add_heat(e, hf > 0.0f ? (int)hf : 0);
    float d = (float)attack;
    if (armoured && (((s->flags & 1) && e->armour_type == 1) || ((s->flags & 2) && e->armour_type == 3) ||
                     ((s->flags & 0x10) && e->armour_type == 2)))
        d *= 0.7f;
    bool defending = e->defend_left > 0.0f;
    if (defending) /* script 0x25 */
        d *= 0.7f;
    if (armoured)
        d = d * k - d * k * (e->armour / 100.0f) - e->armour * 0.5f;
    if (d <= 1.0f)
        d = 1.0f;
    int dmg = (int)d;
    e->hp -= dmg; /* FUN_0883d7f4 */
    if (e->hp < 0)
        e->hp = 0;
    if (bone >= 0 && e->part_hp[bone] > 0) { /* FUN_088412b0 */
        e->part_hp[bone] -= dmg;
        if (e->part_hp[bone] <= 0) {
            e->part_hp[bone] = 0;
            for (int w = 0; w < 5; w++)
                if (e->firing == w && weapon_lost(e, w))
                    e->firing = -1;
        }
    }
    if (e->hp <= 0)
        return;

    /* Reactions. */
    int t = e->stagger > 0 ? e->stagger : 40;
    if (defending)
        t *= 2;
    bool big = dmg > t, huge = dmg > 3 * t;
    Battle *b = e->battle;
    bool knock = big && ((rnd(b) & 7) == 0 || huge);
    bool flinch = big || rnd(b) % 20 == 0;
    float len = hypotf(s->vx, s->vy);
    float ux = len > 0.0f ? s->vx / len : 0.0f, uy = len > 0.0f ? s->vy / len : 0.0f;
    if (knock) {
        react(e, 0x5b, DOWN_TIME);
        e->down_left = DOWN_TIME;
        /* The ragdoll starts with the shot's velocity; the body slides with it. */
        e->motion.vx += s->vx * 0.1f;
        e->motion.vy += s->vy * 0.1f;
        return;
    }
    if (flinch && e->react_left <= 0.0f) {
        /* Shot direction against facing: from the front 0x57, from behind 0x58, the
         * sides 0x59 / 0x5a (mirror unchecked). */
        float a = wrap180(atan2f(ux, -uy) / DEG - e->heading);
        int dir = fabsf(a) > 135.0f ? 0 : fabsf(a) < 45.0f ? 1 : a <= 45.0f ? 3 : 2;
        react(e, 0x57 + dir, 0.3f);
    }
    /* Knock-back (FUN_08842a94): along the shot, turned up to 30 degrees towards the
     * side that was hit, d / 3t x 2500 for one physics step; plus a spin from the
     * hit point's lever arm, capped at 100. */
    float ox = e->x - hx, oy = e->y - hy, ol = hypotf(ox, oy);
    float px = ux, py = uy;
    if (ol > 1e-4f) {
        ox /= ol;
        oy /= ol;
        if (ux * ox + uy * oy < 0.866025f) {
            float side = ux * oy - uy * ox > 0.0f ? 0.5f : -0.5f;
            px = ux * 0.866025f + uy * side;
            py = uy * 0.866025f - ux * side;
        } else {
            px = ox;
            py = oy;
        }
    }
    float f = (float)dmg / (float)(3 * t);
    e->push_x += px * f * 2500.0f;
    e->push_y += py * f * 2500.0f;
    float lever = ((hx - e->x) * uy - (hy - e->y) * ux) / 3.0f * f;
    float spin = lever * 100.0f;
    if (spin > 100.0f) spin = 100.0f;
    if (spin < -100.0f) spin = -100.0f;
    e->push_spin += spin;
}

/* Where a shot at (x, y) meets the target's boxes, in the body frame (x left, z
 * forward); -1 when it misses. Without geometry, a 12-unit circle stands in. */
static int hit_box(const Oke *e, float x, float y, int *cls)
{
    *cls = 0;
    if (!e->motion.s || e->motion.s->n_boxes == 0)
        return hypotf(x - e->x, y - e->y) < OKE_RADIUS ? OKE_BONES : -1;
    float dx = x - e->x, dy = y - e->y;
    if (dx * dx + dy * dy > 40.0f * 40.0f)
        return -1;
    float h = e->heading * DEG;
    float fz = dx * sinf(h) - dy * cosf(h);   /* forward */
    float rx = dx * cosf(h) + dy * sinf(h);   /* right */
    int i = oke_body_box_at(e->motion.s, e->motion.pose, -rx, fz, NULL);
    if (i < 0)
        return -1;
    *cls = e->motion.s->box[i].cls;
    return e->motion.s->box[i].bone;
}

void battle_sim_step(Battle *b, float dt)
{
    b->tick++;
    b->time += dt;
    for (int i = 0; i < 2; i++)
        if (b->oke[i].hp > 0)
            pgchip_update(&b->oke[i].vm, dt, &b->oke[i].base, b->history[i]);
    for (int i = 0; i < 2; i++)
        oke_step(&b->oke[i], dt);
    /* The two can't overlap. */
    Oke *a = &b->oke[0], *c = &b->oke[1];
    float dx = c->x - a->x, dy = c->y - a->y, d = hypotf(dx, dy);
    if (d < 2 * OKE_RADIUS && d > 0.01f) {
        float push = (2 * OKE_RADIUS - d) / 2;
        a->x -= dx / d * push; a->y -= dy / d * push;
        c->x += dx / d * push; c->y += dy / d * push;
    }
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot *s = &b->shots[i];
        if (!s->live)
            continue;
        Oke *e = &b->oke[s->owner ^ 1];
        /* Sweep the step in half-unit samples so fast shots can't skip a box. */
        float step = hypotf(s->vx, s->vy) * dt;
        int n = (int)(step / 0.5f) + 1, bone = -1, cls = 0;
        float hx = s->x, hy = s->y;
        for (int k = 1; k <= n && e->hp > 0; k++) {
            hx = s->x + s->vx * dt * k / n;
            hy = s->y + s->vy * dt * k / n;
            if ((bone = hit_box(e, hx, hy, &cls)) >= 0)
                break;
        }
        s->x += s->vx * dt;
        s->y += s->vy * dt;
        s->age += dt;
        if (s->drag > 0.0f) {
            float v = hypotf(s->vx, s->vy), nv = fmaxf(v - s->drag * dt, s->min_speed);
            if (v > 0.0f && nv < v) {
                s->vx *= nv / v;
                s->vy *= nv / v;
            }
        }
        if (bone >= 0) {
            receive_hit(e, s, bone < OKE_BONES ? bone : -1, cls, hx, hy);
            b->oke[s->owner].hits++;
            s->live = false;
        } else if (fabsf(s->x - FIELD * 0.5f) > 500.0f || fabsf(s->y - FIELD * 0.5f) > 500.0f) {
            /* The game frees a bullet once it is 500 units from the centre on any
             * axis; there's no time limit (the fuse at +0x1c / +0x20 is off for
             * every bullet). Hitting the ground isn't simulated. */
            s->live = false;
        }
    }
    for (int i = 0; i < 2; i++)
        if (b->oke[i].hp < 0)
            b->oke[i].hp = 0;
    for (int i = 0; i < 2; i++) { /* stand-in cooling */
        Oke *o = &b->oke[i];
        o->heat -= (int)ceilf(o->heat_max * COOLING * dt);
        if (o->heat < 0)
            o->heat = 0;
    }
    int pa = oke_hp_percent(a), pc = oke_hp_percent(c);
    if (a->hp <= 0 || c->hp <= 0 || b->time >= TIME_LIMIT)
        b->result = pa > pc ? 0 : pa < pc ? 1 : 2;
}

void battle_sim_start(Battle *b, uint32_t seed)
{
    memset(b->shots, 0, sizeof b->shots);
    b->time = 0.0f;
    b->tick = 0;
    b->rng = seed;
    b->result = -1;
    oke_init(&b->oke[0], b, 0, FIELD * 0.5f, FIELD * 0.7f, 0.0f);
    oke_init(&b->oke[1], b, 1, FIELD * 0.5f, FIELD * 0.3f, 180.0f);
}
