# src/game

Decompiled game code goes here, written as C that compiles for the PC port. It calls the
PSP imports declared in `src/platform/psp_imports.h`, which the shim in `src/platform/`
implements on SDL3.

The directory is empty for now. CMake picks up every `*.c` file here automatically.

Rules:
- Write code by hand from the disassembly. Never paste Ghidra output or anything else
  derived from `orig/`.
- Never put game assets or data tables extracted from the game in the repository. Load
  them at runtime from the user's own dump.
- Struct layouts must match the PSP. Keep the default 32-bit build (`-m32`) so pointers
  and `long` are 4 bytes.
