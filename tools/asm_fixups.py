#!/usr/bin/env python3
"""Build-time fixups so splat/spimdisasm asm reassembles byte-for-byte with GNU as.

Applied to each .s file on its way to the assembler; the generated asm/ stays untouched.
Each fix uses the raw bytes spimdisasm writes in line comments ("/* ROM VRAM BYTES */"):

1. VFPU instructions -> `.word`. GNU binutils accepts -march=allegrex but has no VFPU.
   The only VFPU instructions with label operands here are bvt/bvf/bvtl/bvfl, which are
   PC-relative and stay correct as long as the function isn't moved. A VFPU instruction
   with a %hi/%lo relocation is a hard error.
2. `.double nan`/`inf` -> two `.word`s. spimdisasm's NaN filter assumes big-endian word
   order, so on little-endian it lets real NaN/Inf doubles through, and gas can't
   reproduce their payloads.
3. String blocks with non-ASCII characters -> `.byte` with the original bytes, taken from
   the `/* HEXBYTES */` comment spimdisasm writes after each string block. gas would
   otherwise write UTF-8, and re-encoding isn't safe: cp932 has duplicate codes
   (e.g. FC4A and EEEB both decode to the same kanji). The codec is only used to work out
   the block's length.

Usage: asm_fixups.py [--encoding CODEC] IN.s OUT.s
"""
import argparse
import re
import sys

INSN = re.compile(
    r"^(?P<pre>\s*/\*\s*[0-9A-F]+\s+[0-9A-F]+\s+(?P<bytes>[0-9A-F]{8})\s*\*/\s*)"
    r"(?P<mnem>[a-z][a-z0-9.]*)(?P<rest>.*)$"
)
# No base MIPS/Allegrex CPU mnemonic starts with 'v'.
VFPU = re.compile(r"^(v[a-z0-9.]*|[ls]v[lr]?\.[sq]|mtv|mfv|mtvc|mfvc|bv[tf]l?)$")
DOUBLE = re.compile(
    r"^(?P<pre>\s*/\*\s*[0-9A-F]+\s+[0-9A-F]+\s+(?P<bytes>[0-9A-F]{16})\s*\*/\s*)"
    r"\.double\s+(?P<val>-?(nan|inf))\s*$"
)
RAWCMT = re.compile(r"^\s*/\*\s*(?P<bytes>[0-9A-F]+)\s*\*/\s*$")
STRING = re.compile(r'^(?P<pre>.*?)\.(?P<dir>ascii|asciz)\s+"(?P<body>(?:[^"\\]|\\.)*)"\s*$')
ESC = {"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, '"': 34, "a": 7, "b": 8, "f": 12, "v": 11}


def le_word(hexbytes):
    return int.from_bytes(bytes.fromhex(hexbytes), "little")


def string_bytes(body, encoding):
    out = bytearray()
    i = 0
    while i < len(body):
        c = body[i]
        if c == "\\":
            n = body[i + 1]
            if n == "x":
                m = re.match(r"[0-9A-Fa-f]{1,2}", body[i + 2:])
                out.append(int(m.group(0), 16))
                i += 2 + len(m.group(0))
                continue
            if n in "01234567":
                m = re.match(r"[0-7]{1,3}", body[i + 1:])
                out.append(int(m.group(0), 8))
                i += 1 + len(m.group(0))
                continue
            out.append(ESC[n])
            i += 2
            continue
        out += c.encode(encoding)
        i += 1
    return bytes(out)


def fix_line(line, encoding, src):
    m = INSN.match(line)
    if m and VFPU.match(m["mnem"]):
        if "%" in m["rest"]:
            sys.exit(f"{src}: VFPU instruction with relocation, can't emit .word: {line.strip()}")
        orig = (m["mnem"] + m["rest"]).replace("/*", "").replace("*/", "").rstrip()
        return f"{m['pre']}.word 0x{le_word(m['bytes']):08X}  # {orig}"
    m = DOUBLE.match(line)
    if m:
        b = m["bytes"]
        return f"{m['pre']}.word 0x{le_word(b[:8]):08X}, 0x{le_word(b[8:]):08X}  # double {m['val']}"
    return line


def fix_strings(lines, encoding, src):
    """Replace non-ASCII .ascii/.asciz blocks with their raw bytes."""
    out = []
    i = 0
    while i < len(lines):
        m = STRING.match(lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue
        j = i
        block = []
        while j < len(lines) and (sm := STRING.match(lines[j])):
            block.append(sm)
            j += 1
        raw = RAWCMT.match(lines[j]) if j < len(lines) else None
        if all(b["body"].isascii() for b in block):
            out.extend(lines[i:j])
            i = j
            continue
        if not raw:
            sys.exit(f"{src}:{i + 1}: non-ASCII string without raw-bytes comment")
        n = sum(len(string_bytes(b["body"], encoding)) + (b["dir"] == "asciz") for b in block)
        data = bytes.fromhex(raw["bytes"])[:n]
        if len(data) != n:
            sys.exit(f"{src}:{i + 1}: raw-bytes comment shorter than string block")
        text = " ".join(b["body"] for b in block)
        out.append(f"{block[0]['pre']}.byte " + ", ".join(f"0x{x:02X}" for x in data) + f"  # \"{text}\"")
        i = j
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--encoding", default="cp932")
    ap.add_argument("src")
    ap.add_argument("dst")
    a = ap.parse_args()
    with open(a.src, encoding="utf-8") as f:
        lines = [fix_line(l.rstrip("\n"), a.encoding, a.src) for l in f]
    lines = [l + "\n" for l in fix_strings(lines, a.encoding, a.src)]
    with open(a.dst, "w", encoding="utf-8") as f:
        f.writelines(lines)


if __name__ == "__main__":
    main()
