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
animated somewhere not yet read, this would change.

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
| Top menu | `setupTopMenu` `FUN_088b9c28` | Items from `DAT_08bfe348` (0-8), text id + 0x18, help id + 0x23, on `bf_menb2` / `bf_menl2` / `bf_new00` (parts 8, 9, 0x85). Battle Mode's menu limits (`FUN_0895de04` on entry, read by `FUN_0895e3c4`) leave out Snapshot. On an installed card (status 2/3, `FUN_089eea58`) all but Notes are disabled (string 0x6F). Software and the rest need a design first (string 0x2E) | OKE Customization (its sub-menu `FUN_088b9edc`: Design, Main / Accent Color, Polish), Emblems, Name OKE (the keyboard), Notes and Privacy Level log a line; the title banner (`bf_ttl1`) and the menu's place are the port's (as Team Formation's) |
| The card window | `COkeCard::drawHardware` `FUN_089e6324` | The body, CPU, armour, additional armour, the body's weapon slots with ammo and its option slots, durability, and weight against payload with the weight state (`FUN_089edf40`) | Drawn as text on `bf_swnd4` with `oc_hdbg` as the header: the game draws the OKE's picture (`CreatePicture`), the CPU picture and equipment icons (`CEquipmentIcon`, `FUN_088d88a4`) on `oc_wbg0-4`; the weight-state colours are the port's |
| Select OKE Card | `FUN_089e6ba8`, `COkeCardSelectMngr` | The 32 cards; circle makes the card current ("Currently editing OKE Card." 0x124 / "This OKE Card is blank." 0x125), installed cards refused (0x6F). Square asks "Delete this design?" (0x66) and clears the card (`FUN_089eea80`), triangle copies: choose where (0x65), "Copy OKE Card here?" (0x67 / 0x68 over a used card) | A deleted card also leaves the teams it was on (the port's choice); the list layout; the card labels (number and name, the body's name without one) |
| Hardware | `CHardDesignMngr` modes 1 (`FUN_088f6748` / `FUN_088f7b00`), menu `FUN_088f9694` | Body, CPU, Armor Thickness, Additional Armor, Weapon 1-5 (as many as the body's `+0x18`), Energy, Engine Output, Option 1-4 (the body's `+0x14`); text `CHardString` 0xCF + item, help 2 / 3 / 0xF3 / 0xF4 / 4 / 0xED / 0xEE / 6. On a blank card only Body can be chosen (`FUN_08ad8944`; 0xF6 for the rest) | Energy and Engine Output (modes 0x10 / 0x11, the tuning sliders) show their percentages but can't be changed; the window layout |
| The parts lists | `CEquipmentSelectWnd` `FUN_088da8b8`: `FUN_088d95f8` bodies, `FUN_088d976c` CPUs, `FUN_088d98cc` / `FUN_088d9a2c` armour, `FUN_088d9b8c` weapons, `FUN_088d9df0` options | The parts the save owns (stock 2, see Shop below): a new Battle Mode game (`FUN_08918d5c`) owns everything but bodies 14 and 33, and the Shop sells the rest. Bodies in the order at `0x08be8a80`; weapons by category, "No Weapon" when the slot may be empty, a weapon only where the body's slot table (`0x08c15c88`, `FUN_089eda88`) takes its type. The help line is the part's `CHardString` text (bodies 0xB+, CPUs 0x30+, weapons 0x4D+, options 0x8A+, armour 0x95, additional armour 0x96+). Choosing: a body takes its default loadout (`FUN_089f3a14`) and keeps paint, colours, CPU, armour and energy (`FUN_088f4df4`); a CPU resizes the program (`FUN_088f5020` → `FUN_089eeb44`: main 4 / 6 / 10 / 15 by the CPU's class at `0x08bd5f68`, sub-programs 7 for class 3; chips inside the new size kept, new ones blank as `FUN_089fcddc`); a weapon of the same category keeps its ammo up to the slot's capacity, another gets 100 rounds (types 1-8), 30 (type 9) or the slot's amount (`FUN_088da16c`, `FUN_089edb24`). On a blank card the chosen body starts a new design (status 1, CPU TP-16, a 4 x 4 program) | The info window's numbers are a choice of the game's stat labels (0x31-0x51); the ammo count window the game opens after some weapons (mode `+0x294` = 1, `FUN_089e15ac`) isn't ported; where the start chip goes when a program shrinks is the port's (kept if inside, else the corner) |
| Software | `CSoftDesignMngr` / `CPgChipWnd` | The main program and the sub-programs (L/R), chip names from `PGCHIP`'s table (`0x08c85100`), the chip tiles `parts/soft/sd_chip0` (faded for chips switched off) | View only: Edit Chips and the rest of the Software Setup menu aren't ported. The tile for each kind (`FUN_089fd528`: system, judge, action, other, Return) is a guess by order; the chip icons (`sd_cicn0`), the exits' arrows and the parameters (shown as bytes) aren't drawn |

The hardware block (`CHardwareDataBase`, `COkeDataBase + 0x30`): `+0x240` body, `+0x244`
paint, `+0x248` / `+0x24C` colours, `+0x250` polish, `+0x254` flags, `+0x258` five
weapons, `+0x26C` their ammo, `+0x280` armour thickness, `+0x284` additional armour,
`+0x288` CPU, `+0x28C` four options, `+0x29C` on-board energy %, `+0x2A0` engine
output %. The weight (`FUN_089edebc`) is the body's `+4`, the energy carried (body `+0xC`
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
- `CSystemMngr`'s own menus (the kinds of save, settings) and `CSaveMngr`'s file list
  and message dialogs; Story Data; the keyboard for save names.
- The phases after the menus: 0xC (new story game), 7 (the player-name keyboard),
  6 for network play, 10 (attract after 65 s idle).
- Shift-JIS text (`gothic16.gim`, the double-byte table) for the Japanese strings.
- Sound effects for the cursor and buttons (CSound).
