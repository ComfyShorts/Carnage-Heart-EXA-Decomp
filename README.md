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
