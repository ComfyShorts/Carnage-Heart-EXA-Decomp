# Battle Mode

Choosing **Battle Mode** on the title's mode menu now runs the game's own Battle Mode
screens (see `docs/screens.md`): the top menu, Battle, then either the **Test
Program** (Enemy and Friend teams of up to three OKE cards, the map, test time,
"Start battle?") or the **Battle Arena** (one of the game's 100 opponents, one of your
Team Formation teams, "Start Battle Arena?"). The battle then starts from those
choices:

- each side's OKEs, up to three: the chosen cards, your team card's members, or all of
  the arena opponent's OKEs (the record's OKE files), each on its formation spot;
- the map: the chosen stage, or the opponent's, through `CPresetStage`'s table
  (`0x08c21854`, field 0 = `land/mapNN`); "Select Randomly" picks one of 00-24;
- the time limit: the Test Time, or the opponent's (`+0x114` of its record).

"BATTLE START", then every robot runs its own chip program until one team has no OKE
left or time runs out. WINNER, LOSER or DRAW follows, and circle goes back to the screen
the battle was started from. The cards you can field are the save's 32 (blank for a
new game, or the ones of the Battle Data loaded with Continue, see "Save data" in
`docs/screens.md`) and the two simulation OKEs. A saved card's record goes to the
battle as it is in the save, copied when the battle starts, so changes made in OKE
Configuration (body, CPU, armour, weapons and ammo, options; see `docs/screens.md`) are
in the next battle. `--make-test-save` writes a save with twelve preset OKEs, two
teams and 5000 Victory Points to try it. The Shop (`docs/screens.md`) sells the parts
the save doesn't own yet (on a new Battle Mode game bodies 14 Hades and 33 Nidhogg)
and maps; what is bought is in OKE Configuration's lists, and so in the next battle,
and bought maps in the Test Program's map list.

Code: `src/game/battle.c` (screens and drawing), `src/game/battle_sim.c` (the
simulation), `src/game/gmo.c`, `okemodel.c`, `terrain.c` (models and field),
`tests/battle_test.c` (headless battles). Screenshots:

```sh
build/pc-m32/chexa-pc --disc DIR --startup --press 70 circle --press 90 down \
    --press 110 circle --press 130 circle --shot 170 orig/bm_top.png  # Battle Mode
build/pc-m32/chexa-battle-test --mine preset/arena/oke_b15_1.bin \
    --theirs preset/arena/oke_b04_1.bin --trace 40   # one battle, first 40 chips
build/pc-m32/chexa-battle-test --arena -v            # all 2450 arena pairings
build/pc-m32/chexa-battle-test --teams -v            # 3-on-3: arena teams 51-75 vs 76-100
# a 3-on-3 arena battle with the test save's Team 01 (R switches the watched OKE)
C="--press 70 down --press 90 circle --press 110 circle"   # Continue -> top menu
build/pc-m32/chexa-pc --disc DIR --saves orig/teamtest --startup $C --press 190 circle \
    --press 210 down --press 230 circle --press 250 down --press 270 circle --press 300 circle \
    --press 320 down --press 340 circle --press 370 circle --shot 1400 orig/tm_battle1.png \
    --press 1500 r --shot 1530 orig/tm_battle_watch.png --frames 1540
build/pc-m32/chexa-pc --disc DIR --startup --oke 8 --shot 20 orig/oke.png   # one body
build/pc-m32/chexa-pc --disc DIR --startup --map 15 --oke 3 --shot 20 orig/map.png
build/pc-m32/chexa-pc --disc DIR --startup --gmo land/map01/map01_sky.gmo --shot 20 orig/m.png
```

## What is the game's and what isn't

| Part | Status | Source |
|---|---|---|
| Robot behaviour | **Real** | `pgchip.c` (`CBattleOkePgChip`) runs the programs stored in the preset records |
| Edited programs | **Real** | A program changed in Software Setup (`docs/screens.md`), chip parameters and ranges included, is written into the save's card; the battle copies the card when it starts and the same interpreter runs it. `chexa-chip-edit-test` fights a two-chip program made with the editor's functions and checks the chip history holds only those two chips |
| Chip speed | **Real** | per CPU part, table at `0x08bd5f88` (`FUN_08856704`) |
| Scan and aim distances | **Real** | `PGCHIP::CRange` (`FUN_089fc37c`), tables at `0x08c1cab4` / `0x08c1cb7c` / `0x08c1cbd0`. A range is a sector (b2 bit 0 clear) or, with bit 0 set, a rectangle in the OKE's frame (`FUN_08841bd8` → `FUN_08860408` → `FUN_0887eab8`): centred dist(b4) to the right and dist(b5) ahead (signed), dist(b3) wide and dist(b2 bits 1-7) deep. The search's distance cap (`+0x9f8c`) isn't applied; the obstacle and area scans still treat every range as a sector |
| 3D models | **Real** | `gmo.c`: GMO reader (bones, parts, meshes, GE vertex arrays, materials, embedded GIM textures) |
| OKE bodies | **Real** | `okemodel.c`: body type at hardware `+0x240` (`FUN_089ed210`), `COkeFile` table at `0x0041666c` (`FUN_089f2bc4` / `FUN_089f2c20` / `FUN_089f2c7c`), parts placed by the `PARTS2` bone tree of `okeedit/TYPE.txt`, paint file textures (`CBattleOkeSetupBody`, `FUN_0885a470`) |
| Terrain | **Real** (mostly) | `terrain.c`: `CStageGenerator` (`FUN_08aa610c`) and its LANDGEN passes from `land/param/mapNN_a.mpp`, see "Terrain" below |
| Sky | **Real** | `land/mapNN/mapNN_sky.gmo`, drawn around the camera |
| Paint scheme | Assumed | hardware `+0x244` (0-3 in all presets), set by OKE Customization's Design (`CPaintWnd`); its reader in battle isn't traced yet |
| Colours, polish and emblem | Not drawn | hardware `+0x248` / `+0x24C` main and accent colours (R \| G<<8 \| B<<16 \| A<<24, 0-100 each, -1 unset), `+0x250` polish 0-4, `+0` / `+0x200` the 32 x 32 emblem and its 16 colours, `+0x254` the emblem's places (bits 0-2). OKE Configuration edits them all (`docs/screens.md`); the battle models don't use them yet |
| Walking and turning | **Real** (mostly) | `oke_script.c`. Move, Rotate, Quick Movement and Quick Rotate start the body's `!SCRIPT` motions as `CBattleOke` does (`FUN_08844450` / `FUN_088445b0` / `FUN_08844940` / `FUN_08844a00`, script tables at `0x08bd2414` and `0x08bd2568`). `CBattleOkeScript::Update` (`FUN_088597e8`) runs the commands: 0x200-0x203 pushes of value × 67.5 (`0x08bd6930`) for frames / 30 s, 0x204/0x205 turns of value × 2.618 rad/s² (`0x08cce2d0`, set to `0x40278d36` by a static initializer), 0x209 loops (`FUN_08858fd8`), poses. The physics step follows `FUN_0884df08`: the pushes add up into an acceleration, a 12/s brake works while no push is active, velocity is damped by exp(-0.3 t) (`+0xb6f4`) and turning by exp(-1.0 t) (`+0xb714`). Move and Rotate end when the observer's distance or angle is covered (amount - 0.5, `FUN_0883aaec`) |
| Weapons: loadout, firing, bullet speed | **Real** (partly) | each OKE carries the five weapon slots of its hardware block (ids at `+0x258`, ammo at `+0x26c`, `CHardwareDataBase` `FUN_089ed2dc` / `FUN_089ed3a8`, copied as in `FUN_0883d4bc`'s caller). Select and Fire / Firing Direction (`FUN_08845038` / `FUN_088452c8`) pick slot 1-5 (or 0, the current weapon) and start its weapon script `0x2a + slot`; the script's 0x20b command pulls the trigger (`FUN_0885910c` → `FUN_08848a68`) and the chip's count of shots goes out, each spending one round. Shot speed is `CBattleOkeEquipment`'s (`FUN_08856528`): weapon table at `0x08bd5574` → bullet table `0x08c69c74` or dispenser table `0x08bcb8fc`, `+0x0c` |
| Damage and heat from hits | **Real** (mostly) | each shot carries the hit record `CBattleBulletManager::ResolveCollisionRecords` (`FUN_08813fa4`) builds: attack, hit heat and flags from the bullet table at `0x08bcb1a4` (clusters and missile dispensers report their child bullet's numbers, as `GetWeaponAttack` `FUN_08856480` does), weakened by 60 attack / 120 heat per second once the shot is older than the bullet's `+0x2c` (floor 20%). Bullets fly until they hit or get 500 units from the centre on any axis (the bullet update around `0x08812c00`); there's no time limit, and the fuse (`+0x1c` time, on when `+0x20` > 0) is off for every bullet. A finite `+0x28` makes a bullet ballistic from its first frame (the value itself isn't a lifetime): it slows by `+0x18` a second down to half its base speed `+0x0c` (shotguns, napalm and other guns: 220 → 110 at 20/s), falls by `+0x30` × gravity and homes by `+0x24` (0 for every bullet). Shotguns fire `GetWeaponNum` pellets (`0x08bd5a24`). Mine dispensers (`CBattleDispenser`, launch `FUN_08815948`, update `FUN_088167a8`) throw a mine at the dispenser row's `+0x2c` units per frame (velocities are per frame, damped ×0.95 a frame, so about 50 units out). Each 30 Hz frame it looks for the nearest enemy within `+0x30` across the ground (`FUN_088151f8`). With `+0x20` = 0 (land mines) it rolls at the target, 1.5 a step, and hits on contact with the row's attack and heat (`+0x24` / `+0x28`). Otherwise it fires `+0x10` of its child bullet `+0x0c` at the target every 0.5 s, `+0x14` times, then is spent. Aerial mines (`+0x18` = 14) hover out of reach and don't hit by contact. The receiver follows `CBattleOke`'s hit handler (`FUN_0884d284`): heat × 0.7 for armour type 4; damage × 0.7 when the armour type matches the flag (1/bit 0, 3/bit 1, 2/bit 4) and when defending, then `d - d × armour/100 - armour × 0.5` (armour value from the table at `0x08bd5ed8` for the part at hardware `+0x280`), at least 1, off HP (`FUN_0883d7f4`). Max HP and heat limit come from the body table at `0x08c14d84` (`+0`, `+0x10` × (200 - hardware `+0x29c`) / 100). Firing heats the shooter by `GetWeaponShotCalorie` (`0x08bd5a30`); heat is capped at 120% (`FUN_0883d960`). Status Assessment reads HP / heat as the game's rounded percentages (`FUN_088462a4`, `FUN_0883fc04`, `FUN_08a184f0`) |
| Hit boxes and part armour | **Real** (mostly) | the `BBOX2` lines of `okeedit/TYPE.txt` (bone, class, centre, size: `CBattleOkeSetupBody` copies them into `CBattleOke`'s 32 slots at `+0x98b0`, `FUN_0885a2c8` / `FUN_088496a0`) placed on the posed bone tree. Each shot's step is swept against them in half-unit samples; the class gives the damage and heat factor (`0x08bd1814`: 1.0 / 0.7 / 0.1). Each bone starts with armour (`+0xa010`): twice the HP times its share of the class-weighted box volume (weights 1.5 / 3 / 5 at `0x08bd1820`), plus 180 per `WEAPON2` mount on it, plus 5 × the armour value. Hits wear it down (`FUN_088412b0`); at 0 the part breaks off (no longer drawn), hits there skip the armour and class factors, and a weapon mounted there can't fire (`FUN_0883dd78`) |
| Hit reactions | **Real** (partly) | from `FUN_0884d284`: the stagger value from the body table (`+0x30`, doubled while defending); a hit above it knocks down 1 time in 8 (always above 3×), otherwise flinches; smaller hits flinch 1 time in 20. A flinch plays script 0x57-0x5a by the side the shot came from (`FUN_08843c90`); a knock-down plays 0x5b. Orders are refused meanwhile. Knock-back pushes along the shot, turned up to 30° towards the side hit (`FUN_08842a94`), d / 3t × 2500 for one 30 Hz physics step, plus a yaw kick from the lever arm capped at 100 |
| Hits: what's still approximate | Stand-in | boxes are tested as vertical columns (shots have no height); the knock-down ragdoll (`CRagdoll`) is a 2.5 s tilt; the flinch side's mirror and the spin's sign aren't checked against the game; broken parts vanish instead of flying off as their `_b` models; shields (option type 9) and the game options that can turn knock-downs and part breaking off aren't read. Only shotgun pellets spread (stand-in); single shots fly straight with the bullet's speed jitter (`+0x10`, real). Heat cools at 8% of the limit per second and has no effect; fuel reads 100%. Gravity (hand grenades: `+0x30` = 49) and hitting the ground aren't simulated, so grenades fly flat; a burst fires every 0.1 s; melee takes 10% of max HP. Mines: the land mine's pop-up within 12 units and its blast effect are a plain contact hit, the aerial mine's height search (targets 8 below) is taken as always true, and mines last until they hit or are spent |
| Movement modifiers | Stand-in | `FUN_08858c8c` / `FUN_08858d44` scale each command by the OKE's performance (legs, weight); taken as 100%, so every body of a type moves alike. Commands 0x206-0x208 and 0x20a-0x210 (pose speed, waits, altitude, loop mode) are skipped |
| Poses | Partly | the pose each script's pose command names is drawn straight away; the game blends to it over the command's frames. No blending yet |
| Weapons on the body | Stand-in | the `PARTS2` defaults; the equipment-dependent part choice isn't read |
| Map choice | **Real** | the stage picked on the Test Program's map screen (the stages the save opens, map state 1; the Shop sells more), or the arena opponent's stage, through `CPresetStage` (`FUN_08a35844`, table at `0x08c21854`). Map size and barricade are chosen but not simulated (the field stays 720×720) |
| OKE choice | **Real** | the cards chosen on the Test Program's team windows (up to three a team), the members of the Team Formation team chosen in the arena (the save's `COkeDataBase` records as OKE Configuration left them, or the simulation OKEs from the pack), and all of the arena opponent's OKE files (`+4` count, `+0x14` + 0x40 n) |
| Teams and spawn positions | **Real** | up to three OKEs a side (`TEAM_SIZE`, `CTeamDataBase`'s three `CFormationDataBase`). Placement as `CBattleScene` `FUN_08869a28` does it: each member's formation spot (`+0x24`: 0-8) is a point of the 3 × 3 grid at `0x08bd7d10` (x ±60 / 0, z -60 / -120 / -180), scaled by map size / 540; the enemy team (the game's team 0) keeps the point and faces the centre, yours takes it mirrored (-x, -z) and turned round. A member without a spot takes the free ones in the order at `0x08c3e228` (4, 3, 5, ...: the middle of the second row, then its sides). Arena opponents' spots come from their record (`+0xD4`). The game's nudge of 16 units away from obstacles near a spot isn't done |
| End of the battle and the winner | **Real** (partly) | the judge (`FUN_088689c8`) ends the battle once a team has no live OKE (`FUN_0881f908`), or at the time limit. A team with none left loses. Otherwise the winner is chosen by the Test Program's default condition (`CBattleParam +0x14` = 0, "Durability Left %", string 0x4B): the team with the higher average HP %, destroyed OKEs counting 0; equal is a draw. The other conditions (Remaining OKEs, OKEs & Durability, Defeat Leader) and the arena opponents' own conditions aren't chosen yet, and the leader isn't treated specially |
| Targets with several enemies | Stand-in | `CBattleOke` keeps the target its search chips found (`FUN_08841bd8`); the port takes the nearest live enemy in the chip's range for the search, lock-on, Select and Fire and Auto Rotate chips and keeps it as the target, else the nearest live enemy. Friendly fire is off: shots and mines only hit the other team. With one OKE a side the battles are exactly as before (the 200 preset battles give the same results with the old spawn distance) |
| Watched OKE | Stand-in | the camera and the HUD follow your leader; L and R step through every OKE on the field (the port's buttons; `CBattleSceneCamera`'s own switching isn't read) |
| Time limit | **Real** | Test Time (30-300 s, `CBattleParam +8`) or the arena record's `+0x114` (90-120 s); `Battle.time_limit` |
| Terrain height scale and tiles | Stand-in | height = byte × block-0 height / 255 over the used cells; one tile texture (`mapNN_a00.gim`); vertex shading from the map's light direction instead of GE lighting |
| Battle camera | Stand-in | a chase camera behind the watched OKE (`CBattleSceneCamera` isn't read) |
| What the robot does with the other chip orders (`CBattleOke`) | Stand-in | a 2D simulation with guessed units on a 720×720 field (the terrain's size): jumps, the special moves and the bodies without scripts move at 50 units/s and turn at 180°/s; melee 10% within 36 units. Heights don't affect it |
| HUD windows and gauges | **Real** (partly) | the `CIndicatorBase` window table at `0x003ed78c` (`FUN_089011d8` / `FUN_0890126c`), each window cut from the `bs_wind0` atlas; the status window's HP, heat and energy gauges as `FUN_089104fc` / `FUN_089106ac` / `FUN_08910860` draw them (130 px per 1000, positions at `0x003f0514`, heights at `0x003f0508`). Windows 2 (status, for the watched OKE), 3 (radar) and 4 are drawn. Energy isn't simulated and shows full; heat shows the simulated heat |
| Team-mate window | **Real** | `CIndicatorFriend` (`FUN_08904200`): in window 4, the watched OKE's two team-mates, the next member first, each with its member mark from `bs_memark0` (16 × 18, cut at `0x08bf290a`), an HP bar from `bs_hpguage` (130 px for 100%, 12 high, `FUN_08904178`) and its name, at the rows of `0x08bf28fe` + `0x08bf28f2`; picture numbers 0x29 / 0x2a of the battle scene's list (`0x08c7a398`). Only drawn when the team has more than one OKE. The name's font is the port's |
| Radar contents | Stand-in | a `bs_radm2` dot for every OKE, placed by hand inside window 3 and centred on the watched OKE: white for it, orange for its team, blue for the other, grey when destroyed. `CIndicatorRadar` (`FUN_08909a98`) isn't read |
| Lock-on and gun sight | **Real** (mostly) | Lock-On (`FUN_088464b0`) locks a target found in range and otherwise keeps the old lock; only Cancel Lock-On clears it (`FUN_088465dc`). `CBattleOkeGunSightController` (`+0xb470`) at 30 Hz: the tracker (`FUN_0883baa4`) gives the locked target's direction and distance (200 without a lock, the sight then holds its last yaw, straight ahead at first); the sight turns half the remaining angle a frame, at most 30° (body table `+0x50`), with up to 50% extra while it keeps turning one way, aiming one frame ahead (`FUN_0884a1c8`, `FUN_0883b300`, `FUN_0883b214`). The HUD mark eases after the aim point at (0.5 + momentum) of the way, at most 20 a frame (`+0x54`, `FUN_0884a4d8`): `gunsite` while locked, `gundot` otherwise |
| Reticle chips and lead | **Real** (mostly) | Start Reticle (`FUN_0883be40`) readies weapon 1-5 (8: broken off, 9: empty slot), 5 puts it away and resets the sight, 6 readies the next usable one. "Current weapon" in the fire chips means the readied one; with none they fail with 0x11. Move Reticle (`FUN_0883c0d4`) sets the sight's goal yaw (0.01°, ±180, relative with b2 = 1) and holds the tracker off for 2 s; it is ignored while firing in mode 2 or 3. Shoot Reticle (`FUN_08848288`) fires at the sight's HUD point (mode 1), Select and Fire at the target itself (mode 2), Firing Direction along its angle (mode 3) (`FUN_0883c298`). The gun rises while the fire control aims, at 2 × body `+0x38` / 100 per second, and drops at once otherwise (`FUN_088509bc`). Once fully raised, with the chip's spread setting 0, 1 or 4 (table `0x08cce2c0` is 0 there), the shot leads: target + its velocity × distance / bullet speed, one pass (`FUN_0883e2c4`). In mode 1 the lead goes into the sight, in mode 2 into the shot. The fire control stays aiming until the weapon is put away |
| Pitch, muzzle cone, part targeting | Stand-in | pitch isn't simulated (shots have no height), so the reticle's pitch is ignored and the mark's height is a stand-in. Shots leave from the body's centre, not the muzzle (`+0x30` of the slot), and aren't limited to the weapon's aiming cone (`DAT_08cce2d8`, `FUN_08810edc`) or its turret limits. Selecting a target part (weapon mounts) isn't ported. The spread values themselves (table `0x08cce2c0`) aren't applied as scatter |
| Names, timer | Stand-in | plain text where the game has its top windows: the watched OKE's name, the other team's name, its OKEs left and durability |
| Parts the player owns | **Real** | the save's parts stock (`+0x47BBC`, 2 = owned): OKE Configuration offers only owned parts, the Shop sells the rest for Victory Points (`+0x48394`); a new Battle Mode game owns all but two bodies (`FUN_08918d5c`) |
| Victory Points from battles | Missing | the game adds them after battles (`FUN_0889d308`, called from `FUN_08808f98` and `FUN_0881da8c`); the port doesn't yet, so the money only comes from the save |
| Selection screens | **Real** (partly) | the game's Battle Mode, Battle, Test Program and Battle Arena screens; their stand-ins are listed in `docs/screens.md` |

The tables come from your EBOOT at run time (`--elf`), so nothing is copied into the
repository. Without it, rough defaults are used.

### The game's flow after Battle Mode

From the entrance code (`FUN_088d5510`, `FUN_088d497c`, `FUN_088d5a70`,
`FUN_088d61b8`, `FUN_088d317c`):

- Phase 9: a yes/no dialog ("there are no tutorials in Battle Mode"), skipped when a
  flag at `+0x74` of a game-state object is set. Ported (always asked).
- Phase 7: the player's name on the system on-screen keyboard (sceUtilityOsk). Not
  ported: skipped.
- Phase 0xB: fade to black, then the entrance ends; `FUN_088d317c` →
  `FUN_088d3d80` mode 4 makes the scene `FUN_0882fba4`, which runs `CBattleModeFlow`
  (`FUN_088372a0`) and the scene behind it (`FUN_0887459c`, not drawn).
- `CBattleModeFlow`'s sub-flows (`FUN_08837c64`): 0 `CDesignFlow`, 1
  `CTeamFormationFlow`, 2 `CBattleSelectFlow` (ported), 3 `CShopFlow` (ported), 4 system.
  `CBattleSelectFlow`'s (`FUN_08880bc4`): 3 `CBattleTestFlow` (ported), 4
  `CBattleMissionFlow`, 5 `CBattleArenaFlow` (ported), 6 `CMatchMakeFlow`.
- The battle itself is `CBattleScene` / `CBattleSceneMngr` (file set "battlescene":
  3D effect models `effect/*.gmo`, HUD art `parts/bs/*`), with `CBattleOke` behind
  each robot.

## How the presets behave in the stand-in

`chexa-battle-test --teams` fights the three-OKE arena teams 51-75 against 76-100 on
their own formation spots (25 battles; the ctest `battle_teams`): a few end with a team
destroyed, most go to the time limit and the durability judge, and many are draws for
the reasons below.

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

## Battle sounds

The battle bank (`sound/se/seset_01bat`, effects `SE_BTL_020`..`099`) is loaded when a
battle starts, as the game does (FUN_08b26998 with slot 1), and freed when the result
screen is left. The simulation reports sound events through `Battle.sound`
(`battle_sim.c`), and `battle.c` places them for the camera, using the game's 3D volume
(`CSound::seCalc3DVolume`, FUN_08b37174): full volume within the row's +0x2C units of
the camera, then less by +0x28 per unit; the pan is the direction across the view.

| Effect | Numbers | From |
|---|---|---|
| gun report | 0x1B..0x26 by weapon (FUN_0884cc40, called with the weapon id from the trigger FUN_08851d30): weapons 1-3 → 0x1B, 4-6 → 0x1C, 7-8 → 0x1D, 9-10 → 0x1E, 11-13 → 0x1F, 14-16 → 0x20, 17-18 → 0x21, 19-20 → 0x22, then the missile and rocket groups 0x23..0x25 and 0x26 for 49-59 | once per trigger pull (not per pellet) |
| launch | word 0 of the sub-bullet rows at `0x08c6940c` (0x1C each) by bullet: 54 or 55, -1 for most | the shot's start (FUN_08811580) |
| impact | +8 of the bullet's row at `0x08bca5a8` (0x28 each): 30, 40-45, 48 | a shot hitting an OKE (FUN_08813fa4 → FUN_0881062c) |

Not played yet:

- shots hitting the ground or obstacles (FUN_0881074c / FUN_0881086c, the other column
  of the impact table) and scenery destroyed (0x34): the port has no ground hits or
  obstacles;
- mines and dispensers (`0x08bcb8e4` + row × 0x34, FUN_08816014), their child shots;
- the body's movement sounds (walking, jumps, landing, turning: FUN_089f224c ..
  FUN_089f293c, tables at `0x08c83f2c` / `0x08c83f30` by body), explosions and
  wrecks (0x2E, FUN_08842fa4), the system effects used in battle (`SE_BTL_080`,
  `092`..`096`), and the HUD and alert sounds;

The battle music plays: at the start the battle list's next track (CBgmPlayer
FUN_0888dfb0, called from the battle scene FUN_088754d8; Battle Mode's battles don't
name a track of their own), and Battle Mode's menu music again after the result
screen. See "Music" in `docs/screens.md`.

## Missing

- **The battle field**: scenery models (`mapNN_nature*.gmo`, `piece_*`, `distant*`),
  obstacles, the `.bbx` collision boxes, map size and barricades, and CLandTexture's
  tiles.
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
- **Sound**: the gun reports, launches, hits and the music play ("Battle sounds"); the
  movement, explosion, ground-hit and HUD sounds don't.
- **Behaviour**: `CBattleOke` (hardware stats: legs, weapons, armour, engine, heat,
  ammunition; flying types; collision) isn't ported, so who wins says little about the
  real game.
- **Flow**: name entry, the parts of OKE Configuration listed as stand-ins in
  `docs/screens.md` (in Software Setup: the range editor's 3D view and number pad, drag / pull, macros, the test program; note templates, Snapshot), the colours, polish and emblem on the battle models,
  the Shop's stand-ins (`docs/screens.md`), Victory Points won in battles, missions and
  matchmaking.
