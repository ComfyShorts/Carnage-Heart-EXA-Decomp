# src/game

Decompiled game code goes here, written as C that compiles for the PC port. It calls the
PSP imports declared in `src/platform/psp_imports.h`, which the shim in `src/platform/`
implements on SDL3.

So far:
- `startup.c`: the start-up path: `module_start`, the crt0 `user_main` thread, `sbrk`,
  `main`, and `CApplication::Init` up to the first graphics call. See `docs/startup.md`.
- `fileio.c`: the game's synchronous file layer, `PackFile::ReadIndex`, file sets
  (`OpenFileSet`, `ReadFile` from a set).
- `heap.c`: a stand-in for the game's block allocator (default and fast heaps,
  `operator new`), with the game's interface but not its block format.
- `pgchip.c`/`pgchip.h`: the battle-time chip program interpreter (`CBattleOkePgChip`) and
  the `CPgChipTarget` robot interface.
- `okedb.h`: the 0x1EC0-byte robot design record (`COkeDataBase`) as a C struct.
- `packfile.c`/`lz.c`: the game's file pack index and ARZ decoder, read through `sceIo`.

CMake picks up every `*.c` file here automatically.

Rules:
- Write code by hand from the disassembly. Never paste Ghidra output or anything else
  derived from `orig/`.
- Never put game assets or data tables extracted from the game in the repository. Load
  them at runtime from the user's own dump.
- Struct layouts must match the PSP. Keep the default 32-bit build (`-m32`) so pointers
  and `long` are 4 bytes.
