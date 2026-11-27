/* Loads robot designs from the game's pack (through the sceIo file shim) and steps their
 * chip programs against a scripted test robot, checking the interpreter's invariants.
 *
 *   chexa-pgchip-test [--disc DIR] [--record PATH] [--ticks N] [--speed N] [--all] [-q]
 *
 * --disc    disc root or its PSP_GAME/USRDIR (default $CHEXA_DISC, else /mnt/c/psp/chexa)
 * --record  file inside the pack (default preset/arena/oke_b01_1.bin)
 * --all     step every 0x1EC0-byte record under preset/ instead
 * Exits 77 (CTest "skipped") when the game files aren't there. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "okedb.h"
#include "packfile.h"
#include "pgchip.h"
#include "psp_shim.h"

#define DT (1.0f / 60.0f)

/* ---- scripted test robot ---- */

typedef struct TestRobot {
    PgChipTarget base;
    uint32_t rng;
    int tick;
    int polls;        /* polls of the current action / halt */
    int calls;        /* target calls this tick */
    int verbose;
} TestRobot;

static int next_bit(TestRobot *r)
{
    r->rng = r->rng * 1103515245u + 12345u;
    return (r->rng >> 16) & 1;
}

static void note(TestRobot *r, const char *what, int argc, ...)
{
    r->calls++;
    if (!r->verbose)
        return;
    va_list ap;
    va_start(ap, argc);
    printf("      %s(", what);
    for (int i = 0; i < argc; i++)
        printf("%s%d", i ? ", " : "", va_arg(ap, int));
    printf(")\n");
    va_end(ap);
}

static int range_int(PgChipRange g) { return g.b[0] | g.b[1] << 8 | g.b[2] << 16 | g.b[3] << 24; }
#define R(t) ((TestRobot *)(t))

/* Judges answer from a fixed-seed generator: 1 = condition true. */
#define JUDGE(name, argc, ...) note(R(t), name, argc, __VA_ARGS__); return next_bit(R(t))
/* Actions are accepted (0), so a chip with a zero wait byte goes busy. */
#define ACTION(name, argc, ...) note(R(t), name, argc, __VA_ARGS__); R(t)->polls = 0; return 0
/* Plain orders succeed. */
#define ORDER(name, argc, ...) note(R(t), name, argc, __VA_ARGS__); return 0

static int t_timestamp(PgChipTarget *t) { return R(t)->tick; }
static int t_halt_start(PgChipTarget *t, int f) { ACTION("halt_start", 1, f); }
static int t_finished(PgChipTarget *t, const char *what, int arg)
{
    note(R(t), what, 1, arg);
    return ++R(t)->polls >= 2;  /* done on the second poll */
}
static int t_halt_finished(PgChipTarget *t) { return t_finished(t, "halt_finished", 0); }
static int t_action_finished(PgChipTarget *t, int c) { return t_finished(t, "action_finished", c); }
static int t_fire_finished(PgChipTarget *t) { return t_finished(t, "fire_finished", 0); }
static int t_halt_action(PgChipTarget *t, int c) { note(R(t), "halt_action", 1, c); R(t)->polls = 0; return 1; }
static int t_move(PgChipTarget *t, int d, int a) { ACTION("move", 2, d, a); }
static int t_rotate(PgChipTarget *t, int d, int a) { ACTION("rotate", 2, d, a); }
static int t_jump(PgChipTarget *t, int d) { ACTION("jump", 1, d); }
static int t_quick_move(PgChipTarget *t, int d) { ACTION("quick_move", 1, d); }
static int t_quick_rotate(PgChipTarget *t, int d) { ACTION("quick_rotate", 1, d); }
static int t_attack(PgChipTarget *t, int k) { ACTION("attack", 1, k); }
static int t_defense(PgChipTarget *t, int k, int b) { ACTION("defense", 2, k, b); }
static int t_special(PgChipTarget *t, int k) { ACTION("special_move", 1, k); }
static int t_altitude(PgChipTarget *t, int a, int b) { ORDER("set_altitude", 2, a, b); }
static int t_select_fire(PgChipTarget *t, PgChipRange g, int a, int b, int c, int o) { ACTION("select_fire", 5, range_int(g), a, b, c, o); }
static int t_fire_dir(PgChipTarget *t, int a, int b, int c, int d, int o) { ACTION("firing_direction", 5, a, b, c, d, o); }
static int t_option(PgChipTarget *t, int a) { ORDER("activate_option", 1, a); }
static int t_remaining(PgChipTarget *t, int a, int b, int c) { JUDGE("judge_remaining", 3, a, b, c); }
static int t_area(PgChipTarget *t, PgChipRange g) { JUDGE("judge_area", 1, range_int(g)); }
static int t_scan_oke(PgChipTarget *t, PgChipRange g, int a, int b, int c, int d) { JUDGE("scan_oke", 5, range_int(g), a, b, c, d); }
static int t_scan_obst(PgChipTarget *t, PgChipRange g, int a, int b) { JUDGE("scan_obstacles", 3, range_int(g), a, b); }
static int t_scan_proj(PgChipTarget *t, PgChipRange g, int a, int b, int c) { JUDGE("scan_projectiles", 4, range_int(g), a, b, c); }
static int t_status(PgChipTarget *t, int a, int b, int c) { JUDGE("judge_status", 3, a, b, c); }
static int t_action(PgChipTarget *t, int a)
{
    if (a == 8) { /* interrupt check from the action chips: never interrupted */
        note(R(t), "judge_action", 1, a);
        return 0;
    }
    JUDGE("judge_action", 1, a);
}
static int t_random(PgChipTarget *t, int a) { JUDGE("judge_random", 1, a); }
static int t_time(PgChipTarget *t, int a, int b, int c) { JUDGE("judge_time", 3, a, b, c); }
static int t_lock_on(PgChipTarget *t, PgChipRange g, int a, int b, int c) { ORDER("lock_on", 4, range_int(g), a, b, c); }
static int t_cancel_lock(PgChipTarget *t) { ORDER("cancel_lock_on", 0, 0); }
static int t_tgt_loc(PgChipTarget *t, PgChipRange g) { JUDGE("judge_target_location", 1, range_int(g)); }
static int t_loc_from(PgChipTarget *t, PgChipRange g, int a) { JUDGE("judge_location_from_target", 2, range_int(g), a); }
static int t_tgt_mov(PgChipTarget *t, int a) { JUDGE("judge_target_movement", 1, a); }
static int t_tgt_code(PgChipTarget *t, int a) { JUDGE("judge_target_code", 1, a); }
static int t_tgt_alert(PgChipTarget *t, int a) { JUDGE("judge_target_alert", 1, a); }
static int t_tgt_weapon(PgChipTarget *t, int a, int b) { JUDGE("judge_target_weapon", 2, a, b); }
static int t_auto_rot(PgChipTarget *t, PgChipRange g, int a) { ORDER("auto_rotate", 2, range_int(g), a); }
static int t_sight(PgChipTarget *t, int a) { ORDER("sight_location", 1, a); }
static int t_lof(PgChipTarget *t, int a) { JUDGE("judge_line_of_fire", 1, a); }
static int t_in_counter(PgChipTarget *t, int a, int b) { ORDER("input_counter", 2, a, b); }
static int t_ctl_counter(PgChipTarget *t, int a, int b, int c, int d, int e) { ORDER("control_counter", 5, a, b, c, d, e); }
static int t_counter_lock(PgChipTarget *t, int a) { ORDER("counter_lock_on", 1, a); }
static int t_counter_ret(PgChipTarget *t, int a, int b, int c) { ORDER("counter_reticle", 3, a, b, c); }
static int t_judge_counter(PgChipTarget *t, int a, int b, int c, int d, int e) { JUDGE("judge_counter", 5, a, b, c, d, e); }
static int t_send(PgChipTarget *t, int a, int b) { ORDER("send_counter", 2, a, b); }
static int t_recv(PgChipTarget *t, int a, int b) { ORDER("receive_counter", 2, a, b); }
static int t_camera(PgChipTarget *t, int a, int b) { ORDER("camera_view", 2, a, b); }
static int t_button_assign(PgChipTarget *t, int a, int b) { JUDGE("judge_button_assign", 2, a, b); }
static int t_button_input(PgChipTarget *t, int wait, int *pressed) { note(R(t), "button_input", 1, wait); *pressed = 1; return 0; }
static int t_analog(PgChipTarget *t, int a, int b, int c) { JUDGE("judge_analog", 3, a, b, c); }
static int t_start_ret(PgChipTarget *t, int a) { ORDER("start_reticle", 1, a); }
static int t_move_ret(PgChipTarget *t, int a, int b, int c) { ORDER("move_reticle", 3, a, b, c); }
static int t_shoot_ret(PgChipTarget *t, int a, int b, int c, int o) { ACTION("shoot_reticle", 4, a, b, c, o); }
static int t_alert(PgChipTarget *t, int a, int b, int c) { ORDER("alert", 3, a, b, c); }

static const PgChipTargetOps k_test_ops = {
    t_timestamp, t_halt_start, t_halt_finished, t_action_finished, t_fire_finished,
    t_halt_action, t_move, t_rotate, t_jump, t_quick_move, t_quick_rotate, t_attack,
    t_defense, t_special, t_altitude, t_select_fire, t_fire_dir, t_option, t_remaining,
    t_area, t_scan_oke, t_scan_obst, t_scan_proj, t_status, t_action, t_random, t_time,
    t_lock_on, t_cancel_lock, t_tgt_loc, t_loc_from, t_tgt_mov, t_tgt_code, t_tgt_alert,
    t_tgt_weapon, t_auto_rot, t_sight, t_lof, t_in_counter, t_ctl_counter, t_counter_lock,
    t_counter_ret, t_judge_counter, t_send, t_recv, t_camera, t_button_assign,
    t_button_input, t_analog, t_start_ret, t_move_ret, t_shoot_ret, t_alert,
};
_Static_assert(sizeof k_test_ops / sizeof(void *) == 53, "every target slot has a test op");

/* ---- checks ---- */

static int check_vm(const PgChipVm *vm, const char *name, int tick)
{
    const PgChipProgram *p = &vm->prog[vm->current];
    const char *bad = NULL;
    if (vm->current < 0 || vm->current >= PGCHIP_PROGRAM_MAX)
        bad = "current program out of range";
    else if (vm->depth < 0 || vm->depth > PGCHIP_STACK_MAX)
        bad = "call depth out of range";
    else if (p->width <= 0)
        bad = "running a program with no grid";
    else if (p->current < 0 || p->current >= p->width * p->width)
        bad = "program counter outside the grid";
    else if (pgchip_type(&p->chips[p->current]) == PGCHIP_NONE)
        bad = "program counter on an empty cell";
    else if (p->busy != 0 && p->busy != 1)
        bad = "busy flag not 0/1";
    if (bad)
        printf("FAIL %s tick %d: %s (program %d pc %d depth %d)\n", name, tick, bad,
               vm->current, p->current, vm->depth);
    return bad ? 1 : 0;
}

static int run_record(const OkeDataBase *oke, const char *name, int ticks, int speed, int verbose)
{
    static PgChipVm vm;
    static PgChipHistory hist;
    TestRobot robot = {.base = {&k_test_ops}, .rng = 12345, .verbose = verbose};
    memset(&hist, 0, sizeof hist);
    pgchip_setup(&vm, oke, speed);
    if (verbose)
        printf("%s: CPU part %d, programs %dx%d / %dx%d / %dx%d, start %d\n", name,
               oke_cpu_part(oke), vm.prog[0].width, vm.prog[0].width, vm.prog[1].width,
               vm.prog[1].width, vm.prog[2].width, vm.prog[2].width, vm.prog[0].start);
    int failures = check_vm(&vm, name, 0);
    for (int tick = 1; tick <= ticks && !failures; tick++) {
        robot.tick = tick;
        robot.calls = 0;
        int before = hist.count;
        if (verbose)
            printf("  tick %d\n", tick);
        pgchip_update(&vm, DT, &robot.base, &hist);
        failures += check_vm(&vm, name, tick);
        if (verbose) {
            const PgChipProgram *p = &vm.prog[vm.current];
            const PgChipCommand *c = &p->chips[p->current];
            printf("    -> program %d at (%d,%d) %-26s busy=%d depth=%d, %d chips run, %d target calls\n",
                   vm.current, p->current % p->width, p->current / p->width,
                   pgchip_type_name(pgchip_type(c)), p->busy, vm.depth, hist.count - before,
                   robot.calls);
        }
    }
    return failures;
}

/* ---- loading ---- */

static int load_record(const Pack *pack, const char *path, OkeDataBase *out)
{
    const PackEntry *e = pack_find(pack, path);
    if (!e) {
        printf("FAIL %s: not in the pack\n", path);
        return -1;
    }
    if (e->size != OKE_RECORD_SIZE) {
        printf("FAIL %s: %u bytes, not a robot record\n", path, (unsigned)e->size);
        return -1;
    }
    long n = pack_read(pack, e, out, sizeof *out);
    if (n != OKE_RECORD_SIZE) {
        printf("FAIL %s: read returned %ld\n", path, n);
        return -1;
    }
    return 0;
}

typedef struct AllCtx {
    const Pack *pack;
    int ticks, speed, records, failures;
} AllCtx;

static int each_record(void *user, const char *path, const PackEntry *e)
{
    AllCtx *ctx = user;
    static OkeDataBase oke;
    if (strncmp(path, "preset/", 7) != 0 || e->size != OKE_RECORD_SIZE)
        return 0;
    if (load_record(ctx->pack, path, &oke) != 0) {
        ctx->failures++;
        return 0;
    }
    ctx->records++;
    ctx->failures += run_record(&oke, path, ctx->ticks, ctx->speed, 0);
    return 0;
}

int main(int argc, char **argv)
{
    const char *disc = getenv("CHEXA_DISC");
    const char *record = "preset/arena/oke_b01_1.bin";
    int ticks = 10, speed = 120, all = 0, quiet = 0;
    if (!disc)
        disc = "/mnt/c/psp/chexa";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) record = argv[++i];
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i + 1 < argc) speed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--all")) all = 1;
        else if (!strcmp(argv[i], "-q")) quiet = 1;
        else {
            fprintf(stderr, "usage: %s [--disc DIR] [--record PATH] [--ticks N] [--speed N] [--all] [-q]\n",
                    argv[0]);
            return 2;
        }
    }

    if (!psp_io_mount_disc(disc)) {
        printf("SKIP: game files not found at %s (set --disc or CHEXA_DISC)\n", disc);
        return 77;
    }
    Pack pack;
    int r = pack_open(&pack, "disc0:/PSP_GAME/USRDIR");
    if (r < 0) {
        printf("FAIL: can't open the pack index (0x%08X)\n", (unsigned)r);
        return 1;
    }
    printf("pack: %u entries\n", (unsigned)pack.entry_count);

    int failures;
    if (all) {
        AllCtx ctx = {&pack, ticks, speed, 0, 0};
        pack_foreach(&pack, each_record, &ctx);
        printf("%d records stepped %d ticks each, %d failures\n", ctx.records, ticks, ctx.failures);
        failures = ctx.failures + (ctx.records == 0);
    } else {
        static OkeDataBase oke;
        failures = load_record(&pack, record, &oke) != 0;
        if (!failures)
            failures = run_record(&oke, record, ticks, speed, !quiet);
    }
    pack_close(&pack);
    printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
