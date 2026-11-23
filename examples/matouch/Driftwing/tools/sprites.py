"""Driftwing's sprites: the plane, from a small 3D model rendered here at
5 bank angles and 5 pitches (flat-shaded triangles, a depth buffer, drawn
3 times larger and scaled down); a striped hot-air balloon; a bird with its
wings up and down; a letter (the mail to gather)."""
import math

from PIL import Image

PW, PH = 56, 34      # a plane frame
SS = 4               # supersampling


def rgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


RED = rgb('#e02a20')
RED_D = rgb('#a01810')
WHITE = rgb('#f4f0e8')
CREAM = rgb('#f0d890')
DARK = rgb('#2a2030')
GLASS = rgb('#80d0f0')
PROP = rgb('#c0c0c8')


def box(cx, cy, cz, hx, hy, hz, col):
    """The 12 triangles of a box (x right, y up, z forward)."""
    c = [(cx + sx * hx, cy + sy * hy, cz + sz * hz) for sz in (-1, 1) for sy in (-1, 1) for sx in (-1, 1)]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    tris = []
    for a, b, cc, d in faces:
        tris.append((c[a], c[b], c[cc], col))
        tris.append((c[a], c[cc], c[d], col))
    return tris


def tri_quad(a, b, c, d, col):
    return [(a, b, c, col), (a, c, d, col)]


def loft(rings, col, cols=None):
    """A round body through rings (z, radius, y centre, x stretch): 10
    sides, quads between one ring and the next; cols: a colour a segment."""
    n = 10
    pts = []
    for (z, r, cy, sx) in rings:
        pts.append([(math.cos(2 * math.pi * k / n) * r * sx, cy + math.sin(2 * math.pi * k / n) * r, z)
                    for k in range(n)])
    t = []
    for i in range(len(pts) - 1):
        c = cols[i] if cols else col
        for k in range(n):
            k2 = (k + 1) % n
            t += tri_quad(pts[i][k], pts[i][k2], pts[i + 1][k2], pts[i + 1][k], c)
    # the ends closed
    for ring_ in (pts[0], pts[-1]):
        cz = ring_[0][2]
        cy = sum(p[1] for p in ring_) / n
        for k in range(n):
            t.append(((0, cy, cz), ring_[k], ring_[(k + 1) % n], cols[0] if cols and ring_ is pts[0] else col))
    return t


def box_roll(cx, cy, cz, hx, hy, hz, col, roll):
    """A box turned round z by `roll` about (cx, cy) (the wings' dihedral)."""
    out = []
    cr, sr = math.cos(roll), math.sin(roll)
    for (a, b, c, cc) in box(0, 0, 0, hx, hy, hz, col):
        tri = []
        for (x, y, z) in (a, b, c):
            x += (hx if roll > 0 else -hx) * 0 + 0
            tri.append((cx + x * cr - y * sr, cy + x * sr + y * cr, cz + z))
        out.append((tri[0], tri[1], tri[2], cc))
    return out


def plane_model():
    t = []
    YEL = rgb('#ffd040')
    # the fuselage: round, the engine's dark cowling at the front, a white
    # band, tapering to the tail
    t += loft([(1.62, 0.27, 0.0, 1.0), (1.45, 0.34, 0.0, 1.0), (1.2, 0.37, 0.0, 1.0), (0.6, 0.38, 0.02, 1.0),
               (0.0, 0.36, 0.04, 1.0), (-0.5, 0.3, 0.06, 1.0), (-1.2, 0.2, 0.09, 1.0), (-1.9, 0.08, 0.14, 1.0)],
              RED, [DARK, PROP, RED, RED, WHITE, RED, RED_D])
    # the spinner and the propeller: two blades, a pale blur round them
    t += loft([(1.62, 0.12, 0.0, 1.0), (1.72, 0.1, 0.0, 1.0), (1.84, 0.02, 0.0, 1.0)], WHITE)
    t += box(0, 0.48, 1.66, 0.07, 0.42, 0.015, PROP)
    t += box(0, -0.48, 1.66, 0.07, 0.42, 0.015, PROP)
    t += box(0.5, 0, 1.665, 0.36, 0.05, 0.01, rgb('#e8e8f0'))
    t += box(-0.5, 0, 1.665, 0.36, 0.05, 0.01, rgb('#e8e8f0'))
    # the canopy, the pilot (a cap, a white scarf flying)
    t += loft([(0.65, 0.05, 0.36, 1.2), (0.45, 0.19, 0.36, 1.1), (0.05, 0.2, 0.36, 1.1), (-0.25, 0.08, 0.36, 1.0)],
              GLASS)
    t += box(0, 0.6, 0.2, 0.1, 0.1, 0.1, rgb('#f0c0a0'))
    t += box(0, 0.68, 0.18, 0.11, 0.04, 0.12, rgb('#6a3a20'))
    t += box(0.04, 0.52, -0.05, 0.05, 0.03, 0.22, WHITE)
    # the wings: low, cream, a little dihedral; yellow bands, red tips
    for side in (-1, 1):
        roll = 0.09 * side
        t += box_roll(side * 1.25, -0.16, 0.45, 1.0, 0.055, 0.42, CREAM, roll)
        t += box_roll(side * 1.75, -0.16 + 0.05, 0.45, 0.12, 0.06, 0.425, YEL, roll)
        t += box_roll(side * 2.3, -0.16 + 0.2, 0.42, 0.1, 0.06, 0.36, RED, roll)
        # the wheels under the wings, on their struts
        t += box(side * 0.55, -0.42, 0.75, 0.035, 0.2, 0.035, DARK)
        t += box(side * 0.55, -0.66, 0.75, 0.07, 0.12, 0.12, DARK)
    # the tail: a fin (a white stripe) and two little wings
    t += box(0, 0.42, -1.72, 0.035, 0.36, 0.24, RED)
    t += box(0, 0.62, -1.76, 0.04, 0.05, 0.2, WHITE)
    t += box(0, 0.12, -1.74, 0.78, 0.035, 0.2, CREAM)
    t += box(-0.72, 0.12, -1.74, 0.08, 0.04, 0.2, RED)
    t += box(0.72, 0.12, -1.74, 0.08, 0.04, 0.2, RED)
    return t


def render_plane(bank, pitch):
    """The plane seen from behind and a little above, banked and pitched."""
    W, H = PW * SS, PH * SS
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    px = img.load()
    zb = [[-1e9] * W for _ in range(H)]
    cb, sb = math.cos(bank), math.sin(bank)
    cp, sp = math.cos(pitch), math.sin(pitch)
    # the camera: behind (looking down z), a little above
    view_tilt = 0.32
    ct, st = math.cos(view_tilt), math.sin(view_tilt)
    light = (0.4, 0.8, -0.45)
    ll = math.sqrt(sum(v * v for v in light))
    light = tuple(v / ll for v in light)

    def xf(p):
        x, y, z = p
        # pitch round x, then bank round z
        y, z = y * cp - z * sp, y * sp + z * cp
        x, y = x * cb - y * sb, x * sb + y * cb
        # the view: tilt round x
        y, z = y * ct + z * st, -y * st + z * ct
        return (x, y, z)

    scale = W / 5.4
    for (a, b, c, col) in plane_model():
        A, B, C = xf(a), xf(b), xf(c)
        n = ((B[1] - A[1]) * (C[2] - A[2]) - (B[2] - A[2]) * (C[1] - A[1]),
             (B[2] - A[2]) * (C[0] - A[0]) - (B[0] - A[0]) * (C[2] - A[2]),
             (B[0] - A[0]) * (C[1] - A[1]) - (B[1] - A[1]) * (C[0] - A[0]))
        nl = math.sqrt(sum(v * v for v in n)) or 1
        n = tuple(v / nl for v in n)
        lit = abs(n[0] * light[0] + n[1] * light[1] + n[2] * light[2])
        # light and a glint where it faces the sun
        k = 0.45 + 0.6 * lit + 0.45 * lit ** 12
        shade = tuple(min(255, int(v * k)) for v in col)
        pts = [(W / 2 + P[0] * scale, H * 0.55 - P[1] * scale, P[2]) for P in (A, B, C)]
        x0 = max(0, int(min(p[0] for p in pts)))
        x1 = min(W - 1, int(max(p[0] for p in pts)) + 1)
        y0 = max(0, int(min(p[1] for p in pts)))
        y1 = min(H - 1, int(max(p[1] for p in pts)) + 1)
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = pts
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-6:
            continue
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                w0 = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / den
                w1 = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / den
                w2 = 1 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                z = -(w0 * az + w1 * bz + w2 * cz)
                if z > zb[y][x]:
                    zb[y][x] = z
                    px[x, y] = shade + (255,)
    small = img.resize((PW, PH), Image.LANCZOS)
    # hard edges: a pixel is there or not, and a dark outline
    out = Image.new('RGBA', (PW, PH), (0, 0, 0, 0))
    sp_ = small.load()
    op = out.load()
    for y in range(PH):
        for x in range(PW):
            r, g, b, a = sp_[x, y]
            if a >= 110:
                k = 255 / a
                op[x, y] = (min(255, int(r * k)), min(255, int(g * k)), min(255, int(b * k)), 255)
    marks = []
    for y in range(PH):
        for x in range(PW):
            if op[x, y][3]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if 0 <= x + dx < PW and 0 <= y + dy < PH and op[x + dx, y + dy][3]:
                    marks.append((x, y))
                    break
    for x, y in marks:
        op[x, y] = (30, 20, 30, 255)
    return out


BANKS = [-0.75, -0.38, 0.0, 0.38, 0.75]
PITCHES = [-0.55, -0.28, 0.0, 0.28, 0.55]   # nose up .. nose down


def plane_frames():
    return [render_plane(b, p) for p in PITCHES for b in BANKS]


def balloon():
    """A hot-air balloon (32x44): striped envelope, a basket."""
    img = Image.new('RGBA', (32, 44), (0, 0, 0, 0))
    px = img.load()
    stripes = [rgb('#ff4060'), rgb('#ffd040'), rgb('#40c0ff'), rgb('#ffffff')]
    for y in range(32):
        for x in range(32):
            u = (x - 15.5) / 15
            v = (y - 14) / 15
            if v > 0:
                v = v * 1.25
            if u * u + v * v < 1:
                ang = math.atan2(u, 1 - abs(v) * 0.5)
                band = int((ang + 1.6) * 2.2) % 4
                c = stripes[band]
                k = 0.7 + 0.45 * (1 - (u + 0.5) ** 2) - max(0, v) * 0.2
                px[x, y] = tuple(max(0, min(255, int(ch * k))) for ch in c) + (255,)
    for y in range(32, 38):
        for x in (12, 19):
            px[x, y] = (60, 40, 30, 255)
    for y in range(37, 43):
        for x in range(12, 20):
            px[x, y] = (150, 100, 50, 255) if y > 37 else (110, 70, 40, 255)
    return img


def bird(f):
    img = Image.new('RGBA', (14, 8), (0, 0, 0, 0))
    px = img.load()
    c = (30, 26, 40, 255)
    pts = [(7, 4), (6, 4), (8, 4)]
    if f == 0:
        pts += [(5, 3), (4, 2), (3, 1), (9, 3), (10, 2), (11, 1)]
    else:
        pts += [(5, 5), (4, 5), (3, 6), (9, 5), (10, 5), (11, 6)]
    for x, y in pts:
        px[x, y] = c
    return img


def letter(f):
    """A letter, the mail: an envelope with a red seal, glinting (12x10)."""
    img = Image.new('RGBA', (12, 10), (0, 0, 0, 0))
    px = img.load()
    for y in range(1, 9):
        for x in range(1, 11):
            edge = x in (1, 10) or y in (1, 8)
            px[x, y] = (80, 60, 40, 255) if edge else (250, 240, 215, 255)
    for k in range(4):
        px[2 + k, 2 + k] = (180, 160, 130, 255)
        px[9 - k, 2 + k] = (180, 160, 130, 255)
    px[5, 5] = px[6, 5] = (220, 40, 40, 255)
    if f == 1:
        px[0, 0] = px[11, 0] = (255, 255, 200, 255)
    return img



# ---------------------------------------------------------------- the 3D plane
# Driftwing draws the plane itself, in 3D, every picture: this is its mesh
# (make_wad.py stores it as MESH). x right, y up, z forward, in units (the
# wings are 5.4 across); the propeller turns round the z axis.

def mesh_loft(rings, cols, sides=8):
    """A round body through rings (z, radius x, radius y, y centre)."""
    pts = []
    for (z, rx, ry, cy) in rings:
        pts.append([(math.cos(2 * math.pi * (k + 0.5) / sides) * rx, cy + math.sin(2 * math.pi * (k + 0.5) / sides) * ry,
                     z) for k in range(sides)])
    t = []
    for i in range(len(pts) - 1):
        for k in range(sides):
            k2 = (k + 1) % sides
            t.append((pts[i][k], pts[i][k2], pts[i + 1][k2], cols[i], 0))
            t.append((pts[i][k], pts[i + 1][k2], pts[i + 1][k], cols[i], 0))
    for ring_, c in ((pts[0], cols[0]), (pts[-1], cols[-1])):
        cy = sum(p[1] for p in ring_) / sides
        for k in range(sides):
            t.append(((0, cy, ring_[0][2]), ring_[k], ring_[(k + 1) % sides], c, 0))
    return t


def mesh_slab(top, thick, col):
    """A flat piece (a wing, a fin): 4 corners on top, as thick as `thick`
    down (or across for a fin: thick is a vector)."""
    dx, dy, dz = thick
    bot = [(x - dx, y - dy, z - dz) for (x, y, z) in top]
    t = []
    for (a, b, c, d) in ((top[0], top[1], top[2], top[3]), (bot[3], bot[2], bot[1], bot[0])):
        t.append((a, b, c, col, 0))
        t.append((a, c, d, col, 0))
    for k in range(4):
        k2 = (k + 1) % 4
        t.append((top[k], top[k2], bot[k2], col, 0))
        t.append((top[k], bot[k2], bot[k], col, 0))
    return t


def mesh_box(cx, cy, cz, hx, hy, hz, col):
    return [(a, b, c, cc, 0) for (a, b, c, cc) in box(cx, cy, cz, hx, hy, hz, col)]


def plane_mesh():
    RED = rgb('#d82820')
    REDD = rgb('#a01a14')
    WH = rgb('#f2efe6')
    YEL = rgb('#ffcc30')
    SIL = rgb('#b8bcc4')
    DK = rgb('#2a2830')
    GL = rgb('#70b8e8')
    t = []
    # the fuselage: the engine's silver cowling, red, a white band, a long
    # tapering tail
    t += mesh_loft([(2.05, 0.30, 0.30, 0.0), (1.85, 0.40, 0.40, 0.0), (1.5, 0.44, 0.44, 0.0),
                    (1.0, 0.45, 0.47, 0.02), (0.2, 0.44, 0.47, 0.05), (-0.4, 0.38, 0.42, 0.08),
                    (-1.4, 0.24, 0.3, 0.14), (-2.5, 0.08, 0.16, 0.22)],
                   [SIL, SIL, RED, RED, WH, RED, REDD, REDD])
    # the spinner
    t += mesh_loft([(2.05, 0.16, 0.16, 0.0), (2.25, 0.12, 0.12, 0.0), (2.4, 0.02, 0.02, 0.0)], [WH, WH, WH], 6)
    # the canopy (glass), its frame, the pilot's head
    t += mesh_loft([(0.75, 0.05, 0.03, 0.44), (0.5, 0.28, 0.22, 0.44), (-0.1, 0.3, 0.26, 0.44),
                    (-0.6, 0.16, 0.1, 0.42)], [GL, GL, GL, GL], 6)
    t += mesh_box(0, 0.62, 0.05, 0.11, 0.11, 0.11, rgb('#f0c0a0'))
    t += mesh_box(0, 0.71, 0.04, 0.12, 0.04, 0.13, rgb('#6a3a20'))
    # the wings: low, tapered, a little dihedral; white, a yellow band,
    # red tips
    for s in (-1, 1):
        def wing(x0, x1, col_):
            c0 = 1.15 - (abs(x0) - 0.3) * 0.16
            c1 = 1.15 - (abs(x1) - 0.3) * 0.16
            y0 = -0.2 + (abs(x0) - 0.3) * 0.1
            y1 = -0.2 + (abs(x1) - 0.3) * 0.1
            z0 = 0.7
            return mesh_slab([(x0, y0, z0), (x1, y1, z0 - (c0 - c1) * 0.3), (x1, y1, z0 - c1 - (c0 - c1) * 0.3),
                              (x0, y0, z0 - c0)], (0, 0.09, 0), col_)
        t += wing(s * 0.3, s * 1.9, WH)
        t += wing(s * 1.9, s * 2.2, YEL)
        t += wing(s * 2.2, s * 2.7, RED)
        # the wheels, on their legs
        t += mesh_box(s * 0.7, -0.5, 0.85, 0.05, 0.25, 0.06, DK)
        t += mesh_box(s * 0.75, -0.8, 0.85, 0.07, 0.16, 0.16, DK)
    # the tail: the stabilisers, the fin (a white stripe on it)
    for s in (-1, 1):
        t += mesh_slab([(s * 0.05, 0.22, -2.0), (s * 1.0, 0.22, -2.2), (s * 1.0, 0.22, -2.6), (s * 0.05, 0.22, -2.65)],
                       (0, 0.06, 0), WH if s < 0 else WH)
    t += mesh_slab([(0.04, 0.25, -1.75), (0.04, 1.05, -2.3), (0.04, 1.05, -2.6), (0.04, 0.25, -2.62)],
                   (0.08, 0, 0), RED)
    t += mesh_slab([(0.05, 0.75, -2.08), (0.05, 0.9, -2.2), (0.05, 0.9, -2.5), (0.05, 0.75, -2.52)],
                   (0.1, 0, 0), WH)
    # the propeller: two blades (they turn), a pale disc where they blur
    for s in (-1, 1):
        a = (0, s * 0.12, 2.12)
        b = (0.12, s * 1.15, 2.12)
        c = (-0.08, s * 1.2, 2.12)
        t.append((a, b, c, DK, 1))
        t.append((a, c, (-0.1, s * 0.12, 2.12), DK, 1))
    n = 14
    for k in range(n):
        a0 = 2 * math.pi * k / n
        a1 = 2 * math.pi * (k + 1) / n
        t.append(((0, 0, 2.1), (math.cos(a0) * 1.2, math.sin(a0) * 1.2, 2.1),
                  (math.cos(a1) * 1.2, math.sin(a1) * 1.2, 2.1), (226, 228, 236), 2))
    return t


def mesh_lump(tris):
    """MESH: u16 vertices, u16 triangles; the vertices (int16 x y z in
    128ths of a unit, u8 part, u8 0); the triangles (u16 a b c, u8 r g b,
    u8 part: 0 the plane, 1 a blade, 2 the propeller's disc)."""
    import struct
    verts = {}
    vlist = []
    out_t = []

    def vid(p, part):
        key = (round(p[0] * 128), round(p[1] * 128), round(p[2] * 128), part)
        if key not in verts:
            verts[key] = len(vlist)
            vlist.append(key)
        return verts[key]

    for (a, b, c, col, part) in tris:
        out_t.append((vid(a, part), vid(b, part), vid(c, part), col, part))
    raw = bytearray(struct.pack('<HH', len(vlist), len(out_t)))
    for (x, y, z, part) in vlist:
        raw += struct.pack('<hhhBB', x, y, z, part, 0)
    for (a, b, c, col, part) in out_t:
        raw += struct.pack('<HHHBBBB', a, b, c, int(col[0]), int(col[1]), int(col[2]), part)
    return bytes(raw), len(vlist), len(out_t)
