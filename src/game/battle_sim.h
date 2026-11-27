/* The battle simulation behind battle.c: two OKEs running their chip programs.
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
#define SHOT_REACH 700.0f   /* stand-in: shots fly this far */
#define BURST_GAP 0.1f      /* stand-in: seconds between the shots of a burst */
#define MELEE_RANGE 36.0f
#define MELEE_DAMAGE 10     /* stand-in: % of max HP */
#define COOLING 0.08f       /* stand-in: share of max heat shed per second */
#define TIME_LIMIT 120.0f   /* seconds */
#define MAX_SHOTS 160

typedef enum { ACT_NONE, ACT_MOVE, ACT_TURN, ACT_ATTACK, ACT_DEFEND, ACT_JUMP } ActKind;

typedef struct Oke {
    PgChipTarget base; /* must come first: the interpreter's view */
    struct Battle *battle;
    int side;          /* 0 you, 1 the opponent */
    OkeDataBase rec;
    PgChipVm vm;
    float x, y, heading; /* heading in degrees, 0 = up the screen, clockwise */
    int hp, hp_max;    /* CBattleOke +0; max from the body table (FUN_089ed5d4) */
    int heat, heat_max;/* +4; max FUN_089ed718 */
    int armour_type;   /* hardware +0x284 (FUN_089ed424) */
    int armour;        /* armour value of the part at hardware +0x280 (FUN_088568c4) */
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
    bool aim_at_enemy;
    float aim_angle;   /* Firing Direction: degrees from the heading */
} Oke;

typedef struct Shot {
    bool live;
    int owner;
    float x, y, vx, vy, life;
    float age, falloff_after; /* attack fades after this many seconds */
    int attack, heat, flags;  /* the hit record CBattleBulletManager passes on */
} Shot;

typedef struct Battle {
    Oke oke[2];
    Shot shots[MAX_SHOTS];
    float time;
    int tick;
    uint32_t rng;
    int result; /* -1 running, 0 you win, 1 you lose, 2 draw */
    PgChipHistory *history[2]; /* optional: chip results per side, for tracing */
} Battle;


/* Places the two designs facing each other and sets up their interpreters. The
 * records must already be in b->oke[0].rec and b->oke[1].rec. */
/* HP as the chips and the HUD see it: 0-100 (FUN_0883d774 + FUN_0883fc04). */
int oke_hp_percent(const Oke *o);
int oke_heat_percent(const Oke *o);

void battle_sim_start(Battle *b, uint32_t seed);

/* One step of dt seconds. Sets b->result once the battle is over. */
void battle_sim_step(Battle *b, float dt);

/* The per-CPU chip speed (FUN_08856704) and PGCHIP::CRange step -> distance
 * (FUN_089fc37c), both from tables read out of the EBOOT at run time. */
int battle_chip_speed(int cpu_part);
int battle_range_distance(int step);

#endif
