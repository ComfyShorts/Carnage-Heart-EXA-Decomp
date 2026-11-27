#!/usr/bin/env python3
"""List and extract files from the game's PIDX pack (chexa.idx + common.dat/voice.dat).

Format (worked out from the game's packfile.c / lz.c code; see docs/program-format.md):

Index header (little-endian u32s):
  +0x00 magic "PIDX"   +0x04 archive-table offset  +0x08 archive count
  +0x0C entry-table offset  +0x10 entry count  +0x14 root entry count
  +0x18 sorted-name table offset  +0x1C its size  +0x20 string-table offset  +0x24 its size
Archive record (0x20 bytes): name (string offset), 12 bytes unknown, 16-byte hash.
Entry (6 x u32), the first `root count` entries are the root directory:
  directory: 1, name, child count, first child index, 0, 0
  file:      0, name, archive name, offset, unpacked size, stored size
Strings are offsets into the string table.

Stored data is "ARZ": 8-byte header ("ARZ" or " 3;", then a method digit, then the
unpacked size as a u32), payload bytes XOR 0x72. Method 0 is stored; method 1 is LZSS
(4 KiB ring buffer starting at 0xFEE, flag byte LSB-first, 1 = literal; a match is
12-bit position + 4-bit length, length + 3).

Extracted files are game data: keep them in orig/ (git-ignored), never commit them.

Usage:
  chexa_pack.py list [GLOB]
  chexa_pack.py extract GLOB [OUT_DIR]      (default OUT_DIR: orig/pack)
"""
import fnmatch
import struct
import sys
from pathlib import Path

USRDIR = Path("/mnt/c/psp/chexa/PSP_GAME/USRDIR")


class Pack:
    def __init__(self, usrdir=USRDIR):
        self.dir = Path(usrdir)
        d = (self.dir / "chexa.idx").read_bytes()
        if d[:4] != b"PIDX":
            raise ValueError("not a PIDX index")
        (_, self.arc_off, self.arc_n, ent_off, ent_n, root_n,
         _, _, self.str_off, _) = struct.unpack_from("<10I", d, 0)
        self.d = d
        entries = [struct.unpack_from("<6I", d, ent_off + 24 * i) for i in range(ent_n)]
        self.files = {}
        self._walk(entries, 0, root_n, "")

    def _str(self, off):
        o = self.str_off + off
        return self.d[o:self.d.index(b"\0", o)].decode("ascii")

    def _walk(self, entries, first, count, prefix):
        for i in range(first, first + count):
            kind, name, a, b, c, e = entries[i]
            if kind == 1:
                self._walk(entries, b, a, prefix + self._str(name) + "/")
            else:
                self.files[prefix + self._str(name)] = (self._str(a), b, c, e)

    def read(self, path):
        archive, off, size, stored = self.files[path]
        with open(self.dir / archive, "rb") as f:
            f.seek(off)
            data = arz_decode(f.read(stored))
        if len(data) != size:
            raise ValueError(f"{path}: unpacked {len(data)} bytes, index says {size}")
        return data


def arz_decode(buf):
    if buf[:3] not in (b"ARZ", b" 3;"):
        raise ValueError("not ARZ data")
    method = int(chr(buf[3]), 16)
    size = struct.unpack_from("<I", buf, 4)[0]
    src = bytes(b ^ 0x72 for b in buf[8:])
    if method == 0:
        return src[:size]
    if method != 1:
        raise ValueError(f"unknown ARZ method {method}")
    ring = bytearray(0x1000)
    pos = 0xFEE
    out = bytearray()
    i = 0
    flags = 0
    while len(out) < size and i < len(src):
        flags >>= 1
        if not flags & 0x100:
            flags = src[i] | 0xFF00
            i += 1
            if i >= len(src):
                break
        if flags & 1:
            b = src[i]
            i += 1
            out.append(b)
            ring[pos] = b
            pos = (pos + 1) & 0xFFF
        else:
            if i + 1 >= len(src):
                break
            lo, hi = src[i], src[i + 1]
            i += 2
            start = lo | (hi & 0xF0) << 4
            for k in range((hi & 0x0F) + 3):
                b = ring[(start + k) & 0xFFF]
                out.append(b)
                ring[pos] = b
                pos = (pos + 1) & 0xFFF
    return bytes(out[:size])


def main(argv):
    if len(argv) < 2 or argv[1] not in ("list", "extract"):
        sys.exit(__doc__)
    pack = Pack()
    pattern = argv[2] if len(argv) > 2 else "*"
    paths = [p for p in sorted(pack.files) if fnmatch.fnmatch(p, pattern)]
    if argv[1] == "list":
        for p in paths:
            arc, off, size, stored = pack.files[p]
            print(f"{p}\t{arc}\t0x{off:08X}\t{size}\t{stored}")
        return
    out = Path(argv[3] if len(argv) > 3 else "orig/pack")
    for p in paths:
        dst = out / p
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(pack.read(p))
    print(f"extracted {len(paths)} files to {out}")


if __name__ == "__main__":
    main(sys.argv)
