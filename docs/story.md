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
  and 4 are the screens Battle Mode already uses.

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

| Op | Args | Meaning (from the arguments; being confirmed in the player) |
|---|---|---|
| 0x00 | | end of script |
| 0x01 | s | a line of text (`"\\"` waits) |
| 0x02 | s | the speaker's name |
| 0x03 | s i | a portrait `adv/chr/*.gim`, position / fade flags (0, 0x100, 0x200; 0x4000 fade) |
| 0x05 | i | hide portraits (0x4000 fade) |
| 0x06 | s i | a background `adv/bg_*.gim` / `adv/ev*.gim`, fade flags |
| 0x07 | i | hide the background |
| 0x08 / 0x09 | i | screen effects |
| 0x0B | i i | play music track N |
| 0x0C | i i | stop music: flags (0x4000 fade), milliseconds |
| 0x0D | i | the message window's style (1, 2, 3) |
| 0x0E | s | a voice `voice/<who>/<id>.at3` (in `voice.dat`) |
| 0x0F | | wait for the pad (end of a message) |
| 0x10 | i i i | a variable operation (flag bits pick system / lesson variables, table `0x08c7d190`) |
| 0x12 / 0x13 / 0x14 / 0x15 | i i i / i i i / / | if / else-if / else / end-if (a stack of 16 levels) |
| 0x1A | i | a sound effect |
| 0x1C | i | wait N frames |
| 0x1F | i i i s | jump to another script |
| 0x21 | s i | a notice ("Mission added.") |
| 0x25 / 0x26 | s i / | a lesson label / clear |
| 0x29 / 0x2A | i i i i / | a lesson highlight rectangle / clear |
| 0x2B / 0x2C | i | team and card setup |
| 0x2D / 0x2E | i / | set / clear the save block's `+0x2B28` |
| 0x2F | | wait for the pad (variant) |

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
| +0x2B2C | the current battle mission |
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
| Scenes, the briefing hub, the ending | | A black screen naming the step; circle goes on (parts 2, 3) |

`--story-step N` (with `--startup`) starts a new game on step N, for screenshots:

```sh
build/pc-m32/chexa-pc --disc DIR --startup --story-step 0 --shot 60 orig/shots/story/01_chapter1.png --frames 65
C="--press 70 circle --press 90 circle"                    # Start Game, Story Mode
build/pc-m32/chexa-pc --disc DIR --startup --skip-movie $C --press 150 circle --press 250 circle \
    --press 300 circle --press 350 circle --shot 395 orig/shots/story/02_save_prompt.png --frames 400
```
