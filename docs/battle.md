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
| Walking and turning | **Real** (mostly) | `oke_script.c`. Move, Rotate, Quick Movement and Quick Rotate start the body's `!SCRIPT` motions as `CBattleOke` does (`FUN_08844450` / `FUN_088445b0` / `FUN_08844940` / `FUN_08844a00`, script tables at `0x08bd2414` and `0x08bd2568`). `CBattleOkeScript::Update` (`FUN_088597e8`) runs the commands: 0x200-0x203 pushes of value × 67.5 (`0x08bd6930`) for frames / 30 s, 0x204/0x205 turns of value × 2.618 rad/s² (`0x08cce2d0`, set to `0x40278d36` by a static initializer), 0x209 loops (`FUN_08858fd8`), poses. The physics step follows `FUN_0884df08`: the pushes add up into an acceleration, a 12/s brake works while no push is active, velocity is damped by exp(-0.3 t) (`+0xb6f4`) and turning by exp(-1.0 t) (`+0xb714`). Move and Rotate end when the observer's distance or angle is covered (amount - 0.5, `FUN_0883aaec`) |
| Weapons: loadout, firing, bullet speed | **Real** (partly) | each OKE carries the five weapon slots of its hardware block (ids at `+0x258`, ammo at `+0x26c`, `CHardwareDataBase` `FUN_089ed2dc` / `FUN_089ed3a8`, copied as in `FUN_0883d4bc`'s caller). Select and Fire / Firing Direction (`FUN_08845038` / `FUN_088452c8`) pick slot 1-5 (or 0, the current weapon) and start its weapon script `0x2a + slot`; the script's 0x20b command pulls the trigger (`FUN_0885910c` → `FUN_08848a68`) and the chip's count of shots goes out, each spending one round. Shot speed is `CBattleOkeEquipment`'s (`FUN_08856528`): weapon table at `0x08bd5574` → bullet table `0x08c69c74` or dispenser table `0x08bcb8fc`, `+0x0c` |
| Damage and heat from hits | **Real** (mostly) | each shot carries the hit record `CBattleBulletManager::ResolveCollisionRecords` (`FUN_08813fa4`) builds: attack, hit heat and flags from the bullet table at `0x08bcb1a4` (clusters and missile dispensers report their child bullet's numbers, as `GetWeaponAttack` `FUN_08856480` does), weakened by 60 attack / 120 heat per second once the shot is older than the bullet's `+0x2c` (floor 20%). Bullets with a `+0x28` lifetime expire then. Shotguns fire `GetWeaponNum` pellets (`0x08bd5a24`). The receiver follows `CBattleOke`'s hit handler (`FUN_0884d284`): heat × 0.7 for armour type 4; damage × 0.7 when the armour type matches the flag (1/bit 0, 3/bit 1, 2/bit 4) and when defending, then `d - d × armour/100 - armour × 0.5` (armour value from the table at `0x08bd5ed8` for the part at hardware `+0x280`), at least 1, off HP (`FUN_0883d7f4`). Max HP and heat limit come from the body table at `0x08c14d84` (`+0`, `+0x10` × (200 - hardware `+0x29c`) / 100). Firing heats the shooter by `GetWeaponShotCalorie` (`0x08bd5a30`); heat is capped at 120% (`FUN_0883d960`). Status Assessment reads HP / heat as the game's rounded percentages (`FUN_088462a4`, `FUN_0883fc04`, `FUN_08a184f0`) |
| Hit boxes and part armour | **Real** (mostly) | the `BBOX2` lines of `okeedit/TYPE.txt` (bone, class, centre, size: `CBattleOkeSetupBody` copies them into `CBattleOke`'s 32 slots at `+0x98b0`, `FUN_0885a2c8` / `FUN_088496a0`) placed on the posed bone tree. Each shot's step is swept against them in half-unit samples; the class gives the damage and heat factor (`0x08bd1814`: 1.0 / 0.7 / 0.1). Each bone starts with armour (`+0xa010`): twice the HP times its share of the class-weighted box volume (weights 1.5 / 3 / 5 at `0x08bd1820`), plus 180 per `WEAPON2` mount on it, plus 5 × the armour value. Hits wear it down (`FUN_088412b0`); at 0 the part breaks off (no longer drawn), hits there skip the armour and class factors, and a weapon mounted there can't fire (`FUN_0883dd78`) |
| Hit reactions | **Real** (partly) | from `FUN_0884d284`: the stagger value from the body table (`+0x30`, doubled while defending); a hit above it knocks down 1 time in 8 (always above 3×), otherwise flinches; smaller hits flinch 1 time in 20. A flinch plays script 0x57-0x5a by the side the shot came from (`FUN_08843c90`); a knock-down plays 0x5b. Orders are refused meanwhile. Knock-back pushes along the shot, turned up to 30° towards the side hit (`FUN_08842a94`), d / 3t × 2500 for one 30 Hz physics step, plus a yaw kick from the lever arm capped at 100 |
| Hits: what's still approximate | Stand-in | boxes are tested as vertical columns (shots have no height); the knock-down ragdoll (`CRagdoll`) is a 2.5 s tilt; the flinch side's mirror and the spin's sign aren't checked against the game; broken parts vanish instead of flying off as their `_b` models; shields (option type 9) and the game options that can turn knock-downs and part breaking off aren't read. Only shotgun pellets spread (stand-in); single shots fly straight with the bullet's speed jitter (`+0x10`, real). Heat cools at 8% of the limit per second and has no effect; fuel reads 100%. Shots without a lifetime fly 700 units; a burst fires every 0.1 s; melee takes 10% of max HP |
| Movement modifiers | Stand-in | `FUN_08858c8c` / `FUN_08858d44` scale each command by the OKE's performance (legs, weight); taken as 100%, so every body of a type moves alike. Commands 0x206-0x208 and 0x20a-0x210 (pose speed, waits, altitude, loop mode) are skipped |
| Poses | Partly | the pose each script's pose command names is drawn straight away; the game blends to it over the command's frames. No blending yet |
| Weapons on the body | Stand-in | the `PARTS2` defaults; the equipment-dependent part choice isn't read |
| Map choice | Stand-in | always map 01 (`BATTLE_MAP`); the arena's own stage pick isn't traced |
| Terrain height scale and tiles | Stand-in | height = byte × block-0 height / 255 over the used cells; one tile texture (`mapNN_a00.gim`); vertex shading from the map's light direction instead of GE lighting |
| Battle camera | Stand-in | a chase camera behind your OKE (`CBattleSceneCamera` isn't read) |
| What the robot does with the other chip orders (`CBattleOke`) | Stand-in | a 2D simulation with guessed units on a 720×720 field (the terrain's size): jumps, the special moves and the bodies without scripts move at 50 units/s and turn at 180°/s; melee 10% within 36 units, a 120 s limit. Heights don't affect it |
| HUD windows and gauges | **Real** (partly) | the `CIndicatorBase` window table at `0x003ed78c` (`FUN_089011d8` / `FUN_0890126c`), each window cut from the `bs_wind0` atlas; the status window's HP, heat and energy gauges as `FUN_089104fc` / `FUN_089106ac` / `FUN_08910860` draw them (130 px per 1000, positions at `0x003f0514`, heights at `0x003f0508`). Only windows 2 (status) and 3 (radar) are drawn. Energy isn't simulated and shows full; heat shows the simulated heat |
| Radar contents | Stand-in | `bs_radm2` dots placed by hand inside window 3, centred on your OKE; `CBattleRadarMngr` isn't read |
| Lock-on and gun sight | **Real** (mostly) | Lock-On (`FUN_088464b0`) locks a target found in range and otherwise keeps the old lock; only Cancel Lock-On clears it (`FUN_088465dc`). `CBattleOkeGunSightController` (`+0xb470`) at 30 Hz: the tracker (`FUN_0883baa4`) gives the locked target's direction and distance (200 without a lock, the sight then holds its last yaw, straight ahead at first); the sight turns half the remaining angle a frame, at most 30° (body table `+0x50`), with up to 50% extra while it keeps turning one way, aiming one frame ahead (`FUN_0884a1c8`, `FUN_0883b300`, `FUN_0883b214`). The HUD mark eases after the aim point at (0.5 + momentum) of the way, at most 20 a frame (`+0x54`, `FUN_0884a4d8`): `gunsite` while locked, `gundot` otherwise |
| Reticle chips and lead | **Real** (mostly) | Start Reticle (`FUN_0883be40`) readies weapon 1-5 (8: broken off, 9: empty slot), 5 puts it away and resets the sight, 6 readies the next usable one. "Current weapon" in the fire chips means the readied one; with none they fail with 0x11. Move Reticle (`FUN_0883c0d4`) sets the sight's goal yaw (0.01°, ±180, relative with b2 = 1) and holds the tracker off for 2 s; it is ignored while firing in mode 2 or 3. Shoot Reticle (`FUN_08848288`) fires at the sight's HUD point (mode 1), Select and Fire at the target itself (mode 2), Firing Direction along its angle (mode 3) (`FUN_0883c298`). The gun rises while the fire control aims, at 2 × body `+0x38` / 100 per second, and drops at once otherwise (`FUN_088509bc`). Once fully raised, with the chip's spread setting 0, 1 or 4 (table `0x08cce2c0` is 0 there), the shot leads: target + its velocity × distance / bullet speed, one pass (`FUN_0883e2c4`). In mode 1 the lead goes into the sight, in mode 2 into the shot. The fire control stays aiming until the weapon is put away |
| Pitch, muzzle cone, part targeting | Stand-in | pitch isn't simulated (shots have no height), so the reticle's pitch is ignored and the mark's height is a stand-in. Shots leave from the body's centre, not the muzzle (`+0x30` of the slot), and aren't limited to the weapon's aiming cone (`DAT_08cce2d8`, `FUN_08810edc`) or its turret limits. Selecting a target part (weapon mounts) isn't ported. The spread values themselves (table `0x08cce2c0`) aren't applied as scatter |
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
- **The robots**: pose blending, the movement modifiers from the hardware stats,
  jumps, flight and altitude, ground height and slopes in the physics, weapon parts by
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
