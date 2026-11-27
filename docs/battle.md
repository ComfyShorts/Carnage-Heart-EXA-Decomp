# Battle Mode (simplest path)

Choosing **Battle Mode** on the title's mode menu now leads to a playable battle
between two shipped preset designs:

1. **Choose your OKE**: the 50 arena designs, the two test designs and the tutorial
   model answers.
2. **Choose your opponent**: the 50 arena designs.
3. **Battle**: "BATTLE START", then both robots run their own chip programs until one
   is destroyed or 120 seconds pass. WINNER, LOSER or DRAW follows. Circle goes back to
   the opponent list, and cross on the first list goes back to the title.

Code: `src/game/battle.c` (screens and drawing), `src/game/battle_sim.c` (the
simulation), `tests/battle_test.c` (headless battles). Screenshots:

```sh
build/pc-m32/chexa-pc --disc DIR --startup --press 70 circle --press 90 down \
    --press 110 circle --shot 130 orig/pick.png      # Battle Mode -> OKE list
build/pc-m32/chexa-battle-test --mine preset/arena/oke_b15_1.bin \
    --theirs preset/arena/oke_b04_1.bin --trace 40   # one battle, first 40 chips
build/pc-m32/chexa-battle-test --arena -v            # all 2450 arena pairings
```

## What is the game's and what isn't

| Part | Source |
|---|---|
| Robot behaviour | **The game's**: `pgchip.c` (`CBattleOkePgChip`) runs the programs stored in the preset records |
| Chip speed | **The game's**: per CPU part, read from the table at `0x08bd5f88` (`FUN_08856704`) |
| Scan and aim distances | **The game's**: `PGCHIP::CRange` (`FUN_089fc37c`), tables at `0x08c1cab4` / `0x08c1cb7c` / `0x08c1cbd0` |
| Move directions | From the order of the game's action names (forward, backward, right, left, then the diagonals), EBOOT strings at `0x003ed040`. Rotation 0 is clockwise |
| What the robot does with a chip order (`CBattleOke`) | **Stand-in.** A 2D simulation with guessed units: an 800×800 field, 50 units/s walking, 180°/s turning, shots at 320 units/s that take 4% each, melee 10% within 36 units, a 120 s limit |
| Selection screens | **Stand-in** for the game's flow (below), drawn with the title's art |
| Battle view | **Stand-in**: top-down field and side panels. The "BATTLE START" and WINNER / LOSER / DRAW banners are the game's (`parts/bs/` in the "battlescene" file set) |

The tables come from your EBOOT at run time (`--elf`), so nothing is copied into the
repository. Without it, rough defaults are used.

### The game's own flow after Battle Mode (not ported)

From the entrance code (`FUN_088d5510`, `FUN_088d497c`, `FUN_088d5a70`,
`FUN_088d61b8`, `FUN_088d317c`):

- Phase 9: a yes/no dialog ("there are no tutorials in Battle Mode"), skipped when a
  flag at `+0x74` of a game-state object is set.
- Phase 7: the player's name on the system on-screen keyboard (sceUtilityOsk).
- Phase 0xB: fade to black, then the entrance ends.
- `FUN_088d317c` → `FUN_088d2ff8`: the **briefing** scene (0x1410 bytes,
  `FUN_0893f578`, file set "briefing"). That's the management side: hangar, OKE design,
  `CBattleSelectMngr` and its arena opponents.
- The battle itself is `CBattleScene` / `CBattleSceneMngr` (file set "battlescene":
  3D effect models `effect/*.gmo`, HUD art `parts/bs/*`), with `CBattleOke` behind
  each robot.
- `CRootTest` (`FUN_08a60ac8`, the debug menu the root screen opens after the
  entrance) has a `BATTLE_SCENE` entry and a `CBattleTestFlow` / `CBattleTestMngr`
  (`FUN_08822b0c` and neighbours). That's a likely shortcut into the real battle scene.

## How the presets behave in the stand-in

`chexa-battle-test --arena` fights every arena design against every other: 2450
battles, about 270 end with a robot destroyed. Ten designs fire in nearly all their
battles (04, 15, 20, 24, 26, 29, 33, 40, 45, 47). Many others never shoot, because
their programs wait on chips the stand-in answers with "no":

- **Counters** (Input / Control / Assess / Send / Receive Counter): not modelled.
- **Target code, target movement, target weapon, line of fire, remaining ammunition.**
- **Status Assessment** is a guess (b2 armour, b3 percentage, b4 comparison).

The tutorial answers are lesson drills (for example lesson 4 repeats an Attack, a
melee move), not fighting programs.

## Missing

- **The battle field**: terrain, obstacles and the arena maps. There's no 3D model or
  terrain loader yet (GMO models, the software renderer's 3D path, textures on models).
- **The robots**: OKE models and animation. Each robot is a triangle.
- **Effects**: shots are dots. There are no explosions, muzzle flashes or damage
  smoke, although the effect models are in "battlescene".
- **HUD**: the radar, gauges, CPU window and lock-on marks exist in `parts/bs/`, but
  the game's layout code (`CBattleDisplayMngr`, `CBattleRadarMngr`) isn't read yet.
- **Sound**: none. No CSound, and no `battle_se` set.
- **Behaviour**: `CBattleOke` (hardware stats: legs, weapons, armour, engine, heat,
  ammunition; flying types; collision) isn't ported, so who wins says little about the
  real game.
- **Flow**: the dialog, name entry, briefing, hangar and the game's own opponent
  selection.
