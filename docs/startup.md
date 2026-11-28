# Start-up path

How the game gets from its ELF entry point to its main loop, and how far the PC port runs
it (`chexa-pc --disc DIR --startup`, code in `src/game/startup.c`).

Addresses are splat addresses; Ghidra's are 0x08804000 higher.

## Call chain

| Address | Ghidra | What |
|---|---|---|
| `0x0033630C` | `FUN_08b3a30c` | ELF entry (`module_start`): `sceKernelSetCompiledSdkVersion606(0x06060010)`, `sceKernelSetCompilerVersion(0x30306)`, then creates and starts the `user_main` thread (priority 0x20, 256 KiB stack, attr 0x80000000) with the boot arguments |
| `0x0033642C` | — | `user_main`, the SN Systems crt0: `sbrk(0)`, splits the argument block into at most 20 `argv` strings, sets up newlib's per-thread state on its stack, `atexit(0x0034A5E8)`, `_init` (`0x0034A5E0`, empty), `main(argc, argv)`, `exit`, `sceKernelExitDeleteThread`. The path through `sceKernelGetModuleIdByAddress` is only taken when an optional (weak) runtime hook is linked in, which it isn't |
| `0x0033695C` | `FUN_08b3a95c` | `sbrk`: the first call allocates the whole user heap, `D_0044D8D0` = 18432 KiB, as partition block "UserSbrk" (partition 2, low-aligned, 4 KiB alignment) |
| `0x0013BE0C` | `FUN_0893fe0c` | `main`: static constructors, `CApplication::Init`, the application object, its run loop |
| `0x00342D74` | `FUN_08b46d74` | static constructors: 26 `(function, priority)` pairs at `0x0049BDF8`, sorted by priority (all 0) and called in turn. They set up font and text-style globals and call no imports |
| `0x00001714` | `FUN_08805714` | `CApplication::Init`, below |
| `0x00001C64` | `FUN_08805c64` | the application object (`new` 0x5C bytes, singleton at `DAT_08c518d8`) |
| `0x000024B0` | `FUN_088064b0` | **the main loop**: per frame it updates the current scene, steps the play-time clock (60 ticks a second, capped at 999:59:59), updates sound, flushes graphics and file requests, and checks the buttons. 618 functions are reachable from it; it never returns |

The data segment starts at `0x0049BDF8` (the constructor table). Data references in code
are relative to it, so `D_0002D698` in the disassembly is the default heap object
(Ghidra `DAT_08ccd490`).

## CApplication::Init

| Step | Calls | Port |
|---|---|---|
| 1 | `FUN_08b3a26c`: empty | — |
| 2 | `sceKernelCreateCallback("exit")` + `sceKernelRegisterExitCallback`; the callback calls `sceKernelExitGame`. Then twice `sceKernelCreateCallback("PowerCB")` + `scePowerRegisterCallback(-1)`; the callback (`0x000016AC`) sets its flag to 2 on resume-complete (0x40000) and 1 on suspending (0x10000) | written |
| 3 | `sceKernelChangeCurrentThreadAttr(0, 0x4000)`: VFPU | written |
| 4 | `sceImposeSetLanguageMode(1, 1)`: English, cross confirms; asserts on failure | written |
| 5 | `sceUmdCheckMedium`, `sceUmdWaitDriveStat(PRESENT)` if no disc, `sceUmdActivate(1, "disc0:")`, `sceUmdWaitDriveStat(READY)` | written |
| 6 | newlib `memalign(16, 18432 KiB − 32 KiB)`, then `InitialzeAllocator` (`FUN_08b19f18`) on it as "default heap", filled with 0xCB | written; the allocator is a stand-in (`heap.c`) |
| 7 | `new[]` 0x280000 into `DAT_08ccd818`. `operator new`/`new[]` (`FUN_08b20b50`/`FUN_08b20b98`) allocate from the default heap, tagged `DEFAULT_NEW`/`DEFAULT_VEC_NEW` | written |
| 8 | `FUN_08b0f538(0xCC000)`: `sceGeEdramGetSize`, `sceGeEdramGetAddr`, "fast heap" on video memory above 0xCC000 | written |
| 9 | Random seed (`FUN_08b04698`): `sceUtilityGetSystemParamInt(6)` (time zone) and `sceRtcGetCurrentClock`, the seed being the sum of the date and time fields; then MT19937 (`FUN_08b52254`, state at `DAT_08ca4f4c`) with 624 warm-up draws | written; matches reference MT19937 output |
| 10 | File I/O init (`FUN_08b12698`): 9-entry open-file cache, mutexes "fileio_mtx" and "fileio_mtx2" | written |
| 11 | `PackFile::ReadIndex("chexa.idx")` (`FUN_08b1eb9c`), retried until it succeeds: size via async open + three `sceIoLseek`, buffer from the default heap (64-aligned), read via async seek + read, then offsets in the index turned into pointers (`FUN_08b1ea38`) | written; the port keeps offsets |
| 12 | `OpenFileSet("boot", default heap, from top, sync)` (`FUN_08b14984`): finds the set in the index's sorted set table, allocates "FILSETMAN", reads the set from `common.dat` in one go, maps its `FSTS` header | written |
| 13 | `CLoadModules` (`FUN_0893fb4c`, `FUN_0893fbd4`): two 128 KiB tables, `sceUtilityLoadModule` 0x305, 0x301, 0x300, 0x302, 0x304, 0x303 (the AV codec modules), `ReadFile` of `common/jis2ucs.bin` and `common/ucs2jis.bin` from the boot set, stored as the Shift-JIS conversion tables | written |
| 14 | `FUN_08b1ecb8`: `sceCtrlSetSamplingMode(1)` and the pad state, with masks 0xF3F0, 0x3F0, 0xF0 | written |
| 15 | `CGraphics::GetInstance` (`FUN_08b205c0`, 0x520-byte object with a `CClear` at +0x4F8) and `FUN_08b1fe38` → `FUN_08b75608`, PSPSDK's `sceGuInit`: `sceGeEdramGetAddr`, `sceKernelCreateEventFlag("SceGuSignal")`, `sceGeSetCallback`, `sceGeListEnQueue` of the initial display list, `sceGeListSync`, `sceGeDrawSync` | written (`src/game/render.c`, libgu in `src/platform/gu.c`; see `docs/graphics.md`) |
| 16 | `CSound::GetInstance` (`new` 0x121A0, ctor `FUN_08b35c34`) and a virtual call into it (`FUN_08b3697c`: the SE and BGM tables) | written as `sound_init` (`src/game/sound.c`, `music.c`) |
| 17+ | the manager singletons, `CloseFileSet` of "boot", the "gamecommon" file set, `FUN_08b36470`, `AssignSpecialBufferToUtilitySavedataExternalMemory` | the file sets written; the managers skipped (see below) |

### The end of CApplication::Init

After CSound, `CApplication::Init` (`FUN_08805714`) makes these, in order. None is needed by the port at
start-up; what each one is and where the port does its work:

| Call | What it is | In the port |
|---|---|---|
| `FUN_08b1f904` | a 0x194-byte singleton that sends GE commands (`FUN_08b762bc`): GU state | `render.c`, `gu.c` |
| `FUN_08b24038` | a 0x6158-byte singleton with a state and a 0x200-byte path, used about 200 times: the save-data utility | `savedata.c` and its own dialogs |
| `FUN_08b1fd84` | the texture manager | `picture.c` |
| `FUN_08b08834`, `FUN_08b09830` | callbacks handed to two libraries (three pairs each) | not needed: the port's codecs are its own |
| `FUN_08b22210` | loads `common/simple.gmo` | not used yet |
| `FUN_0889ea30` | the 0xD0690-byte game database | `savedata.c` (`g_save`) |
| `FUN_0885c390` | a 0x24-byte singleton holding three default-heap blocks | not identified |
| `FUN_08a70f1c` | `CShadowMgr`, loads `shadow/kage00.gim` | not ported: OKEs have no shadow |
| `FUN_089f2ecc` | a 0x178-byte scene file-set manager: sets opened per id, counted, closed with `CloseFileSet` | the port's screens open and close their sets themselves (battle.c, entrance.c) |
| `FUN_08b14f70` .. `FUN_08b150f0` | `CFontMngr`, from the boot set's font | `font.c`, on first use (the font files are loose entries too) |
| `FUN_08a35690` | `CPresetStage`: the list of stages for a random pick (0-24 but 13, those whose table word at `0x08c39138` is below 2) | "Select Randomly" picks among the available stages (`battle_menu.c`); no reader of the list was found in the export, so it isn't known to differ |
| `CloseFileSet(boot)`, `OpenFileSet("gamecommon", default heap, from top, sync)` | written | see below |
| `FUN_08b36470(sound, 0)` | `CAplSound`: the system SE bank (`seset_00sys`) | `sound_init` |
| `FUN_08805f38` | `AssignSpecialBufferToUtilitySavedataExternalMemory`: the step-7 buffer for the save dialogs | not needed: the port's dialogs don't use it |

The file sets: "boot" (35 files, 703 KiB) holds two files that aren't also loose pack
entries, `sound/sound_se_tbl.bin` and `sound/sound_bgm_tbl.bin`, and both are read in
step 16 before the game closes it. "gamecommon" (39 files, 245 KiB: the common
`parts/bf` menu and dialog parts, the result banners `parts/bs/rs_*`, the system sound
bank, `adv/tutorial_a.chs`, two preset OKEs, the system save icon) is all loose entries
too. Reads look in the open sets first and then the pack, so neither set changes what
loads, only where from. The port now switches them as the game does: start-up ends with
4451 KiB of the default heap in use instead of 4909.

## After CApplication::Init

After `CApplication::Init`, `main` creates the application object and enters the main
loop. The port runs the graphics part of `CApplication::Start` (`FUN_08806960`: display
set-up `FUN_08b1fe70`, then the first screen's black clear colour) and, every frame,
the graphics part of the loop (`FUN_08b200ec`). The screens and the scene update aren't
ported. The loop ends when the HOME menu's exit callback calls `sceKernelExitGame`
(`chexa-pc --frames N` requests that after N displayed frames, as does closing the window).

## File formats met on the way

`chexa.idx` header (u32s): `"PIDX"`, archive table offset and count (32-byte records,
the first word a name: `common.dat`, `voice.dat`), entry table offset and count (the
`PackEntry` tree), root entry count, file-set table offset and size, string table offset
and size. The file-set table is a count followed by that many offsets (from the table
start) to 32-byte records sorted by name: name, archive, offset, size, member count, two
unknown words, and the offset of the set that follows in the archive.

A file set starts with an `FSTS` header: magic, member count, member table offset, string
table offset. Members are 16 bytes, sorted by name: name offset, data offset from the set
start, unpacked size, stored size (0 when stored as-is, otherwise ARZ data).

## Imports the start-up path calls

In order, with consecutive repeats folded (`--trace` prints each call with its arguments):

```
sceKernelSetCompiledSdkVersion606, sceKernelSetCompilerVersion, sceKernelCreateThread,
sceKernelStartThread,
  -- user_main thread --
sceKernelAllocPartitionMemory, sceKernelGetBlockHeadAddr,                      sbrk
sceKernelCreateCallback, sceKernelRegisterExitCallback,                         exit callback
(sceKernelCreateCallback, scePowerRegisterCallback) x2,                         power callbacks
sceKernelChangeCurrentThreadAttr, sceImposeSetLanguageMode,
sceUmdCheckMedium, sceUmdActivate, sceUmdWaitDriveStat,
sceGeEdramGetSize, sceGeEdramGetAddr,                                           fast heap
sceUtilityGetSystemParamInt, sceRtcGetCurrentClock,                             random seed
sceKernelCreateMutex x2,                                                        file I/O init
  -- ReadIndex: size of chexa.idx, twice (once in ReadIndex, once in the read helper) --
(sceKernelLockMutexCB, sceKernelUnlockMutex, sceIoOpenAsync, sceIoWaitAsyncCB,
 sceIoLseek x3, sceIoCloseAsync, sceIoWaitAsyncCB) x2,
  -- then read it; and the same read pattern for the boot set from common.dat --
(sceKernelLockMutexCB, sceKernelUnlockMutex, sceIoOpenAsync, sceIoWaitAsyncCB,
 sceKernelLockMutexCB, sceIoLseekAsync, sceIoWaitAsyncCB,
 sceKernelDcacheWritebackInvalidateAll, sceIoReadAsync, sceIoWaitAsyncCB,
 sceKernelUnlockMutex, sceIoCloseAsync, sceIoWaitAsyncCB) x2,
sceUtilityLoadModule x6,                                                        CLoadModules
sceCtrlSetSamplingMode,
sceGeEdramGetAddr, sceKernelCreateEventFlag, sceGeSetCallback,                 sceGuInit
sceGeListEnQueue, sceGeListSync, sceGeDrawSync,
  -- CApplication::Start --
sceGeListEnQueue, sceDisplaySetMode, sceGeListUpdateStallAddr, sceGeDrawSync,  display set-up
sceDisplaySetFrameBuf,                                                          sceGuDisplay(1)
  -- each frame --
sceKernelDcacheWritebackRange, sceGeDrawSync,
(sceDisplayWaitVblankStartCB, sceDisplayGetVcount)..., sceDisplayGetVcount x2,
sceDisplaySetFrameBuf, sceGeListEnQueue, sceGeListUpdateStallAddr x2,
  -- on exit --
sceKernelExitGame                                                               from the exit callback
```

44 distinct imports are called (43 by the game plus `sceIoGetstat`, which `chexa-pc`
uses itself to check the disc), and all of them are implemented. Kernel objects,
threads, callbacks, mutexes and event flags are in `src/platform/kernel.c`; the GE and
display imports in `ge.c` and `display.c`.

## What the port does differently

- The game's block allocator is replaced by `heap.c`, which has the same interface but
  its own block format. newlib's `memalign` is a bump of the `sbrk` break, used once.
- The static constructors aren't written yet. Nothing the port runs reads what they set up.
- Pointer fix-ups in the pack index and file sets are replaced by offsets plus bounds checks.
- The file layer's retry loops, which wait forever for the disc on the PSP, give up after
  30 tries.
- `sceUtilityLoadModule` succeeds without loading anything. The codecs are part of the port:
  `sceAtrac3plus` and `sceMpeg` (module 0x303, `PSP_MODULE_AV_MPEGBASE`) decode with the
  system libavcodec.

## Before the title: the logo and the opening movie

After the start-up path, the root screen opens the entrance (`CEntranceMngr`,
`src/game/entrance.c`). Its first update (`FUN_088d502c`) opens phase 3, the logo, once
per run:

| Phase | Game | What happens | Port |
|---|---|---|---|
| 3 | `FUN_088d4a10`, `FUN_088d5bec` | One update: the draw puts `title/logo_a.gim` (ARTDINK) at (112, 72) under an opaque black window, so the frame is black; then phase 4 | The same |
| 4 | `FUN_088d4ae0`, `FUN_088d5c20` | The display off, CRender reset, then the movie player plays `movie/che_op.pmf` (`FUN_08b1d0ec`) on the game thread until it ends or a button stops it; then phase 0 | `movie_play` (`src/game/movie.c`) on the `sceMpeg` imports (`src/platform/video.c`); `--skip-movie` leaves it out |
| 0 → 5 | `FUN_088d5088`, `FUN_088d4624` | The title and the top menu, with the title music | The same |

The movie (39.6 s) opens on black, then the Natsume logo, the CG intro and the title
card the menu then appears over. Its format, the decoder it needs and the player's
threads are in [screens.md](screens.md#opening-movie).
- When `main` returns, `user_main` ends the thread instead of calling newlib `exit`.
