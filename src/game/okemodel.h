/* An OKE's body as the battle draws it (CBattleOkeSetupBody, FUN_0885a470): the
 * body type's parts model battle/TYPE/part.gmo, one of its paint files for the
 * textures, and the assembly description okeedit/TYPE.txt (COkeFile, the table at
 * 0x0041666C) that places the parts on a bone tree and holds the poses.
 *
 * Read from okeedit/TYPE.txt: the !BODY section's PARTS2 lines (parent bone, part,
 * pivot, rotation limits) and the !POSE sections (root translation plus one
 * rotation per bone). Weapons use the parts PARTS2 names; the equipment-dependent
 * choice isn't read yet. */
#ifndef GAME_OKEMODEL_H
#define GAME_OKEMODEL_H

#include <stdbool.h>
#include <stdint.h>

#define OKE_ID_MAX 0x24
#define OKE_MAX_BONES 24

typedef struct OkeModel OkeModel;

/* Loads body type id (hardware block +0x240) with paint scheme (0-3). As the battle
 * does (FUN_089f3064), the body's file set is opened around the reads: six bodies
 * (abi, agr, gaz, gs, kei, nh) have their battle/ models only in their sets. */
OkeModel *okemodel_load(int oke_id, int paint);

/* COkeFile::GetOkeFileSetName3 (FUN_089f2dec): the file set of body oke_id with
 * paint 0-3 ("<body>_<paint>"), or 4 for the one with every paint ("<body>"); NULL
 * out of range or without the EBOOT. */
const char *okemodel_set_name(int oke_id, int paint);
void okemodel_free(OkeModel *m);

int okemodel_pose_count(const OkeModel *m);

/* Draws the body in a pose with the current libgum model matrix as its ground
 * point (feet on y = 0, facing +z). */
void okemodel_draw(const OkeModel *m, int pose);

/* The same, leaving out the bones set in `hidden` (bit per PARTS2 bone) and
 * everything attached below them: parts that have broken off. */
void okemodel_draw_hiding(const OkeModel *m, int pose, uint32_t hidden);

/* The body type's directory name ("tri", "neg", ...), or "" */
const char *okemodel_type_name(int oke_id);

#endif
