/* OKE motion scripts and the movement they drive, as CBattleOke does it.
 *
 * Each body type's okeedit/TYPE.txt lists 256 "!SCRIPT" motions of COMMAND lines
 * {command, modifier, value, frames}. The chips start them through
 * CBattleOke::SetScript (FUN_0883ed94): Move picks script {4,3,1,2,8,7,10,9}[dir]
 * (FUN_08844450, table at 0x08bd2414), Rotate script 6 or 5 (FUN_088445b0).
 * CBattleOkeScript::Update (FUN_088597e8) runs the commands:
 *
 *   0x200-0x203  push along +right, -right, +forward, -forward: an acceleration of
 *                value x 67.5 (0x08bd6930) for frames / 30 s (FUN_08848d8c ...)
 *   0x204-0x205  turn: an angular acceleration of value x 2.618 rad/s^2
 *                (0x08cce2d0, set by a static initializer) (FUN_088484f8 ...)
 *   0x209        loop back `value` commands while the action's counter allows
 *                (FUN_08858fd8; the chips set the counter to -1: repeat until stopped)
 *   0x20b        pull weapon `value`'s trigger (FUN_0885910c -> FUN_08848a68)
 *   < 0x200      blend to that pose and wait
 *
 * CBattleOke's physics step (FUN_0884df08) sums the active pushes into an
 * acceleration, brakes with 12/s (0.4 x 30) whenever no push is active, damps the
 * velocity by exp(-0.3 t) (+0xb6f4) and the turn rate by exp(-1.0 t) (+0xb714),
 * then moves the body. See docs/battle.md for what's approximated. */
#ifndef GAME_OKE_SCRIPT_H
#define GAME_OKE_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

#define OKE_SCRIPTS 256

typedef struct OkeScriptCmd {
    int16_t cmd, mod;
    uint16_t value, frames;
} OkeScriptCmd;

#define OKE_BONES 24
#define OKE_BOXES 32 /* CBattleOke's part box slots at +0x98b0 */
#define OKE_MOUNTS 6 /* weapon mounts at +0x9790 */

/* A BBOX2 line: a box on a bone, in the bone's frame. Class picks the damage
 * factor (0x08bd1814: 1.0, 0.7, 0.1) and the armour share weight (1.5, 3, 5). */
typedef struct OkeBox {
    int bone, cls;
    float c[3], s[3]; /* centre, full size */
} OkeBox;

typedef struct OkeScripts {
    int body;
    OkeScriptCmd *cmds;   /* all scripts back to back */
    int first[OKE_SCRIPTS], count[OKE_SCRIPTS];
    /* The !BODY geometry: PARTS2 bone tree, BBOX2 boxes, WEAPON2 mount bones. */
    int n_bones, parent[OKE_BONES];
    float pivot[OKE_BONES][3];
    int n_boxes;
    OkeBox box[OKE_BOXES];
    int mount_bone[OKE_MOUNTS];
    float ground; /* lowest EARTH point */
    int n_poses;
    float (*pose_root)[3];
    float (*pose_rot)[OKE_BONES][3];
} OkeScripts;

/* Which box (if any) contains a point given in the body's frame (x left, z
 * forward, feet at y = 0), the body standing in `pose`. Each box is tested at its
 * own height, so this is a test against the box's column. Returns the box index
 * or -1; `centre` gets that box's centre in the body frame. */
int oke_body_box_at(const OkeScripts *s, int pose, float x, float z, float centre[3]);

/* Loaded once per body type (hardware +0x240) from the disc and kept. NULL if the
 * file or the EBOOT's file table isn't available (the battle then falls back to the
 * old stand-in movement). */
const OkeScripts *oke_scripts_get(int body);

/* Script player and physics state for one OKE, in the battle plane: x right, y
 * down the field, heading in degrees clockwise from "up". */
typedef struct OkeMotion {
    const OkeScripts *s;
    int script, pc;      /* -1: none */
    float wait;          /* seconds left on a pose command */
    int pose;            /* the last pose command (POSE index in TYPE.txt) */
    int trigger[5];      /* 0x20b "fire weapon n" commands not yet taken */
    int loop_count, loop_key;
    float push[6][2];    /* acceleration per channel, world plane */
    float push_t[6];
    float spin[2];       /* angular acceleration, degrees/s^2 */
    float spin_t[2];
    float vx, vy, omega; /* units/s, degrees/s */
} OkeMotion;

void oke_motion_reset(OkeMotion *m, const OkeScripts *s);
void oke_motion_start(OkeMotion *m, int script); /* SetScript; the counter goes to -1 */
void oke_motion_stop(OkeMotion *m);              /* back to the idle script 0 */
bool oke_motion_busy(const OkeMotion *m);        /* a non-idle script is running */

/* One frame: the script runs until it waits, then the physics moves x, y, heading. */
void oke_motion_step(OkeMotion *m, float dt, float *x, float *y, float *heading);

#endif
