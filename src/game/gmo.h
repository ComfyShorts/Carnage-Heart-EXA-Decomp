/* GMO models (Sony's PSP model format, "OMG.00.1PSP"), as the game uses them for
 * OKE bodies (battle/TYPE/part.gmo), their paint schemes (paint<n>.gmo, textures
 * only), map scenery (land/mapNN/ models) and effects.
 *
 * The game draws them through Sony's GMO runtime, statically linked. This is a
 * hand-written reader for the parts the battle needs: bones with their matrices,
 * parts made of meshes, vertex arrays in raw GE vertex formats, materials and the
 * embedded GIM textures. Motions (animation curves) are skipped. Format notes are
 * in docs/battle.md. */
#ifndef GAME_GMO_H
#define GAME_GMO_H

#include <stdint.h>

typedef struct Gmo Gmo;

/* Loads a model through ReadFile (file sets, then loose pack files). */
Gmo *gmo_load(const char *path);
void gmo_free(Gmo *g);

/* Uses another model's embedded textures for this one's materials (the OKE body
 * parts carry materials but their textures are in the paint files). */
void gmo_use_textures_from(Gmo *g, const Gmo *textures);

int gmo_part_count(const Gmo *g);
const char *gmo_part_name(const Gmo *g, int part);
int gmo_find_part(const Gmo *g, const char *name);

/* Bounding box of the whole model, model units: min xyz then max xyz. */
void gmo_bounds(const Gmo *g, float out[6]);

/* Draws the whole model through its bone hierarchy, or (models without bones) every
 * part, with the current libgum model matrix as the root. */
void gmo_draw(const Gmo *g);

/* Draws one part with the current model matrix. */
void gmo_draw_part(const Gmo *g, int part);

#endif
