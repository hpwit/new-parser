#!/usr/bin/env python3
"""Ironforge's monsters from Freedoom: python3 freedoom.py freedoom2.wad ../data/ironforge.wad [preview.png]

Freedoom (https://freedoom.github.io) is a free game data set whose art is
the Freedoom project's own, under a BSD licence (data/freedoom-COPYING.txt,
which has to go with the WAD). This takes four of its monsters out of
freedoom2.wad (release 0.13.0) and writes them under the names Ironforge
looks for (the same names tools/wad.py gives its own monsters, so either
file works):

  HUSK  <- TROO: walking A-D, striking E and G, falling I J K M
  DRON  <- HEAD: floating A, attacking C, falling G I L
  WISP  <- SKUL: burning A B (seen from the front)
  FBAL  <- BAL1: the fireball A B
  TITLEPIC         the title picture, Ironforge's own (title.py)

and, under their own names: the face of the status bar (STFST00-42 looking,
STFOUCH0-4 hurt, STFDEAD0), the three guns as they are held (PISG pistol
and its flash PISF, SHTG shotgun and SHTF, CHGG chaingun and CHGF) and the
pickups (SHOTA0, MGUNA0, SHELA0, CLIPA0, MEDIA0, BKEYA0 BKEYB0).

Its pictures are in the same column format; this changes three things:
  - the colours: Freedoom's palette (PLAYPAL) to the game's VGA palette;
  - the mirrored views: Freedoom keeps some angles once for two sides
    (TROOA2A8 is angle 2, and angle 8 flipped left to right); here each angle
    has its own picture, flipped where needed;
  - the names, frame letters included (TROOG -> HUSKF).
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wad import nearest, write_wad, save_png, PAL, picture
from title import title


def read_wad(path):
    w = open(path, "rb").read()
    sig, n, d = struct.unpack_from("<4sii", w, 0)
    assert sig in (b"IWAD", b"PWAD"), sig
    lumps = {}
    for i in range(n):
        pos, size, name = struct.unpack_from("<ii8s", w, d + 16 * i)
        lumps[name.rstrip(b"\0").decode("latin1")] = w[pos:pos + size]
    return lumps


def parse_picture(blob):
    """-> width, height, left, top, columns (lists of (start row, bytes))"""
    w, h, left, top = struct.unpack_from("<hhhh", blob, 0)
    cols = []
    for x in range(w):
        p = struct.unpack_from("<I", blob, 8 + 4 * x)[0]
        posts = []
        while blob[p] != 255:
            start, n = blob[p], blob[p + 1]
            posts.append((start, blob[p + 3:p + 3 + n]))
            p += n + 4
        cols.append(posts)
    return w, h, left, top, cols


def build_picture(w, h, left, top, cols):
    data = []
    for posts in cols:
        c = bytearray()
        for start, px in posts:
            c += bytes([start, len(px), 0]) + bytes(px) + b"\x00"
        c += b"\xff"
        data.append(bytes(c))
    offs, pos = [], 8 + 4 * w
    for c in data:
        offs.append(pos)
        pos += len(c)
    return struct.pack("<hhhh", w, h, left, top) + b"".join(struct.pack("<I", o) for o in offs) + b"".join(data)


def find(lumps, sprite, frame, angle):
    """the picture of sprite, frame (a letter), angle (0-8), and whether it
    is stored for the other side (to flip)"""
    for name, blob in lumps.items():
        if not name.startswith(sprite) or len(name) < 6:
            continue
        if name[4] == frame and name[5] in (str(angle), "0"):
            return blob, False
        if len(name) == 8 and name[6] == frame and name[7] == str(angle):
            return blob, True
    raise KeyError("%s%s%d" % (sprite, frame, angle))


def convert(blob, flip, colmap):
    w, h, left, top, cols = parse_picture(blob)
    cols = [[(s, bytes(colmap[b] for b in px)) for s, px in posts] for posts in cols]
    if flip:
        cols = cols[::-1]
        left = w - left
    return build_picture(w, h, left, top, cols), (w, h, left, top, cols)


def main():
    src, out = sys.argv[1], sys.argv[2]
    lumps = read_wad(src)
    playpal = lumps["PLAYPAL"][:768]
    colmap = [nearest(tuple(playpal[i * 3:i * 3 + 3])) for i in range(256)]
    plan = []
    for k, f in enumerate("ABCD"):
        for a in range(1, 9):
            plan.append(("HUSK", "ABCD"[k], a, "TROO", f, a))
    for k, f in enumerate("EG"):
        for a in range(1, 9):
            plan.append(("HUSK", "EF"[k], a, "TROO", f, a))
    for k, f in enumerate("IJKM"):
        plan.append(("HUSK", "GHIJ"[k], 0, "TROO", f, 0))
    for k, f in enumerate("AC"):
        for a in range(1, 9):
            plan.append(("DRON", "AB"[k], a, "HEAD", f, a))
    for k, f in enumerate("GIL"):
        plan.append(("DRON", "CDE"[k], 0, "HEAD", f, 0))
    for k, f in enumerate("ABAB"):
        plan.append(("WISP", "ABCD"[k], 0, "SKUL", f, 1))
    for k, f in enumerate("AB"):
        plan.append(("FBAL", "AB"[k], 0, "BAL1", f, 0))
    out_lumps, shown = [], []
    for name, frame, angle, sprite, sframe, sangle in plan:
        blob, flip = find(lumps, sprite, sframe, sangle)
        pic, parts = convert(blob, flip, colmap)
        out_lumps.append(("%s%s%d" % (name, frame, angle), pic))
        shown.append(parts)
    keep = ["STFST%d%d" % (l, k) for l in range(5) for k in range(3)] + ["STFOUCH%d" % l for l in range(5)] + ["STFDEAD0"]
    keep += ["PISG%s0" % f for f in "ABCDE"] + ["PISFA0"] + ["SHTG%s0" % f for f in "ABCD"] + ["SHTFA0", "SHTFB0"]
    keep += ["CHGGA0", "CHGGB0", "CHGFA0", "CHGFB0", "SHOTA0", "MGUNA0", "SHELA0", "CLIPA0", "MEDIA0", "BKEYA0", "BKEYB0"]
    for name in keep:
        pic, parts = convert(lumps[name], False, colmap)
        out_lumps.append((name, pic))
    out_lumps.append(("TITLEPIC", picture(title())))
    size = write_wad(out, out_lumps)
    print("%s: %d pictures from %s, %d bytes" % (out, len(out_lumps), os.path.basename(src), size))
    if len(sys.argv) > 3:
        # a sheet: 8 a row
        cell = 96
        rows = []
        for r in range(0, len(shown), 8):
            block = [[None] * (cell * 8) for _ in range(cell)]
            for i, (w, h, left, top, cols) in enumerate(shown[r:r + 8]):
                ox = i * cell + cell // 2 - left
                oy = cell - 4 - top
                for x, posts in enumerate(cols):
                    for s, px in posts:
                        for j, b in enumerate(px):
                            yy, xx = oy + s + j, ox + x
                            if 0 <= yy < cell and 0 <= xx < cell * 8:
                                block[yy][xx] = PAL[b]
            rows += block
        save_png(sys.argv[3], rows)


if __name__ == "__main__":
    main()
