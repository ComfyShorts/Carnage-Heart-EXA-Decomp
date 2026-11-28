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
- A program is a **square grid of chips**, `width × width`, stored compactly row-major
  (`index = y * width + x`, so a 10×10 program fills the first 100 slots and does not keep
  the 15-wide row stride). The main program has room for 15×15 chips, each sub-program
  for 7×7. **Confirmed** against 205 shipped records: the stored width is the only size
  field, and the programs are only consistent when read this way (see
  [Validation against real data](#validation-against-real-data)).
- Each program has a **start index**. Execution begins there and returns there when
  the main program runs off the grid or hits a Return chip.
- Each chip is an 8-byte `PGCHIP::tagCOMMAND`. After executing, a chip yields one of
  eight directions. The program counter moves one cell in that direction.

## `PGCHIP::tagCOMMAND`: one chip (8 bytes, confirmed size)

| Byte | Bits | Meaning | Confidence |
|---|---|---|---|
| 0 | 0-5 | `eCOMMAND_TYPE` (1..0x35; 0 = `COMMAND_NONE`, asserted never to be executed) | confirmed |
| 0 | 6 | unknown; the interpreter ignores it. Set on every reachable chip in the data | — |
| 0 | 7 | "active" flag: if clear, the chip is skipped, nothing is called, and execution continues along the default direction. The test comes *after* the Return check, so Return chips ignore it | confirmed behaviour, name a guess |
| 1 | 0-3 | default / "process" exit direction (0-7) | confirmed |
| 1 | 4-7 | branch exit direction (0-7), taken when a judge chip's target call returns 1 (condition true) | confirmed |

What the data shows about these bits (205 records; details under Validation):

- Every **reachable non-Return chip** has bits 7 and 6 both set (0xC0 | type). Clear bits
  only appear on Return chips (where the interpreter never looks) and on 16 chips that
  can't be reached from the start.
- **Non-judge chips** carry 0xF (most) or 4 in the branch nibble. Judges (0x0A-0x1C)
  always have 0-7 there. So 0xF is "no branch" and 4 is probably an editor default
  (**likely**).
- Return chips often have byte 1 = 0xCD, MSVC-style uninitialised fill, because they have
  no exit. Empty cells (type 0) hold 0x00/0x80/0xC0 in byte 0 and 0x40/0xFF/0xCD in byte 1.
  None is ever reached.
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
| 0x0000 | 0x30 | header, below | layout from data; meanings guessed |
| 0x0030 | 0xFB0 | hardware design. The CPU part id is at hardware+0x288 (s32, 1-7 in the data) | confirmed (decides the program size, see below) |
| 0x0FE0 | 0xA60 | **software** (programs, below) | confirmed |
| 0x1A40 | 0x460 | **comment block**: 10 × 11-byte tag lists, then 10 × 101-byte Shift-JIS texts | layout confirmed; tag meaning unknown |
| 0x1EA0 | 0x1C | 7 × s32: six zeros and a **colour index** (0-7) in word 6 | likely |
| 0x1EBC | 4 | zero-initialised field; 0 in every record | — |

**Header.**
- +0x00 is a u32, 1 in every record (a version, **guess**).
- +0x04..0x1F is a 28-byte field: a 0 byte, then a NUL-terminated Shift-JIS string of
  up to 26 bytes, then leftover 0xCD/0xFF fill. Probably the design name (**guess**).
  Many shipped records leave it empty.
- +0x20 is a u32, always 0.
- +0x24..0x2F is a 12-byte field with the same shape (0 byte, then ASCII/Shift-JIS up to
  10 bytes). Probably a model code or author tag (**guess**).
- The 0xCD fill means the records were written from uninitialised heap/stack memory by an
  MSVC debug build of the editor (**likely**).

**Comment block (0x1A40).**
- Ten entries. Entry *k* has an 11-byte list at `0x1A40 + 11k` and a 101-byte NUL-terminated
  Shift-JIS text at `0x1AAE + 101k` (up to 100 bytes of text). Together these fill the
  0x460 bytes exactly.
- In default records, entry *k* holds the list `[k+1, 0, …]` and a numbered placeholder
  label. Edited records carry real notes, with lists of up to six small numbers
  (e.g. `[3, 13, 5, 6, 7, 8]`).
- Since the list values are 1-14, they're probably references to other comments or
  palette/marker ids, not chip positions. That's a **guess**; they need the editor code read.
- The block's purpose (chip-area comments/labels) is a **likely** reading of the data, not
  confirmed from code.

**0x1EA0 block.**
- Its constructor (`0x08A77378 / 0x273378`) zeroes six words and sets word 6 to 1. Every
  shipped record holds exactly that.
- A neighbouring accessor (`0x08A773BC`) passes word 6 to a lookup (`0x08A7740C`) that
  returns an entry from an 8-entry 32-bit colour table, or `0xFF3F3F3F` if out of range.
  Another helper cycles an index 0→7→0. So word 6 is a colour index (**likely**); words
  0-5 are unknown.

Accessors: OKE → hardware `0x089EEB2C / 0x1EAB2C`, OKE → software
`0x089EEB1C / 0x1EAB1C`, hardware → CPU id `0x089ED434 / 0x1E9434`.

### Software block (at `COkeDataBase + 0xFE0`)

Constructor `0x089EE02C / 0x1EA02C` clears every chip and counter.

| Offset | Size | Contents |
|---|---|---|
| 0x000 | 225 × 8 = 0x708 | main program chips (15×15 buffer) |
| 0x708 | 49 × 8 = 0x188 | sub-program 1 chips (7×7 buffer) |
| 0x890 | 49 × 8 = 0x188 | sub-program 2 chips (7×7 buffer) |
| 0xA18 | 3 × 4 | grid width for main, sub 1, sub 2 (0 = sub-program not available) |
| 0xA24 | 3 × 4 | start index for main, sub 1, sub 2 (compact `y*width+x`) |
| 0xA30 | 8 × 6 | 8 counters (`PGCHIP::tagCOUNTER`, indexed by `eCOUNTER_TYPE`; read by `CSoftwareDataBase::GetCounter`). In the data each is just a **counter name**: up to 5 ASCII characters plus a NUL. No value or range is stored |

**Program size comes from the CPU part (confirmed against the data).** Each width triple
appears only with these CPU part ids (hardware+0x288):

| CPU part id | Widths (main, sub 1, sub 2) | Records |
|---|---|---|
| 1, 2 | 6, 0, 0 | 33 |
| 3, 4 | 10, 0, 0 | 121 |
| 5, 6, 7 | 15, 7, 7 | 51 |

So only the largest CPUs have sub-programs. Every Call Sub-Program chip in the data sits
in a 15/7/7 record. Whether the game rewrites the widths when a CPU part is swapped
hasn't been checked in code.

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

### Where the shipped records live: the PIDX pack (confirmed)

The game's files sit in `PSP_GAME/USRDIR` as an index, `chexa.idx`, plus two archives,
`common.dat` and `voice.dat`. `tools/chexa_pack.py` lists and extracts them; its
docstring gives the byte layout. In short:

- **Index.** A "PIDX" header, a table of 0x20-byte archive records (name, a 16-byte hash),
  and a flat entry table (6 × u32 per entry). The entries form a directory tree: a
  directory entry gives a child count and first child, a file entry gives the archive,
  offset, unpacked size and stored size. Names are offsets into a string table.
- **Stored files.** Each starts with an 8-byte "ARZ" header (magic `ARZ` or ` 3;`, a
  method digit, the unpacked size), and the payload is XORed with 0x72.
  - Method 0 is stored as-is.
  - Method 1 is classic Okumura LZSS: a 4 KiB ring that starts at 0xFEE, LSB-first flag
    bytes where 1 means literal, and matches of 12-bit position + 4-bit length (plus 3).

Every 0x1EC0-byte file under `preset/` is a raw `COkeDataBase` (205 records):

| Path | Contents |
|---|---|
| `preset/arena/oke_bNN_1.bin` | arena opponents |
| `preset/lecture/oke_lNN_{1,2,a}.bin` | tutorial OKEs (`_a` looks like the model answer) |
| `preset/mission/…` | mission enemies |
| `preset/oke_t01_{1,2}.bin` | test/default designs |

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

### Details settled while writing the C version

`src/game/pgchip.c` reimplements the interpreter by hand. `tests/pgchip_test.c` runs all
205 shipped records through it for 600 ticks each against a scripted robot: no program
counter ever lands on an empty cell or leaves its grid. These points come from reading
the code closely (**confirmed**):

- **Accumulator.** The step accumulator at +0x04 is a float and the speed at +0x00 an
  int. The fraction carries over between ticks.
- **History and last result.** History is a fixed array of 256 entries of 16 bytes, with
  the count at +0x1000. Entries past 256 are dropped, not wrapped. A chip that was already
  busy when entered records nothing. The **last-result field is only written when a
  history buffer is passed in**, but the last position is always written.
- **Poll timing.** Halt, Wait, Halt Action and the movement chips 0x1E-0x25 poll on the
  same visit that issues the order, so a short action can finish within the step that
  started it. Select and Fire, Firing Direction and Shoot Reticle only poll from the next
  visit.
- **Categories and interrupts.** The movement chips poll "is action of category *k*
  finished" with *k* = 1 Move, 2 Rotate, 4 Jump, 3 Firing Direction, 0 for the rest. Each
  poll first asks Action Assessment (+0xE8) with value 8, and a 1 there also ends the wait.
- **Halt Action** turns busy only when the target call returns non-zero, then polls
  category 0.
- **Button Input** with b2 = 0 passes "wait" to the target and stays busy until the
  target reports a press through an out-parameter.
- **Signedness.** Parameter bytes are loaded unsigned, except b3 of Assess Analog and
  Firing Direction (signed). The 16-bit halves at b2 are unsigned; those at b4 and b6
  are signed in the target's parameter list. For a few whole-byte b6/b7 arguments
  (e.g. Scan for Obstacles) the signedness wasn't pinned down. Every shipped value is
  below 0x80, so it doesn't matter for real data.
- **Bad directions.** An exit direction above 7 would index past the direction table in
  the game. The C version treats it as leaving the grid; no reachable chip in the data
  has one.
- **Speed table.** The per-CPU speed comes from a table at `0x08BD5F88 / 0x3D1F88`,
  indexed by CPU part id. It isn't copied into the repo, so `pgchip_setup` takes the
  speed as an argument.

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

## `PGCHIP::tagRANGE`: the 4-byte scan/aim area (bytes 2-5)

Used by the chips the editor's `CSoftDesignMngr::GetCommandRange` (`0x08A876AC`, via
`0x08A8AE60`) accepts: 0x0B, 0x0C, 0x0D, 0x0E, 0x13, 0x14, 0x26, 0x2E and 0x2F. The
editor's polar preview (`l_PolarDraw`, `0x08A0DD10`) decodes it as follows:

| Byte | Meaning | Confidence |
|---|---|---|
| 2 | bits 1-7: inner distance step; bit 0: a flag (unknown; set in about 11% of range chips) | likely |
| 3 | half-width of the arc in degrees; the arc spans direction ± this, so 180 = full circle | likely (5-180 in data, 90 and 45 most common) |
| 4 | outer distance step | likely (0-110 in data) |
| 5 | arc direction in 2° units (×2, then values above 180 wrap to negative) | likely |

The "steps" become distances through `PGCHIP::CRange`'s piecewise scale. There are five
scale levels (`ms_ScaleLevelCount`), each with a base step, base length and magnification
(`GetScaleBaseMeter` / `GetScaleBaseLength` / `GetScaleMagnification`,
`0x089FC14C..0x089FC264`). `0x089FC37C` maps a step to a distance and `0x089FC410` maps
it back, so larger steps cover coarser distances. The table values (in
`.rodata`, `0x08C1CAB4` / `0x08C1CB7C` / `0x08C1CBD0`) aren't listed here.

## Parameter value ranges seen in the data

Ranges for the chips in the 205 shipped records (all programs, empty and Return chips
excluded). Bytes not listed were always 0, which confirms that the byte layout in the
tables below doesn't over-read.
`tools/okedb.py dump` prints the decoded fields for a record.

- 0x01 NOP, 0x32 Cancel Lock-On: no parameters (all zero).
- 0x02 Halt: b2 = 1-120 (so up to 4 s at 1/30 s).
- 0x04 Call: b2 = 0/1.
- 0x09 Button Input: b2 always 1.
- 0x0F Status Assessment: b3 = 1-100, likely a percentage; b2 = 0-2 and b4 = 0/1,
  likely an enum and a comparison.
- 0x10 Action Assessment: b2 = 0-16.
- 0x11 Random Branching: b2 = 1-80, likely a percentage.
- 0x12 Assess Time: b2 = 1-200.
- 0x16 Assess Target Code: b2 = 1-255.
- 0x17 Lock-On Alert: b2 = 1/2.
- 0x18 Target Weapon: b2 = 0-3, b3 = 0-16.
- 0x19 Line of Fire: b2 = 0-3.
- 0x1D Halt Action: b2 = 0-3.
- 0x2B Shoot Reticle: b2 = 0-5, b3 = 0-2, b4 = 1-16, b5 = 0/1 (the no-wait flag).
- 0x2C Set Flying Altitude: b2 = 20-100, b3 = 0-2.
- 0x2D Activate Option: b2 = 0-3.
- 0x30 Change Sight Location: b2 = 1-3.
- 0x31 Counter and Lock-On: b2 = 0-7, likely a counter index.
- 0x33 Change Camera View: b3 always 2.
- Range chips: b2 bit 0 is set in 374 of 3302 range chips; b6/b7 are small enums (e.g. Scan for
  OKE b7 = 1, 2 or 17; Lock-On b7 = 0-6).

All units except the wait flags, counter indices, and the range layout are **guesses**
until the target functions are read.

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
| 0x05 | Input Counter | +0x160 | b2 = counter index (0-7, confirmed in data), b3 |
| 0x06 | Control Counter | +0x168 | 16-bit at b2, 16-bit at b4, b6 low nibble (always 0-7: likely a counter index) / high nibble (mostly 8), b7 |
| 0x07 | Send Counter | +0x190 | b2 = counter index (0-7), b3 (0/1) |
| 0x08 | Receive Counter | +0x198 | b2 = counter index (0-7), b3 (0/1) |
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
| 0x1A | Assess Counter | +0x188 | 16-bit at b2, 16-bit at b4, b6 nibbles (low always 0-7, high mostly 8), b7 |

### Movement and actions (multi-tick: issue the order, then poll until done)

| Type | Name | Target method | Target slot | Params |
|---|---|---|---|---|
| 0x1D | Halt Action | (cancels the current action; `CBattleOke::SetScript` underneath) | +0x40 | b2 = action category |
| 0x1E | Move | wrapper around `CBattleOke::SetPgChipCommandMove(dir)` (**likely**) | +0x50 | b2 = direction (0-7), b3 = no-wait flag, s16 at b4 = amount (0-150 seen) |
| 0x1F | Rotate | — | +0x60 | b2 (0/1, likely left/right), b3 = no-wait flag, s16 at b4 = angle (0-180 seen, likely degrees) |
| 0x20 | Jump | `CBattleOke::SetPgChipCommandJump` (confirmed) | +0x68 | b2 (0-8, likely a direction with 8 = straight up), b3 = no-wait flag |
| 0x21 | Quick Movement | `CBattleOke::SetPgChipCommandRapidMove` (confirmed) | +0x70 | b2 = direction (0-7), b3 = no-wait flag |
| 0x22 | Quick Rotation | — | +0x78 | b2 (0/1), b3 = no-wait flag |
| 0x23 | Attack | — | +0x80 | b2 (0-3), b3 = no-wait flag |
| 0x24 | Defense | — | +0x88 | b2 (0/1), b3 (5-60 seen, likely a duration), b4 = no-wait flag |
| 0x25 | Special Move | — | +0x90 | b2 (0-2), b3 = no-wait flag |
| 0x26 | Select and Fire | `CBattleOke::SetPgChipCommandShoot(tagRANGE, …)` (confirmed) | +0xA0 | range at b2-5, b6 split 3/5 bits, b7 low nibble; b7 high nibble = no-wait flag |
| 0x27 | Firing Direction | `CBattleOke::SetPgChipCommandDirectionalShoot` (confirmed) | +0xA8 | b2 = unsigned angle, b3 = signed angle (both in 2° units; the editor's `CSoftDesignMngr` helper `0x08A8B0B8`/`0x08A8B1A4` scales them ×200, i.e. to hundredths of a degree), b4 (2-5), b5 (1-16); b6 = no-wait flag |

**Wait flags (confirmed from the interpreter):**
- The interpreter sets the busy flag, so the chip waits for the action to finish, only
  when the target call returned 0 *and* the chip's wait byte is zero. A non-zero byte
  means "continue at once".
- The wait byte is b3 for 0x1E-0x23 and 0x25, b4 for Defense, the high nibble of b7 for
  Select and Fire, b6 for Firing Direction and b5 for Shoot Reticle.
- In the data these bytes are only ever 0 or 1.

The 16-bit amount in Move and Rotate is read from bytes 4-5. The old "b2, b3" reading
missed it.

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

## Editing

The editor's rules, as `src/game/pgchip_edit.c` ports them (the screen is
`src/game/oke_soft.c`, see `docs/screens.md` "Software Setup").

- `CPgChipArea` (`pgchipdata.cpp`): a resizable `width × height` chip grid on the heap
  (8 bytes per chip). Width at +0x08, height at +0x0C, a start position at +0x10.
  Functions: resize `0x089F7AD4`, `GetIndex(x,y)` `0x089F7C9C`, `GetPosX` `0x089F7D0C`,
  `GetPosY` `0x089F7D98`, in-bounds test `0x089F7E24`, `GetDirPosition` `0x089F7E5C`.
- **Directions** 0-7 are up, up-right, right, down-right, down, down-left, left, up-left
  (steps at `0x08C1C3F8`). All eight can be exits (`FUN_089f7a20`). The next free
  direction (`FUN_08a1ec84`) is searched from the current one + step, skipping the
  chip's other exit.
- **Incoming** (`FUN_089f77f8`): the neighbours whose exit (or a judge's branch) points
  into the cell, plus one for the start column's top cell, entered from above.
- **Returns** (`FUN_08a1dff4`): an empty cell with something pointing in gets a Return
  (`35 40 00 00 00 00 00 00`, bit 7 set by the next rule); a Return nothing points into
  becomes blank (`00 40 00 ...`).
- **Bit 6** (`FUN_08a1e2ac`): set on a real chip with something pointing in.
- **Errors** (`GetCursorPgChipErrorType` `FUN_08a1e710`): -1 for blank cells and
  Returns; 2 for a Call Sub-Program in the main area when the CPU lacks either
  sub-area (0x28F); 0 for a chip nothing points into (0x28D); otherwise -1.
- **Bit 7** (`FUN_08a1e578`): set on every cell whose error is -1 or 0, cleared
  otherwise, so only a misplaced chip is switched off.
- **After an edit at a cell** (`FUN_08a194c4` and the others): Returns and bit 6 for
  the cell and its 8 neighbours (`FUN_08a1e230`, `FUN_08a1e4fc`), bit 7 for the cell.
  A placed chip whose exit isn't a direction gets the first free one; a judge's branch
  likewise.
- **NormalizeArea** (`FUN_08a190dc`): a start off the area goes to column 0, then every
  cell gets the three updates. The editor runs it on opening. On the 205 shipped
  records it changes only bit 7 on blank cells and Returns in 66 programs.
- **ClearArea** (`FUN_08a18ce4`, Delete All): every cell blank; the normalize then puts
  a Return on each start.
- **Default chips** (`FUN_089fcd54`, per-type values at `0x08C1D0B0`): byte 1 is 0x40
  (exit up, branch down), both status bits clear, the range bytes `00 2D 32 00`, the
  rest per type. A new game copies one per type into `CSoftwareSceneDataBase`
  (`+0x41370` in the shared block, `FUN_0889c724`); placing a chip copies from there
  and writes the finished chip back (`FUN_08a1cbd8`), so the next one of that type
  starts the same. After them: `+0x1B0` the chip list's page (4, all chips), `+0x1B4`
  its cursor on each of the 5 pages, `+0x1C8` eight macros of 0x72C bytes
  (`PGCHIP::tagMACRO`, `GetMacro`, not ported).
- **Parameters** (`GetSubMenuValue` `FUN_08a959c8`, `SetSubMenuValue` `FUN_08a96284`):
  each type lists up to 8 sub-menus (`0x08C2DCFC`, 0x24 bytes a type: a count, then
  the ids, Confirm = 0 last). A value is a byte, a signed byte, a 16- or 32-bit word,
  a nibble, or (Battle Function 0x26) 3 + 5 bits of byte 6; `pgchip_edit.c` has the
  whole table. Move (0x1E) and Rotate (0x1F) with distance / angle 0 (endless) read
  processing mode 1 and have byte 3 forced to 1 when written. Counter chips (6, 0x1A)
  keep a counter in byte 6's low nibble and the parameter in its high one, 8 meaning
  "Value", the 32-bit number in bytes 2-5. Every listed value of every chip in the 205
  shipped records reads and writes back unchanged.
- **Ranges** (`PGCHIP::tagRANGE`, bytes 2-5 of chips 0x0B-0x0E, 0x13, 0x14, 0x26,
  0x2E, 0x2F): byte 2 bit 0 the shape. A sector: byte 2 >> 1 the inner distance step,
  byte 3 the half-width in 2-degree units (0..0xB4), byte 4 the outer step (0..0x6E,
  never below the inner), byte 5 the bearing in 2-degree units (0..0xB3, clockwise from
  ahead). A rectangle: byte 4 / 5 its centre right / ahead (signed steps, -0x6E..0x6E),
  byte 3 its width and byte 2 >> 1 its depth (0..0x6E). Steps become distances through
  five scale levels (`FUN_089fc37c`, tables `0x08C1CB7C` / `0x08C1CBD0` /
  `0x08C1CAB4`) and back (`FUN_089fc410`). Changing the shape (`FUN_089fc9ac`) turns a
  rectangle's centre into a distance and bearing, and a sector's far point into a
  rectangle's centre with a square as wide as the arc.
- **Angles**: Select and Fire (0x27) keeps direction / elevation in bytes 2 / 3 in
  2-degree units (rounded away from zero, `FUN_089fc4dc`); Turn Turret (0x2A) in
  hundredths of a degree, u16 at byte 4 and s16 at byte 6, edited in steps of 1 degree,
  or 0.1 when its byte 2 isn't 0.
- `CPgChipWnd`, `CPgChipSelectWnd`, `CPgChipInfoWnd`, `CPgChipRefWnd`, `CSoftDesignMngr`
  and `CSoftDesignFlow` form the program editor UI ("software design"). Undo uses
  `CPgChipAreaBufferBackup` (one step). `CPgChipDeformArea` / `CPgChipDeformElementSet`
  turn and flip a block while it's moved or pasted (not ported).

## Validation against real data

`tools/okedb.py validate orig/pack/preset` parses all 205 shipped records (extract them
first with `tools/chexa_pack.py extract 'preset/*'`) and checks them against this
document. All checks pass:

- **Widths.** The width triple is always (6,0,0), (10,0,0) or (15,7,7), and it matches
  the CPU part. Starts are inside the grid.
- **Grid layout.** Following the interpreter's rules, a static flood fill from the start
  index:
  - never reaches an empty cell;
  - never meets an exit nibble above 7;
  - never leaves the compact `y*width+x` grid in a way the interpreter wouldn't treat as
    "back to start".

  This holds for all 307 programs. Reading the smaller grids with a 15-wide row stride
  doesn't give consistent programs.
- **Chip bits.** Every reachable non-Return chip has bits 7 and 6 set. Types are always
  1-0x35, and all 53 types appear.
- **Sub-programs.** Every Call Sub-Program chip (213 of them) targets a sub-program
  whose width is non-zero.
- **Counters.** Counter-chip indices are below 8. Counter names are ≤5 printable ASCII
  characters.
- **Ranges.** Every range chip's arc width is 0-360°.
- **Other blocks.** The 0x1EA0 block's colour index is 0-7.

Leftovers: in 13 records, cells *beyond* `width²` (inside the 15×15/7×7 buffer) still hold
chips. There are 145 such chips in main programs and 10 in each sub-program. They match
neither layout and are never executed. They look like stale data from when the design had
a bigger CPU (**likely**). Such data is harmless, because the interpreter only addresses
`width²` cells.

## Open questions

- **Byte 0 bit 6.** Ignored by the interpreter and set on every live chip. Probably an
  editor flag (e.g. "placed" or "selected"), but no code reading it has been found.
- **Comment tag lists** (the 11-byte lists in the 0x1A40 block) and words 0-5 of the
  0x1EA0 block: what they refer to is unknown.
- **Header fields** at +0x04 and +0x24: the name/model reading is a guess from the
  shape of the strings.
- **Parameter units and enums.** Byte positions, the no-wait flags, counter indices and
  the tagRANGE layout are settled. Most other parameter meanings (speeds, distances,
  enums like `ACTION` or `ATTRIBUTE`) are guesses from value ranges until each target
  function is read. The `CRange` scale tables also need decoding to turn steps into
  metres.
- **Width vs CPU part in code.** The data ties them together, but the code that sets the
  widths when the CPU part changes hasn't been found.
- **`CPgChipTarget::RET` values.** Only 1 (true/branch) and the default 2 are clear. The
  console has a glyph for each value up to `RET_MAX`, but the full list isn't mapped.
- **The speed unit.** `speed × dt` with a default of 2. Whether `dt` is seconds or frames,
  and the per-CPU values, haven't been checked.
- **Save format.** Whether `DATA.BIN` holds plain `COkeDataBase` images (the shipped
  preset files do) is unverified.
- **Slots without assert names.** For those, behaviour is inferred from the name table
  plus argument shapes, not from the target code.
