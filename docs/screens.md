# The first screens

With `--startup`, the main loop now runs the root screen's first child, the entrance
(`src/game/entrance.c`). It draws the title screen and its two menus through CRender's
render list, using the game's own images from the "entrance" file set.

## What draws

| Screen | State |
|---|---|
| Company logo (`title/logo_a.gim`, ARTDINK) | Drawn for one frame at (112, 72), as in the game. An opaque black window is drawn over it the same frame, so the frame shows black (see below) |
| Opening movie (`movie/che_op.pmf`) | Skipped and logged. There's no MPEG player yet. The publisher and developer logos you see on a PSP are most likely in this movie |
| Title: `ti_back` in 4×2 tiles, `ti_title` at (70, 12) | Matches the original images |
| Top menu: Start Game / Continue / Network Play | Buttons, text, the fade and slide-in from the left, cursor highlight (additive), up/down |
| Mode menu: Story Mode / Battle Mode | Circle on Start Game slides the top menu out to the right and this one in from the left. Cross goes back |

Battle Mode asks phase 9's question ("There are no tutorials in Battle Mode. Start
anyway?", design string 0x11) and on Yes opens the Battle Mode screens below. Choosing
Story Mode, Continue or Network Play only logs a line: the phases after the menus
(0xC, 6) aren't ported.

### Why the logo frame is black

`CEntranceMngr` phase 3 lasts one update (`FUN_088d5bec` pops it and opens phase 4).
Its draw (`FUN_088d63b4`) draws `logo_a` first, then the window stack, and phase 3
pushed the full-screen colour window (`+0x68`, 480×272) with colour (0, 0, 0, 255).
`FUN_0888f264` fills the rectangle with that colour × opacity (1.0), which covers the
logo. The port follows that order. If the colour window's opacity turns out to be
animated somewhere not yet read, this would change.

## Classes

| Class | Functions | In the port |
|---|---|---|
| Root screen | ctor `FUN_088f1d98`, update `0x088f1f64` | Black clear colour, starts the entrance (in `startup.c`'s main loop). The debug menu it opens afterwards isn't ported |
| Entrance screen (0x44) | ctor `FUN_088d2d30`, update `FUN_088d36e4`, `FUN_088d3560`, `FUN_088d35a4` | States 0–2: render node at priority 2, "entrance" file set, then `CEntranceMngr`. `CSystemMngr` (mode 0xD, the `CSaveMngr` start-up check) is skipped |
| `CEntranceMngr` | init `FUN_088d6b40`, update `FUN_088d6360`, phases `FUN_088d4efc`, opens `FUN_088d448c`, draw `FUN_088d63b4`, title `FUN_088d6524` | Phases −1, 3, 4, 0, 5 and 2 |
| `CEntranceWnd` (0x104) | create `FUN_088d7104`, update `FUN_088d7778`, input `FUN_088d7ac0`, draw `FUN_088d7efc`, open `FUN_088d7ce0`, close `FUN_088d7330`, done `FUN_088d7cac` | All of it. The cursor pulse (`FUN_0888f3d8` family) is approximated by a sine between 0.3 and 1 |
| Colour window (0x70) | `FUN_0888eb80`, draw `FUN_0888f264`, colour `FUN_0888ec28` | Filled rectangle |
| `CWndStack` | push `FUN_08ae6234`, pop `FUN_08ae637c`, draw `FUN_08ae66d4` | Push/pop with active window, draw in order |
| Parts cache | `FUN_0888763c`, `FUN_08887834`, names at `0x08c79468` | `ui.c`: the names are read from your EBOOT's table (the five title entries are known without it). `parts/bf/%s.gim`, or `%s.gim` for names with a slash |
| `CPicture` | draw `FUN_08b08c78`, `FUN_08b08afc`, `FUN_08b08c14`, blend `FUN_08b08da4` (table `0x08c8d01c`) | `picture.c` |
| Texture manager / GIM | `FUN_08b1fd84`, `FUN_08b1fcf8`, `FUN_08b1fa24`, bind `FUN_08b581c4` | `picture.c`: idx4/idx8 with 8888 palettes, swizzled or linear, frames |
| `CFontMngr` / `FSTRING` | `FUN_08b14f70`, `FUN_08b15fb0` (size), `FUN_08b161d0` (draw), `FUN_08b17054` (char class) | `font.c`: single-byte glyphs from `gothic8.gim`, layout mode 0. Shift-JIS glyphs (`gothic16.gim`) only advance |
| `CDesignString` | `FUN_088c09bc`, table at `0x08c7a868` | `gamedata.c` reads the strings from your EBOOT at run time (`--elf`, default `orig/EBOOT.elf`). Without it the menus draw without text |
| Pad | `FUN_08b1ed50`, queries `FUN_08b0e974` / `ea34` / `ea94` / `eaf4` | `pad.c`, with a fixed repeat delay instead of the game's tables |

## Battle Mode screens

`src/game/battle_menu.c`, drawn with `src/game/ui.c`. After phase 9, the entrance hands
over to the scene `FUN_0882fba4` (mode 4 of `FUN_088d3d80`), which runs
`CBattleModeFlow` (`FUN_088372a0`). From there the port follows the game's flows:

| Screen | Game | Real | Stand-in |
|---|---|---|---|
| Phase 9 question | `FUN_088d4c84` / `FUN_088d5eb8` | Text (design string 0x11, "Yes" 5), circle on Yes goes on, No or cross stays | Dialog layout; "No" comes from `CBattleTestString` 32 (the dialog class `FUN_088c0ba0` isn't in the export). The question is always asked (no save data says the tutorials were seen) |
| Phase 7 player name | `FUN_088d497c` / `FUN_088d5a70` (sceUtility OSK) | | Skipped: no on-screen keyboard; the name isn't taken |
| Battle Mode top menu | `CBattleModeMngr::Initialize` `FUN_08838aa0`, help `FUN_088387a4` | Items from `0x08c76be0` (1-5), buttons `bf_scmb2-6` / `bf_scml2-6` (parts 0x87 + id, 0x8D + id), text id + 4 and help cursor + 0x10 from the table at `0x08c7a6b0`, menu at (-8, 48), 32 px pitch | Only Battle opens its flow; OKE Configuration, Team Formation, Shop and System Menu log a line. The picture on the right (`bf_ttl(id+1)_pic`) and its place are the port's reading; the 3D scene behind the menus isn't drawn (`bf_back` instead). The game's "back to the title?" question on cross isn't ported |
| Battle (select) | `CBattleSelectMngr::setupTopMenu` `FUN_088838f8`, help `FUN_088832a0` | Test Program, Battle Arena, Matchmaking (Mission is left out in game mode 2), strings from `0x08c79410` | Matchmaking (`CMatchMakeFlow`, network) logs a line |
| Test Program | `CBattleTestMngr` `FUN_08827c98`, `setupTopMenu` `FUN_08828db8` | Six items, text id + 3 and help id + 0x39 from `CBattleTestString` (`0x08c6ad50`); Start Test checks for an enemy and a friend (strings 84, 85) and asks "Start battle?" (33, Yes 31 / No 32) | Damage Settings and Conditions log a line. Test Time steps by 10 s with left/right (the game takes a typed number) within `CBattleParam`'s 30-300 s; it starts at the param's default of 30 s |
| Select OKE to Test | team windows `CTeamCardListWnd`, cards `COkeCardSelectMngr` | Enemy / Friend, L/R or left/right changes team, square unregisters, a blank card gives string 44. The cards are what a new game holds (`CCarnageHeart::Reinitialize` `FUN_0889f260`): 32 blank cards and the two simulation OKEs (`preset/oke_t01_1.bin`, `_2.bin`) | One OKE per team (the battle is one against one), so no formation; card labels ("Simulation OKE 1", "01 -") and the windows' layout are the port's |
| Map | `setupFieldMenu` `FUN_088291cc`, `CStageSelectWnd` | Select Map lists stages 0-25 in the game's order (`0x08c3902c`) with their names (`0x08c852f8`) and previews (`0x08c38fc4` → `bf_mpiNN`), "Select Randomly" picks a map at random; Map Size (180/360/540 m) and Barricade (strings 14-19), help 0x44-0x46; the default stage is 2 (`FUN_08824084`), the default size 540 m (`FUN_0887e190`) | One stage instead of the game's several (`+0x6c4` flags); locked stages aren't checked; size and barricade are kept but the simulation doesn't use them |
| Battle Arena | `CBattleArenaMngr` `FUN_0880a310` / `setupTopMenu` `FUN_0880d424` | Select Opponent, Select Team, Start Battle Arena, strings from `0x08c69338`; the 100 opponents (names `0x08c518f0`, records of 0x3C0 bytes at `0x08c51c10`, read as `FUN_08807dbc` does): stage (`+0`), OKE count (`+4`), OKE files (`+0x14`, 0x40 each), time limit (`+0x114`, → `CBattleParam +8`); "Start Battle Arena?" (36) | Select Team picks one card instead of a Team Formation team (a new game has no teams and the game would refuse, string 26); all 100 opponents are offered (unlocks and Victory Points aren't read); the opponent's leader (its first OKE) fights alone; the rules (banned parts) aren't checked |
| Battle | `CBattleTestFlow::buildBattle` `FUN_08823774`, `CBattleArenaFlow::CImpl::buildBattle` `FUN_088098e0` | The battle starts with the chosen OKEs on the chosen stage's map (`CPresetStage` entries at `0x08c21854` (0x14 bytes each), field 0 = `land/mapNN`) and time limit. After it, circle returns to the screen it was started from | See `docs/battle.md` |

Shared pieces (`ui.c`): `CTopMenuWnd` (update `FUN_08ad7248`, input `FUN_08ad75ec`, draw
`FUN_08ad77ec`) with its `CBtnWnd` buttons (update `FUN_08896aec`, draw `FUN_08897248`,
select `FUN_088967c4`): up/down with hold repeat and wrap on a fresh press; the selected
button draws its second picture, growing over 0.1 s to 1/10 wider and 2 px taller; text
greyed when disabled. The open fade and 32 px slide, the title banner (`bf_ttl4` with
the latest name, where the game stacks names with `pushMenuTitle` `FUN_08826ad0`), the
help window's place (`FUN_088ffcac` makes a full-screen window; the port puts
`sd_wind0` at the bottom), the list windows and the Yes/No dialog's layout are the
port's. The key guide (`FUN_08a60304`) isn't drawn: the font has no button icons yet.

Walk-through (the screenshots were taken this way, into `orig/`):

```sh
B="--press 70 circle --press 90 down --press 110 circle --press 130 circle"  # -> top menu
build/pc-m32/chexa-pc --disc DIR --startup $B --shot 170 orig/bm_top.png \
    --press 180 circle --shot 210 orig/bm_select.png --press 220 circle \
    --shot 260 orig/bm_test.png --frames 270
```

## GIM notes

The image and palette blocks start with a 48-byte header: format at +4, order at +6
(1 = swizzled), width and height at +8 / +0xA, bits per pixel at +0xC, pitch
alignment at +0xE, the level/frame offset table at +0x18, the pixels at +0x1C to
+0x20, the level count at +0x2A and the frame count at +0x2E. All offsets are from the
header. The texture width given to the GE is the row pitch in pixels, and the texture
size is rounded up to a power of two.

## Checking with screenshots

`--shot N FILE` saves displayed frame N, and `--press N BUTTON` holds a button for four
frames from frame N:

```sh
build/pc-m32/chexa-pc --disc DIR --startup --frames 150 --shot 2 orig/logo.png \
    --shot 60 orig/title.png --press 70 circle --shot 100 orig/menu2.png \
    --press 110 down --shot 120 orig/menu2_battle.png
```

Frame 2 is the logo frame (black), frame 4 onwards is the title. The keyboard plays
the pad: arrows, Z/Enter = circle, X/Backspace = cross, A = square, S = triangle,
Q/W = L/R, Space = start, Right Shift = select.

## Next

- The opening movie: an MPEG/PMF player (sceMpeg, sceVideocodec) to play `che_op.pmf`.
- `CSystemMngr` / `CSaveMngr`: the save-data check before the title (sceUtility).
- The phases after the menus: 0xC (new story game), 7 (the player-name keyboard),
  6 (load or network), 10 (attract after 65 s idle).
- Shift-JIS text (`gothic16.gim`, the double-byte table) for the Japanese strings.
- Sound effects for the cursor and buttons (CSound).
