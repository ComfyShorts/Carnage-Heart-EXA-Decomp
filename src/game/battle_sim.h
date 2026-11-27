/* The battle simulation behind battle.c: two OKEs running their chip programs.
 *
 * The chip interpreter (pgchip.c) and the numbers it depends on (the per-CPU chip
 * speed, the PGCHIP::CRange distance scale) are the game's. Everything a robot does
 * in response (moving, turning, firing, sensing, damage) is a stand-in for
 * CBattleOke, with guessed units; see docs/battle.md. */
#ifndef GAME_BATTLE_SIM_H
#define GAME_BATTLE_SIM_H

#include <stdbool.h>
#include <stdint.h>

#include "okedb.h"
#include "pgchip.h"

#define FIELD 800.0f        /* square arena, in range units */
#define OKE_RADIUS 12.0f
#define MOVE_SPEED 50.0f    /* units per second */
#define QUICK_SPEED 160.0f
#define TURN_SPEED 180.0f   /* degrees per second */
#define SHOT_SPEED 320.0f
#define SHOT_DAMAGE 4
#define MELEE_RANGE 36.0f
#define MELEE_DAMAGE 10
#define TIME_LIMIT 120.0f   /* seconds */
#define MAX_SHOTS 64

typedef enum { ACT_NONE, ACT_MOVE, ACT_TURN, ACT_ATTACK, ACT_DEFEND, ACT_JUMP } ActKind;

typedef struct Oke {
    PgChipTarget base; /* must come first: the interpreter's view */
    struct Battle *battle;
    int side;          /* 0 you, 1 the opponent */
    OkeDataBase rec;
    PgChipVm vm;
    float x, y, heading; /* heading in degrees, 0 = up the screen, clockwise */
    int hp;            /* 0-100 */
    ActKind act;
    float act_left;    /* seconds */
    float vx, vy, turn_rate;
    float halt_left, fire_left, defend_left;
    bool locked;
    int counters[8];
    int shots_fired, hits;
} Oke;

typedef struct Shot {
    bool live;
    int owner;
    float x, y, vx, vy, life;
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
void battle_sim_start(Battle *b, uint32_t seed);

/* One step of dt seconds. Sets b->result once the battle is over. */
void battle_sim_step(Battle *b, float dt);

/* The per-CPU chip speed (FUN_08856704) and PGCHIP::CRange step -> distance
 * (FUN_089fc37c), both from tables read out of the EBOOT at run time. */
int battle_chip_speed(int cpu_part);
int battle_range_distance(int step);

#endif
