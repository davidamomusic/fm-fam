#!/usr/bin/env python3
"""FM-FAM: the strip of steps and the columns under it must start and end at the same x (v0.2.1).
   fam/build/host/fm-fam_host fam/tests/v021b.txt DIRECTORY && python3 fam/tests/align_check.py DIRECTORY
In each screenshot of v021b.txt (seqN.png: SEQ with N steps; kickN.png: KICK sequence 1 with N steps) the strip of boxes is the
row y = 35, and a baseline under every column is the row y = 134 (SEQ) or 108 (KICK). The step selected is the fourth: its background is a
pixel wider on each side than its column, and nothing else may differ. Needs nothing but Python."""
import struct, sys, zlib


def read_png(path):
    d = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(d):
        n, t = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        pos += 12 + n
        if t == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", body[:10])
        elif t == b"IDAT":
            idat += body
    assert bd == 8 and ct == 2, "8-bit RGB expected"
    raw, stride, prev, o, rows = zlib.decompress(idat), w * 3, bytearray(w * 3), 0, []
    for _ in range(h):
        f, line = raw[o], bytearray(raw[o + 1:o + 1 + stride])
        o += 1 + stride
        for i in range(stride):
            a = line[i - 3] if i >= 3 else 0
            b = prev[i]
            c = prev[i - 3] if i >= 3 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, rows


def runs(rows, y, w):
    """the stretches of a row that are not black: [(first x, last x)]"""
    out, start = [], None
    for x in range(w + 1):
        on = x < w and any(rows[y][3 * x:3 * x + 3])
        if on and start is None:
            start = x
        elif not on and start is not None:
            out.append((start, x - 1))
            start = None
    return out


def main(d):
    bad = 0
    for kind, y in (("seq", 134), ("kick", 108)):
        for n in (5, 7, 8, 11, 12, 13, 16):
            try:
                w, rows = read_png(f"{d}/{kind}{n}.png")
            except FileNotFoundError:
                continue
            strip, base = runs(rows, 35, w), runs(rows, y, w)
            # (the step selected, the fourth, has a background a pixel wider than its column on each side)
            grown = [(a - 1, b + 1) if i == 3 else (a, b) for i, (a, b) in enumerate(strip)]
            ok = len(strip) == n and len(base) == n and base == grown
            print(("ok  " if ok else "FAIL"), f"{kind} {n} steps: strip {strip[0]}..{strip[-1]} ({len(strip)} boxes), columns {base[0]}..{base[-1]} ({len(base)})")
            bad += not ok
    print("all ok" if not bad else f"{bad} FAILED")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
