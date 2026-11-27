/* The battle field's ground: the game's procedural terrain (CStageGenerator,
 * FUN_08aa610c, and the LANDGEN passes it calls) built from a map's parameter file
 * land/param/mapNN_a.mpp, then drawn as a textured height grid.
 *
 * Real: the parameter blocks, the random source (MT19937 seeded from the file, as
 * FUN_08b52254 / FUN_08b469c0), the BSD qsort the game shuffles with, and the passes
 * midpoint displacement, Voronoi + merge, global smoothing, hydraulic erosion and
 * aliasing (terracing) and the stretch copy. Not yet: thermal erosion (logged and
 * skipped when a map asks for it), CLandGridMngr's exact height scale and
 * CLandTexture's tile choice (see docs/battle.md). */
#ifndef GAME_TERRAIN_H
#define GAME_TERRAIN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Terrain Terrain;

Terrain *terrain_load(int map);
void terrain_free(Terrain *t);

/* Field extent in world units: min x, min z, size x, size z. */
void terrain_extent(const Terrain *t, float out[4]);

/* Ground height at a world position (clamped to the field). */
float terrain_height(const Terrain *t, float x, float z);

/* Draws the ground with the current view and projection. */
void terrain_draw(const Terrain *t);

/* For tests: the generated height map (size x size bytes). */
const uint8_t *terrain_heights(const Terrain *t, int *size);

#endif
