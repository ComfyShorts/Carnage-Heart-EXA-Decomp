# Carnage Heart EXA decomp (non-matching)

Decompilation of Carnage Heart EXA for PSP (NPUH10188). You need your own dump; nothing from
`iso/` or `orig/` is committed.

## Setup

Requirements: Python 3 with venv, GNU binutils for `mipsel-linux-gnu` (Debian/Ubuntu:
`python3-venv binutils-mipsel-linux-gnu`).

1. Put the decrypted EBOOT at `orig/EBOOT.elf` (SHA1 `8c0eeaf5ce4fe43bbfd8ee7ef1a6bf47ac5fda38`).
2. Run:

```sh
make setup   # .venv + splat64
make split   # splat -> asm/, assets/, eboot.ld (multiple passes, see below)
make check   # assemble + link + objcopy, compare SHA1 against orig/EBOOT.elf
```

## How the split works

- `config/eboot.yaml` covers the whole ELF file (header, both LOAD segments, padding,
  relocation tables and section headers), so `make check` is a byte-for-byte comparison
  of the full file.
- The EBOOT is a relocatable PRX linked at base 0, so spimdisasm reads lots of small
  integers as pointers. `tools/gen_reloc_overrides.py` checks every symbol reference
  against the PRX relocation table. Unrelocated references are written as constants
  (`config/reloc_addrs_auto.txt`) and their addresses are banned as symbols
  (`config/reloc_addrs_auto_ignore.txt`). `make split` repeats this until no new
  overrides appear. These two files contain values taken from the game, so they are
  generated locally and never committed. Put hand-written overrides in `config/reloc_addrs.txt`.
- `tools/asm_fixups.py` runs on each `.s` before `as`. It handles what GNU as can't take
  as-is: VFPU instructions (emitted as `.word`), little-endian NaN doubles, and
  Shift-JIS strings (emitted as raw bytes).
- `config/symbol_addrs.txt` names the 299 import stubs. See `docs/imports.md`.

## PC port (work in progress)

A native build that runs decompiled game code against a PSP shim on SDL3. Nothing from the
game is included. It reads the game's files from your own dump at runtime. With
`--startup` it runs the game's own start-up code (entry point, C runtime start-up, `main`,
`CApplication::Init`) and the graphics side of its main loop, drawing through a
software GE renderer, up to the title screen and its Story Mode / Battle Mode menu.
Battle Mode leads to the game's Battle Mode screens (OKE Configuration, Team
Formation, the Test Program, the Battle Arena) and a battle between teams of up to three robots running
their chip programs. See `docs/startup.md`, `docs/graphics.md`,
`docs/screens.md` and `docs/battle.md`. Without
it, it opens a window and runs an empty frame loop. The chip interpreter and pack reader
in `src/game/` are exercised by the tests.

| Path | Contents |
|---|---|
| `src/game/` | hand-written game C: start-up path (`startup.c`), CRender frame loop (`render.c`), the title screens (`entrance.c`), Battle Mode (`battle.c`, `battle_menu.c`, `oke_menu.c` with `oke_parts.c`, `team_menu.c`, `battle_sim.c`), GIM textures and sprites (`picture.c`), bitmap text (`font.c`), pad (`pad.c`), strings read from your EBOOT (`gamedata.c`), the game's file layer and file sets (`fileio.c`), heap allocator stand-in (`heap.c`), chip program interpreter (`pgchip.c`), robot record layout (`okedb.h`), pack index and ARZ reader (`packfile.c`, `lz.c`) |
| `src/platform/` | PSP shim: one file per subsystem (`files`, `input`, `threads`, `graphics`, `audio`, `video`, `net`, `system`) with a stub for each of the 299 imports in `docs/imports.md`, the implemented kernel imports (`kernel.c`), libgu (`gu.c`), the GE interpreter and software renderer (`ge.c`), the display (`display.c`), plus the SDL3 window layer |
| `src/pc/` | entry point |
| `tests/` | `chexa-io-test` (file shim), `chexa-gu-test` (renderer pixels), `chexa-pgchip-test` (loads robot records from the pack and steps them) and `chexa-battle-test` (headless battles between preset designs) and `chexa-oke-parts-test` (OKE Configuration's hardware rules against the preset designs) |

The default build is 32-bit (`-m32`) so pointers and `long` are 4 bytes and decompiled
structs keep their PSP layout.

```sh
tools/setup_pc_deps.sh                         # no root: multilib sysroot + 32-bit SDL3 in .deps/
cmake --preset m32 && cmake --build --preset m32
build/pc-m32/chexa-pc --disc /path/to/disc     # also --ms DIR, --saves DIR, --frames N, --scale N
build/pc-m32/chexa-pc --disc /path/to/disc --startup   # run the game's start-up path (--trace: log every import call)
build/pc-m32/chexa-pc --disc /path/to/disc --startup --gu-trace 3 --screenshot   # sceGu calls of 3 frames, first frame to orig/
build/pc-m32/chexa-pc --disc /path/to/disc --startup --shot 60 orig/title.png --press 70 circle --shot 100 orig/menu.png
CHEXA_DISC=/path/to/disc ctest --test-dir build/pc-m32
```

`--disc` takes the extracted disc root (the folder holding `PSP_GAME/`) or its
`PSP_GAME/USRDIR` folder, and serves it read-only as `disc0:`, where the game looks for
`disc0:/PSP_GAME/USRDIR/...`. Lookups are case-insensitive like the UMD. `--ms` serves a
writable folder as the memory stick (`ms0:`). Without it the game is told no memory
stick is inserted. Save data (`sceUtilitySavedata*`) goes in the folder given with
`--saves DIR`, default `saves/` (gitignored), one directory per save as on a memory
stick; `--make-test-save` writes a Battle Data save holding twelve of the shipped preset
OKEs and two teams of them (see "Save data" in `docs/screens.md`). The file and UMD imports are implemented (sync and async open, read,
write, seek, close, directory listing, stat, mkdir, rename). The tests skip themselves
(exit 77) when the game files aren't found; they default to `$CHEXA_DISC`, then
`/mnt/c/psp/chexa`.

Needs `cmake`, `ninja-build` and `build-essential`. `setup_pc_deps.sh` fetches Ubuntu's
multilib and 32-bit X11 packages with `apt-get download` and unpacks them into `.deps/`, so
`gcc-multilib` isn't required. Installing these system-wide works too:
`gcc-multilib libx11-6:i386 libxext6:i386 libxrandr2:i386 libxcursor1:i386 libxi6:i386`
(after `dpkg --add-architecture i386`). For OpenGL rendering and sound, add
`libgl1:i386 libpulse0:i386` and rebuild SDL3 with PulseAudio enabled. Without them SDL
uses its software renderer and dummy audio.

Every import logs its first call, implemented or not, with arguments and result for the
implemented ones (`--trace` or `PSP_SHIM_LOG=all` logs every call). At exit a summary
lists every import called, in first-call order, marking the ones that are still stubs.
Stubs are declared `int32_t name(void)` until each import gets its real signature.
`tools/gen_shim_stubs.py` generated them once; edit the files directly from now on.
