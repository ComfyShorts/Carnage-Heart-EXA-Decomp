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

Battle Mode goes to the port's battle setup (see `docs/battle.md`). Choosing Story
Mode, Continue or Network Play only logs a line: the phases after the menus (0xC, 6)
aren't ported.

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
| Parts cache | `FUN_0888763c`, `FUN_08887834`, names at `0x08c79468` | Only the five title entries are named. `parts/bf/%s.gim`, or `%s.gim` for names with a slash |
| `CPicture` | draw `FUN_08b08c78`, `FUN_08b08afc`, `FUN_08b08c14`, blend `FUN_08b08da4` (table `0x08c8d01c`) | `picture.c` |
| Texture manager / GIM | `FUN_08b1fd84`, `FUN_08b1fcf8`, `FUN_08b1fa24`, bind `FUN_08b581c4` | `picture.c`: idx4/idx8 with 8888 palettes, swizzled or linear, frames |
| `CFontMngr` / `FSTRING` | `FUN_08b14f70`, `FUN_08b15fb0` (size), `FUN_08b161d0` (draw), `FUN_08b17054` (char class) | `font.c`: single-byte glyphs from `gothic8.gim`, layout mode 0. Shift-JIS glyphs (`gothic16.gim`) only advance |
| `CDesignString` | `FUN_088c09bc`, table at `0x08c7a868` | `gamedata.c` reads the strings from your EBOOT at run time (`--elf`, default `orig/EBOOT.elf`). Without it the menus draw without text |
| Pad | `FUN_08b1ed50`, queries `FUN_08b0e974` / `ea34` / `ea94` / `eaf4` | `pad.c`, with a fixed repeat delay instead of the game's tables |

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
- The phases after the menus: 0xC (new story game), 7/9 (battle mode), 6 (load or
  network), 10 (attract after 65 s idle).
- Shift-JIS text (`gothic16.gim`, the double-byte table) for the Japanese strings.
- Sound effects for the cursor and buttons (CSound).
