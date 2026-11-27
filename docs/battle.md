# Battle Mode

Choosing **Battle Mode** on the title's mode menu now leads to a playable battle
between two shipped preset designs:

1. **Choose your OKE**: the 50 arena designs, the two test designs and the tutorial
   model answers.
2. **Choose your opponent**: the 50 arena designs.
3. **Battle**: "BATTLE START", then both robots run their own chip programs until one
   is destroyed or 120 seconds pass. WINNER, LOSER or DRAW follows. Circle goes back to
   the opponent list, and cross on the first list goes back to the title.

Code: `src/game/battle.c` (screens and drawing), `src/game/battle_sim.c` (the
simulation), `src/game/gmo.c`, `okemodel.c`, `terrain.c` (models and field),
`tests/battle_test.c` (headless battles). Screenshots:

```sh
build/pc-m32/chexa-pc --disc DIR --startup --press 70 circle --press 90 down \
    --press 110 circle --shot 130 orig/pick.png      # Battle Mode -> OKE list
build/pc-m32/chexa-battle-test --mine preset/arena/oke_b15_1.bin \
    --theirs preset/arena/oke_b04_1.bin --trace 40   # one battle, first 40 chips
build/pc-m32/chexa-battle-test --arena -v            # all 2450 arena pairings
build/pc-m32/chexa-pc --disc DIR --startup --oke 8 --shot 20 orig/oke.png   # one body
build/pc-m32/chexa-pc --disc DIR --startup --map 15 --oke 3 --shot 20 orig/map.png
build/pc-m32/chexa-pc --disc DIR --startup --gmo land/map01/map01_sky.gmo --shot 20 orig/m.png
```

## What is the game's and what isn't

| Part | Status | Source |
|---|---|---|
| Robot behaviour | **Real** | `pgchip.c` (`CBattleOkePgChip`) runs the programs stored in the preset records |
| Chip speed | **Real** | per CPU part, table at `0x08bd5f88` (`FUN_08856704`) |
| Scan and aim distances | **Real** | `PGCHIP::CRange` (`FUN_089fc37c`), tables at `0x08c1cab4` / `0x08c1cb7c` / `0x08c1cbd0` |
| 3D models | **Real** | `gmo.c`: GMO reader (bones, parts, meshes, GE vertex arrays, materials, embedded GIM textures) |
| OKE bodies | **Real** | `okemodel.c`: body type at hardware `+0x240` (`FUN_089ed210`), `COkeFile` table at `0x0041666c` (`FUN_089f2bc4` / `FUN_089f2c20` / `FUN_089f2c7c`), parts placed by the `PARTS2` bone tree of `okeedit/TYPE.txt`, paint file textures (`CBattleOkeSetupBody`, `FUN_0885a470`) |
| Terrain | **Real** (mostly) | `terrain.c`: `CStageGenerator` (`FUN_08aa610c`) and its LANDGEN passes from `land/param/mapNN_a.mpp`, see "Terrain" below |
| Sky | **Real** | `land/mapNN/mapNN_sky.gmo`, drawn around the camera |
| Paint scheme | Assumed | hardware `+0x244` (0-3 in all presets); not traced to its reader yet |
| Poses | Partly | pose 0 of `okeedit/TYPE.txt` only. The `!SCRIPT` motions (walking, firing) aren't played |
| Weapons on the body | Stand-in | the `PARTS2` defaults; the equipment-dependent part choice isn't read |
| Map choice | Stand-in | always map 01 (`BATTLE_MAP`); the arena's own stage pick isn't traced |
| Terrain height scale and tiles | Stand-in | height = byte × block-0 height / 255 over the used cells; one tile texture (`mapNN_a00.gim`); vertex shading from the map's light direction instead of GE lighting |
| Battle camera | Stand-in | a chase camera behind your OKE (`CBattleSceneCamera` isn't read) |
| What the robot does with a chip order (`CBattleOke`) | Stand-in | a 2D simulation with guessed units: an 800×800 field mapped onto the terrain, 50 units/s walking, 180°/s turning, shots at 320 units/s that take 4% each, melee 10% within 36 units, a 120 s limit. Heights don't affect it |
| HUD windows and gauges | **Real** (partly) | the `CIndicatorBase` window table at `0x003ed78c` (`FUN_089011d8` / `FUN_0890126c`), each window cut from the `bs_wind0` atlas; the status window's HP, heat and energy gauges as `FUN_089104fc` / `FUN_089106ac` / `FUN_08910860` draw them (130 px per 1000, positions at `0x003f0514`, heights at `0x003f0508`). Only windows 2 (status) and 3 (radar) are drawn. Heat and energy aren't simulated: heat shows empty, energy full |
| Radar contents | Stand-in | `bs_radm2` dots placed by hand inside window 3, centred on your OKE; `CBattleRadarMngr` isn't read |
| Lock-on | Stand-in | the game's `gunsite` / `gundot` marks drawn on the opponent while the simulation says you're locked on; `CBattleOkeGunSightCon` isn't read |
| Names, timer | Stand-in | plain text where the game has its top windows |
| Selection screens | Stand-in | for the game's flow (below), drawn with the title's art |

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

## GMO notes

Files start with `OMG.00.1PSP` and 16 bytes of header, then chunks: u16 type, u16
header size, u32 chunk size. Structural chunks (2 file, 3 model, 4 bone, 5 part,
6 mesh, 7 arrays, 8 material, 9 texture, 0x0A image file, 0x0B motion) carry a child
offset, a data offset and a name; data chunks (0x8000 and up) hold values after the
8-byte header. Used here:

- `0x8014` bounding box, `0x8015` vertex offset and scale (applied as a translate and
  scale; positions are normalized 8- or 16-bit GE values).
- `0x8041` bone parent, `0x8047` bone matrix, `0x804e` bone draws part.
- arrays: u32 GE vertex format, count, morph count, flags, then raw vertices.
- `0x8061` mesh material, `0x8066` draw: arrays index (local to the part), primitive,
  vertices per strip, strip count, u16 indices.
- `0x8082` diffuse colour, `0x8091` texture → image file, `0x8013` GIM bytes.

OKE bodies (`battle/TYPE/part.gmo`) have no bones: each part is centred on its own
pivot, and there are two of each (`part-X` and the wrecked `part-X_b`). `PARTS2` in
`okeedit/TYPE.txt` numbers only the intact ones: `parent part pivotX pivotY pivotZ`
followed by six rotation limits. Each `!POSE` is a root translation and one rotation
(x, y, z radians) per bone, applied here in Y, X, Z order. The textures are in
`battle/TYPE/paintN.gmo`.

## Terrain

`land/param/mapNN_a.mpp` starts with `CHLG`. The generator reads seven blocks
(`FUN_08924eb0` .. `FUN_08924f10`), at file offsets 0x40, 0x60, 0x70 ... 0xB0:

| Block | Contents |
|---|---|
| 0 | seed, min x, ?, min z, size x, height, size z, cells x, cells z, border height |
| 1 | roughness (float), random corners flag, four corner heights, Voronoi merge weight |
| 2 | Voronoi feature points, Voronoi weight |
| 3 | smoothing passes |
| 4 | erosion threshold, erosion passes |
| 5 | thermal threshold, thermal passes |
| 6 | terrace step, stretch factor |

The order is `FUN_08aa610c`'s: seed the MT19937 (`FUN_08b52254`: standard
`init_genrand` plus 624 discarded steps; `FUN_08b469c0` tempers and regenerates one
word per call), size the grid to the next power of two plus one, mark the border
strips, midpoint displacement (`FUN_08922e30`), a Voronoi map (`FUN_08920218`, its
points shuffled with the game's BSD qsort `FUN_08b41288` and a random comparator) merged
by weight (`FUN_0891d988`), smoothing (`FUN_0892347c`), hydraulic erosion
(`FUN_0892391c`), thermal erosion (`FUN_08923dc0`, **not ported**: maps 05, 06, 08 and
14 ask for it), terracing (`FUN_08921180`) and the stretch copy (`FUN_0891dc64`).
Nothing is checked against the game's output yet: the next step is comparing a
height map dumped from a running game or emulator.

## Missing

- **The battle field**: scenery models (`mapNN_nature*.gmo`, `piece_*`, `distant*`),
  obstacles, the `.bbx` collision boxes, the stage choice and CLandTexture's tiles.
- **The robots**: motions (`!SCRIPT` in `okeedit/TYPE.txt`), weapon parts by
  equipment, wreck parts, flying types. The renderer still lacks GE lighting and
  skinning.
- **Effects**: shots are dots. There are no explosions, muzzle flashes or damage
  smoke, although the effect models are in "battlescene".
- **HUD**: the other indicator windows (CPU / chip window, weapon gauge `bs_wepguage`,
  `bs_tnum0` timer digits, alerts), the radar's own layout and the gun sight's
  movement. The parts table is at `0x00476398` (64 names, `CBsPicture`,
  `FUN_08895e2c` / `FUN_08895f64`).
- **Sound**: none. No CSound, and no `battle_se` set.
- **Behaviour**: `CBattleOke` (hardware stats: legs, weapons, armour, engine, heat,
  ammunition; flying types; collision) isn't ported, so who wins says little about the
  real game.
- **Flow**: the dialog, name entry, briefing, hangar and the game's own opponent
  selection.
