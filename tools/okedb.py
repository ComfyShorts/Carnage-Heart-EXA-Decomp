#!/usr/bin/env python3
"""Parse and validate COkeDataBase robot-design records (0x1EC0 bytes each).

Layout and chip encoding are documented in docs/program-format.md; this script checks
real records against that description. Get records with tools/chexa_pack.py, e.g.

    python3 tools/chexa_pack.py extract 'preset/*'          # -> orig/pack/preset/...
    python3 tools/okedb.py validate orig/pack/preset
    python3 tools/okedb.py dump orig/pack/preset/arena/oke_b01_1.bin [OUT.txt]

Records and dumps are game data: keep them under orig/ (git-ignored), never commit them.
"""
import collections
import struct
import sys
from pathlib import Path

RECORD_SIZE = 0x1EC0
HW = 0x0030                 # hardware block
HW_CPU = HW + 0x288         # CPU part id (s32)
SW = 0x0FE0                 # software block
PROG_OFF = (0x000, 0x708, 0x890)   # chip buffers inside the software block
PROG_CAP = (15, 7, 7)              # buffer capacity (width) per program
SW_WIDTH = 0xA18                   # 3 x s32
SW_START = 0xA24                   # 3 x s32
SW_COUNTERS = 0xA30                # 8 x 6-byte names
COMMENTS = 0x1A40                  # 10 x 11-byte tag lists, then 10 x 101-byte texts
COMMENT_TEXT = COMMENTS + 10 * 11
COLOR_BLOCK = 0x1EA0               # 7 x s32; word 6 = colour index 0-7

TYPE_RETURN = 0x35
JUDGES = range(0x0A, 0x1D)         # chips that use the branch nibble
RANGE_TYPES = {0x0B, 0x0C, 0x0D, 0x0E, 0x13, 0x14, 0x26, 0x2E, 0x2F}
DIRS = [(0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1)]

# Names from the game's PGCHIP::GetCommandName table (see docs/program-format.md).
CHIP_NAMES = {
    0x01: "NOP", 0x02: "Halt", 0x03: "Wait", 0x04: "Call Sub-Program",
    0x05: "Input Counter", 0x06: "Control Counter", 0x07: "Send Counter",
    0x08: "Receive Counter", 0x09: "Button Input", 0x0A: "Determine Remaining",
    0x0B: "Assess Battle Area", 0x0C: "Scan for OKE", 0x0D: "Scan for Obstacles",
    0x0E: "Scan for Projectiles", 0x0F: "Status Assessment", 0x10: "Action Assessment",
    0x11: "Random Branching", 0x12: "Assess Time", 0x13: "Assess Target Loc.",
    0x14: "Assess Location From Target", 0x15: "Assess Target Mov.",
    0x16: "Assess Target Code", 0x17: "Assess Target Lock-On Alert",
    0x18: "Assess Target Weapon", 0x19: "Assess Line of Fire", 0x1A: "Assess Counter",
    0x1B: "Button Assignment", 0x1C: "Assess Analog", 0x1D: "Halt Action", 0x1E: "Move",
    0x1F: "Rotate", 0x20: "Jump", 0x21: "Quick Movement", 0x22: "Quick Rotation",
    0x23: "Attack", 0x24: "Defense", 0x25: "Special Move", 0x26: "Select and Fire",
    0x27: "Firing Direction", 0x28: "Counter-Type Reticle", 0x29: "Start Reticle",
    0x2A: "Move Reticle", 0x2B: "Shoot Reticle", 0x2C: "Set Flying Altitude",
    0x2D: "Activate Option", 0x2E: "Lock-On", 0x2F: "Auto-Rotate Targ.",
    0x30: "Change Sight Location", 0x31: "Counter and Lock-On", 0x32: "Cancel Lock-On",
    0x33: "Change Camera View", 0x34: "Alert", 0x35: "Return",
}


class Chip:
    __slots__ = ("raw",)

    def __init__(self, raw):
        self.raw = raw

    type = property(lambda s: s.raw[0] & 0x3F)
    flags = property(lambda s: s.raw[0] >> 6)          # bit 7 = active, bit 6 = unknown
    default_dir = property(lambda s: s.raw[1] & 0x0F)
    branch_dir = property(lambda s: s.raw[1] >> 4)
    params = property(lambda s: s.raw[2:8])

    def range(self):
        """tagRANGE at bytes 2-5: (inner steps, flag bit, width deg, outer steps, dir deg)."""
        b0, b1, b2, b3 = self.raw[2:6]
        direction = b3 * 2                  # 2-degree units, wrapped to -180..180
        if direction > 180:
            direction -= 360
        return b0 >> 1, b0 & 1, b1 * 2, b2, direction


class Program:
    def __init__(self, index, chips, width, start):
        self.index, self.width, self.start = index, width, start
        self.chips = chips          # full buffer (capacity^2), compact row-major w*w used

    def cell(self, i):
        return self.chips[i]

    def reachable(self):
        """Static flood fill from the start chip, following the interpreter's rules."""
        w = self.width
        seen, todo, problems = set(), [self.start], []
        while todo:
            i = todo.pop()
            if i in seen:
                continue
            seen.add(i)
            c = self.chips[i]
            if c.type == 0:
                problems.append(f"empty cell {i} reached")
                continue
            if c.type == TYPE_RETURN:
                continue
            exits = [c.default_dir]
            if c.type in JUDGES and c.flags & 2:
                exits.append(c.branch_dir)
            for e in exits:
                if e > 7:
                    problems.append(f"chip {i} type {c.type:#x} exit nibble {e}")
                    continue
                x, y = i % w + DIRS[e][0], i // w + DIRS[e][1]
                if 0 <= x < w and 0 <= y < w:      # leaving the grid = back to start
                    todo.append(y * w + x)
        return seen, problems


class OkeRecord:
    def __init__(self, data, name="?"):
        if len(data) != RECORD_SIZE:
            raise ValueError(f"{name}: {len(data)} bytes, expected {RECORD_SIZE:#x}")
        self.d, self.name = data, name
        self.cpu = self.s32(HW_CPU)
        widths = struct.unpack_from("<3i", data, SW + SW_WIDTH)
        starts = struct.unpack_from("<3i", data, SW + SW_START)
        self.programs = []
        for p in range(3):
            base = SW + PROG_OFF[p]
            chips = [Chip(data[base + 8 * i:base + 8 * i + 8]) for i in range(PROG_CAP[p] ** 2)]
            self.programs.append(Program(p, chips, widths[p], starts[p]))
        self.counters = [self.cstr(SW + SW_COUNTERS + 6 * k, 6) for k in range(8)]
        self.comment_tags = [list(data[COMMENTS + 11 * k:COMMENTS + 11 * k + 11]) for k in range(10)]
        self.comment_len = [self.d[COMMENT_TEXT + 101 * k:COMMENT_TEXT + 101 * (k + 1)].find(b"\0")
                            for k in range(10)]
        self.color_block = struct.unpack_from("<7i", data, COLOR_BLOCK)

    def s32(self, off):
        return struct.unpack_from("<i", self.d, off)[0]

    def cstr(self, off, size):
        raw = self.d[off:off + size]
        return raw[:raw.find(b"\0")] if b"\0" in raw else raw


def validate(rec):
    """Return a list of (check, ok, detail) for one record."""
    out = []
    w = [p.width for p in rec.programs]
    out.append(("widths are 6/0/0, 10/0/0 or 15/7/7", tuple(w) in {(6, 0, 0), (10, 0, 0), (15, 7, 7)}, w))
    expect = {1: 6, 2: 6, 3: 10, 4: 10, 5: 15, 6: 15, 7: 15}.get(rec.cpu)
    out.append(("main width matches CPU part", expect == w[0], f"cpu {rec.cpu} width {w[0]}"))
    for p in rec.programs:
        if p.width <= 0:
            continue
        tag = ("main", "sub1", "sub2")[p.index]
        out.append((f"{tag} start inside grid", 0 <= p.start < p.width ** 2, p.start))
        seen, problems = p.reachable()
        out.append((f"{tag} reachable chips never hit empty cells / bad exits", not problems, problems[:3]))
        used = [i for i in range(p.width ** 2) if p.chips[i].type]
        out.append((f"{tag} chip types in 1..0x35", all(1 <= p.chips[i].type <= 0x35 for i in used), ""))
        bad_flags = [i for i in seen if p.chips[i].type not in (0, TYPE_RETURN) and p.chips[i].flags != 3]
        out.append((f"{tag} reachable non-Return chips have bits 7+6 set", not bad_flags, bad_flags[:5]))
        for i in used:
            c = p.chips[i]
            if c.type == 0x04:
                ok = rec.programs[1 + (c.params[0] != 0)].width > 0
                out.append((f"{tag} call at {i} targets an existing sub-program", ok, c.params[0]))
            if c.type in (0x05, 0x07, 0x08):
                out.append((f"{tag} counter chip {i} index < 8", c.params[0] < 8, c.params[0]))
            if c.type in RANGE_TYPES:
                inner, _, width_deg, outer, _ = c.range()
                out.append((f"{tag} range chip {i} width 0..360", 0 <= width_deg <= 360, width_deg))
    out.append(("counter names are <=5 ASCII chars",
                all(len(n) <= 5 and all(32 <= ch < 127 for ch in n) for n in rec.counters), rec.counters))
    out.append(("colour block word 6 is 0..7", 0 <= rec.color_block[6] <= 7, rec.color_block))
    return out


def load_dir(path):
    for f in sorted(Path(path).rglob("*.bin")):
        data = f.read_bytes()
        if len(data) == RECORD_SIZE:
            yield OkeRecord(data, str(f))


def cmd_validate(path):
    totals = collections.Counter()
    failures = []
    n = 0
    for rec in load_dir(path):
        n += 1
        for check, ok, detail in validate(rec):
            # group per-chip checks ("main range chip 17 ...") into one line per kind
            words = check.split()
            key = " ".join("#" if w.isdigit() and i > 1 else w for i, w in enumerate(words))
            totals[(key, ok)] += 1
            if not ok:
                failures.append((rec.name, check, detail))
    print(f"{n} records")
    for key in sorted({k for k, _ in totals}):
        print(f"  {'PASS' if not totals[(key, False)] else 'FAIL'}  {key}: "
              f"{totals[(key, True)]} ok, {totals[(key, False)]} failed")
    for name, check, detail in failures[:20]:
        print(f"    {name}: {check}: {detail}")
    return 1 if failures else 0


def cmd_dump(path, out=None):
    rec = OkeRecord(Path(path).read_bytes(), path)
    lines = [f"record {path}", f"cpu part {rec.cpu}",
             f"counter names {[n.decode('ascii') for n in rec.counters]}",
             f"colour block {rec.color_block}",
             f"comment tag lists {[t[:6] for t in rec.comment_tags]}",
             f"comment text lengths {rec.comment_len}"]
    for p in rec.programs:
        if p.width <= 0:
            continue
        lines.append(f"program {p.index}: {p.width}x{p.width}, start {p.start} "
                     f"(x={p.start % p.width}, y={p.start // p.width})")
        for i in range(p.width ** 2):
            c = p.chips[i]
            if not c.type:
                continue
            extra = ""
            if c.type in RANGE_TYPES:
                inner, flag, wdeg, outer, ddeg = c.range()
                extra = f" range(inner={inner} flag={flag} width={wdeg}deg outer={outer} dir={ddeg}deg)"
            br = f" branch={c.branch_dir}" if c.type in JUDGES else ""
            lines.append(f"  ({i % p.width:2},{i // p.width:2}) {c.type:#04x} "
                         f"{CHIP_NAMES.get(c.type, '?'):<28} flags={c.flags} "
                         f"exit={c.default_dir}{br} params={c.params.hex(' ')}{extra}")
    text = "\n".join(lines) + "\n"
    if out:
        Path(out).parent.mkdir(parents=True, exist_ok=True)
        Path(out).write_text(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "validate":
        sys.exit(cmd_validate(sys.argv[2]))
    if len(sys.argv) >= 3 and sys.argv[1] == "dump":
        cmd_dump(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
        sys.exit(0)
    sys.exit(__doc__)
