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
game is included or needed yet: for now it opens a window and runs an empty frame loop.

| Path | Contents |
|---|---|
| `src/game/` | decompiled game C (empty so far) |
| `src/platform/` | PSP shim: one file per subsystem (`files`, `input`, `threads`, `graphics`, `audio`, `video`, `net`, `system`) with a stub for each of the 299 imports in `docs/imports.md`, plus the SDL3 window layer |
| `src/pc/` | entry point |

The default build is 32-bit (`-m32`) so pointers and `long` are 4 bytes and decompiled
structs keep their PSP layout.

```sh
tools/setup_pc_deps.sh                         # no root: multilib sysroot + 32-bit SDL3 in .deps/
cmake --preset m32 && cmake --build --preset m32
build/pc-m32/chexa-pc                          # --frames N, --scale N, --call-all-stubs
```

Needs `cmake`, `ninja-build` and `build-essential`. `setup_pc_deps.sh` fetches Ubuntu's
multilib and 32-bit X11 packages with `apt-get download` and unpacks them into `.deps/`, so
`gcc-multilib` isn't required. Installing these system-wide works too:
`gcc-multilib libx11-6:i386 libxext6:i386 libxrandr2:i386 libxcursor1:i386 libxi6:i386`
(after `dpkg --add-architecture i386`). For OpenGL rendering and sound, add
`libgl1:i386 libpulse0:i386` and rebuild SDL3 with PulseAudio enabled. Without them SDL
uses its software renderer and dummy audio.

Stubs log their first call (set `PSP_SHIM_LOG=all` to log every call), and a call summary
prints at exit. They're declared `int32_t name(void)` until each import gets its real
signature. `tools/gen_shim_stubs.py` generated them once; edit the files directly from now on.
