/* Chip programs and the battle-time interpreter (CBattleOkePgChip).
 * Behaviour and layouts are described in docs/program-format.md. */
#ifndef PGCHIP_H
#define PGCHIP_H

#include <stdint.h>

#define PGCHIP_PROGRAM_MAX 3     /* main, sub 1, sub 2 */
#define PGCHIP_MAIN_CAP 15       /* buffer capacity (width) of the main program */
#define PGCHIP_SUB_CAP 7
#define PGCHIP_STACK_MAX 10      /* ms_iStackMax */
#define PGCHIP_HISTORY_MAX 256
#define PGCHIP_DIR_RETURN 8      /* pseudo-direction yielded by Return chips */

enum {
    PGCHIP_NONE = 0x00,
    PGCHIP_CALL = 0x04,
    PGCHIP_RETURN = 0x35,
    PGCHIP_TYPE_END = 0x36,
};

/* PGCHIP::tagCOMMAND: byte 0 = type (bits 0-5), unknown (6), active (7); byte 1 =
 * default exit (low nibble), branch exit (high nibble); bytes 2-7 = parameters. */
typedef struct PgChipCommand {
    uint8_t b[8];
} PgChipCommand;

static inline int pgchip_type(const PgChipCommand *c) { return c->b[0] & 0x3F; }
static inline int pgchip_active(const PgChipCommand *c) { return (c->b[0] & 0x80) != 0; }
static inline int pgchip_exit(const PgChipCommand *c) { return c->b[1] & 0x0F; }
static inline int pgchip_branch(const PgChipCommand *c) { return c->b[1] >> 4; }

/* PGCHIP::tagRANGE (bytes 2-5 of scan/aim chips), passed to the target by value. */
typedef struct PgChipRange {
    uint8_t b[4];
} PgChipRange;

/* ---- the robot interface (CPgChipTarget) ----
 * One function per virtual slot the interpreter calls; the comment gives the slot's
 * offset in CBattleOke's CPgChipTarget vtable. Return values are CPgChipTarget::RET:
 * judges return 1 for "condition true" (take the branch exit). Actions return 0 when
 * the order was accepted and still runs; the *_finished polls return 1 when done.
 * Arguments are the chip bytes, in the order and signedness the game passes them. */
typedef struct PgChipTarget PgChipTarget;
typedef struct PgChipTargetOps {
    int (*timestamp)(PgChipTarget *t);                                        /* +0x10 */
    int (*halt_start)(PgChipTarget *t, int frames);                           /* +0x18 */
    int (*halt_finished)(PgChipTarget *t);                                    /* +0x20 */
    int (*action_finished)(PgChipTarget *t, int category);                    /* +0x28 */
    int (*fire_finished)(PgChipTarget *t);                                    /* +0x30 */
    int (*halt_action)(PgChipTarget *t, int category);                        /* +0x40 */
    int (*move)(PgChipTarget *t, int dir, int amount);                        /* +0x50 */
    int (*rotate)(PgChipTarget *t, int dir, int amount);                      /* +0x60 */
    int (*jump)(PgChipTarget *t, int dir);                                    /* +0x68 */
    int (*quick_move)(PgChipTarget *t, int dir);                              /* +0x70 */
    int (*quick_rotate)(PgChipTarget *t, int dir);                            /* +0x78 */
    int (*attack)(PgChipTarget *t, int kind);                                 /* +0x80 */
    int (*defense)(PgChipTarget *t, int kind, int b3);                        /* +0x88 */
    int (*special_move)(PgChipTarget *t, int kind);                           /* +0x90 */
    int (*set_altitude)(PgChipTarget *t, int b3, int b2);                     /* +0x98 */
    int (*select_fire)(PgChipTarget *t, PgChipRange r, int b6_lo3, int b6_hi5,
                       int b7_lo, int one);                                   /* +0xA0 */
    int (*firing_direction)(PgChipTarget *t, int b2, int b3_signed, int b4, int b5,
                            int one);                                         /* +0xA8 */
    int (*activate_option)(PgChipTarget *t, int b2);                          /* +0xB0 */
    int (*judge_remaining)(PgChipTarget *t, int b4, int b2_u16, int b5);      /* +0xB8 */
    int (*judge_area)(PgChipTarget *t, PgChipRange r);                        /* +0xC0 */
    int (*scan_oke)(PgChipTarget *t, PgChipRange r, int b6_lo, int b6_hi, int b7_lo,
                    int b7_hi);                                               /* +0xC8 */
    int (*scan_obstacles)(PgChipTarget *t, PgChipRange r, int b6, int b7);    /* +0xD0 */
    int (*scan_projectiles)(PgChipTarget *t, PgChipRange r, int b6, int b7_lo,
                            int b7_hi);                                       /* +0xD8 */
    int (*judge_status)(PgChipTarget *t, int b2, int b3, int b4);             /* +0xE0 */
    int (*judge_action)(PgChipTarget *t, int b2);                             /* +0xE8 */
    int (*judge_random)(PgChipTarget *t, int b2);                             /* +0xF0 */
    int (*judge_time)(PgChipTarget *t, int b2_u16, int b4, int b5);           /* +0xF8 */
    int (*lock_on)(PgChipTarget *t, PgChipRange r, int b6_hi, int b7, int b6_lo); /* +0x100 */
    int (*cancel_lock_on)(PgChipTarget *t);                                   /* +0x108 */
    int (*judge_target_location)(PgChipTarget *t, PgChipRange r);             /* +0x110 */
    int (*judge_location_from_target)(PgChipTarget *t, PgChipRange r, int b6); /* +0x118 */
    int (*judge_target_movement)(PgChipTarget *t, int b2);                    /* +0x120 */
    int (*judge_target_code)(PgChipTarget *t, int b2);                        /* +0x128 */
    int (*judge_target_alert)(PgChipTarget *t, int b2);                       /* +0x130 */
    int (*judge_target_weapon)(PgChipTarget *t, int b2, int b3);              /* +0x138 */
    int (*auto_rotate)(PgChipTarget *t, PgChipRange r, int b6);               /* +0x140 */
    int (*sight_location)(PgChipTarget *t, int b2);                           /* +0x148 */
    int (*judge_line_of_fire)(PgChipTarget *t, int b2);                       /* +0x158 */
    int (*input_counter)(PgChipTarget *t, int counter, int b3);               /* +0x160 */
    int (*control_counter)(PgChipTarget *t, int b2_u16, int b4_s16, int b6_lo, int b6_hi,
                           int b7);                                           /* +0x168 */
    int (*counter_lock_on)(PgChipTarget *t, int b2);                          /* +0x170 */
    int (*counter_reticle)(PgChipTarget *t, int b4, int b2, int b3);          /* +0x180 */
    int (*judge_counter)(PgChipTarget *t, int b2_u16, int b4_s16, int b6_lo, int b6_hi,
                         int b7);                                             /* +0x188 */
    int (*send_counter)(PgChipTarget *t, int counter, int b3);                /* +0x190 */
    int (*receive_counter)(PgChipTarget *t, int counter, int b3);             /* +0x198 */
    int (*camera_view)(PgChipTarget *t, int b2, int b3);                      /* +0x1A0 */
    int (*judge_button_assign)(PgChipTarget *t, int b2_u16, int b4);          /* +0x1A8 */
    int (*button_input)(PgChipTarget *t, int wait, int *pressed);             /* +0x1B0 */
    int (*judge_analog)(PgChipTarget *t, int b2, int b3_signed, int b4);      /* +0x1B8 */
    int (*start_reticle)(PgChipTarget *t, int b2);                            /* +0x1C0 */
    int (*move_reticle)(PgChipTarget *t, int b2, int b4_s16, int b6_s16);     /* +0x1C8 */
    int (*shoot_reticle)(PgChipTarget *t, int b2, int b3, int b4, int one);   /* +0x1D0 */
    int (*alert)(PgChipTarget *t, int b2, int b3, int b4);                    /* +0x1D8 */
} PgChipTargetOps;

struct PgChipTarget {
    const PgChipTargetOps *ops;
};

/* ---- history (COkePgChipHistory): the last 256 chip results ---- */
typedef struct PgChipHistoryEntry {
    int32_t timestamp, program, position, result;
} PgChipHistoryEntry;

typedef struct PgChipHistory {
    PgChipHistoryEntry entries[PGCHIP_HISTORY_MAX];
    int32_t count;  /* entries past the 256th are dropped */
} PgChipHistory;

/* ---- interpreter state ---- */

/* CBattleOkePgChip::CProgram */
typedef struct PgChipProgram {
    const PgChipCommand *chips;  /* 0x00: into the OKE's software block */
    int32_t width;               /* 0x04: 0 = program unavailable */
    int32_t start;               /* 0x08 */
    int32_t current;             /* 0x0C: program counter */
    int32_t last_pos;            /* 0x10 */
    int32_t last_result;         /* 0x14: only updated when a history is attached */
    int32_t busy;                /* 0x18: a multi-tick chip is waiting */
    int32_t id;                  /* 0x1C: 0 main, 1/2 sub */
    int32_t exit_request;        /* 0x20: 0 none, 1/2 call sub 1/2, 3 return */
} PgChipProgram;

/* CBattleOkePgChip */
typedef struct PgChipVm {
    int32_t speed;               /* 0x00: chips per unit of dt */
    float accum;                 /* 0x04: fractional steps carried between ticks */
    int32_t current;             /* 0x08: running program */
    PgChipProgram prog[PGCHIP_PROGRAM_MAX];      /* 0x0C */
    int32_t depth;               /* 0x78: call-stack depth */
    PgChipProgram stack[PGCHIP_STACK_MAX];       /* 0x7C */
} PgChipVm;

#ifdef PCPORT_M32
_Static_assert(sizeof(PgChipProgram) == 0x24, "CProgram layout");
_Static_assert(sizeof(PgChipVm) == 0x1E4, "CBattleOkePgChip layout");
_Static_assert(__builtin_offsetof(PgChipVm, depth) == 0x78, "CBattleOkePgChip layout");
#endif

struct OkeDataBase;

/* Points the three programs at the OKE's software block. The game takes `speed` from a
 * per-CPU-part table in the EBOOT; that table isn't copied here, so the caller passes it. */
void pgchip_setup(PgChipVm *vm, const struct OkeDataBase *oke, int32_t speed);

/* One battle tick: runs floor(accumulated speed * dt) chip steps. `history` may be NULL. */
void pgchip_update(PgChipVm *vm, float dt, PgChipTarget *target, PgChipHistory *history);

/* Lower-level pieces, exposed for tests. */
int pgchip_program_update(PgChipProgram *p, PgChipTarget *t, PgChipHistory *h, int *steps,
                          int can_call);
int pgchip_execute(PgChipProgram *p, PgChipTarget *t, PgChipHistory *h,
                   const PgChipCommand *chip, int can_call);
int pgchip_advance(PgChipProgram *p, unsigned dir);

const char *pgchip_type_name(int type);

#endif
