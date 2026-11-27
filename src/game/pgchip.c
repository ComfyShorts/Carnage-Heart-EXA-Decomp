/* Battle-time chip interpreter (the game's CBattleOkePgChip, battleokepgchip.cpp).
 * Written from the behaviour described in docs/program-format.md. */
#include "pgchip.h"

#include <stddef.h>

#include "okedb.h"

/* PGCHIP::eCONNECT_DIR_TYPE: the 8 neighbours clockwise from "up". */
static const int8_t k_dir_step[8][2] = {
    {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
};

/* Category argument of the action_finished poll, per action chip. */
enum { CAT_ANY = 0, CAT_MOVE = 1, CAT_ROTATE = 2, CAT_FIRE_DIR = 3, CAT_JUMP = 4 };
/* judge_action() value the movement chips use to ask "was the action interrupted?". */
#define ACTION_INTERRUPTED 8

/* ---- chip byte accessors, matching how the game loads each field ---- */

static int u8_at(const PgChipCommand *c, int i) { return c->b[i]; }
static int s8_at(const PgChipCommand *c, int i) { return (int8_t)c->b[i]; }
static int u16_at(const PgChipCommand *c, int i) { return c->b[i] | c->b[i + 1] << 8; }
static int s16_at(const PgChipCommand *c, int i) { return (int16_t)(c->b[i] | c->b[i + 1] << 8); }
static int lo4(const PgChipCommand *c, int i) { return c->b[i] & 0x0F; }
static int hi4(const PgChipCommand *c, int i) { return c->b[i] >> 4; }

static PgChipRange range_at(const PgChipCommand *c)
{
    PgChipRange r = {{c->b[2], c->b[3], c->b[4], c->b[5]}};
    return r;
}

/* ---- multi-tick helpers ----
 * Action chips issue their order on the first visit. If the robot accepted it (result 0)
 * and the chip's wait byte is zero, the chip turns busy and the program stays on it
 * until a poll reports the action finished or interrupted. */

static void poll_action(PgChipProgram *p, PgChipTarget *t, int category)
{
    if (t->ops->judge_action(t, ACTION_INTERRUPTED) == 1)
        p->busy = 0;
    if (t->ops->action_finished(t, category) == 1)
        p->busy = 0;
}

static void poll_fire(PgChipProgram *p, PgChipTarget *t)
{
    if (t->ops->judge_action(t, ACTION_INTERRUPTED) == 1)
        p->busy = 0;
    if (t->ops->fire_finished(t) == 1)
        p->busy = 0;
}

static void wait_if_accepted(PgChipProgram *p, int result, int wait_byte)
{
    if (result == 0 && wait_byte == 0)
        p->busy = 1;
}

/* Executes one chip and returns its exit direction (0-7, or PGCHIP_DIR_RETURN). */
int pgchip_execute(PgChipProgram *p, PgChipTarget *t, PgChipHistory *h,
                   const PgChipCommand *c, int can_call)
{
    const PgChipTargetOps *op = t->ops;
    const int was_busy = p->busy;
    const int type = pgchip_type(c);
    int dir = pgchip_exit(c);
    int result = 2;  /* inactive chips report "not executed" */

    /* Type 0 is asserted never to run (COMMAND_NONE); it falls through as a no-op. */
    if (type == PGCHIP_RETURN) {
        /* Checked before the active bit, so Return chips ignore it. */
        result = 0;
        dir = PGCHIP_DIR_RETURN;
    } else if (pgchip_active(c)) {
        int judge = 0;  /* set for chips that branch on result == 1 */
        result = 0;
        switch (type) {
        case 0x01: /* NOP */
            break;
        case 0x02: /* Halt: wait b2 frames */
            if (!p->busy) {
                result = op->halt_start(t, u8_at(c, 2));
                p->busy = 1;
            }
            if (p->busy && op->halt_finished(t) == 1)
                p->busy = 0;
            break;
        case 0x03: /* Wait for the action category in b2 to finish */
            p->busy = 1;
            if (op->action_finished(t, u8_at(c, 2)) == 1)
                p->busy = 0;
            break;
        case PGCHIP_CALL:
            if (!can_call)
                result = 2;  /* stack full */
            else
                p->exit_request = u8_at(c, 2) == 0 ? 1 : 2;
            break;
        case 0x05:
            result = op->input_counter(t, u8_at(c, 2), u8_at(c, 3));
            break;
        case 0x06:
            result = op->control_counter(t, u16_at(c, 2), s16_at(c, 4), lo4(c, 6), hi4(c, 6),
                                         u8_at(c, 7));
            break;
        case 0x07:
            result = op->send_counter(t, u8_at(c, 2), u8_at(c, 3));
            break;
        case 0x08:
            result = op->receive_counter(t, u8_at(c, 2), u8_at(c, 3));
            break;
        case 0x09: { /* Button Input: b2 == 0 waits for a press */
            int wait = u8_at(c, 2) == 0;
            int pressed = 0;
            result = op->button_input(t, wait, &pressed);
            if (wait)
                p->busy = pressed == 0;
            break;
        }
        case 0x0A:
            result = op->judge_remaining(t, u8_at(c, 4), u16_at(c, 2), u8_at(c, 5));
            judge = 1;
            break;
        case 0x0B:
            result = op->judge_area(t, range_at(c));
            judge = 1;
            break;
        case 0x0C:
            result = op->scan_oke(t, range_at(c), lo4(c, 6), hi4(c, 6), lo4(c, 7), hi4(c, 7));
            judge = 1;
            break;
        case 0x0D:
            result = op->scan_obstacles(t, range_at(c), u8_at(c, 6), u8_at(c, 7));
            judge = 1;
            break;
        case 0x0E:
            result = op->scan_projectiles(t, range_at(c), u8_at(c, 6), lo4(c, 7), hi4(c, 7));
            judge = 1;
            break;
        case 0x0F:
            result = op->judge_status(t, u8_at(c, 2), u8_at(c, 3), u8_at(c, 4));
            judge = 1;
            break;
        case 0x10:
            result = op->judge_action(t, u8_at(c, 2));
            judge = 1;
            break;
        case 0x11:
            result = op->judge_random(t, u8_at(c, 2));
            judge = 1;
            break;
        case 0x12:
            result = op->judge_time(t, u16_at(c, 2), u8_at(c, 4), u8_at(c, 5));
            judge = 1;
            break;
        case 0x13:
            result = op->judge_target_location(t, range_at(c));
            judge = 1;
            break;
        case 0x14:
            result = op->judge_location_from_target(t, range_at(c), u8_at(c, 6));
            judge = 1;
            break;
        case 0x15:
            result = op->judge_target_movement(t, u8_at(c, 2));
            judge = 1;
            break;
        case 0x16:
            result = op->judge_target_code(t, u8_at(c, 2));
            judge = 1;
            break;
        case 0x17:
            result = op->judge_target_alert(t, u8_at(c, 2));
            judge = 1;
            break;
        case 0x18:
            result = op->judge_target_weapon(t, u8_at(c, 2), u8_at(c, 3));
            judge = 1;
            break;
        case 0x19:
            result = op->judge_line_of_fire(t, u8_at(c, 2));
            judge = 1;
            break;
        case 0x1A:
            result = op->judge_counter(t, u16_at(c, 2), s16_at(c, 4), lo4(c, 6), hi4(c, 6),
                                       u8_at(c, 7));
            judge = 1;
            break;
        case 0x1B:
            result = op->judge_button_assign(t, u16_at(c, 2), u8_at(c, 4));
            judge = 1;
            break;
        case 0x1C:
            result = op->judge_analog(t, u8_at(c, 2), s8_at(c, 3), u8_at(c, 4));
            judge = 1;
            break;
        case 0x1D: /* Halt Action: busy while the cancel is in progress */
            if (!p->busy && (result = op->halt_action(t, u8_at(c, 2))) != 0)
                p->busy = 1;
            if (p->busy && op->action_finished(t, CAT_ANY) == 1)
                p->busy = 0;
            break;

        /* Movement and actions: issue, then poll on the same visit. */
        case 0x1E:
            if (!p->busy)
                wait_if_accepted(p, result = op->move(t, u8_at(c, 2), s16_at(c, 4)), u8_at(c, 3));
            if (p->busy)
                poll_action(p, t, CAT_MOVE);
            break;
        case 0x1F:
            if (!p->busy)
                wait_if_accepted(p, result = op->rotate(t, u8_at(c, 2), s16_at(c, 4)), u8_at(c, 3));
            if (p->busy)
                poll_action(p, t, CAT_ROTATE);
            break;
        case 0x20:
            if (!p->busy)
                wait_if_accepted(p, result = op->jump(t, u8_at(c, 2)), u8_at(c, 3));
            if (p->busy)
                poll_action(p, t, CAT_JUMP);
            break;
        case 0x21:
            if (!p->busy)
                wait_if_accepted(p, result = op->quick_move(t, u8_at(c, 2)), u8_at(c, 3));
            if (p->busy)
                poll_action(p, t, CAT_ANY);
            break;
        case 0x22:
            if (!p->busy)
                wait_if_accepted(p, result = op->quick_rotate(t, u8_at(c, 2)), u8_at(c, 3));
            if (p->busy)
                poll_action(p, t, CAT_ANY);
            break;
        case 0x23:
            if (!p->busy)
                wait_if_accepted(p, result = op->attack(t, u8_at(c, 2)), u8_at(c, 3));
            if (p->busy)
                poll_action(p, t, CAT_ANY);
            break;
        case 0x24: /* Defense: the wait byte is b4 */
            if (!p->busy)
                wait_if_accepted(p, result = op->defense(t, u8_at(c, 2), u8_at(c, 3)), u8_at(c, 4));
            if (p->busy)
                poll_action(p, t, CAT_ANY);
            break;
        case 0x25:
            if (!p->busy)
                wait_if_accepted(p, result = op->special_move(t, u8_at(c, 2)), u8_at(c, 3));
            if (p->busy)
                poll_action(p, t, CAT_ANY);
            break;

        /* Firing: issue, then poll from the next visit on. */
        case 0x26: /* Select and Fire: wait flag is b7's high nibble */
            if (!p->busy)
                wait_if_accepted(p, result = op->select_fire(t, range_at(c), c->b[6] & 7,
                                                             c->b[6] >> 3, lo4(c, 7), 1),
                                 hi4(c, 7));
            else
                poll_fire(p, t);
            break;
        case 0x27: /* Firing Direction: wait flag is b6 */
            if (!p->busy)
                wait_if_accepted(p, result = op->firing_direction(t, u8_at(c, 2), s8_at(c, 3),
                                                                  u8_at(c, 4), u8_at(c, 5), 1),
                                 u8_at(c, 6));
            else
                poll_action(p, t, CAT_FIRE_DIR);
            break;
        case 0x2B: /* Shoot Reticle: wait flag is b5 */
            if (!p->busy)
                wait_if_accepted(p, result = op->shoot_reticle(t, u8_at(c, 2), u8_at(c, 3),
                                                               u8_at(c, 4), 1),
                                 u8_at(c, 5));
            else
                poll_fire(p, t);
            break;

        /* One-shot orders. */
        case 0x28:
            result = op->counter_reticle(t, u8_at(c, 4), u8_at(c, 2), u8_at(c, 3));
            break;
        case 0x29:
            result = op->start_reticle(t, u8_at(c, 2));
            break;
        case 0x2A:
            result = op->move_reticle(t, u8_at(c, 2), s16_at(c, 4), s16_at(c, 6));
            break;
        case 0x2C:
            result = op->set_altitude(t, u8_at(c, 3), u8_at(c, 2));
            break;
        case 0x2D:
            result = op->activate_option(t, u8_at(c, 2));
            break;
        case 0x2E:
            result = op->lock_on(t, range_at(c), hi4(c, 6), u8_at(c, 7), lo4(c, 6));
            break;
        case 0x2F:
            result = op->auto_rotate(t, range_at(c), u8_at(c, 6));
            break;
        case 0x30:
            result = op->sight_location(t, u8_at(c, 2));
            break;
        case 0x31:
            result = op->counter_lock_on(t, u8_at(c, 2));
            break;
        case 0x32:
            result = op->cancel_lock_on(t);
            break;
        case 0x33:
            result = op->camera_view(t, u8_at(c, 2), u8_at(c, 3));
            break;
        case 0x34:
            result = op->alert(t, u8_at(c, 2), u8_at(c, 3), u8_at(c, 4));
            break;
        default:
            break;
        }
        if (judge && result == 1)
            dir = pgchip_branch(c);
    }

    /* Record the result unless the chip was already waiting when it was entered. */
    if (!was_busy && h) {
        if (h->count < PGCHIP_HISTORY_MAX) {
            PgChipHistoryEntry *e = &h->entries[h->count++];
            e->timestamp = op->timestamp(t);
            e->program = p->id;
            e->position = p->current;
            e->result = result;
        } else {
            (void)op->timestamp(t);
        }
        p->last_result = result;
    }
    p->last_pos = p->current;
    return dir;
}

int pgchip_advance(PgChipProgram *p, unsigned dir)
{
    const int w = p->width;
    if (dir < 8) {
        int x = p->current % w + k_dir_step[dir][0];
        int y = p->current / w + k_dir_step[dir][1];
        if (x >= 0 && x < w && y >= 0 && y < w) {
            p->current = y * w + x;
            return 0;
        }
    }
    /* Off the grid (or an invalid direction, which the game never stores on a live chip):
     * the main program restarts, a sub-program returns to its caller. */
    if (p->id == 0) {
        p->current = p->start;
        return 0;
    }
    p->exit_request = 3;
    return 1;
}

/* Runs up to *steps chips. Returns the steps used, or -1 when the program can't continue
 * this tick (it has no grid, or a chip is busy). A busy chip that finishes gives its step
 * back by raising *steps. */
int pgchip_program_update(PgChipProgram *p, PgChipTarget *t, PgChipHistory *h, int *steps,
                          int can_call)
{
    if (p->width == 0)
        return -1;
    int used = 0;
    while (used != *steps) {
        const int was_busy = p->busy;
        const int dir = pgchip_execute(p, t, h, &p->chips[p->current], can_call);
        if (was_busy && !p->busy)
            (*steps)++;
        if (p->exit_request == 1 || p->exit_request == 2)
            return used + 1;
        if (p->busy)
            return -1;
        if (dir == PGCHIP_DIR_RETURN) {
            if (p->id != 0) {
                p->exit_request = 3;
                return used + 1;
            }
            p->current = p->start;
        } else if (pgchip_advance(p, (unsigned)dir) != 0) {
            return used + 1;
        }
        used++;
    }
    return used;
}

static void reset_program(PgChipProgram *p)
{
    p->current = p->start;
    p->last_pos = p->start;
    p->last_result = 0;
    p->busy = 0;
    p->exit_request = 0;
}

void pgchip_setup(PgChipVm *vm, const OkeDataBase *oke, int32_t speed)
{
    vm->speed = speed;
    vm->accum = 0.0f;
    vm->current = 0;
    vm->depth = 0;
    for (int i = 0; i < PGCHIP_PROGRAM_MAX; i++) {
        PgChipProgram *p = &vm->prog[i];
        p->chips = oke_program_chips(oke, i);
        p->width = oke->software.width[i];
        p->start = oke->software.start[i];
        p->id = i;
        reset_program(p);
    }
}

void pgchip_update(PgChipVm *vm, float dt, PgChipTarget *target, PgChipHistory *history)
{
    const float total = vm->accum + (float)vm->speed * dt;
    vm->accum = total;
    if (total < 1.0f)
        return;
    int steps = (int)total;
    vm->accum = total - (float)steps;

    while (steps > 0) {
        PgChipProgram *p = &vm->prog[vm->current];
        const int used = pgchip_program_update(p, target, history, &steps,
                                               vm->depth < PGCHIP_STACK_MAX);
        if (used == -1)
            return;
        steps -= used;

        /* Returns: pop callers until one can move on from its Call chip. */
        while (vm->prog[vm->current].exit_request == 3 && vm->depth > 0) {
            vm->prog[vm->current].exit_request = 0;
            const PgChipProgram *saved = &vm->stack[--vm->depth];
            vm->prog[saved->id] = *saved;
            vm->current = saved->id;
            PgChipProgram *caller = &vm->prog[vm->current];
            if (pgchip_advance(caller, (unsigned)pgchip_exit(&caller->chips[caller->current])) == 0)
                break;
        }

        /* Calls: save the caller and start the sub-program from its start chip. */
        p = &vm->prog[vm->current];
        if (p->exit_request == 1 || p->exit_request == 2) {
            const int sub = p->exit_request;
            p->exit_request = 0;
            vm->stack[vm->depth++] = *p;
            vm->current = sub;
            reset_program(&vm->prog[sub]);
        }
    }
}

/* PGCHIP::GetCommandName (English names as shown in the game). */
const char *pgchip_type_name(int type)
{
    static const char *const names[PGCHIP_TYPE_END] = {
        "(none)", "NOP", "Halt", "Wait", "Call Sub-Program", "Input Counter",
        "Control Counter", "Send Counter", "Receive Counter", "Button Input",
        "Determine Remaining", "Assess Battle Area", "Scan for OKE", "Scan for Obstacles",
        "Scan for Projectiles", "Status Assessment", "Action Assessment", "Random Branching",
        "Assess Time", "Assess Target Loc.", "Assess Location From Target",
        "Assess Target Mov.", "Assess Target Code", "Assess Target Lock-On Alert",
        "Assess Target Weapon", "Assess Line of Fire", "Assess Counter", "Button Assignment",
        "Assess Analog", "Halt Action", "Move", "Rotate", "Jump", "Quick Movement",
        "Quick Rotation", "Attack", "Defense", "Special Move", "Select and Fire",
        "Firing Direction", "Counter-Type Reticle", "Start Reticle", "Move Reticle",
        "Shoot Reticle", "Set Flying Altitude", "Activate Option", "Lock-On",
        "Auto-Rotate Targ.", "Change Sight Location", "Counter and Lock-On", "Cancel Lock-On",
        "Change Camera View", "Alert", "Return",
    };
    return type >= 0 && type < PGCHIP_TYPE_END ? names[type] : "?";
}
