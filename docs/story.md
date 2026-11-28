# Story Mode

What the game's Story Mode is made of, mapped from the Ghidra export. Addresses are
PSP addresses (an EBOOT file offset is address − 0x08804000). Everything here is read
from your EBOOT and disc at run time; nothing in this file is game data beyond names
and numbers.

Status: being ported; what is real and what is a stand-in is listed at the end.

## Structure

Story Mode is one linear list of **85 steps** (`CScenarioInfoDataBase`, table at
`0x08c2a344`, 0x56 entries of 0x28 bytes; entry 85 is a debug step). There are no
branching routes. The current step is the first word of the story save block;
`GoNextStep` (`FUN_08a69700`) adds one (up to 0x54).

| Offset | Field (accessor) | Notes |
|---|---|---|
| +0x00 | script file (`GetScriptFilePath` `FUN_08a69784`) | `adv/scenarioNN.chs`, `adv/tutorial_X.chs`, `adv/missionNN.chs`; none on title steps |
| +0x04 | first script id (`GetScriptIDFirst` `FUN_08a69804`) | `scenario01`, `tutorial01a`, `mission01` |
| +0x08 | `IsLecturePart` `FUN_08a698e4` | 1 for steps 0-45 (the lesson half), 0 after |
| +0x0C | `IsHideScriptBeforeBriefing` `FUN_08a69964` | 1 on lesson and mission steps: their script becomes the hub's briefing |
| +0x10 | `IsTitleMode` `FUN_08a699e4` | 1 on chapter title cards |
| +0x14 | `GetStoryIndex` `FUN_08a69af4` | lesson day (1-13) or mission group (14-20), -1 else |
| +0x18 | `GetStoryTitle` `FUN_08a69b74` | "Progress :Chapter 1" (the second form skips "Progress :") |
| +0x1C | `GetFileSetName` `FUN_08a69d2c` | a preload group (`story01_adv`, `lecture01_adv`, `mission01`) |
| +0x20 | `GetChapterTitlePicture` `FUN_08a69c0c` | `adv/chpt_NN.gim` on title steps |
| +0x24 | the next step | |

The kinds of step:

- **Chapter title cards** (20, `adv/chpt_01`-`20.gim`), e.g. steps 0, 4, 8.
- **Story scenes** (`adv/scenario01`-`41.chs`).
- **Lessons** "Lesson Day 1"-"13" (`adv/tutorial_a`-`m.chs`), steps 2-44.
- **Missions** (`adv/mission01`-`11.chs`), steps 48-83: "Executing Vengeance",
  "Assault at Midnight", ... "Risking Life and Limb".

## Flow

- **New Game.** The entrance's mode menu → phase 0xC (`FUN_088d4e64` /
  `FUN_088d628c`): fade to black, game mode 1. The scene manager (`FUN_088d3xxx`)
  calls the mode switch `FUN_088d3d80` with 0: the story save block and others are
  reset (`FUN_0895ddd4`, `FUN_08918cb0`, `FUN_0882f64c`, the value at `+0x2B28` = -1),
  then a **`CTutorialFlow`** (`FUN_08ae0df0`, 0x34 bytes) of type 1 is pushed. Mode 3
  continues a loaded story (type 2 or 4).
- **`CTutorialFlow`** (Update `FUN_08ae1938`) runs one child flow at a time and, when
  it finishes, moves to the next step:
  - a script step → `moveAdventurePart` `FUN_08ae1464`: a **`CStoryFlow`**
    (`FUN_08ab16b4`) with the step's script file, script id, chapter picture and
    progress title; the file set and the `story_adv` sound bank are loaded
    (`FUN_08ae179c`);
  - a lesson or mission step → `hideEventScriptBeforeBriefing` `FUN_08ae1668` stores
    the step's script in the save block as the briefing, then the **briefing hub**
    (`FUN_08ae1024` → `FUN_088926d0`, `CBriefingMngr`);
  - step 0x54 → the ending, then mission 11 again and `CScenarioClearSaveFlow`
    (`FUN_08a68dc8`); the system data's "Story Mode completed" is set
    (`FUN_0889c408`).
- **The briefing hub** (`CBriefingMngr`, update `FUN_08892c1c`) is Story Mode's
  version of the Battle Mode top menu. Its items open: 0 `CLectureFlow` (lessons),
  1 `CDesignFlow` (OKE Configuration), 2 `CTeamFormationFlow`, 3 `CBattleSelectFlow`,
  4 `CShopFlow`, 5 `CSystemMngr` (System Menu), 7 `CBriefingPresetFlow`. Items 1, 2
  and 4 are the screens Battle Mode already uses. The menu (`FUN_08894664`) lists
  the six ids at `0x08bfe330` with the Battle Mode buttons (parts 0x87 + id,
  0x8D + id), text id + 4 and help id + 10 of `0x08c7a6b0` (`FUN_08894300`); an item
  shows when its menu limit (group 0, `FUN_0895e2fc`) is 1, with the "new" mark when
  it is 2, and choosing it turns 2 into 1 (`FUN_0893b748`). A new story has only
  Lesson; the lessons' scripts unlock the rest (op 0x10, flag 0x40000000).
- When a child of the hub ends, the game-common word `+0x45D4C` (`FUN_0889e214`,
  set by `FUN_0889e170`) says the step is done: the hub ends and `CTutorialFlow`
  goes to the next step. Two kinds of child set it: a lesson finished
  (`FUN_08934480`) and a won mission battle (`FUN_08818ff0`: result 1 at
  `+0x45D24`, the stage between 0x40 and 0x4A, the step not a lesson's).
- **Missions.** The scene before a mission picks its record (op 0x10 with flag
  0x80000000, through the table at `0x08c7d280`: `scenario21` sets 0x33 → record 38)
  and hides the mission's briefing on Battle → Mission (op 0x1F on menu 0x34,
  `mission01`). Battle → Mission (`CBattleSelectFlow` kind 4, `FUN_08880bc4`) opens
  `CBattleMissionFlow` and its manager (`CBattleMissionMngr`, `FUN_0881a26c`): the
  hidden briefing plays from the step's script file, then a team list
  (`FUN_0881a428`), then the battle.
- **CEventScriptField menus.** Op 0x1F / 0x20's menu numbers are screens: 10 Lesson,
  0x14 and 0x1E-0x23 OKE Configuration and its parts (0x21 the chip editor, whose
  keys are chip ids), 0x32 Battle, 0x34 Mission, 0x46 and 0x5A others the lessons
  use.

## Scene scripts (`.chs`)

68 files in `adv/` (`scenario00`-`42`, `tutorial_a`-`m`, `mission01`-`11`, `test`),
376 scripts in all. Loaded by `CEventScriptAnalysis::Load` (`FUN_088deef0`):

| Offset | Meaning |
|---|---|
| +0x00 | `"CHS "` `"1.10"` |
| +0x08 | script count |
| +0x10 | script table: (name offset in the string pool, first word index) pairs |
| +0x14 | the word stream (u32) |
| +0x1C | the string pool (Shift-JIS; the text on this disc is English) |

A script is a stream of opcodes, each followed by its arguments: an integer word or a
string-pool offset. The argument counts below are the player's (`CEventScriptWnd`
update `FUN_088e6e30`); with them every script on the disc decodes to its end marker.
(The load-time pass `FUN_088e1864` only looks for a few side effects and isn't a
reliable argument table.)

| Op | Args | Meaning (from the player, `FUN_088e6e30`) |
|---|---|---|
| 0x00 | | end of script |
| 0x01 | s | a line of text (`"\\"` waits) |
| 0x02 | s | the speaker's name |
| 0x03 | s i | a portrait `adv/chr/*.gim`: slot = flags & 0xF (3), place & 0xF00 (0 centre, 0x100 right, 0x200 left), 0x4000 fades in |
| 0x04 | i | move a portrait (slot, place) |
| 0x05 | i | hide a portrait (slot; 0x4000 fades out) |
| 0x06 | s i | a background `adv/bg_*.gim` / `adv/ev*.gim`: 0x4000 fades from black, 0x2000 from white |
| 0x07 | i | hide the background |
| 0x08 / 0x09 | i | shake the background (1 on, 2 off) / its tint colour |
| 0x0A | | show the page |
| 0x0B | i i | play music track N |
| 0x0C | i i | stop music: flags (0x4000 fade), milliseconds |
| 0x0D | i | the message window's style: 1 normal, 2 none, 3 full screen |
| 0x0E | s | a voice `voice/<who>/<id>.at3` (in `voice.dat`) |
| 0x0F | | the end of a message (the voice stops) |
| 0x10 | i i i | a variable: op (5 set, 6 OR, 1-4 add / sub / inc / dec), flags \| index (0x80000000 battle mission, 0x40000000 menu limit, 0x20000000 parts stock, 0x10000000 lesson state, none the script's own), value |
| 0x12 / 0x13 / 0x14 / 0x15 | i i i / i i i / / | if / else-if / else / end-if (a stack of 16 levels) |
| 0x1A | i | a sound effect: row & 0xFFFF, volume % in the high half |
| 0x1B | | stop the sound effects |
| 0x1C | i | wait N frames |
| 0x1D | i i | shake a portrait for N frames |
| 0x1F | i i i s | hide a script on a menu point: menu, key, -, script id (`CEventScriptField`) |
| 0x20 | i i | remove it: menu, key |
| 0x21 | s i | a notice ("Avispa added.") with a jingle |
| 0x22 | i | what the end does: 1 asks to read the tutorial again |
| 0x25 / 0x26 | s i / | a lesson label / clear |
| 0x29 / 0x2A | i i i i / | a lesson highlight rectangle / clear |
| 0x2B / 0x2C | i | the current OKE card / the battle team's member (cards 1-32) |
| 0x2D / 0x2E | i / | set / clear the save block's `+0x2B28` |
| 0x2F | | show the page (variant) |

The pictures: message windows `adv/ad_win*.gim`, `ad_chr*`, `ad_bmen*`, `ad_bak00`;
backgrounds `adv/bg_*.gim` (27 plus variants); event pictures `adv/ev*.gim` (14);
portraits `adv/chr/chr_<who><n>_<face>.gim` (226); lesson slides `adv/lc_*.gim` (284).

## Battles

`CBattleMissionFlow` (`FUN_088185d8`, `buildBattle` `FUN_08819478`) builds the battle
from the save block's current battle (`+0x2B2C`) through
`CBattleMissionDataBase::GetBattleInfoScenarioCurrent` (`FUN_08836bd0` →
`FUN_08836ed4`): 49 records of 0x3D0 bytes at `0x08c6b110`, named "Battle Mission
1"-"49" (`0x08c6b040`), filled into the same `CBattleSettingInfo` the arena uses.

| Offset | Field |
|---|---|
| +0x00 | stage (`CPresetStage`, 26-74) |
| +0x08 | enemy count (1-3) |
| +0x0C | three enemy designs of 0x40 (`preset/lecture/oke_lNN_n.bin` for 0-37, `preset/mission/oke_mNN_n.bin` for 38-48) |
| +0x114 | time limit (float seconds: 30, 120, 180, 300) |

The battle itself is the one Battle Mode runs (`CBattleScene`, the chip interpreter).

## Save data

The story save block is `CScenarioModeDataBase` at `CCarnageHeart + 0x83570`, 0x2B80
bytes (`FUN_0889eac8`; constructor `FUN_0889d3a0`):

| Offset | Field |
|---|---|
| +0x0000 | current step |
| +0x0088 | 0x34 lesson states (two nibbles each) |
| +0x00BC | 0x34 lesson-cleared flags |
| +0x0198 | the hidden-script field (`CEventScriptField`) |
| +0x2B28 | the value ops 0x2D / 0x2E set (-1 in a new game) |
| +0x2B2C | the current battle mission (a record, 0-48; -1 none) |
| +0x2B34 | the briefing script path |
| +0x2B74 / +0x2B78 | the most OKE / team cards (2-32 / 4-16) |
| +0x2B7C | |

It is saved as **Story Data**, kind `S` (`CSystemMngr::GetSaveSize` `FUN_08ab8114`):
the version, the shared block (0x483A0) and this block, 0x4AF20 bytes plus the
version, icon `parts/icon/icon0_scenario.png`. Kind `T` (0x350 bytes, icon
`icon0_tutorial.png`) is a separate tutorial block.

## Relation to Battle Mode

Story Mode reuses most of what Battle Mode has ported: the save code (one more
kind), OKE Configuration, Team Formation and the Shop from the hub, the battle and
chip interpreter, and the music player. What is new is `CTutorialFlow`, `CStoryFlow`
with `CEventScriptWnd` (the scene player), the hub `CBriefingMngr`, the lessons
(`CLectureFlow`) and the mission records.

## Port status

Ported in `src/game/story.c` (the flow), `src/game/storydb.c` (the step list) and
`src/game/savedata.c` (the story block and Story Data).

### 1. New Game and the save flow

| Part | Real | Stand-in |
|---|---|---|
| Story Mode on the title | The mode menu's Story Mode ends the entrance and starts a new story (phase 0xC, `FUN_088d3d80` mode 0) | Phase 0xC's fade to black |
| New game | `CScenarioModeDataBase`'s reset (`FUN_0889d3dc`); mode 0's menu limits, parts stock and maps (`FUN_0895ddd4`, `FUN_08918cb0`, `FUN_0882f64c`); `loadPreset` (`FUN_08ae1090`): OKE cards 1 and 2 and team card 1 from `preset/lecture/` in the pack, and the battle teams | |
| The step list | All 85 steps from the EBOOT (`0x08c2a344`), walked as `CTutorialFlow::Update` does: an adventure part for chapter cards and scenes, the briefing for lessons and missions, the ending after step 0x54 | |
| Chapter cards | The step's `adv/chpt_NN.gim` from the disc, fading in over 0.5 s, circle, fading out over 1 s (`FUN_08ab2894`, `FUN_08ab296c`, `FUN_08ab2ad0`) | |
| "Save your progress?" | Asked before a chapter card that follows another step (`CStoryMngr` state 0, string 0x38 of `0x08c7a6b0`); Yes saves Story Data | The game's dialog (`FUN_088c0ba0`) is the port's Yes/No box. The game's save lets the player choose or name the file (`CSaveMngr` type 1, exec 5); the port saves to the file loaded, else `NEWDATA.CHE` |
| Story Data | Kind `S`: `ULJM05698S/<NAME>.CHE`, the version, the shared block and the story block, 0x4AF26 bytes; the SFO's title "Story Data" and the step's progress title; icon `icon0_scenario.png` | |
| Continue | Story Data or Battle Data, the kind's files, then the story resumes on its saved step (`FUN_088d3d80` mode 3, `CTutorialFlow` type 2): a scene or card, or the briefing for a lesson or mission step | Plain menus (see screens.md, "Save data") |
| The clear data | After the ending: Story Mode completed in the system data, step 0x53, mission 11's briefing and battle 0x30, then "Save cleared data?" (string 0x39) | |
| The ending | | A black screen naming the step; circle goes on |

### 2. The scene player

`src/game/scene.c`: `CEventScriptWnd` on one script of a `.chs` file, started by
`CStoryMngr` for every scene step.

| Part | Real | Stand-in |
|---|---|---|
| Scripts | Read from the disc (`CEventScriptAnalysis`: header, Seek by name, words and the string pool); every command with the player's argument counts; the if-stack | |
| Backgrounds and portraits | `CEventScriptPicture`: the disc's `adv/bg_*.gim`, `adv/ev*.gim`, `adv/chr/*.gim`; placed left / right / centred on the bottom edge (`FUN_088e0cec`); fades of 5% a frame from black or white (0x4000 / 0x2000), waited for (state 3); hiding, moving, shaking (ops 8, 0x1D) and tint (op 9) | |
| The message window | Style 1: `parts/bf/sd_wind1.gim` drawn as the game slices it (four rows: the tab with the name, the body; `FUN_088eb690`), the name at (7, 0xAC), up to 4 lines at (7, 0xC3) in colour 8 of the game's table (`0x08c480d0`), the blinking page cursor `parts/bf/sd_page0.gim` at (0x1CE, 0xFC). Style 3: the screen half-darkened and up to 12 lines at (6, 0x1C) in colour 0. Style 2: no window. A `"\\"` line shows the page | |
| Input | Circle: next page; square: remove the window until a button; L or R held: skip (no fades, waits or pages; "Skipping") | The backlog (triangle, `CEventScriptLinkedLog`) isn't ported; its guide item is left out |
| The guide | The game's text (strings 0x2A / 0x31 of `0x08c7a6b0`) on the window's last row, at y 0xF9 | Buttons as words ("Square:") instead of the icons |
| Waits, notices | Op 0x1C (frames); op 0x21 ("Avispa added.") with its jingle, closed with circle or cross | The notice uses the port's frame (`FUN_088c0ba0` isn't in the export) |
| Variables (op 0x10) | Set / OR of the current battle mission (table `0x08c7d280`), the parts stock (`0x08c7cab0`), the menu limits (`0x08c7c4c0`, groups as `FUN_0895e1cc`) and the script's ten variables | The lesson states (flag 0x10000000) and the one if (in a lesson) are logged and not taken: part 5 |
| Hidden scripts (ops 0x1F / 0x20) | Stored on and removed from the menu points of the save (`CEventScriptField`, menu numbers `FUN_088df724`) | |
| Sound | Effects (op 0x1A) from the story bank (`seset_02adv`, opened with Story Mode); music and voices (part 4) | |
| Lesson commands (0x23-0x2A) | Read and skipped | Part 5 |

### 3. The briefing hub and mission battles

`src/game/battle_menu.c` (`battle_menu_start_story`) and `src/game/battle.c`
(`battle_mode_start_story`): the Battle Mode screens and battle, in the hub's variant.

| Part | Real | Stand-in |
|---|---|---|
| The hub | The six items with the game's buttons, picture, text and help; which show and which are "new" from the save's menu limits; "new" cleared when chosen; cross does nothing (the hub has no way back) | The game's own hub layout (`FUN_08894664` places the list and a help window `FUN_088ffcac` differently) is the Battle Mode top menu's |
| OKE Configuration, Team Formation, Shop | The screens Battle Mode uses, on the story's cards, teams and stock | As in Battle Mode |
| Battle | Test Program, Mission, Battle Arena, Matchmaking (strings `0x08c79410`); Mission is enabled while the save has a mission | Battle Arena offers all opponents, as in Battle Mode |
| Mission | The briefing hidden on menu 0x34 plays over the menu with the scene player (`adv/missionNN.chs`), once; then the 16 team cards; then "Start battle?" | The game's mission screen (`CBattleMissionMngr`: its own team window at (0xBE, 0x10) and layout) is the port's team list |
| The mission battle | `buildBattle` from the record of `0x08c6b110`: stage `+0`, enemy count `+8`, designs `+0xC + 0x40 n` (`preset/mission/oke_mNN_n.bin`), spots `+0xCC + 4 n`, time limit `+0x114`; the chosen team on its spots; the battle and chip interpreter Battle Mode runs | The robots' side of the battle is Battle Mode's stand-in simulation (battle.md) |
| After the battle | A win on a mission's stage (0x40-0x4A) on a step that isn't a lesson's ends the hub, and the story goes on; a loss or draw goes back to the Battle menu | |
| System Menu | Saves Story Data (to the file loaded, else `NEWDATA.CHE`) | As in Battle Mode: the port's question in place of `CSystemMngr`'s menu |
| Lesson | | Lessons come last. On a lesson step, Lesson asks (the port's text) whether to count the lesson as completed; Yes runs all of that day's scripts (`adv/tutorial_X.chs`) for their effects, the items, parts and missions they unlock, and the story goes on |

`--story-step N` also runs the scenes and lesson days before step N for their
effects, so a mission step starts with its record, its hidden briefing and the
unlocked hub. `--battle-result win|lose|draw` ends every battle a second in, for
scripted runs:

```sh
B="--press 90 circle --press 120 down --press 140 circle"        # Battle, Mission
B="$B --press 220 circle --press 260 circle --press 300 circle"   # the briefing's pages
B="$B --press 400 circle --press 440 circle"                      # Kyle's Team, Start battle?
build/pc-m32/chexa-pc --disc DIR --startup --story-step 48 --battle-result win $B \
    --press 760 circle --frames 900          # scenario22 starts: the story goes on
```

### 4. Music and voices

| Part | Real | Stand-in |
|---|---|---|
| Scene music | Op 0xB fades out what plays and starts BGM table row N, looping (`FUN_088e3808` → `FUN_0888c648`, `CSound::BGMPlay`; e.g. `scenario01` plays row 28, "Try and Error"); op 0xC stops it; the music stops when a scene ends (`FUN_08b34f44`). The tracks are the disc's `sound/bgm/bgmNN.at3` through the port's music player (screens.md, "Music") | Op 0xC's fade time (0x4000 with milliseconds) is the player's fixed half-second fade |
| The hub and battles | The hub plays the menu list (`FUN_0888d2ac`), the mission battles the battle list, as Battle Mode does | |
| Voices | Op 0xE plays `voice/<who>/<id>.at3` from `voice.dat` (ATRAC3plus, mono, 44.1 kHz) on its own audio channel (`src/game/voice.c`, through the same `sceAtrac` decoder as the music); a new voice, the page turned (op 0xF) or the scene's end stops it; nothing plays while L/R skip | The game's voice player and its volume setting aren't read: voices play at full volume. Replaying a voice from the backlog comes with the backlog |

`chexa-story-test` also decodes `scenario01`'s first voice and checks it is speech
(mono, RMS and zero crossings), so a broken decoder path fails the test.

`chexa-story-test` (CTest `story_steps`) reads every step's script from the disc to
its end with the player's argument table (85 steps, 23,732 commands), checks the
chapter pictures are there and that a new story gets loadPreset's cards.

`--story-step N` (with `--startup`) starts a new game on step N, for screenshots:

```sh
build/pc-m32/chexa-pc --disc DIR --startup --story-step 0 --shot 60 orig/shots/story/01_chapter1.png --frames 65
C="--press 70 circle --press 90 circle"                    # Start Game, Story Mode
build/pc-m32/chexa-pc --disc DIR --startup --skip-movie $C --press 150 circle --press 250 circle \
    --press 300 circle --press 350 circle --shot 395 orig/shots/story/02_save_prompt.png --frames 400
build/pc-m32/chexa-pc --disc DIR --startup --story-step 1 --press 130 circle --press 200 circle \
    --shot 260 orig/shots/story/11_scene_talk.png --frames 265   # a page with a speaker
```
