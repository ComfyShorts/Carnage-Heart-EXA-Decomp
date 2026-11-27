/* The entrance: the first screen after start-up, with the opening and the title menu.
 *
 * The root screen (FUN_088f1d98) starts it as its state-3 child. Classes, as far as
 * they're written here:
 *
 *   entrance screen  FUN_088d2d30 (0x44 bytes), update FUN_088d36e4: opens the
 *                    "entrance" file set, runs CSystemMngr (a save-data check, not
 *                    ported) and then CEntranceMngr; drawn as a CRender node.
 *   CEntranceMngr    ctor FUN_088d6770, init FUN_088d6b40, update FUN_088d6360,
 *                    draw FUN_088d63b4: the title's phases and its window stack.
 *   CEntranceWnd     ctor FUN_088d6f48, create FUN_088d7104, update FUN_088d7778,
 *                    input FUN_088d7ac0, draw FUN_088d7efc, open FUN_088d7ce0,
 *                    close FUN_088d7330: a vertical button menu.
 *   colour window    FUN_0888eb80, draw FUN_0888f264: a filled rectangle.
 *   CWndStack        push FUN_08ae6234, pop FUN_08ae637c, draw FUN_08ae66d4.
 *   parts cache      FUN_0888763c / FUN_08887834: interface textures by index. */
#ifndef GAME_ENTRANCE_H
#define GAME_ENTRANCE_H

/* Constructs the screen (as the root screen does) and registers it for drawing. */
void entrance_start(void);

/* One main-loop step; dt in seconds. Returns 0 once the entrance has ended. */
int entrance_update(float dt);

/* What the player chose in the mode menu once the entrance has ended (0 Story Mode,
 * 1 Battle Mode). */
int entrance_selection(void);

#endif
