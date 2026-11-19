#!/usr/bin/env python3
"""Ironforge's monsters, rendered from simple 3D shapes and written as a WAD
file: python3 wad.py ../data/ironforge.wad [preview.png]

The WAD is the classic layout: a header ("PWAD", the number of entries, where
the directory is), the entries' data, then the directory (offset, size, an
8-character name each). Each sprite is a picture in the classic column
format: width, height, left and top offsets (16 bits each), the offset of
each column (32 bits), then each column as posts: the row it starts at, its
length, a pad byte, its pixels, a pad byte; 255 ends the column. Pixels are
colours of the game's VGA palette (Vault13H's), so its light table shades
them.

Names follow the old sprite convention: 4 letters, a frame letter, an angle
digit: HUSKA1 is the husk walking (frame A) seen from the front (1), the
angles going round to 8 every 45 degrees; angle 0 is the same from every
side (deaths, flames). The art is original: a hunched husk, a seeker drone,
a wisp of flame, a fireball, 64 pixels a cell as the old sprites were drawn.
(The WAD on the board is made by freedoom.py instead, with Freedoom's
monsters under the same names: either file works.)
"""
import math, struct, sys, zlib

# ---------------------------------------------------------------- palette
# the VGA default palette, as Vault13H's makePalette() builds it (6-bit)
EGA_R = [0, 0, 0, 0, 42, 42, 42, 42, 21, 21, 21, 21, 63, 63, 63, 63]
EGA_G = [0, 0, 42, 42, 0, 0, 21, 42, 21, 21, 63, 63, 21, 21, 63, 63]
EGA_B = [0, 42, 0, 42, 0, 42, 0, 42, 21, 63, 21, 63, 21, 63, 21, 63]
GRAYS = [0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63]
RAMP = [0, 16, 31, 47, 63, 31, 39, 47, 55, 63, 45, 49, 54, 58, 63,
        0, 7, 14, 21, 28, 14, 17, 21, 24, 28, 20, 22, 24, 26, 28,
        0, 4, 8, 12, 16, 8, 10, 12, 14, 16, 11, 12, 13, 15, 16]
HUE_R = [0, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0]
HUE_G = [0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1]
HUE_B = [4, 4, 4, 4, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 4, 4, 4]


def vga_palette():
    pal = [(0, 0, 0)] * 256
    for i in range(16):
        pal[i] = (EGA_R[i], EGA_G[i], EGA_B[i])
        pal[16 + i] = (GRAYS[i], GRAYS[i], GRAYS[i])
    for g in range(9):
        for h in range(24):
            pal[32 + g * 24 + h] = (RAMP[g * 5 + HUE_R[h]], RAMP[g * 5 + HUE_G[h]], RAMP[g * 5 + HUE_B[h]])
    return [(r * 255 // 63, g * 255 // 63, b * 255 // 63) for r, g, b in pal]


PAL = vga_palette()
# 0 is "clear" in the game's own sprites; the WAD's columns say what is
# drawn, but 0 and the last 8 (black) are not used for colours
USABLE = [i for i in range(1, 248)]
_near = {}


def nearest(c):
    c = tuple(max(0, min(255, int(v))) for v in c)
    key = (c[0] >> 2, c[1] >> 2, c[2] >> 2)
    if key not in _near:
        _near[key] = min(USABLE, key=lambda i: (PAL[i][0] - c[0]) ** 2 * 3 + (PAL[i][1] - c[1]) ** 2 * 4 + (PAL[i][2] - c[2]) ** 2 * 2)
    return _near[key]


# ---------------------------------------------------------------- shapes
# a model is a list of balls: (x, y, z) centre, (rx, ry, rz) radii, colour,
# glow (lit or not). x right, y up, z towards the front of the monster.

def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def render(balls, angle, size, scale, cy, outline=(30, 20, 18)):
    """the balls seen from `angle` (radians, 0 = from the front), orthographic,
    size x size pixels, `scale` pixels a unit, the model's y = cy at the
    middle row. Returns rows of RGB or None (clear)."""
    ca, sa = math.cos(angle), math.sin(angle)
    light = (-0.5, 0.7, 0.55)
    ln = math.sqrt(sum(v * v for v in light))
    light = tuple(v / ln for v in light)
    img = [[None] * size for _ in range(size)]
    depth = [[-1e9] * size for _ in range(size)]
    for (bx, by, bz), (rx, ry, rz), col, glow in balls:
        # the ball's centre in view space: turn the model by -angle about y
        vx = bx * ca - bz * sa
        vz = bx * sa + bz * ca
        sx0 = size / 2 + vx * scale
        sy0 = size / 2 - (by - cy) * scale
        r = max(rx, ry, rz) * scale + 1
        for py in range(max(0, int(sy0 - r)), min(size, int(sy0 + r) + 1)):
            for px in range(max(0, int(sx0 - r)), min(size, int(sx0 + r) + 1)):
                # back into model space, solve for the near surface
                x = (px + 0.5 - size / 2) / scale
                y = cy - (py + 0.5 - size / 2) / scale
                # view ray along z; the ellipsoid turned: sample along the ray
                # (the radii are close: use the turned x radius)
                rxv = math.sqrt((rx * ca) ** 2 + (rz * sa) ** 2)
                rzv = math.sqrt((rx * sa) ** 2 + (rz * ca) ** 2)
                dx = (x - vx) / rxv
                dy = (y - by) / ry
                q = 1 - dx * dx - dy * dy
                if q <= 0:
                    continue
                dz = math.sqrt(q)
                z = vz + dz * rzv
                if z <= depth[py][px]:
                    continue
                depth[py][px] = z
                if glow:
                    img[py][px] = col
                else:
                    n = (dx, dy, dz)
                    d = max(0.0, n[0] * light[0] + n[1] * light[1] + n[2] * light[2])
                    k = 0.35 + 0.8 * d
                    spec = max(0.0, d) ** 12 * 60
                    img[py][px] = tuple(min(255, c * k + spec) for c in col)
    # a dark outline round the shape, as hand-drawn sprites have
    out = [row[:] for row in img]
    for y in range(size):
        for x in range(size):
            if img[y][x] is None:
                for ddx, ddy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + ddx, y + ddy
                    if 0 <= xx < size and 0 <= yy < size and img[yy][xx] is not None:
                        out[y][x] = outline
                        break
    return out


def limb(a, b, r0, r1, col, n=6):
    """a chain of balls from a to b, radius r0 to r1"""
    return [((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t),
             (r0 + (r1 - r0) * t,) * 3, col, False) for t in [i / (n - 1) for i in range(n)]]


SKIN = (150, 100, 70)
SKIN_D = (105, 62, 44)
HIDE = (95, 70, 60)
BONE = (225, 215, 190)
EYE = (255, 230, 60)
MOUTH = (200, 30, 20)


def husk(step, pose):
    """the husk: hunched, long arms, small head low between the shoulders.
    pose: 'walk' (step 0..3), 'strike' (step 0 raise, 1 slam)"""
    b = []
    swing = math.sin(step * math.pi / 2) if pose == "walk" else 0
    # legs
    for side, ph in ((-1, 1), (1, -1)):
        hip = (side * 0.16, 0.42, 0.0)
        knee = (side * 0.18, 0.22, 0.10 * swing * ph)
        foot = (side * 0.2, 0.03, 0.02 + 0.16 * swing * ph)
        b += limb(hip, knee, 0.09, 0.075, HIDE, 9)
        b += limb(knee, foot, 0.075, 0.07, HIDE, 9)
        b.append(((foot[0], 0.03, foot[2] + 0.05), (0.07, 0.04, 0.1), SKIN_D, False))
    # body, bent forward
    b.append(((0, 0.55, 0.0), (0.26, 0.2, 0.2), SKIN, False))
    b.append(((0, 0.72, 0.06), (0.3, 0.2, 0.22), SKIN, False))
    b.append(((0, 0.66, -0.12), (0.22, 0.2, 0.14), SKIN_D, False))
    # spine ridges
    for i in range(4):
        b.append(((0, 0.62 + i * 0.07, -0.2 - i * 0.01), (0.04, 0.04, 0.05), BONE, False))
    # head, low and forward
    head = (0, 0.84, 0.24)
    b.append((head, (0.12, 0.11, 0.12), SKIN, False))
    b.append(((head[0], head[1] - 0.06, head[2] + 0.05), (0.09, 0.05, 0.08), SKIN_D, False))
    for side in (-1, 1):
        b.append(((side * 0.045, head[1] + 0.02, head[2] + 0.11), (0.025, 0.02, 0.02), EYE, True))
    b.append(((0, head[1] - 0.06, head[2] + 0.12), (0.05, 0.02, 0.02), MOUTH, True))
    # arms
    for side, ph in ((-1, -1), (1, 1)):
        sh = (side * 0.32, 0.78, 0.08)
        if pose == "walk":
            el = (side * 0.38, 0.52, 0.12 + 0.1 * swing * ph)
            ha = (side * 0.36, 0.28, 0.2 + 0.14 * swing * ph)
        elif step == 0:
            el = (side * 0.38, 0.98, 0.1)
            ha = (side * 0.3, 1.16, 0.2)
        else:
            el = (side * 0.34, 0.66, 0.32)
            ha = (side * 0.22, 0.42, 0.5)
        b += limb(sh, el, 0.09, 0.075, SKIN, 11)
        b += limb(el, ha, 0.075, 0.065, SKIN, 11)
        for f in (-1, 0, 1):
            b.append(((ha[0] + f * 0.03, ha[1] - 0.06, ha[2] + 0.03), (0.018, 0.05, 0.018), BONE, False))
    return b


def husk_dead(step):
    """the husk falling: 0..3, a heap at the end"""
    b = []
    t = step / 3
    tilt = t * 1.35
    for i, (y, rx, ry, rz, col) in enumerate(((0.55, 0.26, 0.2, 0.2, SKIN), (0.72, 0.3, 0.2, 0.22, SKIN), (0.84, 0.12, 0.11, 0.12, SKIN))):
        yy = y * math.cos(tilt) * (1 - t * 0.3)
        zz = -y * math.sin(tilt) * 0.8
        b.append(((0, max(0.08, yy), zz), (rx, ry * (1 - t * 0.4), rz), col, False))
    for side in (-1, 1):
        b += limb((side * 0.16, 0.4 * (1 - t), 0), (side * 0.2, 0.04, 0.12), 0.09, 0.07, HIDE, 4)
        b += limb((side * 0.3, 0.6 * (1 - t * 0.8), -0.1 * t), (side * (0.4 + t * 0.2), 0.08, 0.2 - t * 0.6), 0.08, 0.065, SKIN, 5)
    if step >= 2:
        for i in range(6):
            a = i * 1.1
            b.append(((math.cos(a) * 0.35, 0.02, math.sin(a) * 0.3 - 0.2), (0.12, 0.02, 0.08), MOUTH, True))
    return b


METAL = (150, 160, 175)
METAL_D = (80, 88, 100)
LENS = (255, 60, 40)


def drone(fire):
    b = [((0, 0.5, 0), (0.3, 0.3, 0.3), METAL, False)]
    # a band, the fins, the antenna
    b.append(((0, 0.5, 0), (0.31, 0.05, 0.31), METAL_D, False))
    for side in (-1, 1):
        b.append(((side * 0.36, 0.5, -0.02), (0.06, 0.14, 0.12), METAL_D, False))
    b += limb((0, 0.78, 0), (0.05, 0.95, -0.03), 0.02, 0.015, METAL_D, 4)
    b.append(((0.05, 0.97, -0.03), (0.03, 0.03, 0.03), (255, 200, 60), True))
    # the eye at the front
    b.append(((0, 0.52, 0.24), (0.13, 0.13, 0.08), (30, 30, 34), False))
    eye = LENS if not fire else (255, 240, 120)
    b.append(((0, 0.52, 0.3), (0.08, 0.08, 0.03), eye, True))
    b.append(((0.03, 0.55, 0.32), (0.025, 0.025, 0.01), (255, 255, 255), True))
    if fire:
        for i in range(8):
            a = i * math.pi / 4
            b.append(((math.cos(a) * 0.12, 0.52 + math.sin(a) * 0.12, 0.36), (0.04, 0.04, 0.02), (255, 220, 80), True))
    return b


def drone_dead(step):
    b = []
    for i in range(7 + step * 2):
        a = i * 2.3
        r = 0.1 + step * 0.08
        col = METAL if i % 2 else METAL_D
        b.append(((math.cos(a) * r, 0.06 + (i % 3) * 0.04 * (2 - step), math.sin(a) * r), (0.09, 0.05, 0.07), col, False))
    if step < 2:
        b.append(((0, 0.2, 0), (0.2 - step * 0.08,) * 3, (255, 160, 40), True))
    return b


def flame(frame, size, base=(255, 90, 20)):
    """the wisp: a flame with two dark eyes, the same from every side"""
    img = [[None] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            u = (x + 0.5) / size * 2 - 1
            v = 1 - (y + 0.5) / size * 2          # -1 bottom .. 1 top
            wob = math.sin(v * 5 + frame * 1.7) * 0.12 + math.sin(v * 9 - frame * 2.3) * 0.05
            w = 0.62 * (1 - (v + 0.35) ** 2 * 0.65) if v > -0.35 else 0.62 * math.sqrt(max(0, 1 - ((v + 0.35) / 0.6) ** 2))
            d = abs(u - wob * (v + 1) * 0.5)
            if v > 0.95 or w <= 0 or d > w:
                continue
            t = d / w
            heat = (1 - t) * (1 - max(0, v) * 0.6)
            col = mix(base, (255, 210, 60), min(1, heat * 1.4))
            if heat > 0.75:
                col = mix(col, (255, 255, 220), (heat - 0.75) * 4)
            img[y][x] = col
    for side in (-1, 1):
        cx = size // 2 + side * size // 9
        cy = int(size * 0.58)
        for yy in range(cy - 1, cy + 2):
            for xx in range(cx - 1, cx + 1):
                img[yy][xx] = (20, 0, 0)
    return img


def fireball(size, frame):
    img = [[None] * size for _ in range(size)]
    c = size / 2
    for y in range(size):
        for x in range(size):
            r = math.hypot(x + 0.5 - c, y + 0.5 - c) / c
            r *= 1 + 0.15 * math.sin(math.atan2(y - c, x - c) * 5 + frame * 2)
            if r < 1:
                img[y][x] = mix((255, 255, 200), (230, 60, 10), r ** 0.8)
    return img


# ---------------------------------------------------------------- pictures

def picture(img):
    """RGB rows -> a picture in the column format, its feet at the bottom
    middle (left offset half the width, top offset the height)"""
    h = len(img)
    w = len(img[0])
    # crop the clear rows and columns round it
    ys = [y for y in range(h) if any(p is not None for p in img[y])]
    xs = [x for x in range(w) if any(img[y][x] is not None for y in range(h))]
    y0, y1, x0, x1 = ys[0], ys[-1] + 1, xs[0], xs[-1] + 1
    cols = []
    for x in range(x0, x1):
        data = bytearray()
        y = y0
        while y < y1:
            if img[y][x] is None:
                y += 1
                continue
            s = y
            while y < y1 and img[y][x] is not None and y - s < 254:
                y += 1
            px = bytes(nearest(img[k][x]) for k in range(s, y))
            data += bytes([s - y0, len(px), 0]) + px + b"\x00"
        data += b"\xff"
        cols.append(bytes(data))
    pw, ph = x1 - x0, y1 - y0
    # offsets: the drawing's left edge and top, from the anchor (the middle of
    # the full square, at its bottom row)
    left = w // 2 - x0
    top = h - y0
    head = struct.pack("<hhhh", pw, ph, left, top)
    offs = []
    pos = 8 + 4 * pw
    for c in cols:
        offs.append(pos)
        pos += len(c)
    return head + b"".join(struct.pack("<I", o) for o in offs) + b"".join(cols)


def write_wad(path, lumps):
    data = bytearray()
    dirs = []
    pos = 12
    for name, blob in lumps:
        dirs.append((pos, len(blob), name))
        data += blob
        pos += len(blob)
    out = struct.pack("<4sii", b"PWAD", len(lumps), pos) + bytes(data)
    for p, s, n in dirs:
        out += struct.pack("<ii8s", p, s, n.encode().ljust(8, b"\0"))
    open(path, "wb").write(out)
    return len(out)


def save_png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = b"".join(b"\0" + b"".join(bytes(p if p is not None else (40, 36, 48)) for p in r) for r in rows)
    ch = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw, 9)) + ch(b"IEND", b""))


def quantised(img):
    return [[PAL[nearest(p)] if p is not None else None for p in row] for row in img]


def main():
    out = sys.argv[1]
    lumps = []
    sheet = []
    angles = [i * math.pi / 4 for i in range(8)]   # 1 front, then round
    S = 80
    # the husk: A-D walking, E-F striking, from 8 angles; G-J falling
    for f in range(4):
        row = []
        for a in range(8):
            img = render(husk(f, "walk"), angles[a], S, 64, 0.625)
            lumps.append(("HUSK%s%d" % ("ABCD"[f], a + 1), picture(img)))
            row.append(img)
        sheet.append(row)
    for f in range(2):
        row = []
        for a in range(8):
            img = render(husk(f, "strike"), angles[a], S, 64, 0.625)
            lumps.append(("HUSK%s%d" % ("EF"[f], a + 1), picture(img)))
            row.append(img)
        sheet.append(row)
    row = []
    for f in range(4):
        img = render(husk_dead(f), 0.3, S, 64, 0.625)
        lumps.append(("HUSK%s0" % "GHIJ"[f], picture(img)))
        row.append(img)
    # the drone: A idle, B firing, from 8 angles; C-E breaking up
    for f in range(2):
        r = []
        for a in range(8):
            img = render(drone(f == 1), angles[a], 64, 64, 0.5)
            lumps.append(("DRON%s%d" % ("AB"[f], a + 1), picture(img)))
            r.append(img)
        sheet.append(r)
    for f in range(3):
        img = render(drone_dead(f), 0.4, 64, 64, 0.5)
        lumps.append(("DRON%s0" % "CDE"[f], picture(img)))
        row.append(img)
    # the wisp: A-D burning; the fireball: A-B
    for f in range(4):
        img = flame(f, 40)
        lumps.append(("WISP%s0" % "ABCD"[f], picture(img)))
        row.append(img)
    for f in range(2):
        img = fireball(20, f)
        lumps.append(("FBAL%s0" % "AB"[f], picture(img)))
    sheet.append(row)
    from title import title
    lumps.append(("TITLEPIC", picture(title())))
    size = write_wad(out, lumps)
    print("%s: %d sprites, %d bytes" % (out, len(lumps), size))
    if len(sys.argv) > 2:
        cell = 80
        w = cell * max(len(r) for r in sheet)
        rows = []
        for r in sheet:
            block = [[None] * w for _ in range(cell)]
            for i, img in enumerate(r):
                q = quantised(img)
                oy = cell - len(q)
                for y in range(len(q)):
                    for x in range(len(q[0])):
                        block[oy + y][i * cell + x] = q[y][x]
            rows += block
        big = [[p for p in r for _ in range(2)] for r in rows for _ in range(2)]
        save_png(sys.argv[2], big)


if __name__ == "__main__":
    main()
