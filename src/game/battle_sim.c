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

/* PGCHIP::tagRANGE: b2 bits 1-7 inner step, b3 half-width, b4 outer step, b5 the arc
 * direction in 2-degree units (values above 180 degrees wrap negative). */
static void range_bounds(PgChipRange r, float *inner, float *outer)
{
    float a = (float)battle_range_distance(r.b[0] >> 1), b = (float)battle_range_distance(r.b[2]);
    /* Some chips store the two the other way round (the inner step above the outer). */
    *inner = a < b ? a : b;
    *outer = a < b ? b : a;
}

static bool in_range(const Oke *o, PgChipRange r, float px, float py)
{
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
static int t_action_finished(PgChipTarget *t, int category) { (void)category; return O(t)->act == ACT_NONE; }
static int t_fire_finished(PgChipTarget *t) { return O(t)->fire_left <= 0.0f; }
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
    if (start_quick(O(t), k_quick_script[dir & 7], ACT_MOVE))
        return 0;
    set_velocity(O(t), dir, QUICK_SPEED);
    start_action(O(t), ACT_MOVE, 0.35f);
    return 0;
}

static int t_quick_rotate(PgChipTarget *t, int dir)
{
    if (start_quick(O(t), dir ? 0x20 : 0x21, ACT_TURN))
        return 0;
    O(t)->turn_rate = dir ? -TURN_SPEED * 2.0f : TURN_SPEED * 2.0f;
    start_action(O(t), ACT_TURN, 0.25f);
    return 0;
}

static int t_attack(PgChipTarget *t, int kind)
{
    (void)kind;
    Oke *o = O(t), *e = ENEMY(o);
    o->vx = o->vy = o->turn_rate = 0.0f;
    start_action(o, ACT_ATTACK, 0.5f);
    if (hypotf(e->x - o->x, e->y - o->y) <= MELEE_RANGE && fabsf(bearing(o, e->x, e->y)) < 60.0f &&
        e->defend_left <= 0.0f) {
        e->hp -= MELEE_DAMAGE;
        o->hits++;
    }
    return 0;
}

static int t_defense(PgChipTarget *t, int kind, int b3)
{
    (void)kind;
    O(t)->defend_left = (b3 > 0 ? b3 : 15) / 30.0f;
    O(t)->vx = O(t)->vy = O(t)->turn_rate = 0.0f;
    start_action(O(t), ACT_DEFEND, O(t)->defend_left);
    return 0;
}

static int t_special_move(PgChipTarget *t, int kind) { return t_quick_move(t, kind ? 2 : 6); }

static void fire_at(Oke *o, float tx, float ty)
{
    Battle *b = o->battle;
    for (int i = 0; i < MAX_SHOTS; i++)
        if (!b->shots[i].live) {
            float d = hypotf(tx - o->x, ty - o->y);
            if (d < 1.0f)
                d = 1.0f;
            float spread = ((float)(rnd(b) % 100) - 50.0f) / 50.0f * 0.06f;
            float ux = (tx - o->x) / d, uy = (ty - o->y) / d;
            float c = cosf(spread), s = sinf(spread);
            b->shots[i] = (Shot){true, o->side, o->x, o->y, (ux * c - uy * s) * SHOT_SPEED,
                                 (ux * s + uy * c) * SHOT_SPEED, 2.5f};
            o->shots_fired++;
            return;
        }
}

static int t_select_fire(PgChipTarget *t, PgChipRange r, int a, int b, int c, int one)
{
    (void)a; (void)b; (void)c; (void)one;
    Oke *o = O(t), *e = ENEMY(o);
    if (!in_range(o, r, e->x, e->y))
        return 2; /* nothing to shoot at */
    fire_at(o, e->x, e->y);
    o->fire_left = 0.4f;
    return 0;
}

static int t_firing_direction(PgChipTarget *t, int b2, int b3, int b4, int b5, int one)
{
    (void)b2; (void)b4; (void)b5; (void)one;
    Oke *o = O(t);
    float h = (o->heading + (float)b3 * 2.0f) * DEG;
    fire_at(o, o->x + sinf(h) * 100.0f, o->y - cosf(h) * 100.0f);
    o->fire_left = 0.4f;
    return 0;
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
static int t_judge_status(PgChipTarget *t, int b2, int b3, int b4)
{
    (void)b2;
    int hp = O(t)->hp;
    return b4 ? hp > b3 : hp <= b3;
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
    o->locked = in_range(o, r, e->x, e->y);
    return o->locked ? 0 : 2;
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
static int t_zero4(PgChipTarget *t, int a, int b, int c, int d)
{
    (void)t; (void)a; (void)b; (void)c; (void)d;
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
    .start_reticle = t_zero1,
    .move_reticle = t_zero3,
    .shoot_reticle = t_zero4,
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
    o->hp = 100;
    int32_t body;
    memcpy(&body, o->rec.hardware + 0x240, sizeof body); /* FUN_089ed210 */
    oke_motion_reset(&o->motion, oke_scripts_get(body));
    o->scripted = false;
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
        s->x += s->vx * dt;
        s->y += s->vy * dt;
        s->life -= dt;
        Oke *e = &b->oke[s->owner ^ 1];
        if (hypotf(s->x - e->x, s->y - e->y) < OKE_RADIUS) {
            if (e->defend_left <= 0.0f)
                e->hp -= SHOT_DAMAGE;
            b->oke[s->owner].hits++;
            s->live = false;
        } else if (s->life <= 0.0f || outside_field(s->x, s->y)) {
            s->live = false;
        }
    }
    for (int i = 0; i < 2; i++)
        if (b->oke[i].hp < 0)
            b->oke[i].hp = 0;
    if (a->hp <= 0 || c->hp <= 0 || b->time >= TIME_LIMIT)
        b->result = a->hp > c->hp ? 0 : a->hp < c->hp ? 1 : 2;
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
