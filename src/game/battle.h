/* Battle Mode: the game's selection screens (battle_menu.c), then a battle between
 * the chosen OKEs on the chosen map, each running its own chip program.
 *
 * What is the game's and what is the port's:
 *   - The robots' behaviour is the game's chip interpreter (pgchip.c,
 *     CBattleOkePgChip) running the programs stored in the preset records, at the
 *     game's per-CPU chip speed. Scan and aim distances use the game's PGCHIP::CRange
 *     scale.
 *   - The robot side of the interface (CBattleOke: movement, weapons, sensors,
 *     damage) isn't ported. A small 2D simulation stands in for it, with guessed
 *     units and speeds (see docs/battle.md).
 *   - The selection screens follow CBattleModeMngr, CBattleSelectMngr,
 *     CBattleTestMngr and CBattleArenaMngr (docs/screens.md). The 3D view is the
 *     port's stand-in for CBattleScene, drawn with the game's models and HUD art. */
#ifndef GAME_BATTLE_H
#define GAME_BATTLE_H

/* Starts at the Battle Mode top menu. Registers its own render node. */
void battle_mode_start(void);

/* One main-loop step. Returns 0 when the player backs out to the title. */
int battle_mode_update(float dt);

#endif
