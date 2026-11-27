#!/usr/bin/env python3
"""Compares two PNG screenshots pixel by pixel (8-bit RGB/RGBA, not interlaced, as
chexa-pc's --shot writes them), without third-party modules.

    tools/png_diff.py A.png B.png [--diff OUT.ppm]

Prints the number of differing pixels and the largest channel difference; exits 0
when the images are identical, 1 otherwise. --diff writes a PPM where differing pixels
are white (brighter for larger differences)."""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f'{path}: only 8-bit RGB/RGBA, not interlaced')
            bpp = 4 if ctype == 6 else 3
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    # RGB only: the display ignores alpha.
    px = [r[i:i + 3] for r in rows for i in range(0, stride, bpp)]
    return w, h, px


def main():
    args = sys.argv[1:]
    out = None
    if '--diff' in args:
        i = args.index('--diff')
        out = args[i + 1]
        del args[i:i + 2]
    if len(args) != 2:
        print(__doc__)
        return 2
    wa, ha, a = read_png(args[0])
    wb, hb, b = read_png(args[1])
    if (wa, ha) != (wb, hb):
        print(f'size differs: {wa}x{ha} vs {wb}x{hb}')
        return 1
    diffs = [max(abs(x - y) for x, y in zip(p, q)) for p, q in zip(a, b)]
    n = sum(1 for d in diffs if d)
    print(f'{n} of {wa * ha} pixels differ, largest channel difference {max(diffs)}')
    if out:
        with open(out, 'wb') as f:
            f.write(b'P6 %d %d 255\n' % (wa, ha))
            f.write(bytes(v for d in diffs for v in (min(255, d * 8 + 64) if d else 0,) * 3))
    return 0 if n == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
