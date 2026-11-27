/* The battle simulation behind battle.c: two teams of up to three OKEs running their
 * chip programs.
 *
 * The chip interpreter (pgchip.c) and the numbers it depends on (the per-CPU chip
 * speed, the PGCHIP::CRange distance scale) are the game's, and so are walking and
 * turning: Move and Rotate run the body's motion scripts through the game's physics
 * (oke_script.c). Firing, sensing, damage and the other actions are still stand-ins
 * for CBattleOke; see docs/battle.md. */
#ifndef GAME_BATTLE_SIM_H
#define GAME_BATTLE_SIM_H

#include <stdbool.h>
#include <stdint.h>

#include "oke_script.h"
#include "okedb.h"
#include "pgchip.h"

#define FIELD 720.0f        /* square arena = the terrain's 720 x 720 world units */
#define OKE_RADIUS 12.0f
#define MOVE_SPEED 50.0f    /* units per second */
#define QUICK_SPEED 160.0f
#define TURN_SPEED 180.0f   /* degrees per second */
#define BURST_GAP 0.1f      /* stand-in: seconds between the shots of a burst */
#define MELEE_RANGE 36.0f
#define MELEE_DAMAGE 10     /* stand-in: % of max HP */
#define DOWN_TIME 2.5f      /* stand-in: seconds a knocked-down OKE stays down */
#define COOLING 0.08f       /* stand-in: share of max heat shed per second */
#define TIME_LIMIT 120.0f   /* seconds */
#define MAX_SHOTS 160
#define TEAM_SIZE 3         /* CTeamDataBase: three CFormationDataBase entries */
#define MAX_OKES (2 * TEAM_SIZE)
#define FORMATION_SPOTS 9   /* the formation grid (0x08bd7d10) */

typedef enum { ACT_NONE, ACT_MOVE, ACT_TURN, ACT_ATTACK, ACT_DEFEND, ACT_JUMP } ActKind;

typedef struct Oke {
    PgChipTarget base; /* must come first: the interpreter's view */
    struct Battle *battle;
    int side;          /* 0 you, 1 the opponent */
    int index;         /* in Battle.oke */
    int member;        /* 0-2 in its team; member 0 is the leader */
    int spot;          /* formation spot 0-8 (CFormationDataBase +0x24), -1: by member */
    int target;        /* the enemy the chips last found or locked, -1 none */
    OkeDataBase rec;
    PgChipVm vm;
    float x, y, heading; /* heading in degrees, 0 = up the screen, clockwise */
    int hp, hp_max;    /* CBattleOke +0; max from the body table (FUN_089ed5d4) */
    int heat, heat_max;/* +4; max FUN_089ed718 */
    int armour_type;   /* hardware +0x284 (FUN_089ed424) */
    int armour;        /* armour value of the part at hardware +0x280 (FUN_088568c4) */
    int stagger;       /* body table +0x30 (FUN_089ed8a8): the hit size that staggers */
    int part_hp[OKE_BONES]; /* per-bone armour, +0xa010; 0 = broken off */
    float react_left;  /* a flinch (scripts 0x57-0x5a) is playing */
    float down_left;   /* knocked down (state +0xb6ac = 2) */
    float push_x, push_y, push_spin; /* this frame's knock-back (+0xb040 / +0xb050) */
    /* CBattleOkeGunSightController (+0xb470): the sight's yaw relative to the body
     * (+0x50), its target yaw now and last frame (+0x58 / +0x60), last step and
     * momentum (+0x68 / +0x24), aim distance (+0x1c); the HUD mark's point (+0x70)
     * and its momentum (+0x2c). Ticks at 30 Hz. */
    float sight_yaw, sight_goal, sight_goal_prev, sight_step, sight_bonus, sight_dist;
    float mark_x, mark_y, mark_bonus, sight_clock;
    float aim_speed, mark_speed; /* degrees / units per frame */
    ActKind act;
    float act_left;    /* seconds */
    float vx, vy, turn_rate;
    float halt_left, fire_left, defend_left;
    bool locked;
    int counters[8];
    int shots_fired, hits;
    /* Real movement (NULL scripts: the stand-in kinematics instead). */
    OkeMotion motion;
    bool scripted;     /* the current ACT_MOVE / ACT_TURN runs a motion script */
    float obs_amount;  /* CBattleOkeObserver: distance or angle to cover */
    float obs_x, obs_y, obs_turned, obs_last_heading;
    /* Weapons: the hardware block's five slots (+0x258 ids, +0x26c ammo,
     * CHardwareDataBase FUN_089ed2dc / FUN_089ed3a8), as CBattleOke copies them into
     * its weapon slots at +0xa2c0 (0x80 bytes each). */
    int weapon[5], ammo[5];
    int firing;        /* weapon slot being fired, -1 none */
    bool armed;        /* the weapon script pulled the trigger */
    int burst_left;
    float burst_timer;
    /* The fire control's chip state (+0xb470): the readied weapon (+0x10, Start
     * Reticle), state 3 "aiming" (+0x0) and the mode of the last fire chip (+0xc:
     * 1 Shoot Reticle, 2 Select and Fire, 3 Firing Direction). */
    int readied;       /* slot, -1 none */
    bool aiming;
    int aim_mode;
    bool lead_ok;      /* the fire chip's spread setting is 0 (+0xa318 == 0) */
    float aim_angle;   /* Firing Direction: degrees from the heading */
    float gun_raise, raise_rate; /* +0xb10c, 0..1; per second (2 x body +0x38 / 100) */
    float sight_hold;  /* +0x18: seconds Move Reticle holds off the tracker */
    float last_x, last_y, vel_x, vel_y; /* for the target's velocity (FUN_0883f648) */
} Oke;

typedef struct Shot {
    bool live;
    int owner;
    float x, y, vx, vy;
    float drag, min_speed;    /* ballistic bullets slow by drag/s down to min_speed */
    float age, falloff_after; /* attack fades after this many seconds */
    int attack, heat, flags;  /* the hit record CBattleBulletManager passes on */
    /* A mine (CBattleDispenser): its dispenser row, volleys left, the volley timer,
     * the 30 Hz clock it steps on; aerial mines hover out of reach. */
    int mine, rounds;
    float cool, clock;
    bool hover;
    int from;                 /* the OKE that fired it (Battle.oke index) */
    int bullet;               /* its bullet row (the sounds and effects), -1 none */
} Shot;

typedef struct Battle {
    Oke oke[MAX_OKES];
    int count;        /* OKEs in oke[]; 0 at start means the 1-on-1 oke[0] vs oke[1] */
    Shot shots[MAX_SHOTS];
    float time;
    float time_limit; /* seconds; 0 means TIME_LIMIT */
    float map_size;   /* metres, 180-540 (CBattleParam +4); 0 means 540 */
    int tick;
    uint32_t rng;
    int result; /* -1 running, 0 you win, 1 you lose, 2 draw */
    PgChipHistory *history[MAX_OKES]; /* optional: chip results per OKE, for tracing */
    /* optional: sound effect `se` (a row of the SE table) at field point x, y */
    void (*sound)(void *user, int se, float x, float y);
    void *sound_user;
} Battle;

/* HP as the chips and the HUD see it: 0-100 (FUN_0883d774 + FUN_0883fc04). */
int oke_hp_percent(const Oke *o);
int oke_heat_percent(const Oke *o);

/* Places the teams on their formation spots and sets up the interpreters. Before it,
 * fill b->oke[0..count-1] with .rec, .side, .member and .spot (or leave count 0 with
 * the two records in oke[0] (you) and oke[1]). */
void battle_sim_start(Battle *b, uint32_t seed);

/* A team's live OKEs (CBattleOkeMngr FUN_0881f908) and its durability: the members'
 * HP percentages averaged, destroyed ones as 0. */
int battle_team_alive(const Battle *b, int side);
int battle_team_percent(const Battle *b, int side);

/* The battle's length: b->time_limit, or TIME_LIMIT when that's unset. */
float battle_time_limit(const Battle *b);

/* One step of dt seconds. Sets b->result once the battle is over. */
void battle_sim_step(Battle *b, float dt);

/* The per-CPU chip speed (FUN_08856704) and PGCHIP::CRange step -> distance
 * (FUN_089fc37c), both from tables read out of the EBOOT at run time. */
int battle_chip_speed(int cpu_part);
int battle_range_distance(int step);

#endif
