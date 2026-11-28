# The first screens

With `--startup`, the main loop now runs the root screen's first child, the entrance
(`src/game/entrance.c`). It draws the title screen and its two menus through CRender's
render list, using the game's own images from the "entrance" file set.

## What draws

| Screen | State |
|---|---|
| Company logo (`title/logo_a.gim`, ARTDINK) | Drawn for one frame at (112, 72), as in the game. An opaque black window is drawn over it the same frame, so the frame shows black (see below) |
| Opening movie (`movie/che_op.pmf`) | Plays with its sound, 39.6 s: black, the Natsume logo (from about 2.5 s), the CG intro, the title card. Any button stops it, as in the game (see "Opening movie" below). `--skip-movie` leaves it out |
| Title: `ti_back` in 4×2 tiles, `ti_title` at (70, 12) | Matches the original images |
| Top menu: Start Game / Continue / Network Play | Buttons, text, the fade and slide-in from the left, cursor highlight (additive), up/down |
| Mode menu: Story Mode / Battle Mode | Circle on Start Game slides the top menu out to the right and this one in from the left. Cross goes back |

Before the title, the start-up save check runs (see "Save data" below). Battle Mode
asks phase 9's question ("There are no tutorials in Battle Mode. Start anyway?",
design string 0x11) unless the system data says Story Mode was completed, then opens
the Battle Mode screens below with a new game's cards. Continue lists the Battle Data
saves and opens Battle Mode on the one chosen. Choosing Story Mode or Network Play only
logs a line: those phases (0xC, 6 with the network screens) aren't ported.

### Why the logo frame is black

`CEntranceMngr` phase 3 lasts one update (`FUN_088d5bec` pops it and opens phase 4).
Its draw (`FUN_088d63b4`) draws `logo_a` first, then the window stack, and phase 3
pushed the full-screen colour window (`+0x68`, 480×272) with colour (0, 0, 0, 255).
`FUN_0888f264` fills the rectangle with that colour × opacity (1.0), which covers the
logo. The port follows that order. If the colour window's opacity turns out to be
animated somewhere not yet read, this would change. Opening phase 3 (`FUN_088d4a10`)
also sets up a 0.2 s fade from 255 to 0 in the manager (`+0x7C` .. `+0x91`, the time
from `DAT_08be7834`), but nothing in the entrance code reads those fields and the phase
is gone the next update, so the port leaves it out. The logos a PSP shows come from the
movie: the Natsume logo is its first picture after the black lead-in.

### Opening movie

Phase 4 (`FUN_088d5c20`) calls the movie player, `FUN_08b1d0ec("movie/che_op.pmf",
1, 0, 440, 253, 0)`, which holds the game thread until the movie ends, then opens
phase 0 (the title). The ending calls the same player with `movie/ending.pmf` and
skip mode 0 (`FUN_08968758`).

The file is PSMF version 0015: a 2048-byte header (stream offset `+8`, 0x800; stream
size `+0xC`, 0x80D000; first and last time stamps 90000 and 3645552 at `+0x56` and
`+0x5C`) and an MPEG-2 program
stream in 2048-byte packs:

| Stream | What | A decoder needs |
|---|---|---|
| Video, PES `0xE0` | H.264 Main profile, level 2.1, 480×272, 29.97 pictures a second (time stamps 3003 apart), one reference frame, no B-frame reordering (picture order type 2); an access unit delimiter before each picture, the parameter sets in the stream | An H.264 decoder taking Annex B access units, YCbCr 4:2:0 to RGBA |
| Audio, PES `0xBD` | ATRAC3plus, 44100 Hz stereo, 744-byte frames of 2048 samples (about 128 kbit/s). The payload starts with 4 bytes (channel, 0, offset of the first frame starting in the packet); each frame has an 8-byte header (`0F D0`, then rate index << 13, channels << 10 and (size − 8) / 8, then four zeros) | An ATRAC3plus decoder |

The system libavcodec the music already uses (FFmpeg, LGPL-2.1-or-later, linked
dynamically) has both decoders (`h264`, `atrac3plus`), so the movie needs no new
dependency. The demuxing, the time stamps and the colour conversion are the port's own
(`src/platform/video.c`).

The game only uses `sceMpeg` (25 imports, `src/platform/video.c`); it reads the PSMF
header itself through `sceMpegQueryStreamOffset` / `sceMpegQueryStreamSize`, and
imports nothing from `scePsmf`. The player (`src/game/movie.c`):

| Game | What | Port |
|---|---|---|
| `FUN_08b1ca28`, `FUN_08b1cb08`, `FUN_08b1b2ec` | Open the file, read the header, seek to the stream | The same, with `sceIoOpen` |
| `FUN_08b1d0ec` | `sceMpegInit`, a ring buffer of 0x3C0 packets filled by `FUN_08b1b0d8` (whole packets from the file), `sceMpegCreate` with frame width 512, the AVC stream and the ATRAC stream on channel 0 | The same |
| `FUN_08b1e274` | Four picture buffers of 0x88000 bytes (512×272 RGBA); `sceDisplaySetMode(0, 480, 272)`; two frame buffers at the start of VRAM | The same |
| `FUN_08b1df28`, soundThread `FUN_08b1bba8` | An audio queue; the thread writes 2048-sample buffers with `sceAudioOutputBlocking` on channel 2 | An SDL thread on its own sceAudio channel, eight buffers |
| readThread `FUN_08b1bef0` | `sceMpegRingbufferPut` | On the game thread, once per vertical blank |
| displayThread `FUN_08b1c5c4`, `FUN_08b1c1e0`, `FUN_08b1b7fc` | One picture per vertical blank into the frame buffer not shown (`sceDmacMemcpy`, `sceDisplaySetFrameBuf`); more than two pictures off the audio it drops or repeats one | On the game thread: the next picture when the audio clock (what the channel is playing) reaches its time stamp, dropping to the newest only when more than two behind |
| `FUN_08b1b158` | Playback starts when both queues are full | The same |
| `FUN_08b1dafc`, `FUN_08b1c8a0`, `FUN_08b1c7e0`, `FUN_08b1c984` | The decode loop: `sceMpegGetAvcAu` / `sceMpegAvcDecode`, `sceMpegGetAtracAu` / `sceMpegAtracDecode`; at the end `sceMpegAvcDecodeStop` until no picture is left | The same |
| `FUN_08b1dafc` | Skip: in mode 1 (the opening) any of start, select, the d-pad, L, R, triangle, circle, cross, square; in mode 0 (the ending) start only. `sceMpegAvcDecodeFlush`, state 10, then SE 0xE at volume 1 | The same (`pad_pressed`) |
| `FUN_08b1bffc` | When `+0x28` holds a picture, draws it over the video at (`+0x34`, `+0x38`) pulsing once a second | Not ported: both movies pass none (the opening passes (440, 253) with no picture) |
| `FUN_088d4ae0` | Opening phase 4 turns the display off and resets CRender | The display shows nothing until the first picture; CRender takes the screen back at its next swap |

The log shows `[movie] movie/che_op.pmf ended after 1184 pictures (0 dropped), 39.62 s`,
or `skipped after N pictures` when a button stopped it. `chexa-movie-test` decodes a
movie without the game (CTest `movie_decode`; `--png N FILE` and `--wav FILE` write a
picture or the sound, which are game data, so keep them in `orig/`), and `movie_skip`
checks that circle stops the opening.

## Classes

| Class | Functions | In the port |
|---|---|---|
| Root screen | ctor `FUN_088f1d98`, update `0x088f1f64` | Black clear colour, starts the entrance (in `startup.c`'s main loop). The debug menu it opens afterwards isn't ported |
| Entrance screen (0x44) | ctor `FUN_088d2d30`, update `FUN_088d36e4`, `FUN_088d3560`, `FUN_088d35a4` | States 0–2: render node at priority 2, "entrance" file set, `CSystemMngr` in mode 0xD (the `CSaveMngr` start-up check, `savedata.c`), then `CEntranceMngr` |
| `CEntranceMngr` | init `FUN_088d6b40`, update `FUN_088d6360`, phases `FUN_088d4efc`, opens `FUN_088d448c`, draw `FUN_088d63b4`, title `FUN_088d6524` | Phases −1, 3, 4, 0, 5 and 2, and 6 for Continue (a stand-in list, see "Save data") |
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
| Phase 9 question | `FUN_088d4c84` / `FUN_088d5eb8` | Text (design string 0x11, "Yes" 5), circle on Yes goes on, No or cross stays; skipped when the system data's `+0x74` (`FUN_0889c400`, Story Mode completed) is set | Dialog layout; "No" comes from `CBattleTestString` 32 (the dialog class `FUN_088c0ba0` isn't in the export) |
| Phase 7 player name | `FUN_088d497c` / `FUN_088d5a70` (sceUtility OSK) | | Skipped: no on-screen keyboard; the name isn't taken |
| Battle Mode top menu | `CBattleModeMngr::Initialize` `FUN_08838aa0`, help `FUN_088387a4` | Items from `0x08c76be0` (1-5), buttons `bf_scmb2-6` / `bf_scml2-6` (parts 0x87 + id, 0x8D + id), text id + 4 and help cursor + 0x10 from the table at `0x08c7a6b0`, menu at (-8, 48), 32 px pitch | Battle opens its flow. System Menu saves: the game opens `CSystemMngr` with its own menu (save, load, settings; `FUN_088b6cb0`), the port asks string 56 ("Save your progress?") and writes the Battle Data through `CSaveMngr`'s save path, to the file it was loaded from or `NEWDATA.CHE`. OKE Configuration, Team Formation and Shop open their screens (below). The picture on the right (`bf_ttl(id+1)_pic`) and its place are the port's reading; the 3D scene behind the menus isn't drawn (`bf_back` instead). The game's "back to the title?" question on cross isn't ported |
| Battle (select) | `CBattleSelectMngr::setupTopMenu` `FUN_088838f8`, help `FUN_088832a0` | Test Program, Battle Arena, Matchmaking (Mission is left out in game mode 2), strings from `0x08c79410` | Matchmaking (`CMatchMakeFlow`, network) logs a line |
| Test Program | `CBattleTestMngr` `FUN_08827c98`, `setupTopMenu` `FUN_08828db8` | Six items, text id + 3 and help id + 0x39 from `CBattleTestString` (`0x08c6ad50`); Start Test checks for an enemy and a friend (strings 84, 85) and asks "Start battle?" (33, Yes 31 / No 32) | Damage Settings and Conditions log a line. Test Time steps by 10 s with left/right (the game takes a typed number) within `CBattleParam`'s 30-300 s; it starts at the param's default of 30 s |
| Select OKE to Test | team windows `CTeamCardListWnd`, cards `COkeCardSelectMngr` | Enemy / Friend, each up to three OKEs (`CTeamDataBase2Use`, two teams at `CCarnageHeart + 0x40C80`); L/R or left/right changes team, up/down the slot, square unregisters, a blank card gives string 44. Members stand on the free formation spots in the game's order (`FUN_08ac8eec`). The 32 cards are the save's (`CCarnageHeart + 0x3AE80`): blank for a new game (`CCarnageHeart::Reinitialize` `FUN_0889f260`), else the loaded Battle Data's; a card is in use when its first word is set. After them, the two simulation OKEs the game loads on every start (`preset/oke_t01_1.bin`, `_2.bin`, not saved) | The slots fill in order (the game refuses a later member first with string 38; the port's cursor stops at the first empty slot, and unregistering moves the later members up); the Formation key (triangle) isn't ported; card labels (number and the card's name at `+4`, "No Name" without one, "Simulation OKE 1"), the list opening on the first used card, and the windows' layout are the port's |
| Map | `setupFieldMenu` `FUN_088291cc`, `CStageSelectWnd` | Select Map lists stages 0-25 in the game's order (`0x08c3902c`) with their names (`0x08c852f8`) and previews (`0x08c38fc4` → `bf_mpiNN`), "Select Randomly" picks a map at random; Map Size (180/360/540 m) and Barricade (strings 14-19), help 0x44-0x46; the default stage is 2 (`FUN_08824084`), the default size 540 m (`FUN_0887e190`) | One stage instead of the game's several (`+0x6c4` flags); size and barricade are kept but the simulation doesn't use them. Locked stages (map state not 1, `FUN_0889ed84`; the Shop sells more) are greyed and can't be chosen, and "Select Randomly" picks among the open ones; keeping "Select Randomly" itself always on is the port's reading |
| Battle Arena | `CBattleArenaMngr` `FUN_0880a310` / `setupTopMenu` `FUN_0880d424` | Select Opponent, Select Team, Start Battle Arena, strings from `0x08c69338`; the 100 opponents (names `0x08c518f0`, records of 0x3C0 bytes at `0x08c51c10`, read as `FUN_08807dbc` does): stage (`+0`), OKE count (`+4`), OKE files (`+0x14`, 0x40 each, `GetEnemyOke` `FUN_08886384`), their formation spots (`+0xD4`, a word each), time limit (`+0x114`, → `CBattleParam +8`); "Start Battle Arena?" (36). Select Team lists the 16 team cards: a team must be the player's (status 1; others give team string 0x1F, an unused one "Invalid team.", 37) and have an OKE (team string 0x49); Start without a team gives string 48. The whole team fights the opponent's whole team, each OKE on its spot | All 100 opponents are offered (unlocks and Victory Points aren't read); the rules (banned parts, the team size limit of string 28, the opponent's own victory condition) aren't checked; the team list's layout is the port's |
| OKE Configuration | `CDesignFlow` `FUN_088b94f0`, `CDesignMngr` (see below) | See below | See below |
| Team Formation | `CTeamFormationFlow` `FUN_08ac01b8`, `CTeamFormationMngr` (see below) | See below | See below |
| Shop | `CShopFlow` `FUN_08a71dc8`, `CShopMngr` (see below) | See below | See below |
| Battle | `CBattleTestFlow::buildBattle` `FUN_08823774`, `CBattleArenaFlow::CImpl::buildBattle` `FUN_088098e0` | The battle starts with the chosen OKEs on the chosen stage's map (`CPresetStage` entries at `0x08c21854` (0x14 bytes each), field 0 = `land/mapNN`) and time limit. After it, circle returns to the screen it was started from | See `docs/battle.md` |

### OKE Configuration

`src/game/oke_menu.c` (the screens) and `src/game/oke_parts.c` (the hardware rules),
from `CDesignFlow` (`FUN_088b94f0`: the top menu's item starts a sub-flow, 0 the card
select, 1 `CHardDesignFlow` `FUN_088f26c4`, 2 `CSoftDesignFlow` `FUN_08a7868c`) and
`CDesignMngr` (constructor `FUN_088bd918`). Text is `CDesignString` (`0x08c7a868`) and,
for the hardware, `CHardString` (`FUN_088ffb4c`, `0x08c7ee48`, 0xFF strings); part
names are `CEquipmentName`'s and `COkeName`'s tables. Everything is read from your EBOOT.
The card worked on is `CGameCommonDataBase`'s current OKE index (`FUN_0889cd3c` /
`FUN_0889cdd0`, shared block `+0x40C00`), so it is saved with the Battle Data. Changes
go into the save's card at once: a battle copies the card when it starts, and System
Menu writes it to the file.

| Screen | Game | Real | Stand-in |
|---|---|---|---|
| Top menu | `setupTopMenu` `FUN_088b9c28` | Items from `DAT_08bfe348` (0-8), text id + 0x18, help id + 0x23, on `bf_menb2` / `bf_menl2` / `bf_new00` (parts 8, 9, 0x85). Battle Mode's menu limits (`FUN_0895de04` on entry, read by `FUN_0895e3c4`) leave out Snapshot. On an installed card (status 2/3, `FUN_089eea58`) all but Notes are disabled (string 0x6F). Software and the rest need a design first (string 0x2E) | The title banner (`bf_ttl1`) and the menu's place are the port's (as Team Formation's); Snapshot isn't ported |
| The card window | `COkeCard` (`src/game/oke_card.c`): `Initialize` `FUN_089e5dcc`, `Draw` `FUN_089e4b50` = `drawWeightLine` `FUN_089e4cf0` + `drawType` `FUN_089e4f48` + `drawHardware` `FUN_089e6324` | The card itself: the weight band `oc_wbg0-3` by the weight state (`FUN_089edf40`; `oc_wbg1` empty, `oc_wbg4` selected), the name band `oc_stbg0/1` with the card's name, the hardware band `oc_hdbg` with the OKE's picture (`CreatePicture` `FUN_089e431c`: 48 x 48 index8 at hardware `+0x2B0`, 256 colours at `+0xBB0`), the emblem (`FUN_089e447c`: 32 x 32 index4 at `+0`, 16 colours at `+0x200`), `oc_bso_0`, and `CEquipmentIcon` (`FUN_088d88a4`) cells of `bf_icon0`: weapons (icon by ability and category, `FUN_088d8770`), armour, additional armour, options. Under it: body, durability, weight / payload, CPU, armour, weapons with ammo, options | The window around the card (`bf_swnd4`) and the stats under it are the port's: the game shows the card in its lists and the 3D model beside them. The weight-state colours of the stats, the half-dark squares behind the pictures (colour 1, alpha 0x7F: the colour is a guess) and the chip-program marks of the weight band (0xFD / 0xFE, not ported) |
| Select OKE Card | `FUN_089e6ba8`, `COkeCardSelectMngr` | The 32 cards; circle makes the card current ("Currently editing OKE Card." 0x124 / "This OKE Card is blank." 0x125), installed cards refused (0x6F). Square asks "Delete this design?" (0x66) and clears the card (`FUN_089eea80`), triangle copies: choose where (0x65), "Copy OKE Card here?" (0x67 / 0x68 over a used card) | A deleted card also leaves the teams it was on (the port's choice); the list layout; the card labels (number and name, the body's name without one) |
| Hardware | `CHardDesignMngr` modes 1 (`FUN_088f6748` / `FUN_088f7b00`), menu `FUN_088f9694` | Body, CPU, Armor Thickness, Additional Armor, Weapon 1-5 (as many as the body's `+0x18`), Energy, Engine Output, Option 1-4 (the body's `+0x14`); text `CHardString` 0xCF + item, help 2 / 3 / 0xF3 / 0xF4 / 4 / 0xED / 0xEE / 6. On a blank card only Body can be chosen (`FUN_08ad8944`; 0xF6 for the rest) | The window layout |
| The parts lists | `CEquipmentSelectWnd` `FUN_088da8b8`: `FUN_088d95f8` bodies, `FUN_088d976c` CPUs, `FUN_088d98cc` / `FUN_088d9a2c` armour, `FUN_088d9b8c` weapons, `FUN_088d9df0` options | The parts the save owns (stock 2, see Shop below): a new Battle Mode game (`FUN_08918d5c`) owns everything but bodies 14 and 33, and the Shop sells the rest. Bodies in the order at `0x08be8a80`; weapons by category, "No Weapon" when the slot may be empty, a weapon only where the body's slot table (`0x08c15c88`, `FUN_089eda88`) takes its type. The help line is the part's `CHardString` text (bodies 0xB+, CPUs 0x30+, weapons 0x4D+, options 0x8A+, armour 0x95, additional armour 0x96+). Choosing: a body takes its default loadout (`FUN_089f3a14`) and keeps paint, colours, CPU, armour and energy (`FUN_088f4df4`); a CPU resizes the program (`FUN_088f5020` → `FUN_089eeb44`: main 4 / 6 / 10 / 15 by the CPU's class at `0x08bd5f68`, sub-programs 7 for class 3; chips inside the new size kept, new ones blank as `FUN_089fcddc`); a weapon of the same category keeps its ammo up to the slot's capacity, another gets 100 rounds (types 1-8), 30 (type 9) or the slot's amount (`FUN_088da16c`, `FUN_089edb24`). On a blank card the chosen body starts a new design (status 1, CPU TP-16, a 4 x 4 program) | The info window's numbers are a choice of the game's stat labels (0x31-0x51); the ammo count window the game opens after some weapons (mode `+0x294` = 1, `FUN_089e15ac`) isn't ported; where the start chip goes when a program shrinks is the port's (kept if inside, else the corner) |
| Software | `CSoftDesignMngr` (`src/game/oke_soft.c`), the rules in `src/game/pgchip_edit.c` | Software Setup: Edit Chips, Undo, Select Area, Move Start Location, Change Counter Name, Copy from Card, Delete All, with the game's checks; the edited program goes back into the card. See "Software Setup" below | Parameter editing, drag / pull, flipping and turning a pasted block, macros, the test program and example solutions; the windows' frames and places. See "Software Setup" below |
| Energy and Engine Output | `CHardDesignMngr` modes 0x10 / 0x11 (`FUN_088f4018` / `FUN_088f4170`), `CTuneWnd` (`FUN_08adf134`, input `FUN_08adf454`, draw `FUN_08adf624`) | 50-150 %, left / right one step (repeating, a fresh press past an end wraps), written into the card at once (`+0x29C` / `+0x2A0`) so the weight follows; circle keeps, cross puts the opening value back. Window `bf_twnd2` | The cursor style |
| Name OKE | mode 9 (`FUN_088bafa8`), the keyboard through `CUtility` (`FUN_08b2552c`) | Starts from the card's name or its body's (`FUN_089eebc0`), titled "Name Input" (`0x08be31a4`); OK writes the name into the card | The keyboard is the PSP's own (system software, not in the game): `src/game/osk.c` is a stand-in, see "On-screen keyboard" |
| OKE Customization | modes 2-6 (`FUN_088ba534`), sub-menu `FUN_088b9edc` (text 0x106 + i, help 0x10A + i, each item on its menu-limit word) | Design (`CPaintWnd`, 0-3, hardware `+0x244`, "Paint Pattern"), Main / Accent Color (`CColorEditWnd` `FUN_088a1128`-`FUN_088a2aa8`: R, G, B 0-100, `+0x248` / `+0x24C` as R \| G<<8 \| B<<16; triangle a random colour, sound 0x11), Polish (`CColorLusterWnd` `FUN_088a3d40`, 0-4, `+0x250`). Each written into the card as it changes; cross puts the opening value back. The game's parts and help strings | The game previews on the OKE's 3D model; the port shows the card window instead. The help line 0x10B is longer than the help window (the game's wrapping isn't known). The cursor style |
| Emblems | mode 7, `CEmblemEditor` (`src/game/oke_emblem.c`; updates `FUN_088c6594`, draw `FUN_088d0004`) | The tool column (table `0x08be6edc`: y, button part, icon; `em_icon_00` cells, help `CHardString` 0x9E + icon), the 32 x 32 canvas at 5 px a pixel on `em_bgwnd`, the picture at its size, the palette `em_plt` (L / R pick, also on the tools), the grid (on at start, toggled). Undo (one level, a swap: `FUN_088c9630`), Pencil (circle arms it, then cross held draws: as decompiled, `FUN_088c6718`), Line / Rectangle / Oval (circle the first point, circle again draws, cross drops the point; the cursor stays where it is, `FUN_088c9cc4`; the oval shows its box), Paint (fill), Palette (the colour window with an A row, R/G/B as (b - 0x1C) / 2, A as 0-255 → 0-100, `FUN_088c96fc`), Delete Image (0xBA, Yes / No), Placement (three places, bits of `+0x254`, `em_brk_bl` / `em_brk_st`). Samples (mode 0xB `FUN_088ce050`, list built at open by `FUN_088d1984`): `EMBLEM02`-`EMBLEM75` (hard parts 0xD-0x52), then the non-blank emblems (`FUN_088cbbd4`) of the OKE cards (not installed private ones) and teams; five at a time at (0x10E + 0x26 i, 0xA7) with `sd_saro0/1` arrows and the `lc_cur` cursor, help 0xB7; choosing copies picture and palette (R, G, B kept within 0x1C-0xE4, `FUN_088c9304`). Triangle there: your own pictures (mode 0xC `FUN_088ce900`), 32 x 32 16-colour BMPs from `ms0:/PICTURE/CHE/` (the `--ms` folder), one directory entry read a frame, up to 50 (`FUN_088cc18c`, parser `FUN_088d2af8` / `FUN_088d2c80`). Ranges: triangle in Pencil, a shape tool without a first point, or Paint sizes a range (mode 0xD, help 0xAA); circle opens Move / Copy / Delete (mode 0xE, table `0x08be6fd8`, help 0xAB + item); Move / Copy lift it (`FUN_088cad88`) to be moved inside the picture, flipped with triangle, turned a quarter with R / L (`FUN_088cb0e0`, re-centred by `FUN_088cb028`), pasted with circle (every pixel, `FUN_088caa40`) or taken back with cross (Undo). Square held erases. Cross writes the picture and palette back into the card (`FUN_088cb970`) | The full-screen window the game shows with the samples (`+0x4EC`) isn't drawn; the strips' scroll and the range column's slide aren't animated; the user strip shows its right arrow with fewer than five pictures, as the game's test reads. The shapes are plain Bresenham / midpoint rasterizations (the game's point list `FUN_088c8c48` isn't followed exactly); the grid colour; the cursor boxes; the 3D preview of the placements isn't drawn |
| Notes | mode 10, `CCommentWnd` (`src/game/oke_notes.c`, rows `FUN_088aa304`), the tag keyboard `CKeyOskWnd` | The card's notes (`COkeDataBase + 0x1A40`: ten tags of up to 10 button marks 1-14, ten texts of up to 100 bytes, the defaults in Japanese), shown with `cm_btn_p` / `cm_wln0` / `cm_wrn0`. Tags typed on the game's keyboard (keys at `0x0047B850`, looks `0x0047B5F8`, icons `0x003F2698`, parts `cm_bf01`-`cm_wnd_p`); Move / Copy / Delete of marked cells as the game's backup and clipboard; Delete All (0x123). An installed card's notes are only viewed. The help window hides while a keyboard is open (`FUN_088abb58`) | Text entry uses the stand-in keyboard ("Enter Comments", `0x003DCBD8`). Save Template, Paste Template and Copy from Card aren't ported (logged). The cursor style, the menu's place and how the row frame is put together |
| Privacy Level | mode 0xB, `CSecretLevelWnd` (draw `FUN_08a6d6a0`) | Public / Private (0x61 / 0x62) on `bf_swnd2` beside the menu at the cursor's row; circle writes the card header `+0x20`, help 0x2B | The cursor style |

The hardware block (`CHardwareDataBase`, `COkeDataBase + 0x30`): `+0` the emblem (32 x
32, 4 bits a pixel, the even pixel in the low nibble), `+0x200` its 16 colours (R, G, B,
A bytes), `+0x240` body, `+0x244` paint pattern (0-3), `+0x248` / `+0x24C` main and
accent colours (R | G<<8 | B<<16 | A<<24, each 0-100; -1 unset), `+0x250` polish
(0-4), `+0x254` the emblem's places (bits 0-2, `FUN_089ed5b0` / `FUN_089ed580`), `+0x258` five
weapons, `+0x26C` their ammo, `+0x280` armour thickness, `+0x284` additional armour,
`+0x288` CPU, `+0x28C` four options, `+0x29C` on-board energy %, `+0x2A0` engine
output %, `+0x2B0` the OKE's picture (48 x 48, 8 bits a pixel) and `+0xBB0` its 256
colours. The card header's `+0x20` is the privacy level, and `COkeDataBase + 0x1A40`
the notes. The weight (`FUN_089edebc`) is the body's `+4`, the energy carried (body `+0xC`
× energy % × 0.2), the armour (`0x08bd5ed8` / `0x08bd6064`: % of the body's weight),
each weapon (`0x08bd5a28`: weight + per-round weight × ammo), each option
(`0x08bd6038`) and the CPU (`0x08bd5fbc` by class + speed × 100 / the fastest's); the
payload (`FUN_089ed624`) is the body's `+8` × engine output %. Under 70% of it is light,
to 100% normal, to 130% heavy, past that too heavy: the shipped arena designs reach
129.96%. `chexa-oke-parts-test` checks these rules against them.

```sh
C="--press 70 down --press 90 circle --press 110 circle"   # Continue -> top menu
O="--press 200 up --press 210 up --press 220 circle"         # -> OKE Configuration
build/pc-m32/chexa-pc --disc DIR --saves orig/teamtest --startup $C $O --shot 270 orig/oke_top.png \
    --press 280 circle --press 290 down --press 296 down --shot 330 orig/oke_cards.png --frames 340
```

The screens one at a time (frames are 30 a second; the top menu's items are 8 frames
apart from frame 250: 3 downs Customize, 4 Emblems, 5 Name, 6 Notes, 7 Privacy):

```sh
D="--press 250 down --press 258 down --press 266 down --press 274 down"   # -> Emblems
build/pc-m32/chexa-pc --disc DIR --saves COPY_OF_teamtest --startup $C $O $D --press 290 circle \
    --shot 320 orig/oke_emblem.png --frames 325
```

Screenshots in `orig/` (not committed): `oke_card.png`, `oke_energy.png`,
`oke_output.png`, `oke_name.png`, `oke_custom.png`, `oke_design.png`, `oke_color.png`,
`oke_polish.png`, `oke_emblem*.png` (with `_samples`, `_user`, `_range*`), `oke_card_emblem.png` (an edit on the card),
`oke_notes*.png`, `oke_privacy*.png`. Saving through the System Menu and loading
again keeps the changes (checked with Privacy Level).

#### Software Setup

`src/game/oke_soft.c` (the screen) and `src/game/pgchip_edit.c` (the rules, no drawing),
from `CSoftDesignMngr` with `CPgChipWnd`, `CPgChipSelectWnd` and `CPgChipConnectWnd`.
Text is `CSoftString` (`0x08c8aba0`, 0x2A3 strings, `FUN_08aa14f8`), the chip names
`PGCHIP`'s (`0x08c85100`), pictures `parts/soft/` by `CSoftPicture`'s name table
(`0x08c8aad8`). All of it is read from your EBOOT and disc when the screen opens.

The editor works on a copy of the card's software block, normalized as the game does
on opening (`NormalizeArea`); leaving Software Setup (cross on its menu) normalizes it
again and writes it into the save's card, from where a battle copies it and the System
Menu saves it. `chexa-chip-edit-test` loads a design, edits it, saves Battle Data,
reloads it and checks that nothing changed, then fights a program written with the
editor's functions and checks the interpreter runs exactly its chips.

| Part | Game | Real | Stand-in |
|---|---|---|---|
| Menu | `setupTopMenu` `FUN_08a79b8c`, `UpdateModeTopMenu` `FUN_08a851f4` | The ten items (`CSoftString` 0x12-0x1B, help 0x231-0x23A), each on its menu-limit word 0x22 + item, so Battle Mode hides Example Solution; cross leaves | The menu's place (the OKE Configuration menu's); the exit's test-team checks (`FUN_08a816cc` / `FUN_08a817d4`) aren't made |
| The chip areas | `CPgChipWnd` (`Create` `FUN_08a25f84`), tiles `FUN_08a0c008`, faces `FUN_08a13430`, arrows `FUN_08a13468` | The three areas side by side (main, then sub 1 over sub 2; cells 0x40 apart by 0x10, a 0x20 margin), the cursor crossing between them, `sd_chip0` tiles by kind (table `0x08c1ea30`: the faded row for chips switched off, the Return and blank tiles), the exit arrows from `sd_cara0` (records `0x08c1e91e`, picked by processing mode and direction `0x08c1f228`, the branch's row and the faded column), `START` (`sd_start`) over the start, the chip's main glyph from `sd_cicn0` (glyph records `0x08c1df90`, the twelve placements of `0x08c1ea78`, the per-type tables of the face routines), counter names on counter chips | Drawn at half size in a scrolling view; the area panels and info window are plain boxes. The faces' numbers and small marks (the parameter values) aren't drawn, only each type's main glyph |
| Edit Chips | mode 3, `FUN_08a7d644` | Circle on an empty cell or a Return opens the chip list, on a chip its exit step; R / L turn the exit / branch to the next free direction (`FUN_08a197b8` / `FUN_08a1a008`); triangle starts selecting chips one by one (mode 6: triangle toggles, square deletes, circle opens Move / Copy / Delete); the help shows the chip's line (0x257 + type); the info box its name and error (0x28D-0x291) | Circle on a chip skips its parameter sub-menu (`0x08c2dcfc`: 70 kinds of sub-menu), so placed chips keep the defaults or the last ones used; square (drag / pull, mode 0xD) isn't ported |
| The chip list | `CPgChipSelectWnd` `FUN_08a16bc8`, the page rule `FUN_08a155f8` | Five pages (`sd_tag0a`-`4a`), L / R switch, a page holds the types of its kind and page 4 all of them (all unlocked outside Story Mode), page and cursor per page kept in the save (`CSoftwareSceneDataBase` at `+0x41370`: `+0x1B0`, `+0x1B4`), the type's help line | The window's frame (CDesignPicture 0xCF-0xD3) and layout |
| Placing and wiring | `FUN_08a194c4`, the exit step mode 0x1A `FUN_08a85770` | The chip is copied from the save's remembered chip of that type (`+0x41370`, one per type, written when a placement finishes, `FUN_08a1cbd8`; built by `FUN_0889c724` in a new game, computed defaults `FUN_089fcd54` when the block is blank); the d-pad sets the exit (two keys a diagonal), L / R turn it, circle finishes, cross goes on to the branch for judges; Returns appear and go, bits 6 / 7 follow | The arrows' animation |
| Undo | `FUN_08a18c50`, `CPgChipAreaBufferBackup` | One step back | |
| Select Area | mode 4, menu mode 0x11 | A rectangle in one area, then Move, Copy (pasted with the cursor at its top-left; cells off the area are dropped, the block's blanks leave what's there) or Delete | Save Macro, and flipping / turning the block while pasting |
| Move Start Location | mode 0xF | Left / right move the start along the top row, L / R pick the area | |
| Change Counter Name | mode 0x1B | The eight counters (0x12A-0x131, default names `CNT_A`.. `0x08c850b8`), the keyboard titled 0x17D, A-Z and 0-9 only (0x189), up to 5 characters | The keyboard is the port's (see "On-screen keyboard") |
| Copy from Card | mode 0x20 | The 32 cards with the card window; refuses the card being edited (0x186), blank cards (0x184) and private installed ones (0x185); asks 0x182, or 0x183 when the CPU is smaller and chips will be cut | Only "From Another OKE Card": copying from another save file (0x21) isn't ported |
| Delete All | mode 0x2C, `ClearArea` `FUN_08a18ce4` | Asks 0x181, then every cell blank and a Return on each start | |
| Start Test Program, Paste Macro, Example Solution | modes 0x2D, 0x1F, 0x27 | | Not ported: the first two play the error sound, the third says 0x2A2 (Battle Mode has none) |

The rules (`pgchip_edit.c`, written up in `docs/program-format.md` "Editing") were
checked on every shipped design: normalizing the 307 programs of the 205 preset
records changes nothing except bit 7 on the blank cells and Returns of 66 programs,
which were shipped without it.

```sh
S="--press 250 down --press 258 down --press 266 circle"   # top menu -> Software
build/pc-m32/chexa-pc --disc DIR --saves COPY_OF_teamtest --startup $C $O $S \
    --press 290 circle --press 300 down --press 308 right --press 316 circle --press 326 r \
    --press 336 right --press 346 circle --press 356 left --shot 380 orig/chipedit_3_connect.png --frames 385
```

Screenshots in `orig/` (not committed): `chipedit_1_menu.png`, `chipedit_1_grid.png`,
`chipedit_2_list.png` (page 1, judges), `chipedit_3_connect.png` (a judge placed, its
branch being set), `chipedit_3_placed.png`, `chipedit_4_back.png`,
`chipedit_5_area.png` (Select Area's menu), `chipedit_5_paste.png`,
`chipedit_6_copy.png`. There is no screenshot of the real game to compare with, so
the layout is the port's.

#### On-screen keyboard

Name OKE and the Notes' texts use the PSP's text-entry keyboard (`sceUtilityOsk`), which
is system software, not part of the game: the game passes a title, the text to start
from and a length (`CUtility` `FUN_08b2552c`) and takes the text back on OK
(`FUN_08b23c3c` / `FUN_08b25730`). `src/game/osk.c` stands in for it: a grid of ASCII
characters, the d-pad moves, circle types, square deletes the last character (both
bytes of a Shift-JIS one), triangle a space, Start accepts, cross cancels. The game's
own keyboard for the Notes' button tags (`CKeyOskWnd`) is ported as the game draws it.

#### Two-byte text

The Notes' default texts are Shift-JIS. `src/game/font.c` draws them from
`font/gothic16.gim` as the game does (`FUN_08b15ae8`): Shift-JIS to JIS (`FUN_08b0ab28`),
the cell (`FUN_08b14f20`) row (j1 - 0x21, or j1 - 0x28 from 0x29) × 0x5E + j2 - 0x21,
through the sheet's u16 map (0 skips the glyph).

### Shop

`src/game/shop_menu.c`, from `CShopMngr` (constructor `FUN_08a72358`, `Initialize`
`FUN_08a73798`, update `FUN_08a74058`) and its select window `CShopSelectWnd`. The Shop
only buys: nothing in it sells, and buying (`FUN_08a733ac`) is the one place the game
takes money (`FUN_0889d338`).

What it reads and writes, all in the shared block saved with the Battle Data:

- Victory Points at `+0x48394` (`FUN_0889d2f8`; adding `FUN_0889d308` caps at 99999,
  spending `FUN_0889d338` stops at 0).
- The parts stock at `+0x47BBC` (`FUN_0889d288`), a word per item: 0 not offered, 1
  for sale, 2 owned. Groups: bodies `+0` (0x25), CPUs `+0x94` (8), armour `+0xB4` (12),
  additional armour `+0xE4` (8), weapons `+0x104` (0x3C), options `+0x1F4` (11),
  program chips `+0x220` (0x36), Other `+0x2F8` (0x1F). Setting one (`FUN_08919354`)
  keeps the "none" entries owned (`FUN_08919694`). OKE Configuration's lists show only
  owned parts, so a bought part is there at once.
- The menu limits at `+0x479B0` (`CMenuLimitDataBase`): words 0x5F + category switch
  the Shop's categories, 6 + n OKE Configuration's items, 0x67 + n the System Menu's.
- The map states at `+0x47F30` (`CBattleFieldStateDataBase`), a word per stage: the
  Test Program's map list (`FUN_08827c98`) offers a stage when it is 1, never 13
  (`FUN_0889ed84`).

A new Battle Mode game (`FUN_088d3d80` mode 1, `savedata_battle_mode_new`) sets the menu
limits (`FUN_0895de04`: OKE Configuration's Snapshot and the System Menu's word 3 off),
owns every part but bodies 14 (Hades) and 33 (Nidhogg), puts the Other items 0x14-0x1E
on sale (`FUN_08918d5c`) and opens stages 0-13 (`FUN_0882f65c`).

| Screen | Game | Real | Stand-in |
|---|---|---|---|
| Categories | `FUN_08a73d78`, chosen `FUN_08a74510` | Body, CPU, Armor Thickness, Additional Armor, Weapon, Option, Other (`CDesignString` 0xB0 + category; Program Chip is never offered), each shown when something in it is for sale (`FUN_08918e1c`) and its menu limit is on; `bf_menb5` / `bf_menl5`; help `CDesignString` 0x118 + id in `sd_wind0`; title "Shop" on `bf_ttl5`; the money window (`FUN_0885e0c4`, `bf_sw0t0` / `bf_sw0c0` / `bf_sw0b0`) at the top right. With nothing for sale the Shop closes at once | The placing of the money window's pieces; the "new" marks (`bf_new00`) aren't drawn |
| Item list | `FUN_08a75b14` with `FUN_08a75360`..`FUN_08a75760`, input `FUN_08a75ca4`, draw `FUN_08a7604c` | The items with stock 1 (bodies class by class, `0x08c14d80`; additional armour, weapons and options from id 1) and their prices from the tables at `0x08c8a760` (bodies), `0x08c8a7f4` (CPUs), `0x08c8a814`, `0x08c8a844`, `0x08c8a864` (weapons), `0x08c8a954`, `0x08c8a980` (Other); `bf_swnd5`, 8 rows of 16 px, name at +0x1E, price at +0xA4. Help: `CHardString` 0xB + body, 0x30 + CPU, 0x96 + additional armour, 0x4D + weapon, 0x8A + option, none for armour, the Other item's own description | The price is drawn as text, not with `CNumber`'s digits; items that cost more than the player has are greyed (the port's hint); the scroll bar and the list's slide are simplified |
| Item info | `FUN_088de8e0` (the parts data window at 0x1E0 - width, 0x23), the map window `FUN_0882f6a4` | A map item shows the stage's name and preview (`0x08c38fc4` → `bf_mpiNN`) | The numbers shown for a part are a choice of `CDesignString`'s stat labels, as in OKE Configuration; the icon window (`FUN_088dcb90`, `bf_icon5` / `bf_icon1`) and the body's 3D view (`FUN_08a75008`) aren't drawn |
| Buying | `FUN_08a733ac`, message closed `FUN_08a7287c` | Circle buys at once (the game asks nothing): not enough points gives "You don't have enough Victory Points."; else the price is spent, the stock set to 2, an Other item takes effect (`FUN_08a721a8`) and "Purchased %s." is shown; closing it rebuilds the list, and an empty list goes back to the categories (which are rebuilt too). Cross goes back | The message window's layout; sounds |
| Other items | `0x08c8a570` (16 bytes: name, description, type, value), `FUN_08a721a8` | Type 2, a map: its stage's map state becomes 1, so the Test Program offers it. Type 3: a menu limit switched on (`0x10005` OKE Configuration's Snapshot, which then shows in its menu; `0x70003` the System Menu's Customize Music) | Types 0 / 1 (more OKE or team cards) change the Story Mode block (`CCarnageHeart + 0x83570`), which isn't ported; Battle Mode never sells them. Snapshot and Customize Music themselves aren't ported |

`--make-test-save` builds a new Battle Mode game (above) with 5000 Victory Points and,
so every category has something to buy, puts a few owned parts back on sale (CPU 7,
armour 11, additional armour 6, weapons 0x23 and 0x2C, option 10). That is the test
save's only departure from the game's new game. A Battle Data file written before the
port kept the stock (every word 0) is given a new game's stock when it is loaded.

```sh
build/pc-m32/chexa-pc --disc DIR --saves orig/shoptest --make-test-save --frames 5
C="--press 70 down --press 90 circle --press 110 circle"   # Continue -> top menu
S="--press 190 down --press 215 circle"                     # -> Shop
build/pc-m32/chexa-pc --disc DIR --saves orig/shoptest --startup $C $S --shot 250 orig/shop_top.png \
    --press 250 circle --press 265 down --shot 285 orig/shop_list.png \
    --press 290 circle --shot 300 orig/shop_bought.png \
    --press 305 circle --press 315 circle --shot 325 orig/shop_no_money.png --frames 330
```

(Copy the save folder somewhere first if it should stay unbought.) The other pictures,
`shop_other.png` (a map item), `shop_weapon.png`, `shop_oke_body.png` (Nidhogg in OKE
Configuration's Body list after buying it) and `shop_stage_list.png` (the bought map
offered by the Test Program, the locked ones greyed), were taken the same way.

### Team Formation

`src/game/team_menu.c`, from `CTeamFormationMngr`: `Initialize` `FUN_08ac536c`,
`setupTopMenu` `FUN_08ac58f0`, and the modes it switches between (enter `FUN_08ac10c0`,
update `FUN_08ac1344`). Text is `CTeamFormationString` (`FUN_08ac7d54`, the table at
`0x08c8ba60`).

| Mode | Game | Real | Stand-in |
|---|---|---|---|
| 3, top menu | `FUN_08ac5d08` / `FUN_08ac6820` | Select Team, Register Member, Enter Team Name, Emblems, Transfer Team Data (text id + 4, help id + 0x33) on `bf_menb3` / `bf_menl3` / `bf_new00` (parts 10, 0xB, 0x85); the team window at (0xBE, 0x41) shows the selected team; Register Member without a selected team gives string 0x44; cross leaves | Enter Team Name (the on-screen keyboard), Emblems (`CEmblemEditor`) and Transfer Team Data log a line; the title banner (`bf_ttl2`) and the window's contents are the port's layout |
| 4, Select Team | `FUN_08ac1710` / `FUN_08ac2964`, the list `FUN_08ac0b3c` | The 16 team cards (`FUN_0889cb10`, `CCarnageHeart + 0x3AE80 + 0x3D800`, 0x340 each). Circle on an unused card makes a team (`createTeam` `FUN_08ac42f4` with `FUN_089f38a0`: status 1, the name "Team NN" from `0x08c8ba08`, owner string 0x19, the date, `+0x338` = 1000, three empty members, the default emblem: blank pixels with the palette at `0x08c83db8`) and selects it (`setSelect` `FUN_08ac452c`); installed teams (status 2/3) give string 0x1E. Square asks "Delete this team?" (0x1A) and clears the card | The selection is kept for the session only, as the game does (`CCarnageHeart + 0xD0170` isn't saved); list layout |
| 5, Register Member | `FUN_08ac5edc` / `FUN_08ac2ba4` | The team's three slots (First/Second/Third OKE, 0x16-0x18), circle picks a card, square asks "Unregister OKE?" (0x1D, `FUN_08ac854c`), triangle moves the member (Formation) | The window's layout (the game's is at (0xCD, 0xC)) |
| 6, Select OKE Card | `FUN_08ac188c` / `FUN_08ac2dcc`, `CTeamMemberCardListWnd::regist` `FUN_08ac8144` | The 32 cards; a blank card gives string 0x24; a new member takes the first free spot in the order at `0x08c3e228` (`FUN_08ac8eec`) | The weight, manual-OKE and team-count rules (`FUN_08891fc8`, strings 0x20-0x23, 0x25) aren't checked |
| 8, Formation | mode 8 (`+0x74`, `+0x68`) | Spots are 0-8 of the grid at `0x08bd7d10` (`CFormationDataBase +0x24`), which the battle uses | The port's grid: the member moves with the d-pad (front row at the top, the team's left on the left), a taken spot is swapped |

The team card (`CTeamDataBase`, constructor `FUN_08ad321c`): `+0` status (0 unused, 1
the player's, 2/3 installed), `+0x10` the emblem (32 x 32 4-bit pixels and 16 palette
words, drawn from the save), `+0x250` name, `+0x269` owner, `+0x290` three
`CFormationDataBase` of 0x30 (two vectors, `+0x20` card, `+0x24` spot), `+0x320` the
date, `+0x330` / `+0x334` counters, `+0x338` 1000. They're read and written in
`savedata.c` and saved with the Battle Data.

```sh
build/pc-m32/chexa-pc --disc DIR --saves orig/teamtest --make-test-save TESTDATA --frames 1
C="--press 70 down --press 90 circle --press 110 circle"   # Continue -> top menu
build/pc-m32/chexa-pc --disc DIR --saves orig/teamtest --startup $C --press 190 up \
    --press 210 circle --shot 240 orig/tf_top.png --press 250 circle \
    --shot 280 orig/tf_teams.png --press 290 circle --press 310 down --press 330 circle \
    --shot 360 orig/tf_members.png --frames 370
```

Shared pieces (`ui.c`): `CTopMenuWnd` (update `FUN_08ad7248`, input `FUN_08ad75ec`, draw
`FUN_08ad77ec`) with its `CBtnWnd` buttons (update `FUN_08896aec`, draw `FUN_08897248`,
select `FUN_088967c4`): up/down with hold repeat and wrap on a fresh press; the selected
button draws its second picture, growing over 0.1 s to 1/10 wider and 2 px taller; text
greyed when disabled. The open fade and 32 px slide, the title banner (`bf_ttl4` with
the latest name, where the game stacks names with `pushMenuTitle` `FUN_08826ad0`), the
help window's place (`FUN_088ffcac` makes a full-screen window; the port puts
`sd_wind0` at the bottom), the list windows and the Yes/No dialog's layout are the
port's. The key guide (`FUN_08a60304`) isn't drawn: the font has no button icons yet.

## Save data

`src/game/savedata.c` (the game side) on `src/platform/savedata.c` (the
`sceUtilitySavedata*` calls). Saves go in the folder given with `--saves DIR`
(default `saves/`, gitignored), laid out like a memory stick's `PSP/SAVEDATA`: one
directory per save, named game name + save name.

| Save | Directory / file | Contents | Written with |
|---|---|---|---|
| System Data | `ULJM05698/DATA.BIN`, secure | The 0x7C bytes at `CCarnageHeart + 0x3AE00` (`FUN_0889ea98`): volumes (two 1.0 floats), options, `+0x74` Story Mode completed (`FUN_0889c400`), `+0x78` play time in seconds (`FUN_0889c418`). Defaults from `FUN_0889c0c0` | MAKEDATASECURE / WRITESECURE |
| Battle Data | `ULJM05698B/<NAME>.CHE`, normal | `"0.0.44"` without its NUL (`0x08c8b760`), the shared block (`+0x3AE80`, 0x483A0 bytes: 32 `COkeDataBase` cards of 0x1EC0, 16 team cards of 0x340 at `+0x3D800`, then the rest), the Battle Mode block (`+0x860F0`, 0x431C bytes; the player name first). 0x4C6C2 bytes in all (`FUN_08ab8114`) | MAKEDATA / WRITE |

The other kinds (Story Data `T`/`S`, replays `R`, `TEAM`, `REPLAY`, `MATCH`; names
from `0x08c858f8`) aren't ported. All share the param setup of `CUtility`
(`FUN_08b25354`, `FUN_08b24da4`, `FUN_08b25090`): game name `ULJM05698`, key `CHEXA`,
SFO title "CARNAGE HEART EXA", the save's title and detail from
`CSystemMngr::GetSaveGameDataName` (`FUN_08ab82f0`: "System Data" with the play time
and Story Mode line; "Battle Data" with "Player Name:" and the name), the icon
`parts/icon/icon0_<kind>.png` from the pack (`0x08c858d4`). These are read from your
EBOOT and the pack at run time.

The sequences are `CSaveMngr`'s (`UpdateUT` `FUN_08a65b54`, result tables at
`0x08c89d98`), one utility call per frame:

- **Start-up check** (entrance state 0, `CSystemMngr` mode 0x17, `CSaveMngr` type 4
  exec 3): state 0x16 FILES on `ULJM05698`; found → 0x17 READSECURE `DATA.BIN` into
  the system block; RW_NO_DATA → 0x18 the space check, then the create path of type 4
  exec 4 (0x20 FILES, 0x21 GETSIZE, 0x22 MAKEDATASECURE, or WRITESECURE when the file
  is there). The game's message dialogs on errors (no memory stick, broken data) are
  logged instead, and it goes on with the defaults. State 0x51 between 0x18 and the
  create path isn't traced: the port creates without asking.
- **Battle Data list** (Continue): FILES on `ULJM05698B`, normal files; the ones of
  five characters or more ending in `.CHE` (`FUN_08a641dc`); a size other than
  0x4C6C2 marks the entry broken (`FUN_08a672f8`'s `+0x30`).
- **Load** (state 0x2B): READSECURE of the file, then as the load in `0x088b7590`:
  skip the version (`FUN_08ab8604` compares it, a mismatch is only logged), copy the
  shared block and the Battle Mode block.
- **Save** (`BeginDataSave` `FUN_08a63290`): FILES, GETSIZE, then MAKEDATA when the
  directory is new (`+0x50` = 0) else WRITE, of the buffer `FUN_08abb680` builds.

Stand-ins: Continue's list (the game first offers the kinds of save, `CSystemMngr`
mode 4, then `CSaveMngr`'s own file list, `UpdateSelectFileList` `FUN_08a676f0`) is a
plain menu of the Battle Data files with the port's texts; the keyboard for a new
file's name isn't there (`NEWDATA.CHE`, the name it starts with in `FUN_08a62160`); a
new game's shared block has the cards, teams and the words `FUN_0889c92c`,
`FUN_0889c420` and `FUN_0889c51c` set, but the sub-blocks after the team cards whose
constructors aren't ported (rankings, matches) start zeroed; Battle Mode's new game then
sets the menu limits, the parts stock and the map states (see Shop). Data loaded from
a save is kept and written back byte for byte.

The platform side follows the system's behaviour for the modes the game uses (FILES,
READ(SECURE), MAKEDATA(SECURE), WRITE(SECURE), GETSIZE) and the others (AUTOLOAD/SAVE,
LOAD/SAVE, LISTLOAD/LISTSAVE, LIST, SIZES, ERASE, DELETE, DELETEDATA), with their
error codes (RW_NO_DATA 0x80110327 for a missing save, RW_FILE_NOT_FOUND 0x80110329
for a missing file). It writes a PARAM.SFO in the system's format, with
`SAVEDATA_FILE_LIST` naming the secure files so FILES can tell them apart. Nothing is
encrypted: a PSP's secure files (`DATA.BIN`) are encrypted with the game key, so
those don't carry over; Battle Data `.CHE` files are plain on the PSP too.

To try it with robots in the card list, write a test save and load it with Continue:

```sh
build/pc-m32/chexa-pc --disc DIR --make-test-save           # saves/ULJM05698B/TESTDATA.CHE
build/pc-m32/chexa-pc --disc DIR --startup --press 70 down --press 90 circle \
    --press 110 circle --shot 185 orig/sv_mode.png --frames 190   # Continue, load it
```

`--make-test-save [NAME]` builds a new game's blocks, puts the two simulation OKEs and
the leaders of the first ten arena teams (their records read from the pack, named
after the team) in cards 1-12, makes two team cards as `createTeam` makes them
("Team 01" with cards 3-5, "Team 02" with cards 1-2; names from `0x08c8ba08`, the
owner string 0x19, members on spots 4, 3, 5), sets the player name "TESTER", gives it
5000 Victory Points and a few parts for sale (see Shop) and saves it as
`NAME.CHE` (default `TESTDATA`) through the same save path. Nothing of it is in the
repository: it's built from your game files when you run it. `tests/savedata_test.c`
checks the whole path without the game files.

Walk-through (the screenshots were taken this way, into `orig/`):

```sh
B="--press 70 circle --press 90 down --press 110 circle --press 130 circle"  # -> top menu
build/pc-m32/chexa-pc --disc DIR --startup $B --shot 170 orig/bm_top.png \
    --press 180 circle --shot 210 orig/bm_select.png --press 220 circle \
    --shot 260 orig/bm_test.png --frames 270
```

## Sound

### The files

| Files | Format | Played |
|---|---|---|
| `sound/sound_se_tbl.bin` (file set "boot") | the SE table, 243 rows of 0x38 bytes | yes (read at start-up) |
| `sound/se/seset_00sys`, `_01bat`, `_02adv` `.phd` / `.pbd` (loose in the pack, also in "gamecommon" / "battle_se") | libsnd banks: a "PPHD" header with programs, tones and samples; the `.pbd` holds PSX-style 4-bit ADPCM ("VAG") at 22050 or 44100 Hz | system and battle banks yes, `_02adv` (story scenes) isn't loaded |
| `sound/se/at3se/NNN.at3` (pack) | ATRAC3plus, mono, 44100 Hz (RIFF WAVE, format 0xFFFE with the ATRAC3plus GUID) | not yet (the decoder is there, the effect path isn't) |
| `USRDIR/sound/bgm/bgm01..54.at3` | ATRAC3plus, stereo, 44100 Hz, about 64 kbit/s, with an `smpl` loop | yes, with libavcodec (see "Music") |
| `sound/sound_bgm_tbl.bin` (file set "boot") | the BGM table, 55 rows of 0x34 bytes | yes |
| `voice/*.at3` (voice.dat), `PSP_GAME/SND0.AT3` | ATRAC3plus | no |

None of the files is plain ATRAC3. The loose pack entries with a stored size of 0 are
kept as is, not ARZ-packed (`pack_read`, `tools/chexa_pack.py`).

### How the game plays an effect

`CSoundDriver::initLibrary` (FUN_08b2d01c) reserves the output channel
(`sceAudioOutput2Reserve(0x200)`) and starts the Sony sound library with 32 voices and
a grain of 0x200 samples (FUN_08b6b8f4 → FUN_08b7a70c: `__sceSasInit` at 44100 Hz,
every voice at pitch 0x1000, simple ADSR 0x000F / 0x5FC0, volume 0). Its thread mixes
a grain with `__sceSasCore` (FUN_08b6ed28) and sends it to the channel.

`CSound::SEPlay` (FUN_08b36b6c, 205 calls with a constant number) looks the number up
in the SE table (FUN_08b27574), works out the delay (FUN_08b37004) and the pan
(FUN_08b37074), checks the repeat guard (FUN_08b28168), then plays either the
ATRAC3plus file of the row (FUN_08b289d0) or the bank program (FUN_08b28664 → the
library's voice start FUN_08b6fc8c). Battle effects go through FUN_08b36cac, which
first works out the volume and pan from the camera (`CSound::seCalc3DVolume`,
FUN_08b37174).

The SE table row:

| Offset | Field |
|---|---|
| +0x00 | name (`SE_SYS_009`, `SE_BTL_041`, `SE_SCN_100`) |
| +0x10 | bank: 0 system, 1 battle, 2 story scenes |
| +0x12 | program in the bank |
| +0x14 | `at3se` file number, 0 for none |
| +0x16 | category: 2 system, 3 battle, 4 scene |
| +0x18 | volume (float) |
| +0x20 / +0x24 | pan and its random spread |
| +0x28 / +0x2C | the 3D fall-off per unit and the full-volume radius |
| +0x30 | a repeat restarts the effect rather than being dropped |
| +0x32 | the repeat guard, in units of two vblanks |
| +0x34 / +0x36 | delay and its random spread, in frames |

The bank (`.phd`): the "PPPG" chunk holds 128 program offsets from the start of the
file (-1 for none), each program a count, three unused words and that many tone
numbers; "PPTN" tones of 0x60 bytes (+0x00 sample, +0x20 volume with 0x1000 full,
+0x24 pan with 0x40 centred, +0x30 root key, +0x50 / +0x54 the ADSR register words);
"PPVA" samples of 16 bytes (offset in the `.pbd`, sample rate, size).

### The port

- `src/platform/sas.c`: `__sceSas*` written from the hardware's behaviour: 32 voices of
  ADPCM at a pitch (0x1000 = 44100 Hz), SPU-style ADSR envelopes, left and right
  volumes, mixed a grain at a time. Reverb, noise and PCM voices are accepted and
  ignored.
- `src/platform/audio.c`: `sceAudio*` on SDL3: each channel is an SDL audio stream on
  one 44100 Hz 16-bit stereo device; the blocking calls wait while the channel holds
  more than two buffers. With `--mute`, or with no device, the calls keep the timing
  only. The device opens at start-up and the log names the SDL driver, the drivers
  built in and the device (`--audio-driver NAME` chooses the driver, `--tone` plays a
  test tone). Under WSL2 the driver is `pulseaudio` on WSLg's "RDP Sink". The 32-bit
  SDL3 from `tools/setup_pc_deps.sh` used to be built without any audio driver but
  `disk` and `dummy`, so nothing was heard; it now has PulseAudio, loaded at run time
  from the i386 `libpulse0` in `.deps`. That library finds its private
  `libpulsecommon-17.0.so` only along its own RUNPATH, a system path, so `audio.c`
  loads that one from the sysroot first (`PCPORT_I386_LIBDIR`).
- `src/game/sound.c`: the SE table, the banks, the voices and the sound thread, set up
  at step 15 of `CApplication::Init`. The pitch is the sample's rate against
  44100 Hz; the category volumes and the system data's volume settings aren't applied.
  `CHEXA_SOUND_TRACE=1` logs every effect played. `chexa-sound-test` (CTest
  `sound_effects`) plays effects offline and checks that each one sounds and then
  ends; `--wav DIR` writes them out to listen to (keep those in `orig/`).

### Which effects play

| Effect | Number | Where |
|---|---|---|
| cursor | 9 (`SE_SYS_009`) | every menu cursor, list, grid and yes/no question |
| decide | 0xC | circle on a menu or list item; Story Mode on the mode menu |
| cancel | 0xB | cross; "no" in a question; closing the Shop's message |
| OK | 0xD | "yes" in a question; a purchase in the Shop |
| error | 0xF | not enough Victory Points; a locked stage |
| start | 10 | Battle Mode on the mode menu (FUN_088d5510) |

Missing on these screens: 8, 0xE, 0x10, 0x11 and the single uses of 2 and 4 (screens
or cases not ported yet, such as the system menus); the four system effects that are
ATRAC3plus files (`SE_SYS_002`..`005`, at3se 252..255) and every `SE_SCN_*` effect of the
story scenes. See "Battle sounds" in `docs/battle.md` for the battle.

### Music

The ATRAC3plus decoder is the system **libavcodec** (FFmpeg's reverse-engineered
`atrac3plus` decoder, **LGPL-2.1-or-later**), linked dynamically. Sony's own library
can't be used. For the 32-bit build, `tools/setup_pc_deps.sh --libavcodec` (or
`--libavcodec-only` on top of an existing `.deps`) fetches Ubuntu's i386 `libavcodec`
and the libraries it loads (about 100 packages, 60 MB) into `.deps/sysroot`, all
but libc, which stays the system's; CMake finds it and defines `CHEXA_HAVE_LIBAVCODEC`
(`-DCHEXA_LIBAVCODEC=OFF` leaves it out). Without it the port builds and runs as
before, with effects only. A 64-bit build would use the system's own `libavcodec`.

**The decoder (`src/platform/atrac.c`).** `sceAtrac*` on libavcodec, for the case
where the caller hands over the whole file: then the remaining-frame count is -1 and
the game never streams (its CSoundPlayerAT3 handles that case: FUN_08b2dfa8 returns at
once). The RIFF `fmt ` chunk gives the channels, rate and frame size (0x178 bytes for
the music), `fact` the sample count and the decoder samples before the first one
played (3217 in `bgm01`), `smpl` the loop in decoder positions. A call decodes one
frame of 2048 samples; at the loop end it starts again at the loop start, priming the
decoder with the frame before so that the join is seamless.

**The player (`src/game/music.c`).** CSoundDriver::strInit (FUN_08b2d82c) calls
`sceAtracReinit(0, 3)` and sets up three players (CSoundPlayerSTR), each on its own
channel. CSoundPlayerAT3 sets the file up (FUN_08b2dbf0: SetDataAndGetID,
GetSoundSample, SetLoopNum -1 when the file has a loop, GetRemainFrame), decodes into a
ring of 0x1800 stereo samples until a grain of 0x200 is ready (FUN_08b2ddbc), and sends
a grain at a time with `sceAudioOutputPannedBlocking` (FUN_08b2e120), padding the end
with silence (FUN_08b2e0a8). The port runs one player on its own thread
(`SOUND_STRThread`) and reads the whole file (about 450 KB) when a track starts,
where the game reads it in pieces. A new track fades the old one out over half a
second; the game's own fades, the system data's BGM volume and the second and third
players (voices) aren't ported.

**The BGM table** (`sound/sound_bgm_tbl.bin`, TblBGMOpen FUN_08b27638): rows of 0x34
bytes, the title at +0x00 ("THE DAWN"), the name at +0x20 (`BGM_36`), the file number
at +0x30 (`sound/bgm/bgm%02d.at3`, -1 for none) and the volume at +0x32 (0-100, a 0-1
factor for the player, FUN_08b35410). CSound::BGMPlay (FUN_08b34de8) takes a row,
the picker that asked (kept at +0x11d9c, FUN_08b35400 reads it) and the loop flag.

**Which track plays** (CBgmPlayer):

| Where | Picker | Track |
|---|---|---|
| the title (top menu, FUN_088d4624) | FUN_0888c718, kind 0 | row 0x24 "THE DAWN" (`bgm35.at3`) |
| Battle Mode's screens | FUN_0888d2ac, kind 1 | the next track of the menu list |
| a battle | FUN_0888dfb0, kind 3 (from the battle scene FUN_088754d8, unless the battle names its own track, FUN_08b34e88) | the next track of the battle list |
| the software designer | FUN_0888d9d4, kind 2 | the designer list (screen not ported) |

A picker does nothing while its kind is already playing, so the music carries on
across Battle Mode's screens. The lists come from the save's BGM Player settings
(`CGameCommonDataBase`): three flag arrays of 99 words (+0x44E98 menus, +0x45024
designer, +0x451B0 battles: the rows ticked) and a mode each (+0x4533C..: 0 in order,
1 shuffled again after the last track, 2 at random). A new game ticks row 20
("Scoffing Crucifix(CHP)") for the menus, 17 for the designer and 21 ("Code Breaker")
for battles. FUN_088d3d80 builds the lists when a mode starts (FUN_0888cbd4 /
FUN_0888cd8c / FUN_0888cf44, shuffling in mode 1). In Story Mode the pickers choose by
story progress instead; that isn't ported.

`chexa-sound-test --music` (CTest `music_decode`) decodes `bgm01` and `bgm36` through the
imports, checks the sample count against the file and that the loop wraps without a
gap; `--wav DIR` writes their first ten seconds. It reports "skipped" without
libavcodec.

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
build/pc-m32/chexa-pc --disc DIR --startup --skip-movie --frames 150 --shot 2 orig/logo.png \
    --shot 60 orig/title.png --press 70 circle --shot 100 orig/menu2.png \
    --press 110 down --shot 120 orig/menu2_battle.png
```

Frame 2 is the logo frame (black), frame 4 onwards is the title. Without `--skip-movie`
each movie picture is a displayed frame too: the 1184 pictures follow the first frame
or two (frame 80 is the Natsume logo, 1180 the title card) and the title comes after
them, unless a button stops it:

```sh
build/pc-m32/chexa-pc --disc DIR --startup --frames 1260 --shot 80 orig/movie_logo.png \
    --shot 600 orig/movie_600.png --shot 1250 orig/after_movie.png
build/pc-m32/chexa-pc --disc DIR --startup --press 40 circle --frames 80 --shot 60 orig/skipped.png
``` The keyboard plays
the pad: arrows, Z/Enter = circle, X/Backspace = cross, A = square, S = triangle,
Q/W = L/R, Space = start, Right Shift = select.

## Next

- The ending movie (`movie/ending.pmf`, the same player with skip mode 0) once Story
  Mode's end is ported; the player's overlay picture (`FUN_08b1bffc`).
- `CSystemMngr`'s own menus (the kinds of save, settings) and `CSaveMngr`'s file list
  and message dialogs; Story Data; the keyboard for save names.
- The phases after the menus: 0xC (new story game), 7 (the player-name keyboard),
  6 for network play, 10 (attract after 65 s idle).
- Shift-JIS text is drawn (see "Two-byte text"); the game's wrapping of long help lines.
- OKE Configuration: Software Setup's parameter sub-menus, drag / pull, macros and test program, Snapshot, the note templates, and the 3D model
  beside the menus.
- Voices and the ATRAC3plus effects (the decoder is in, their players aren't); the BGM
  Player screen; the remaining menu effects.
