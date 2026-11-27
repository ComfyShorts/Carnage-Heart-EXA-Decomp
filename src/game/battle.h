/* Battle Mode, simplest path: pick your OKE and an opponent from the shipped preset
 * designs, then watch the two fight with their own chip programs.
 *
 * What is the game's and what is the port's:
 *   - The robots' behaviour is the game's chip interpreter (pgchip.c,
 *     CBattleOkePgChip) running the programs stored in the preset records, at the
 *     game's per-CPU chip speed. Scan and aim distances use the game's PGCHIP::CRange
 *     scale.
 *   - The robot side of the interface (CBattleOke: movement, weapons, sensors,
 *     damage) isn't ported. A small 2D simulation stands in for it, with guessed
 *     units and speeds (see docs/battle.md).
 *   - The selection screens stand in for the game's name entry, briefing, hangar
 *     and CBattleSelectMngr, and the top-down field stands in for CBattleScene's 3D
 *     view. Both are drawn with the game's own 2D art (title and battle HUD
 *     textures). */
#ifndef GAME_BATTLE_H
#define GAME_BATTLE_H

/* Starts at the OKE selection screen. Registers its own render node. */
void battle_mode_start(void);

/* One main-loop step. Returns 0 when the player backs out to the title. */
int battle_mode_update(float dt);

#endif
