# Chip programs: storage format and interpreter

How Carnage Heart EXA stores an OKE's chip program (the grid-based robot AI) and how
battle code runs it each tick.

Sources: the game's own `__PRETTY_FUNCTION__`/assert strings (class, method, enum and
source-file names), a local Ghidra export used as read-only reference, and our splat
disassembly. Nothing here is copied code; layouts and behaviour are described in prose.
Confidence is marked as **confirmed** (read directly from code or named by an assert),
**likely** (strong inference) or **guess**.

## Address conventions

Ghidra loaded the PRX at `0x08804000`. Splat/our repo uses base 0. To convert:
`splat = ghidra - 0x08804000` (so `FUN_08857748` is `func_00053748`). Both are given
below as `ghidra / splat`.

Relevant source files (from assert strings):

| Source file | Contents |
|---|---|
| `src/product/battleokepgchip.cpp` | `CBattleOkePgChip`: battle-time interpreter |
| `src/product/battleoke.cpp` | `CBattleOke`: the robot in battle; implements `CPgChipTarget` |
| `src/product/pgchipdata.cpp` | `PGCHIP` namespace: chip types, names, `CPgChipArea`, counters |

## Concepts

- An OKE carries **three programs**, indexed by the enum `PGCHIP_PROGRAM` (asserted
  `< PGCHIP_PROGRAM_MAX`, which is 3): `0` = main program, `1` and `2` = sub-programs.
- A program is a **square grid of chips**, `width × width`, stored row-major
  (`index = y * width + x`). The main program has room for 15×15 chips, each sub-program
  for 7×7. The active width is stored separately, so smaller grids use only the top-left
  part of the buffer (**likely**; see open questions).
- Each program has a **start index**. Execution begins there and returns there when
  the main program runs off the grid or hits a Return chip.
- Each chip is an 8-byte `PGCHIP::tagCOMMAND`. After executing, a chip yields one of
  eight directions. The program counter moves one cell in that direction.

## `PGCHIP::tagCOMMAND`: one chip (8 bytes, confirmed size)

| Byte | Bits | Meaning | Confidence |
|---|---|---|---|
| 0 | 0-5 | `eCOMMAND_TYPE` (1..0x35; 0 = `COMMAND_NONE`, asserted never to be executed) | confirmed |
| 0 | 6 | unknown | — |
| 0 | 7 | "active" flag: if clear, the chip is skipped, nothing is called, and execution continues along the default direction | confirmed behaviour, name a guess |
| 1 | 0-3 | default / "process" exit direction (0-7) | confirmed |
| 1 | 4-7 | branch exit direction (0-7), taken when a judge chip's target call returns 1 (condition true) | confirmed |
| 2-7 | | per-type parameters (see the chip table). Read as signed/unsigned bytes, 16-bit halves, nibbles, a 3+5-bit split, or a 4-byte `PGCHIP::tagRANGE` at bytes 2-5 | confirmed per type |

Directions (`PGCHIP::eCONNECT_DIR_TYPE`, 0-7) step through the 8 neighbours clockwise,
starting with "up" (y−1): 0 up, 1 up-right, 2 right, 3 down-right, 4 down, 5 down-left,
6 left, 7 up-left. The same table appears twice, at `0x08BD66DC / 0x3D26DC` (battle) and
`0x08C1C3F8 / 0x41C3F8` (editor). Return value 8 is a pseudo-direction meaning "Return".

There is also an unpacked class form, `PGCHIP::CCommand`, built with
`CCommand::FromStruct(tagCOMMAND)` and validated with `CheckValid()`. The editor works on
`CCommand`; the battle interpreter reads `tagCOMMAND` bytes directly.

## Stored form: `COkeDataBase` (0x1EC0 bytes, confirmed size)

One OKE's complete design (hardware plus software). Constructor `0x089EEA80 / 0x1EAA80`.

| Offset | Size | Contents | Confidence |
|---|---|---|---|
| 0x0000 | 0x30 | header: name object at +0x04, misc fields at +0x20 / +0x24 | likely |
| 0x0030 | 0xFB0 | hardware design. The CPU part id is at hardware+0x288 | likely |
| 0x0FE0 | 0xA60 | **software** (programs, below) | confirmed |
| 0x1A40 | 0x460 | unknown block (own constructor) | — |
| 0x1EA0 | 0x1C | unknown block (own constructor) | — |
| 0x1EBC | 4 | zero-initialised field | — |

Accessors: OKE → hardware `0x089EEB2C / 0x1EAB2C`, OKE → software
`0x089EEB1C / 0x1EAB1C`, hardware → CPU id `0x089ED434 / 0x1E9434`.

### Software block (at `COkeDataBase + 0xFE0`)

Constructor `0x089EE02C / 0x1EA02C` clears every chip and counter.

| Offset | Size | Contents |
|---|---|---|
| 0x000 | 225 × 8 = 0x708 | main program chips (15×15 buffer) |
| 0x708 | 49 × 8 = 0x188 | sub-program 1 chips (7×7 buffer) |
| 0x890 | 49 × 8 = 0x188 | sub-program 2 chips (7×7 buffer) |
| 0xA18 | 3 × 4 | grid width for main, sub 1, sub 2 |
| 0xA24 | 3 × 4 | start index for main, sub 1, sub 2 |
| 0xA30 | 8 × 6 | 8 counter definitions (`PGCHIP::tagCOUNTER`, indexed by `eCOUNTER_TYPE`; read by `CSoftwareDataBase::GetCounter`) |

Getter pairs (address-of, then value) for each field sit at
`0x089EE138..0x089EE1C0 / 0x1EA138..0x1EA1C0`.

### On disk and in saves

- `COkeDataBase` records are read **raw, 0x1EC0 bytes at a time**, by the game's
  `ReadFile(name, buffer, size)` (`0x08B1385C / 0x30F85C`). Examples are mission enemy
  OKEs in `CBattleSelectScene` (`0x08884FB0 / 0x80FB0`) and another loader at
  `0x088098E0 / 0x58E0`. So the in-memory layout above is also the file format for these
  records (**confirmed** for those loaders).
- Arrays of 32 and 34 `COkeDataBase` exist (team/user databases), and a routine at
  `0x0889F260 / 0x9B260` copies 32 of them verbatim into a larger buffer (**likely**
  carry-in/backup). Saves go through `sceUtilitySavedata` into `DATA.BIN`. Whether a
  save holds plain `COkeDataBase` images, and whether the savedata utility encrypts them,
  has **not been checked**.

## Battle-time state: `CBattleOkePgChip` (0x1E4 bytes)

It lives inside `CBattleOke` at byte offset `+0x1EF0` (built in what looks like the
`CBattleOke` constructor, `0x0884AAF8 / 0x46AF8`). Its constructor is
`0x08856CF4 / 0x52CF4`. The OKE's `COkeDataBase*` is at `CBattleOke + 0x94`. A thunk at
`0x0883EEA8 / 0x3AEA8` passes it to Setup.

| Offset | Contents |
|---|---|
| 0x00 | speed: chips per unit time (constructor default 2, then set from the CPU part) |
| 0x04 | fractional step accumulator |
| 0x08 | current program (`PGCHIP_PROGRAM`) |
| 0x0C | `CProgram[3]` (0x24 each) |
| 0x78 | call-stack depth (`m_iCurrentStack`, asserted `< ms_iStackMax`, 10) |
| 0x7C | saved `CProgram` stack, 10 × 0x24 |

### `CBattleOkePgChip::CProgram` (0x24 bytes)

| Offset | Contents | Confidence |
|---|---|---|
| 0x00 | pointer to the chip array (into the OKE's software block) | confirmed |
| 0x04 | grid width (the "program size" getter returns this) | confirmed |
| 0x08 | start index | confirmed |
| 0x0C | current index (program counter) | confirmed |
| 0x10 | last run position | confirmed (getter named by assert) |
| 0x14 | last run result (`CPgChipTarget::RET`) | confirmed (getter named by assert) |
| 0x18 | busy flag: 1 while a multi-tick chip waits for its action to finish | confirmed behaviour |
| 0x1C | program id (0 main, 1/2 sub). Also decides what running off the grid means | confirmed |
| 0x20 | exit request: 0 none, 1 call sub 1, 2 call sub 2, 3 return to caller | confirmed |

### Functions

| Ghidra / splat | Name (from assert string, or inferred) | Role |
|---|---|---|
| `0x08857280 / 0x53280` | Setup (inferred) | Points the three `CProgram`s at the OKE's software block. Copies width and start index, sets current = last position = start, tags program ids 0/1/2, sets speed from a CPU-part table (`0x08856704 / 0x52704`, table at `0x08BD5F88`) |
| `0x088573DC / 0x533DC` | `CBattleOkePgChip::Update` | Per tick: turns `speed × dt` into a whole number of chip steps, runs the current program, and handles sub-program call/return using the save stack |
| `0x08856FEC / 0x52FEC` | `CBattleOkePgChip::CProgram::Update(CPgChipTarget*, COkePgChipHistory*, int& steps, int)` | Runs chips until the step budget is used up, a busy chip blocks, or an exit is requested |
| `0x08857748 / 0x53748` | execute one chip (inferred; its assert string is a `CBattleOkePgChip` method returning `unsigned char`) | Decodes the chip, calls the matching `CPgChipTarget` virtual, logs history, returns the exit direction |
| `0x088571E8 / 0x531E8` | advance (inferred) | Moves the program counter one cell in a direction. Off the grid: main wraps to start, a sub-program requests return |
| `0x08856D6C`, `…E0C`, `…EAC`, `…F4C` | `GetProgramCommand`, `GetProgramSize`, `GetLastRunPosition`, `GetLastRunResult` | accessors (asserted `0 <= program < PGCHIP_PROGRAM_MAX`) |
| `0x08856D64 / 0x52D64` | current-program getter (inferred) | |
| `0x08840494 / 0x3C494` | `CBattleOke` per-frame update (inferred) | calls `CBattleOkePgChip::Update(dt, target, history)`. The target is `CBattleOke + 0x2C` (its `CPgChipTarget` subobject). The history comes from a manager singleton (`0x0885EBDC` → `0x08860344`), so it may be shared or null; this is unverified |

### Execution model, per tick

1. `Update` adds `speed × dt` to the accumulator. The integer part is the step budget for
   this tick and the fraction carries over.
2. `CProgram::Update` runs the current program. For each step it executes the chip at
   the current index:
   - An **inactive** chip (byte 0 bit 7 clear) calls nothing and exits along its default
     direction.
   - **Type 0x35 (Return)** yields pseudo-direction 8. In the main program that jumps back
     to the start index; in a sub-program it requests a return to the caller.
   - A **judge** chip calls a query on the target. If it returns 1 the chip exits along
     the branch direction, otherwise along the default direction.
   - An **action** chip issues an order to the robot. Several are multi-tick: the first
     visit issues the order and sets the busy flag, and later ticks poll the target until
     the action is finished (or interrupted). While busy the program stays on that chip
     and the tick ends early. Finishing a wait refunds that step to the budget.
3. When a chip's result is recorded, a history entry is pushed (target timestamp, program
   id, position, result; `0x089F376C` adds to `COkePgChipHistory`). The last run
   position and result are also updated; these drive the in-battle chip console
   (`CIndicatorPgChipConsole`) and area display (`CIndicatorPgChipArea`).
4. The program counter then moves one cell in the chip's exit direction (`advance`).
   Leaving the grid acts like Return.
5. **Sub-programs:** a Call Sub-Program chip (type 0x04) sets exit request 1 or 2
   (parameter byte 2 zero → sub 1, otherwise sub 2). It is refused (result 2) when the
   stack is full (depth ≥ 10). `Update` pushes the caller's `CProgram`, switches to the
   sub-program and resets it to its start index. On return (exit request 3) it pops the
   caller back and advances it along the call chip's default direction. Sub-programs can
   therefore call each other recursively up to depth 10.

## The robot interface: `CPgChipTarget`

The interpreter never touches the robot directly. It calls virtuals on a
`CPgChipTarget*`, which `CBattleOke` implements as a secondary base at `+0x2C` (hence
the vtable this-adjust of −0x2C).
In `CBattleOke`'s vtable group that sub-table starts at `0x08C8F4FC / 0x48B4FC`. Entries
are SNC-style, 8 bytes each (a 16-bit this-adjust, then a function pointer). Results are
`CPgChipTarget::RET` values; the interpreter treats 1 as "condition true / branch"
(**confirmed**) and 2 as "could not execute" (**likely**).

Shared slots used across many chips (confirmed from the call pattern; names inferred):

| Slot offset | Function | Role |
|---|---|---|
| +0x10 | `0x0883D530 / 0x39530` | timestamp for history (reads a counter in `CBattleOke`) |
| +0x18 / +0x20 | `0x08843FC0` / `0x08843FE8` | Halt: start a timer of *n* × 1/30 s / poll whether it has expired |
| +0x28 | `0x08844010` | "is action of category *k* finished?" (used by Wait and the movement chips) |
| +0x30 | `0x0884421C` | "is firing finished?" (used by Select and Fire and Shoot Reticle) |
| +0xE8 | `0x08846040` | Action Assessment query; the action chips also use it with value 8 as an interrupt check |

Calls Ghidra never defined as functions (they're only reached through the vtable) were
read from the splat disassembly.

## Chip types: `PGCHIP::eCOMMAND_TYPE`

53 types, `COMMAND_BEGIN` = 1 to `COMMAND_END` = 0x36. The names come from the game's
`PGCHIP::GetCommandName` table (`0x089FD570 / 0x1F9570`, pointers at
`0x08C85100 / 0x481100`). `GetCommandShortName` (`0x089FD5FC`, table at `0x08C851D8`)
falls back to the full name. Chips are grouped by what they do in the interpreter.

"Target slot" is the `CPgChipTarget` vtable offset the chip calls. A named method means
the slot's function carries that method's assert string. "Params" lists the chip bytes
passed to the call.

### Flow and control

| Type | Name | Behaviour | Target slot | Params |
|---|---|---|---|---|
| 0x01 | NOP | does nothing, exits along the default direction | — | — |
| 0x02 | Halt | multi-tick: waits *n*/30 s | +0x18, +0x20 | b2 = duration |
| 0x03 | Wait | multi-tick: waits until the action category in b2 finishes | +0x28 | b2 |
| 0x04 | Call Sub-Program | requests a sub-program call (see execution model) | — | b2: 0 → sub 1, else sub 2 |
| 0x35 | Return | back to start (main) or return to caller (sub) | — | — |

### Counters and input

| Type | Name | Target slot | Params |
|---|---|---|---|
| 0x05 | Input Counter | +0x160 | b2, b3 |
| 0x06 | Control Counter | +0x168 | 16-bit at b2, 16-bit at b4, b6 low/high nibbles, b7 |
| 0x07 | Send Counter | +0x190 | b2, b3 |
| 0x08 | Receive Counter | +0x198 | b2, b3 |
| 0x09 | Button Input | +0x1B0 | b2 (zero = multi-tick wait for input) |
| 0x1B | Button Assignment (judge) | +0x1A8 | 16-bit at b2, b4 |
| 0x1C | Assess Analog (judge) | +0x1B8 | b2, b3, b4 |

### Judges (branch when the query returns 1)

| Type | Name | Target slot | Params |
|---|---|---|---|
| 0x0A | Determine Remaining | +0xB8 | b4, 16-bit at b2, b5 |
| 0x0B | Assess Battle Area | +0xC0 | 4 bytes at b2 |
| 0x0C | Scan for OKE (`CBattleOke::IsPgChipCommandJudgeOke(tagRANGE, …)`, confirmed) | +0xC8 | range at b2-5, b6/b7 nibbles |
| 0x0D | Scan for Obstacles | +0xD0 | range at b2-5, b6, b7 |
| 0x0E | Scan for Projectiles | +0xD8 | range at b2-5, b6, b7 nibbles |
| 0x0F | Status Assessment | +0xE0 | b2, b3, b4 |
| 0x10 | Action Assessment | +0xE8 | b2 |
| 0x11 | Random Branching | +0xF0 | b2 |
| 0x12 | Assess Time | +0xF8 | 16-bit at b2, b4, b5 |
| 0x13 | Assess Target Loc. | +0x110 | 4 bytes at b2 |
| 0x14 | Assess Location From Target | +0x118 | 4 bytes at b2, b6 |
| 0x15 | Assess Target Mov. | +0x120 | b2 |
| 0x16 | Assess Target Code | +0x128 | b2 |
| 0x17 | Assess Target Lock-On Alert | +0x130 | b2 |
| 0x18 | Assess Target Weapon | +0x138 | b2, b3 |
| 0x19 | Assess Line of Fire | +0x158 | b2 |
| 0x1A | Assess Counter | +0x188 | 16-bit at b2, 16-bit at b4, b6 nibbles, b7 |

### Movement and actions (multi-tick: issue the order, then poll until done)

| Type | Name | Target method | Target slot | Params |
|---|---|---|---|---|
| 0x1D | Halt Action | (cancels the current action; `CBattleOke::SetScript` underneath) | +0x40 | b2 = action category |
| 0x1E | Move | wrapper around `CBattleOke::SetPgChipCommandMove(dir)` (**likely**) | +0x50 | b2 = direction (0-7), b3 |
| 0x1F | Rotate | — | +0x60 | b2, b3 |
| 0x20 | Jump | `CBattleOke::SetPgChipCommandJump` (confirmed) | +0x68 | b2, b3 |
| 0x21 | Quick Movement | `CBattleOke::SetPgChipCommandRapidMove` (confirmed) | +0x70 | b2, b3 |
| 0x22 | Quick Rotation | — | +0x78 | b2, b3 |
| 0x23 | Attack | — | +0x80 | b2, b3 |
| 0x24 | Defense | — | +0x88 | b2, b3 |
| 0x25 | Special Move | — | +0x90 | b2, b3 |
| 0x26 | Select and Fire | `CBattleOke::SetPgChipCommandShoot(tagRANGE, …)` (confirmed) | +0xA0 | range at b2-5, b6 split 3/5 bits, b7 low nibble |
| 0x27 | Firing Direction | `CBattleOke::SetPgChipCommandDirectionalShoot` (confirmed) | +0xA8 | b2, b3, b4, b5 |

The b3 (or b7 high nibble) byte in these chips decides whether the chip waits for the
action to finish or continues at once (**likely**).

### Aiming, lock-on and misc

| Type | Name | Target method | Target slot | Params |
|---|---|---|---|---|
| 0x28 | Counter-Type Reticle | — | +0x180 | b4, b2, b3 |
| 0x29 | Start Reticle | — | +0x1C0 | b2 |
| 0x2A | Move Reticle | — | +0x1C8 | b2, 16-bit at b4, 16-bit at b6 |
| 0x2B | Shoot Reticle | `CBattleOke::SetPgChipCommandGunSightShoot` (confirmed) | +0x1D0 | b2, b3, b4 (multi-tick unless b5 set) |
| 0x2C | Set Flying Altitude | — | +0x98 | b3, b2 |
| 0x2D | Activate Option | — | +0xB0 | b2 |
| 0x2E | Lock-On | — | +0x100 | 4 bytes at b2, b6 nibbles, b7 |
| 0x2F | Auto-Rotate Targ. | — | +0x140 | 4 bytes at b2, b6 |
| 0x30 | Change Sight Location | — | +0x148 | b2 |
| 0x31 | Counter and Lock-On | — | +0x170 | b2 |
| 0x32 | Cancel Lock-On | — | +0x108 | — |
| 0x33 | Change Camera View | — | +0x1A0 | b2, b3 |
| 0x34 | Alert | — | +0x1D8 | b2, b3, b4 |

`CBattleOke::SetPgChipCommandCounterShoot` exists in the vtable at +0x178, but no chip
calls it directly. It's probably used through Counter-Type Reticle or Counter and Lock-On.

Parameter enums named in asserts (value meanings not yet mapped): `ACTION`, `AIM`,
`ALERT_DISPLAY_TYPE`, `ALERT_SOUND_TYPE`, `ALTITUDE_ASSIGN`, `ANALOG_AXIS_TYPE`,
`ATTRIBUTE`, `AUTO_TURN`, `BASE_TIME`, `BRANCH`, `BUTTON_BRANCH_TYPE`, `BUTTON_TYPE`,
`ACTION_CATEGORY` (the "cancel type"), `CARTRIDGE`, `CHANNEL_NUMBER`, `COUNTER_TYPE`,
`WEAPON_NUMBER_TYPE`, and `DIR_*` for directions.

## Editor side (for reference)

- `CPgChipArea` (`pgchipdata.cpp`): a resizable `width × height` chip grid on the heap
  (8 bytes per chip). Width at +0x08, height at +0x0C, a start position at +0x10.
  Functions: resize `0x089F7AD4`, `GetIndex(x,y)` `0x089F7C9C`, `GetPosX` `0x089F7D0C`,
  `GetPosY` `0x089F7D98`, in-bounds test `0x089F7E24`, `GetDirPosition` `0x089F7E5C`.
- `CPgChipWnd`, `CPgChipSelectWnd`, `CPgChipInfoWnd`, `CPgChipRefWnd`, `CSoftDesignMngr`
  and `CSoftDesignFlow` form the program editor UI ("software design"). Undo uses
  `CPgChipAreaBufferBackup`. `CPgChipDeformArea` / `CPgChipDeformElementSet` convert
  between a "deformed" layout and normal grid positions; their purpose isn't understood.
- `CSoftwareSceneDataBase::GetMacro` returns `PGCHIP::tagMACRO` (chip macros/templates).

## Open questions

- **Rectangular vs square.** The editor's `CPgChipArea` has separate width and height,
  but the stored format keeps one width per program and the interpreter assumes a square
  grid (`y = index / width`, bounds-checked against width). Either battle programs are
  always square or the stored "width" means something else. Not checked against real
  data.
- **Where the active width comes from.** Probably the CPU part or the game mode; the
  setup code just copies it from the software block.
- **Byte 0 bit 6** of a chip is unused by the interpreter. It may be an editor flag.
- **`CPgChipTarget::RET` values.** Only 1 (true/branch) and the default 2 are clear. The
  console has a glyph for each value up to `RET_MAX`, but the full list isn't mapped.
- **Exact meaning of each chip parameter.** Byte positions are confirmed from the
  interpreter, but units and enums (for example range encoding in `tagRANGE`, speeds,
  angles) need the target functions read one by one.
- **The speed unit.** `speed × dt` with a default of 2. Whether `dt` is seconds or frames,
  and the per-CPU values, haven't been checked.
- **Save format.** Whether the savedata blob contains plain `COkeDataBase` images, and
  what the 0x1A40 and 0x1EA0 blocks are, is unverified.
- **Slots without assert names.** For those, behaviour is inferred from the name table
  plus argument shapes, not from the target code.
