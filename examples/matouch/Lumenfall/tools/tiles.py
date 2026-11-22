"""Lumenfall's 16x16 tiles: one set per land, the same numbers in each
(the level maps and the script use these numbers), drawn per pixel.

Fill textures repeat every 16 pixels, so the ground tiles join without
seams. A transparent pixel is None.
"""
import math
import random

from PIL import Image

from art import rgb, mix, ramp, shade, q565, Noise2

T = 16

# the tile numbers (make_wad.py writes them into the script as #defines)
NAMES = [
    'EMPTY',
    'TOP', 'TOP_L', 'TOP_R', 'FILL', 'FILL_L', 'FILL_R', 'FILL2',
    'PLAT_L', 'PLAT_M', 'PLAT_R',
    'BLOCK', 'BOX_A', 'BOX_B', 'BOX_USED', 'SPIKES',
    'LIQTOP_A', 'LIQTOP_B', 'LIQ_A', 'LIQ_B',
    'CRATE',
    'DECO1', 'DECO2', 'DECO3', 'DECO4', 'DECO5', 'DECO6', 'DECO7', 'DECO8',
    'COLUMN', 'COLUMN_TOP',
    'GLINT_A', 'GLINT_B', 'GLINT_C', 'GLINT_D',
    'UNDER',
]
ID = {n: i for i, n in enumerate(NAMES)}


class Tile:
    def __init__(self):
        self.c = [[None] * T for _ in range(T)]

    def put(self, x, y, col):
        if 0 <= x < T and 0 <= y < T and col is not None:
            self.c[int(y)][int(x)] = col

    def get(self, x, y):
        if 0 <= x < T and 0 <= y < T:
            return self.c[int(y)][int(x)]
        return None

    def image(self):
        img = Image.new('RGBA', (T, T), (0, 0, 0, 0))
        px = img.load()
        for y in range(T):
            for x in range(T):
                if self.c[y][x] is not None:
                    px[x, y] = q565(self.c[y][x], x, y, dither=False) + (255,)
        return img

    def copy(self):
        t = Tile()
        t.c = [row[:] for row in self.c]
        return t


_noise = {}


def tnoise(seed, x, y, cells=4):
    """Smooth noise repeating every 16 pixels both ways."""
    key = (seed, cells)
    if key not in _noise:
        _noise[key] = Noise2(seed, cells, cells, T, T)
    return _noise[key].at(x, y)


def texture(seed, cols, contrast=1.0, speck=0.0, cells=(4, 8)):
    t = Tile()
    r = random.Random(seed)
    for y in range(T):
        for x in range(T):
            v = tnoise(seed, x, y, cells[0]) * 0.6 + tnoise(seed + 1, x, y, cells[1]) * 0.4
            v = 0.5 + (v - 0.5) * contrast
            if speck and r.random() < speck:
                v += r.choice((-0.3, 0.3))
            t.put(x, y, ramp(cols, v))
    return t


def pebbles(t, seed, n, cols):
    r = random.Random(seed)
    for _ in range(n):
        x, y = r.randrange(1, T - 2), r.randrange(4, T - 1)
        w = r.choice((2, 3))
        for dx in range(w):
            t.put(x + dx, y, cols[1])
            t.put(x + dx, y + 1, cols[0])
        t.put(x, y, cols[2])


# ------------------------------------------------------------ each land's materials

MATERIALS = [
    dict(  # Dawnmere: loam, a grass top, wooden planks, grey stone, a pond
        soil=[rgb('#3a2418'), rgb('#5a3a24'), rgb('#7a5030'), rgb('#9a6a3e')],
        soil2=[rgb('#2e1c14'), rgb('#4a3020'), rgb('#68442a')],
        peb=[rgb('#4a4040'), rgb('#8a8078'), rgb('#c0b8a8')],
        top=[rgb('#1e5a24'), rgb('#3a8a30'), rgb('#6ab83a'), rgb('#b0e060')],
        topkind='grass',
        plank=[rgb('#4a2a16'), rgb('#7a4a24'), rgb('#a86a36'), rgb('#d09a5a')],
        stone=[rgb('#4a4a58'), rgb('#7a7a88'), rgb('#a8a8b4'), rgb('#d8d8e0')],
        liquid=[rgb('#1a3a8a'), rgb('#2a6ac0'), rgb('#5aa0e8'), rgb('#c0e8ff')],
        spike=[rgb('#4a4a58'), rgb('#9a9aa8'), rgb('#e0e0f0')],
        deco='meadow',
    ),
    dict(  # Mirewood: black peat, moss, mossy logs, old stones, a bog
        soil=[rgb('#14100e'), rgb('#261c18'), rgb('#3a2c24'), rgb('#4e3c30')],
        soil2=[rgb('#100c0c'), rgb('#1e1614'), rgb('#2e221c')],
        peb=[rgb('#20221e'), rgb('#3a4036'), rgb('#5a6050')],
        top=[rgb('#1a3a1e'), rgb('#2e5a2a'), rgb('#4a8a3a'), rgb('#8ac060')],
        topkind='moss',
        plank=[rgb('#1e1418'), rgb('#3a2a28'), rgb('#5a4234'), rgb('#7a5e44')],
        stone=[rgb('#262a2a'), rgb('#3e4644'), rgb('#5a6660'), rgb('#7a8a7e')],
        liquid=[rgb('#14260e'), rgb('#2a4a18'), rgb('#4a7a2a'), rgb('#a0c870')],
        spike=[rgb('#2a1e1a'), rgb('#5a4434'), rgb('#9a8060')],
        deco='forest',
    ),
    dict(  # Glimmerdeep: blue rock, crystal-studded edges, crystal ledges, a glowing pool
        soil=[rgb('#0a1222'), rgb('#16223a'), rgb('#243656'), rgb('#344a70')],
        soil2=[rgb('#080e1a'), rgb('#121c30'), rgb('#1c2a48')],
        peb=[rgb('#1a2a44'), rgb('#3a5a7a'), rgb('#6a90b0')],
        top=[rgb('#1a3a5a'), rgb('#2a6a8a'), rgb('#40c0d0'), rgb('#b0fff8')],
        topkind='crystal',
        plank=[rgb('#12304a'), rgb('#1e5a7a'), rgb('#3aa0c0'), rgb('#a0f0ff')],
        stone=[rgb('#1a1a4a'), rgb('#3a3a8a'), rgb('#6a60c0'), rgb('#b0a0ff')],
        liquid=[rgb('#082a3a'), rgb('#0e5a6a'), rgb('#20a0a8'), rgb('#90fff0')],
        spike=[rgb('#1a3a6a'), rgb('#4a8ac0'), rgb('#c0f0ff')],
        deco='cave',
    ),
    dict(  # The Hollow Spire: dressed stone, cobbles, iron grates, dark blocks, molten iron
        soil=[rgb('#16142a'), rgb('#26243e'), rgb('#3a3856'), rgb('#504e6e')],
        soil2=[rgb('#100e20'), rgb('#1c1a30'), rgb('#2a2844')],
        peb=[rgb('#1a1830'), rgb('#3a3854'), rgb('#5a587a')],
        top=[rgb('#2a2840'), rgb('#4a4866'), rgb('#6e6c8e'), rgb('#a4a2c4')],
        topkind='cobble',
        plank=[rgb('#1a1a24'), rgb('#3a3a4a'), rgb('#6a6a7e'), rgb('#a0a0b8')],
        stone=[rgb('#2a1a2a'), rgb('#4a2a40'), rgb('#6a3a5a'), rgb('#9a5a7a')],
        liquid=[rgb('#6a1008'), rgb('#c03a10'), rgb('#ff8a20'), rgb('#ffe080')],
        spike=[rgb('#2a2a34'), rgb('#6a6a7a'), rgb('#c0c0d0')],
        deco='castle',
    ),
]


def soil(m, seed, bricks=False):
    if bricks:
        t = texture(seed, m['soil'], contrast=0.7, cells=(4, 8))
        for y in range(T):
            for x in range(T):
                row = y // 8
                xx = (x + (8 if row % 2 else 0)) % T
                if y % 8 == 7 or xx % T == 15:
                    t.put(x, y, m['soil'][0])
                elif y % 8 == 0 or xx == 0:
                    t.put(x, y, mix(t.get(x, y), m['soil'][3], 0.4))
        return t
    t = texture(seed, m['soil'], contrast=1.1, speck=0.06)
    pebbles(t, seed + 5, 3, m['peb'])
    return t


def topper(t, m, seed, kind):
    r = random.Random(seed)
    top = m['top']
    if kind == 'grass':
        for x in range(T):
            h = 4 + int(tnoise(seed, x, 0, 4) * 3)
            for y in range(h + 1):
                t.put(x, y, ramp(top, 0.95 - y / (h + 1) * 0.7))
            t.put(x, h + 1, mix(top[0], m['soil'][0], 0.5))
            if r.random() < 0.45:
                t.put(x, 0, top[3])
        for _ in range(4):
            x = r.randrange(T)
            t.put(x, 0, top[3])
            t.put(x, 1, top[2])
    elif kind == 'moss':
        for x in range(T):
            h = 3 + int(tnoise(seed, x, 0, 4) * 4)
            for y in range(h + 1):
                t.put(x, y, ramp(top, 0.85 - y / (h + 1) * 0.6 + (tnoise(seed + 2, x, y, 8) - 0.5) * 0.3))
            if r.random() < 0.25:
                for k in range(r.randrange(2, 5)):
                    t.put(x, h + 1 + k, mix(top[1], m['soil'][1], k / 5))
    elif kind == 'crystal':
        for x in range(T):
            for y in range(3):
                t.put(x, y, ramp(m['soil'], 0.95 - y * 0.15))
        for cx in (3, 11):
            h = r.randrange(3, 6)
            for y in range(h):
                for dx in range(-1, 2):
                    if abs(dx) <= (h - y) // 2 + 0:
                        t.put(cx + dx, 2 - y + 1, ramp(top, 0.95 - abs(dx) * 0.3 - y * 0.05))
            t.put(cx, 3 - h + 1, top[3])
        for x in range(T):
            t.put(x, 3, mix(t.get(x, 3) or top[1], top[2], 0.6))
    elif kind == 'cobble':
        stones = [(0, 6), (6, 5), (11, 5)]
        for x0, w in stones:
            for x in range(x0, x0 + w):
                for y in range(5):
                    k = (x - x0) / w
                    v = 0.75 - y * 0.08 - k * 0.2 + (tnoise(seed, x, y, 8) - 0.5) * 0.3
                    c = ramp(top, v)
                    if x == x0 + w - 1 or y == 4:
                        c = top[0]
                    if y == 0:
                        c = top[3]
                    t.put(x, y, c)


def tile_set(land):
    m = MATERIALS[land]
    seed = 1000 * (land + 1)
    bricks = land == 3
    tiles = [Tile() for _ in NAMES]

    fill = soil(m, seed, bricks)
    tiles[ID['FILL']] = fill
    fill2 = soil(m, seed + 50, bricks)
    if not bricks:
        pebbles(fill2, seed + 51, 2, m['peb'])
        r = random.Random(seed + 52)
        if land in (0, 1):  # a root
            x = r.randrange(3, 12)
            for y in range(2, 14):
                x += r.choice((-1, 0, 0, 1))
                fill2.put(x, y, m['soil'][0])
                fill2.put(x + 1, y, m['soil2'][2])
        if land == 2:  # a vein of crystal
            for k in range(10):
                fill2.put(3 + k, 4 + (k * 7) % 9 // 2, m['top'][2] if k % 3 else m['top'][3])
    tiles[ID['FILL2']] = fill2
    tiles[ID['UNDER']] = texture(seed + 60, m['soil2'], contrast=0.8)

    top = fill.copy()
    topper(top, m, seed + 1, m['topkind'])
    tiles[ID['TOP']] = top

    def edge(base, side, with_top):
        t = base.copy()
        x0 = 0 if side < 0 else T - 1
        for y in range(T):
            for k in range(2):
                x = x0 - side * k
                c = t.get(x, y)
                if c is not None:
                    t.put(x, y, mix(c, m['soil'][0] if k == 0 else m['soil2'][1], 0.75 if k == 0 else 0.3))
        if with_top:
            # rounded corner: cut the outer corner, keep the top's colour
            for y in range(3):
                for k in range(3 - y):
                    t.c[y][x0 - side * k] = None
            for y in range(6):
                c = t.get(x0 - side * (3 - min(3, y)), y)
        return t

    tiles[ID['TOP_L']] = edge(top, -1, True)
    tiles[ID['TOP_R']] = edge(top, 1, True)
    tiles[ID['FILL_L']] = edge(fill, -1, False)
    tiles[ID['FILL_R']] = edge(fill, 1, False)

    # one-way platform: 6 pixels thick, its underside open
    pl = m['plank']
    for name, side in (('PLAT_L', -1), ('PLAT_M', 0), ('PLAT_R', 1)):
        t = Tile()
        for x in range(T):
            for y in range(6):
                v = 0.78 - y * 0.1 + (tnoise(seed + 70, x * 2, y, 8) - 0.5) * 0.3
                c = ramp(pl, v)
                if y == 0:
                    c = pl[3]
                if y == 5:
                    c = pl[0]
                if land in (0, 1) and x in (0, 8):
                    c = pl[0] if y > 0 else pl[2]
                if land == 3 and (x % 4 == 0) and 0 < y < 5:
                    c = pl[0]
                t.put(x, y, c)
            if land == 1 and x % 5 == 2:
                t.put(x, 0, m['top'][2])
        if side != 0:
            x0 = 0 if side < 0 else T - 1
            for y in range(6):
                t.put(x0, y, pl[0])
            t.c[0][x0] = None
            t.c[5][x0] = None
            if land == 0:  # a peg
                t.put(x0 - side * 2, 2, pl[0])
                t.put(x0 - side * 2, 3, pl[1])
        if land == 2:
            for y in range(6, 9):
                for x in range(T):
                    if (x * 5 + y * 3) % 7 < 2 and y - 6 < 2:
                        t.put(x, y, pl[2])
        tiles[ID[name]] = t

    st = m['stone']
    block = Tile()
    for y in range(T):
        for x in range(T):
            v = 0.55 + (tnoise(seed + 80, x, y, 4) - 0.5) * 0.35
            if x == 0 or y == 0:
                v += 0.35
            if x == T - 1 or y == T - 1:
                v -= 0.4
            if (x == 1 or y == 1) and x < T - 1 and y < T - 1:
                v += 0.15
            block.put(x, y, ramp(st, v))
    if land == 1:
        for x in range(T):
            for y in range(int(2 + 2 * tnoise(seed + 81, x, 0, 4))):
                block.put(x, y, ramp(m['top'], 0.8 - y * 0.2))
    tiles[ID['BLOCK']] = block

    tiles[ID['BOX_A']] = lumen_box(land, 0)
    tiles[ID['BOX_B']] = lumen_box(land, 1)
    tiles[ID['BOX_USED']] = lumen_box(land, 2)

    sp = Tile()
    sc = m['spike']
    for k in range(4):
        cx = k * 4 + 2
        for y in range(8, T):
            half = (y - 8) / 8 * 2
            for x in range(int(cx - half), int(cx + half) + 1):
                d = x - cx
                sp.put(x, y, sc[2] if d < 0 else (sc[1] if d == 0 else sc[0]))
        sp.put(cx, 7, sc[2])
    for x in range(T):
        sp.put(x, 15, sc[0])
    tiles[ID['SPIKES']] = sp

    lq = m['liquid']
    for f in range(2):
        t = Tile()
        u = Tile()
        for x in range(T):
            wave = 2 + math.sin((x + f * 4) / T * 2 * math.pi) * 1.4
            for y in range(T):
                if y < wave:
                    continue
                v = 0.55 + (tnoise(seed + 90 + f, x, y, 4) - 0.5) * 0.4 - (y - wave) * 0.02
                c = ramp(lq, v)
                if y - wave < 1.5:
                    c = lq[3] if (x + f * 3) % 5 else lq[2]
                t.put(x, y, c)
            for y in range(T):
                v = 0.4 + (tnoise(seed + 92, x + f * 4, y + f * 3, 4) - 0.5) * 0.45
                u.put(x, y, ramp(lq, v))
            if land == 3:  # molten iron glows brighter in its cracks
                for y in range(T):
                    if tnoise(seed + 94, x + f * 8, y, 8) > 0.68:
                        u.put(x, y, lq[3])
        tiles[ID['LIQTOP_A'] + f] = t
        tiles[ID['LIQ_A'] + f] = u

    cr = Tile()
    for y in range(T):
        for x in range(T):
            v = 0.55 + (tnoise(seed + 100, x * 3, y, 8) - 0.5) * 0.3
            if y in (0, 15) or x in (0, 15):
                v = 0.05
            elif y in (1, 14) or x in (1, 14):
                v = 0.85
            elif x == y or x == 15 - y:
                v = 0.25
            cr.put(x, y, ramp(m['plank'], v))
    tiles[ID['CRATE']] = cr

    for i, d in enumerate(decorations(land, m)):
        tiles[ID['DECO1'] + i] = d

    col = Tile()
    ctop = Tile()
    for y in range(T):
        for x in range(2, 14):
            k = (x - 2) / 11
            v = 0.3 + 0.55 * math.sin(k * math.pi) * (1.1 - k * 0.5)
            if (x - 2) % 4 == 0:
                v -= 0.15
            col.put(x, y, ramp(st, v))
            ctop.put(x, y, ramp(st, v))
    for y in range(5):
        for x in range(T):
            v = 0.7 - y * 0.1 - abs(x - 7.5) / 20
            if y == 4:
                v = 0.15
            ctop.put(x, y, ramp(st, v))
    tiles[ID['COLUMN']] = col
    tiles[ID['COLUMN_TOP']] = ctop

    for f in range(4):
        tiles[ID['GLINT_A'] + f] = glint(f)
    return tiles


def lumen_box(land, frame):
    """The lumen box: hit from below, it gives what is inside."""
    t = Tile()
    gold = [rgb('#5a3a10'), rgb('#a8701a'), rgb('#e8b030'), rgb('#fff0a0')]
    if frame == 2:
        gold = [rgb('#2a2420'), rgb('#4a3e34'), rgb('#6a5a4a'), rgb('#8a7a66')]
    for y in range(T):
        for x in range(T):
            v = 0.6
            if x == 0 or y == 0:
                v = 0.95
            if x == T - 1 or y == T - 1:
                v = 0.1
            if (x in (1, 14) or y in (1, 14)) and 0 < x < 15 and 0 < y < 15:
                v = 0.4
            t.put(x, y, ramp(gold, v))
    for (x, y) in ((2, 2), (13, 2), (2, 13), (13, 13)):
        t.put(x, y, gold[0])
    if frame < 2:
        flame = [rgb('#ff8a20'), rgb('#ffd040'), rgb('#ffffff')] if frame == 0 else [rgb('#ff6a10'), rgb('#ffe060'), rgb('#fff8d0')]
        # a little lantern: cap, glass, flame
        for x in range(6, 10):
            t.put(x, 4, gold[0])
        for y in range(5, 12):
            for x in range(5, 11):
                edge = x in (5, 10) or y in (5, 11)
                t.put(x, y, gold[0] if edge else flame[0])
        h = 4 if frame == 0 else 5
        for y in range(11 - h, 11):
            for x in range(7, 9):
                t.put(x, y, flame[1] if y > 11 - h + 1 else flame[2])
        t.put(7 + frame, 7, flame[2])
    else:
        for x in range(5, 11):
            t.put(x, 8, gold[0])
    return t


def glint(frame):
    """The glint, the light Wren gathers: a spinning star of light."""
    t = Tile()
    cols = [rgb('#b06a10'), rgb('#ffc030'), rgb('#fff0a0'), rgb('#ffffff')]
    w = [5, 3, 1, 3][frame]
    for y in range(3, 13):
        dy = abs(y - 7.5)
        half = max(0, (w * (1 - dy / 5.5)))
        for x in range(int(8 - half - 0.5), int(8 + half + 0.5)):
            dx = (x - 7.5)
            c = cols[2] if dx < 0 else cols[1]
            if abs(dy) > 4 or abs(dx) >= half - 0.5:
                c = cols[0]
            t.put(x, y, c)
    if frame == 0:
        t.put(6, 5, cols[3])
        t.put(6, 6, cols[3])
    t.put(8, 2, cols[2] if frame % 2 == 0 else None)
    return t


def decorations(land, m):
    """Eight things that stand on the ground and don't block: each land its own."""
    out = []
    r = random.Random(land * 77)
    for i in range(8):
        t = Tile()
        kind = m['deco']
        if kind == 'meadow':
            [tuft, flower_red, flower_blue, rock, sign, fence, mushroom_red, bush][i](t, m, r)
        elif kind == 'forest':
            [fern, glowshroom, glowshroom2, stump, root, reeds, skull_rock, bramble][i](t, m, r)
        elif kind == 'cave':
            [crystal_s, crystal_m, crystal_violet, stalag, rubble, glowworm, bones, ore][i](t, m, r)
        else:
            [candle, banner, chain, grave, urn, gargoyle_head, brazier, iron_fence][i](t, m, r)
        out.append(t)
    return out


# --- meadow

def tuft(t, m, r):
    for k in range(9):
        x = 3 + k + r.choice((0, 0, 1))
        h = r.randrange(4, 9)
        for y in range(h):
            t.put(x + (y // 4) * (1 if k % 2 else -1), 15 - y, ramp(m['top'], 0.3 + y / h * 0.6))


def _flower(t, m, r, petal, x0=7):
    for y in range(9, 16):
        t.put(x0, y, m['top'][1])
    t.put(x0 + 1, 12, m['top'][2])
    t.put(x0 + 2, 11, m['top'][2])
    for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1), (-1, -1), (1, 1), (1, -1), (-1, 1)):
        t.put(x0 + dx, 7 + dy, petal if abs(dx) + abs(dy) == 1 else shade(petal, 0.75))
    t.put(x0, 7, rgb('#ffe060'))
    tuft2 = (x0 - 3, x0 + 3)
    for x in tuft2:
        for y in range(3):
            t.put(x, 15 - y, m['top'][2])


def flower_red(t, m, r):
    _flower(t, m, r, rgb('#ff5a6a'), 6)
    _flower(t, m, r, rgb('#ff9ab0'), 11)


def flower_blue(t, m, r):
    _flower(t, m, r, rgb('#7aa0ff'), 8)


def rock(t, m, r):
    for y in range(9, 16):
        for x in range(2, 14):
            d = ((x - 7.5) / 6) ** 2 + ((y - 15) / 6.5) ** 2
            if d < 1:
                v = 0.75 - (x - 2) / 14 - (y - 9) / 20 + (r.random() - 0.5) * 0.1
                t.put(x, y, ramp(m['stone'], v))
    for x in range(4, 12):
        if r.random() < 0.5:
            t.put(x, 9 + int(abs(x - 7.5) // 3), m['top'][2])


def sign(t, m, r):
    for y in range(8, 16):
        t.put(7, y, m['plank'][1])
        t.put(8, y, m['plank'][0])
    for y in range(3, 9):
        for x in range(2, 14):
            v = 0.7 if y > 3 else 0.95
            if y == 8 or x in (2, 13):
                v = 0.15
            t.put(x, y, ramp(m['plank'], v))
    for x in range(5, 11):  # an arrow pointing on
        t.put(x, 6, m['plank'][0])
    t.put(10, 5, m['plank'][0])
    t.put(10, 7, m['plank'][0])


def fence(t, m, r):
    for x in (2, 12):
        for y in range(5, 16):
            t.put(x, y, m['plank'][2])
            t.put(x + 1, y, m['plank'][1])
        t.put(x, 4, m['plank'][3])
    for y in (7, 11):
        for x in range(T):
            t.put(x, y, m['plank'][2])
            t.put(x, y + 1, m['plank'][0])


def mushroom_red(t, m, r):
    for y in range(11, 16):
        t.put(7, y, rgb('#f0e0c8'))
        t.put(8, y, rgb('#c8b8a0'))
    for y in range(6, 11):
        for x in range(3, 13):
            d = ((x - 7.5) / 5) ** 2 + ((y - 11) / 5) ** 2
            if d < 1:
                c = mix(rgb('#ff5040'), rgb('#902018'), (x - 3) / 12 + (y - 6) / 12)
                t.put(x, y, c)
    for x, y in ((5, 8), (9, 7), (10, 9)):
        t.put(x, y, rgb('#fff8f0'))


def bush(t, m, r):
    for y in range(6, 16):
        for x in range(0, 16):
            d = min(((x - 4.5) / 4.5) ** 2 + ((y - 11) / 5) ** 2, ((x - 10.5) / 5.5) ** 2 + ((y - 10) / 6) ** 2)
            if d < 1:
                v = 0.7 - (x / 16) * 0.3 - (y - 6) / 20 + (r.random() - 0.5) * 0.25
                t.put(x, y, ramp(m['top'], v))
    for x, y in ((4, 9), (11, 8), (8, 12)):
        t.put(x, y, rgb('#ff6a80'))


# --- forest

def fern(t, m, r):
    for k in range(5):
        ang = math.pi * (0.2 + 0.6 * k / 4)
        for i in range(12):
            s = i / 12
            x = 8 + math.cos(ang) * 8 * s
            y = 15 - math.sin(ang) * 9 * s + s * s * 4
            t.put(x, y, ramp(m['top'], 0.35 + 0.5 * (1 - s)))


def glowshroom(t, m, r, col=rgb('#60f0d0')):
    for (x0, h, w) in ((5, 7, 3), (10, 5, 2)):
        for y in range(15 - h, 16):
            t.put(x0, y, rgb('#c0c8c0'))
        for dx in range(-w, w + 1):
            t.put(x0 + dx, 15 - h, mix(col, rgb('#ffffff'), 0.3 if abs(dx) < w else 0))
            if abs(dx) < w:
                t.put(x0 + dx, 14 - h, mix(col, rgb('#ffffff'), 0.6))


def glowshroom2(t, m, r):
    glowshroom(t, m, r, rgb('#f090f0'))


def stump(t, m, r):
    for y in range(7, 16):
        for x in range(3, 13):
            k = (x - 3) / 9
            v = 0.2 + 0.6 * math.sin(k * math.pi) + (r.random() - 0.5) * 0.15
            t.put(x, y, ramp(m['plank'], v))
    for x in range(3, 13):
        t.put(x, 7, m['plank'][3] if 4 < x < 11 else m['plank'][2])
        t.put(x, 6, m['top'][2] if x % 3 else None)
    t.put(7, 8, m['plank'][1])


def root(t, m, r):
    x = 1
    for y in range(9, 16):
        for k in range(4):
            t.put(x + k, y, ramp(m['plank'], 0.3 + k * 0.15))
        x += 2


def reeds(t, m, r):
    for k in range(5):
        x = 3 + k * 2
        h = r.randrange(7, 13)
        for y in range(h):
            t.put(x, 15 - y, ramp(m['top'], 0.3 + y / h * 0.5))
        for y in range(3):
            t.put(x, 15 - h + y, rgb('#5a3a24'))


def skull_rock(t, m, r):
    rock(t, m, r)
    t.put(6, 12, rgb('#101010'))
    t.put(9, 12, rgb('#101010'))


def bramble(t, m, r):
    for k in range(40):
        a = r.uniform(0, math.pi)
        d = r.uniform(0, 7)
        x, y = 8 + math.cos(a) * d, 15 - math.sin(a) * d * 1.1
        t.put(x, y, rgb('#4a2a3a') if r.random() < 0.7 else rgb('#a04060'))


# --- cave

def _cr(t, x, y, h, w, hi, lo):
    for yy in range(int(y - h), int(y) + 1):
        tt = (yy - (y - h)) / max(1, h)
        half = w * min(1, tt * 2.0)
        for xx in range(int(x - half), int(x + half) + 1):
            k = (xx - x) / max(0.5, half)
            t.put(xx, yy, mix(hi, lo, 0.1 + (k + 1) * 0.35 + tt * 0.2))


def crystal_s(t, m, r):
    _cr(t, 7, 15, 7, 2, rgb('#80fff0'), rgb('#1a6a8a'))
    _cr(t, 11, 15, 4, 1.5, rgb('#80fff0'), rgb('#1a6a8a'))


def crystal_m(t, m, r):
    _cr(t, 8, 15, 12, 3, rgb('#a0fff8'), rgb('#1a5a8a'))
    _cr(t, 4, 15, 6, 2, rgb('#80f0f0'), rgb('#1a5a8a'))
    _cr(t, 12, 15, 8, 2, rgb('#80f0f0'), rgb('#1a5a8a'))


def crystal_violet(t, m, r):
    _cr(t, 8, 15, 10, 3, rgb('#f0b0ff'), rgb('#4a2a8a'))
    _cr(t, 12, 15, 5, 2, rgb('#d090ff'), rgb('#4a2a8a'))


def stalag(t, m, r):
    for y in range(4, 16):
        half = (y - 4) / 12 * 4
        for x in range(int(8 - half), int(8 + half) + 1):
            t.put(x, y, ramp(m['soil'], 0.7 - (x - 8 + half) / (2 * half + 1) * 0.5))


def rubble(t, m, r):
    for (x0, y0, s) in ((4, 13, 3), (9, 12, 4), (13, 14, 2)):
        for y in range(y0 - s, 16):
            for x in range(x0 - s, x0 + s + 1):
                if (x - x0) ** 2 + (y - y0) ** 2 <= s * s + 1:
                    t.put(x, y, ramp(m['peb'], 0.8 - (x - x0 + s) / (2 * s + 1) * 0.6))


def glowworm(t, m, r):
    for k in range(5):
        x, y = r.randrange(2, 14), r.randrange(2, 14)
        t.put(x, y, rgb('#a0ffd0'))
        t.put(x + 1, y, rgb('#40a080'))


def bones(t, m, r):
    for x in range(3, 13):
        t.put(x, 14, rgb('#d0c8b0'))
    for (x, y) in ((3, 13), (3, 15), (12, 13), (12, 15)):
        t.put(x, y, rgb('#e8e0c8'))


def ore(t, m, r):
    rubble(t, m, r)
    for (x, y) in ((4, 12), (9, 11), (10, 13)):
        t.put(x, y, rgb('#ffd060'))


# --- castle

def candle(t, m, r):
    for y in range(9, 15):
        for x in range(6, 10):
            t.put(x, y, rgb('#f0e8d0') if x < 8 else rgb('#c8c0a8'))
    for x in range(4, 12):
        t.put(x, 15, rgb('#5a5a6a'))
    t.put(8, 8, rgb('#3a3a3a'))
    for y, c in ((7, rgb('#ffd040')), (6, rgb('#ffe890')), (5, rgb('#fff8e0'))):
        t.put(8, y, c)
    t.put(7, 7, rgb('#ff8a20'))


def banner(t, m, r):
    for x in range(3, 13):
        t.put(x, 0, rgb('#6a6a7a'))
    for y in range(1, 14):
        for x in range(4, 12):
            if y > 11 and abs(x - 7.5) < (y - 11) * 2:
                continue
            c = mix(rgb('#8a1a2a'), rgb('#4a0a1a'), (x - 4) / 8)
            if x in (7, 8) and 3 < y < 9:
                c = rgb('#e0b040')
            t.put(x, y, c)


def chain(t, m, r):
    for y in range(T):
        if y % 4 < 3:
            t.put(7, y, rgb('#8a8a9a'))
            t.put(8, y, rgb('#4a4a5a'))
        else:
            t.put(6, y, rgb('#6a6a7a'))
            t.put(9, y, rgb('#6a6a7a'))


def grave(t, m, r):
    for y in range(4, 16):
        for x in range(3, 13):
            if y < 7 and ((x - 7.5) ** 2 + (y - 7) ** 2) > 20:
                continue
            v = 0.7 - (x - 3) / 14 + (r.random() - 0.5) * 0.1
            t.put(x, y, ramp(m['soil'], v + 0.2))
    for y in range(7, 11):
        t.put(7, y, m['soil'][0])
    for x in range(6, 9):
        t.put(x, 8, m['soil'][0])


def urn(t, m, r):
    for y in range(6, 16):
        half = 3 + math.sin((y - 6) / 9 * math.pi) * 2
        for x in range(int(8 - half), int(8 + half)):
            t.put(x, y, ramp(m['stone'], 0.8 - (x - 8 + half) / (2 * half) * 0.6))
    for x in range(5, 11):
        t.put(x, 5, m['stone'][3])


def gargoyle_head(t, m, r):
    for y in range(5, 16):
        for x in range(2, 14):
            if ((x - 8) / 6) ** 2 + ((y - 11) / 6) ** 2 < 1:
                t.put(x, y, ramp(m['soil'], 0.85 - (x - 2) / 16))
    t.put(5, 9, rgb('#ff4020'))
    t.put(9, 9, rgb('#ff4020'))
    for x in (3, 12):
        t.put(x, 5, m['soil'][3])
        t.put(x, 4, m['soil'][2])


def brazier(t, m, r):
    for x in range(4, 12):
        t.put(x, 10, rgb('#4a4a5a'))
        t.put(x, 11, rgb('#2a2a3a'))
    for y in range(12, 16):
        t.put(7, y, rgb('#3a3a4a'))
        t.put(8, y, rgb('#2a2a3a'))
    for k in range(18):
        x = 5 + r.randrange(6)
        y = 9 - r.randrange(6)
        t.put(x, y, r.choice([rgb('#ff6a10'), rgb('#ffc040'), rgb('#fff0a0')]))


def iron_fence(t, m, r):
    for x in range(1, 16, 3):
        for y in range(4, 16):
            t.put(x, y, rgb('#2a2a3a'))
        t.put(x, 3, rgb('#7a7a8a'))
    for x in range(T):
        t.put(x, 7, rgb('#3a3a4a'))
        t.put(x, 13, rgb('#3a3a4a'))
