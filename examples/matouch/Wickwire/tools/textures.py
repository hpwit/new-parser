"""Wickwire's textures: 32x32, tileable, drawn per pixel here.

All of them share one palette of 256 colours (make_wad.py reduces them
together); the last 24 entries are kept for the flat colours of the
characters (FLAT below), which have no texture. The script makes 16
light levels and 8 fog levels of every palette colour.
"""
import math
import random

from PIL import Image

N = 32


def rgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def ramp(cols, t):
    t = max(0.0, min(0.9999, t))
    f = t * (len(cols) - 1)
    i = int(f)
    return mix(cols[i], cols[i + 1], f - i)


class Noise:
    """Smooth value noise repeating every 32 pixels."""

    def __init__(self, seed, cells):
        r = random.Random(seed)
        self.c = cells
        self.v = [[r.random() for _ in range(cells)] for _ in range(cells)]

    def at(self, x, y):
        fx, fy = (x % N) * self.c / N, (y % N) * self.c / N
        x0, y0 = int(fx), int(fy)
        tx, ty = fx - x0, fy - y0
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        c, v = self.c, self.v
        a = v[y0 % c][x0 % c]
        b = v[y0 % c][(x0 + 1) % c]
        d = v[(y0 + 1) % c][x0 % c]
        e = v[(y0 + 1) % c][(x0 + 1) % c]
        return (a * (1 - tx) + b * tx) * (1 - ty) + (d * (1 - tx) + e * tx) * ty


_n = {}


def noise(seed, x, y, cells=(4, 8, 16), w=(.55, .3, .15)):
    out = 0.0
    for i, (c, k) in enumerate(zip(cells, w)):
        key = (seed * 7 + i, c)
        if key not in _n:
            _n[key] = Noise(key[0], c)
        out += _n[key].at(x, y) * k
    return out


class Tex:
    def __init__(self):
        self.c = [[(0, 0, 0)] * N for _ in range(N)]

    def put(self, x, y, col):
        self.c[int(y) % N][int(x) % N] = col

    def get(self, x, y):
        return self.c[int(y) % N][int(x) % N]

    def image(self):
        img = Image.new('RGB', (N, N))
        px = img.load()
        for y in range(N):
            for x in range(N):
                px[x, y] = tuple(int(max(0, min(255, v))) for v in self.c[y][x])
        return img


def fill(t, seed, cols, contrast=1.0, speck=0.0):
    r = random.Random(seed)
    for y in range(N):
        for x in range(N):
            v = 0.5 + (noise(seed, x, y) - 0.5) * contrast
            if speck and r.random() < speck:
                v += r.choice((-0.25, 0.25))
            t.put(x, y, ramp(cols, v))


def bricks(seed, cols, mortar, bw=16, bh=8, rows_offset=8, chip=0.15, moss=None):
    """Courses of bricks with mortar and bevelled edges."""
    t = Tex()
    r = random.Random(seed)
    tints = {}
    for y in range(N):
        row = y // bh
        off = rows_offset if row % 2 else 0
        for x in range(N):
            xx = (x + off) % N
            col = xx // bw
            key = (row, col)
            if key not in tints:
                tints[key] = r.uniform(-0.12, 0.12)
            lx, ly = xx % bw, y % bh
            if ly == bh - 1 or lx == bw - 1:
                c = mix(mortar, cols[0], noise(seed + 5, x, y) * 0.4)
            else:
                v = 0.5 + tints[key] + (noise(seed, x, y) - 0.5) * 0.45
                if ly == 0 or lx == 0:
                    v += 0.18
                if ly == bh - 2 or lx == bw - 2:
                    v -= 0.15
                if r.random() < chip * 0.05:
                    v -= 0.3
                c = ramp(cols, v)
                if moss is not None and noise(seed + 9, x, y) > 0.62:
                    c = mix(c, moss, 0.55)
            t.put(x, y, c)
    return t


# ------------------------------------------------------------ the textures

def stone():
    return bricks(1, [rgb('#3a3a44'), rgb('#6a6a76'), rgb('#9a98a4'), rgb('#c4c2cc')], rgb('#24222a'))


def stone_dark():
    return bricks(2, [rgb('#20222a'), rgb('#3a3e48'), rgb('#5a6070'), rgb('#7a8090')], rgb('#121218'),
                  moss=rgb('#3a5a34'))


def flags():
    """Floor flagstones, large and irregular."""
    t = Tex()
    r = random.Random(3)
    cols = [rgb('#3e3a3a'), rgb('#6a6262'), rgb('#8a8280'), rgb('#aaa29e')]
    cuts_x = [0, 14, 32]
    cuts_y = [0, 18, 32]
    for y in range(N):
        for x in range(N):
            gx = 1 if x >= (14 if y < 18 else 20) else 0
            gy = 1 if y >= 18 else 0
            edge = x in (0, 14 if y < 18 else 20) or y in (0, 18)
            v = 0.5 + (noise(3, x, y) - 0.5) * 0.5 + (0.08 if (gx + gy) % 2 else -0.04)
            c = ramp(cols, v)
            if edge:
                c = rgb('#2a2626')
            t.put(x, y, c)
    return t


def wood():
    """Floor planks running along v."""
    t = Tex()
    cols = [rgb('#3a2414'), rgb('#6a4224'), rgb('#94643a'), rgb('#b8865a')]
    for y in range(N):
        for x in range(N):
            plank = x // 8
            lx = x % 8
            grain = math.sin((y + plank * 11) * 0.45 + noise(4, x, y) * 6) * 0.12
            v = 0.5 + grain + (plank % 2) * 0.06 + (noise(4, x * 3, y) - 0.5) * 0.25
            if lx == 7:
                v = 0.05
            if lx == 0:
                v += 0.12
            if (y + plank * 13) % 32 == 0:
                v = 0.1
            t.put(x, y, ramp(cols, v))
    for plank in range(4):
        for yy in (4, 20):
            t.put(plank * 8 + 2, (yy + plank * 5) % N, rgb('#2a1a10'))
            t.put(plank * 8 + 5, (yy + plank * 5) % N, rgb('#2a1a10'))
    return t


def crate():
    """A crate: planks, a diagonal brace, iron corners."""
    t = Tex()
    cols = [rgb('#4a2e14'), rgb('#7a5228'), rgb('#a8763e'), rgb('#d4a464')]
    for y in range(N):
        for x in range(N):
            v = 0.55 + (noise(5, x * 2, y) - 0.5) * 0.3
            if y % 8 == 7:
                v = 0.2
            if x < 3 or x > 28 or y < 3 or y > 28:
                v = 0.75 if (x in (1, 30) or y in (1, 30)) else 0.4
            if abs(x - y) < 2 and 3 <= x <= 28:
                v = 0.85 if x - y == -1 else 0.6
            c = ramp(cols, v)
            if (x < 6 or x > 25) and (y < 6 or y > 25):
                c = mix(rgb('#3a3a44'), rgb('#a0a0b0'), 0.6 if (x + y) % 3 else 0.3)
            t.put(x, y, c)
    return t


def crate_gear():
    """The gear crate: a brass gear burnt into the lid."""
    t = crate()
    for y in range(N):
        for x in range(N):
            dx, dy = x - 15.5, y - 15.5
            d = math.hypot(dx, dy)
            a = math.atan2(dy, dx)
            teeth = 7.5 + (1.5 if math.cos(a * 8) > 0.3 else 0)
            if 3 < d < teeth:
                t.put(x, y, mix(rgb('#ffd060'), rgb('#a06010'), (dx + dy + 10) / 24))
    return t


def roof():
    """Slates in overlapping rows, cut round at the bottom."""
    t = Tex()
    cols = [rgb('#1e2234'), rgb('#343a54'), rgb('#4e5674'), rgb('#7078a0')]
    for y in range(N):
        row = y // 8
        off = 4 if row % 2 else 0
        for x in range(N):
            lx = (x + off) % 8
            ly = y % 8
            v = 0.45 + (noise(6, x, y) - 0.5) * 0.3 + ly * 0.04
            if ly == 7 or (ly == 6 and lx in (0, 7)):
                v = 0.05
            if lx == 0:
                v -= 0.15
            t.put(x, y, ramp(cols, v))
    return t


def carpet():
    t = Tex()
    for y in range(N):
        for x in range(N):
            v = 0.5 + (noise(7, x * 2, y * 2) - 0.5) * 0.25
            c = ramp([rgb('#4a0a14'), rgb('#7a1424'), rgb('#a02234')], v)
            if x in (2, 3, 28, 29):
                c = ramp([rgb('#a07010'), rgb('#f0c040')], v)
            if 10 < x < 22 and (y % 16) in (6, 7, 8, 9) and abs(x - 16) < 6 - abs((y % 16) - 7.5):
                c = rgb('#d0a030')
            t.put(x, y, c)
    return t


def stained(seed, cols):
    """Stained glass: leaded cells of coloured glass, lit from behind."""
    t = Tex()
    r = random.Random(seed)
    pts = [(r.uniform(0, N), r.uniform(0, N), r.choice(cols)) for _ in range(14)]
    for y in range(N):
        for x in range(N):
            ds = sorted(((min(abs(x - px), N - abs(x - px)) ** 2 + min(abs(y - py), N - abs(y - py)) ** 2, c)
                         for px, py, c in pts))
            d0, c0 = ds[0]
            d1 = ds[1][0]
            if math.sqrt(d1) - math.sqrt(d0) < 1.1:
                t.put(x, y, rgb('#1a1418'))
            else:
                glow = 0.75 + noise(seed, x, y) * 0.35
                t.put(x, y, tuple(min(255, v * glow) for v in c0))
    return t


def gold():
    t = Tex()
    for y in range(N):
        for x in range(N):
            v = 0.5 + 0.35 * math.sin((x + y) * 0.4) + (noise(8, x, y) - 0.5) * 0.3
            t.put(x, y, ramp([rgb('#5a3a08'), rgb('#a8781a'), rgb('#f0c040'), rgb('#fff0b0')], v))
    return t


def iron():
    t = Tex()
    for y in range(N):
        for x in range(N):
            v = 0.45 + (noise(9, x, y) - 0.5) * 0.3
            if x % 16 in (0, 15) or y % 16 in (0, 15):
                v = 0.2 if (x % 16 == 15 or y % 16 == 15) else 0.7
            t.put(x, y, ramp([rgb('#1e2026'), rgb('#3e424c'), rgb('#6a707e'), rgb('#9aa0ae')], v))
    for cx in (3, 12, 19, 28):
        for cy in (3, 12, 19, 28):
            t.put(cx, cy, rgb('#c0c4d0'))
            t.put(cx + 1, cy + 1, rgb('#1a1c20'))
    return t


def grass():
    t = Tex()
    fill(t, 10, [rgb('#1a3a1a'), rgb('#2e5a24'), rgb('#4a8030'), rgb('#7ab048')], 1.2, 0.08)
    r = random.Random(10)
    for _ in range(40):
        x, y = r.randrange(N), r.randrange(N)
        t.put(x, y, rgb('#9ad060'))
        t.put(x, y + 1, rgb('#4a8030'))
    return t


def moss_stone():
    return bricks(11, [rgb('#2a3028'), rgb('#4a5444'), rgb('#6a7860'), rgb('#8a9a7c')], rgb('#182018'),
                  bw=10, bh=10, rows_offset=5, moss=rgb('#4a7a30'))


def marble():
    t = Tex()
    for y in range(N):
        for x in range(N):
            dark = ((x // 16) + (y // 16)) % 2
            v = 0.5 + math.sin((x * 0.7 + y * 0.3) + noise(12, x, y) * 8) * 0.2
            c = ramp([rgb('#101014'), rgb('#2a2a34'), rgb('#44444e')], v) if dark else \
                ramp([rgb('#b0aab0'), rgb('#d8d2d8'), rgb('#f4f0f4')], v)
            if x % 16 == 0 or y % 16 == 0:
                c = rgb('#7a6a40')
            t.put(x, y, c)
    return t


def banner():
    t = Tex()
    for y in range(N):
        for x in range(N):
            v = 0.5 + math.sin(x * 0.5) * 0.15 + (noise(13, x, y) - 0.5) * 0.2
            c = ramp([rgb('#24103a'), rgb('#4a2070'), rgb('#7040a0')], v)
            if x in (1, 2, 29, 30):
                c = rgb('#d0a030')
            # a crest: a candle flame
            dx, dy = x - 15.5, y - 14
            if abs(dx) < 3 and 14 < y < 24:
                c = rgb('#f0e0c0')
            if (dx / 3) ** 2 + ((dy + 2) / 5) ** 2 < 1 and y <= 14:
                c = mix(rgb('#ffffff'), rgb('#ffb030'), (dx * dx + dy * dy) / 20)
            t.put(x, y, c)
    return t


def portrait(seed):
    """A painting in a gilt frame: a dim figure or a stormy sea."""
    t = Tex()
    r = random.Random(seed)
    for y in range(N):
        for x in range(N):
            if x < 3 or x > 28 or y < 3 or y > 28:
                v = 0.5 + 0.4 * math.sin((x + y) * 0.7)
                t.put(x, y, ramp([rgb('#4a3008'), rgb('#a8781a'), rgb('#f0c860')], v))
                continue
            if seed == 1:
                # a lady in green, her face pale, a dark ground
                bg = ramp([rgb('#141008'), rgb('#2a2014'), rgb('#3a2c1a')], noise(14, x, y))
                c = bg
                if (x - 15.5) ** 2 / 25 + (y - 11) ** 2 / 30 < 1:
                    c = mix(rgb('#e0c8a8'), rgb('#a08060'), (x - 10) / 12)
                if y > 17 and abs(x - 15.5) < (y - 14) * 0.9:
                    c = ramp([rgb('#1a3a20'), rgb('#3a6a3a')], noise(15, x, y))
                if (x - 15.5) ** 2 / 36 + (y - 8) ** 2 / 12 < 1 and y < 9:
                    c = rgb('#2a1a10')
                if y == 11 and x in (13, 18):
                    c = rgb('#1a0a0a')
            else:
                # a moonlit sea and a lighthouse
                c = ramp([rgb('#0a1028'), rgb('#1a2a50'), rgb('#3a5080')], y / 32 + (noise(16, x, y) - 0.5) * 0.3)
                if y > 20:
                    c = ramp([rgb('#0a1a2a'), rgb('#2a4a6a')], noise(17, x * 2, y) + math.sin(x * 0.8 + y) * 0.2)
                if (x - 22) ** 2 + (y - 9) ** 2 < 9:
                    c = rgb('#f0f0d0')
                if 8 < x < 11 and 10 < y < 22:
                    c = rgb('#d8d0c0') if y % 4 else rgb('#a02020')
                if 8 <= x <= 11 and y == 10:
                    c = rgb('#ffe080')
            t.put(x, y, c)
    return t


def window():
    """A tall window onto the night: dark blue, the moon's light, lead bars."""
    t = Tex()
    for y in range(N):
        for x in range(N):
            c = ramp([rgb('#0a1030'), rgb('#1a2a5a'), rgb('#3a4a8a')], 1 - y / 40 + noise(18, x, y) * 0.2)
            if (x - 22) ** 2 + (y - 8) ** 2 < 16:
                c = rgb('#e8ecff')
            if x % 16 == 0 or y % 16 == 0:
                c = rgb('#14141c')
            if x in (0, 31) or y in (0, 31):
                c = rgb('#3a3a44')
            t.put(x, y, c)
    return t


def door():
    t = Tex()
    for y in range(N):
        for x in range(N):
            v = 0.5 + (noise(19, x * 2, y) - 0.5) * 0.3 + (0.08 if (x // 8) % 2 else 0)
            if x % 8 == 7:
                v = 0.1
            c = ramp([rgb('#2a1608'), rgb('#4a2a14'), rgb('#6a4224')], v)
            if y in (5, 6, 25, 26):
                c = rgb('#2a2a30') if y in (6, 26) else rgb('#5a5a66')
            if y in (5, 25) and x % 6 == 2:
                c = rgb('#a0a0b0')
            t.put(x, y, c)
    return t


def books():
    t = Tex()
    r = random.Random(20)
    for shelf in range(2):
        y0 = shelf * 16
        x = 0
        while x < N:
            w = r.randrange(2, 5)
            c = r.choice([rgb('#6a1a1a'), rgb('#1a3a6a'), rgb('#2a5a2a'), rgb('#6a4a1a'), rgb('#4a2a5a')])
            h = r.randrange(10, 15)
            for xx in range(x, min(N, x + w)):
                for y in range(y0 + 15 - h, y0 + 15):
                    k = (xx - x) / max(1, w - 1)
                    t.put(xx, y, mix(c, rgb('#000000'), 0.4 * k) if not (y - y0) % 5 == 0 else rgb('#c0a040'))
            x += w
        for xx in range(N):
            t.put(xx, y0 + 15, rgb('#3a2414'))
            for y in range(y0, y0 + 15):
                if t.get(xx, y) == (0, 0, 0):
                    t.put(xx, y, rgb('#140a06'))
    return t


def water():
    t = Tex()
    for y in range(N):
        for x in range(N):
            v = 0.5 + math.sin(x * 0.4 + noise(21, x, y) * 5) * 0.2 + (noise(21, x, y) - 0.5) * 0.4
            t.put(x, y, ramp([rgb('#06142a'), rgb('#0e2a4a'), rgb('#1e4a70'), rgb('#6aa0c0')], v))
    return t


def moon_rock():
    t = Tex()
    fill(t, 22, [rgb('#3a3a48'), rgb('#5e5e70'), rgb('#8a8a9c'), rgb('#b8b8c8')], 1.1, 0.05)
    for (cx, cy, rr) in ((8, 9, 5), (22, 20, 7), (25, 6, 3)):
        for y in range(N):
            for x in range(N):
                d = math.hypot(x - cx, y - cy)
                if d < rr:
                    t.put(x, y, mix(t.get(x, y), rgb('#2a2a36'), 0.5 * (1 - d / rr)))
                elif d < rr + 1.2:
                    t.put(x, y, mix(t.get(x, y), rgb('#d8d8e8'), 0.5))
    return t


def moon_moss():
    t = Tex()
    fill(t, 23, [rgb('#1a3a3a'), rgb('#2a5a50'), rgb('#4a8a70'), rgb('#90d0a0')], 1.2, 0.08)
    r = random.Random(23)
    for _ in range(12):
        x, y = r.randrange(N), r.randrange(N)
        t.put(x, y, rgb('#ffe0f0'))
    return t


def starstone():
    t = Tex()
    for y in range(N):
        for x in range(N):
            v = 0.4 + (noise(24, x, y) - 0.5) * 0.5
            t.put(x, y, ramp([rgb('#1a1040'), rgb('#3a2a80'), rgb('#6a5ad0')], v))
    r = random.Random(24)
    for _ in range(18):
        x, y = r.randrange(N), r.randrange(N)
        t.put(x, y, rgb('#ffffff'))
        if r.random() < 0.3:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                t.put(x + dx, y + dy, rgb('#a0a0ff'))
    return t


def red_brick():
    return bricks(25, [rgb('#4a1a12'), rgb('#7a2e1e'), rgb('#a0442a'), rgb('#c06a48')], rgb('#2a2220'),
                  bw=8, bh=4, rows_offset=4)


def pillar():
    t = Tex()
    for y in range(N):
        for x in range(N):
            flute = math.cos(x / N * 2 * math.pi * 4)
            v = 0.55 + flute * 0.25 + (noise(26, x, y) - 0.5) * 0.15
            t.put(x, y, ramp([rgb('#4a4650'), rgb('#7a7682'), rgb('#aaa6b0'), rgb('#d4d0d8')], v))
    return t


def bell_bronze():
    t = Tex()
    for y in range(N):
        for x in range(N):
            v = 0.5 + math.sin(y * 0.3) * 0.2 + (noise(27, x, y) - 0.5) * 0.4
            c = ramp([rgb('#2a1a08'), rgb('#6a4a1a'), rgb('#a8803a'), rgb('#e0c070')], v)
            if noise(28, x, y) > 0.66:
                c = mix(c, rgb('#3a8a6a'), 0.5)   # verdigris
            t.put(x, y, c)
    return t


def candle_wall():
    """A wall sconce glowing on stone (for lit corners)."""
    t = bricks(29, [rgb('#3a342e'), rgb('#6a5e50'), rgb('#9a8a76'), rgb('#c8b498')], rgb('#2a2420'))
    for y in range(N):
        for x in range(N):
            d = math.hypot(x - 15.5, y - 12) / 16
            if d < 1:
                t.put(x, y, mix(t.get(x, y), rgb('#ffc060'), (1 - d) ** 2 * 0.8))
    for y in range(14, 24):
        t.put(15, y, rgb('#f0e8d0'))
        t.put(16, y, rgb('#d0c8b0'))
    for y, c in ((11, rgb('#fff8e0')), (12, rgb('#ffe080')), (13, rgb('#ffb030'))):
        t.put(15, y, c)
        t.put(16, y, c)
    return t


TEXTURES = [
    ('STONE', stone), ('STONE_DARK', stone_dark), ('FLAGS', flags), ('WOOD', wood),
    ('CRATE', crate), ('CRATE_GEAR', crate_gear), ('ROOF', roof), ('CARPET', carpet),
    ('GLASS_ROSE', lambda: stained(30, [rgb('#e02040'), rgb('#f0c020'), rgb('#2060e0'), rgb('#20a060'), rgb('#a030d0')])),
    ('GLASS_SEA', lambda: stained(31, [rgb('#2080e0'), rgb('#20c0c0'), rgb('#40e080'), rgb('#e0e0ff')])),
    ('GOLD', gold), ('IRON', iron), ('GRASS', grass), ('MOSS_STONE', moss_stone),
    ('MARBLE', marble), ('BANNER', banner), ('PORTRAIT1', lambda: portrait(1)), ('PORTRAIT2', lambda: portrait(2)),
    ('WINDOW', window), ('DOOR', door), ('BOOKS', books), ('WATER', water),
    ('MOON_ROCK', moon_rock), ('MOON_MOSS', moon_moss), ('STARSTONE', starstone), ('RED_BRICK', red_brick),
    ('PILLAR', pillar), ('BRONZE', bell_bronze), ('SCONCE', candle_wall),
]

# the characters' flat colours: they take the palette's last entries
FLAT = [
    ('FOX', rgb('#ff7a1a')), ('FOX_DARK', rgb('#d04a10')), ('CREAM', rgb('#fff2d8')),
    ('BRASS', rgb('#ffc830')), ('BRASS_DARK', rgb('#c88a10')), ('EYE', rgb('#1a1030')),
    ('EYE_GLOW', rgb('#60ffff')), ('FLAME', rgb('#ffd830')), ('FLAME_HOT', rgb('#fffbe0')),
    ('GHOST', rgb('#d8f0ff')), ('GHOST_DARK', rgb('#70a8ff')), ('BOMB', rgb('#30305a')),
    ('BOMB_HI', rgb('#8a8ab8')), ('FUSE_RED', rgb('#ff3010')), ('ARMOUR', rgb('#c0d0f0')),
    ('ARMOUR_DARK', rgb('#5870a8')), ('PLUME', rgb('#ff2050')), ('BAT', rgb('#6a3aa0')),
    ('SPIDER', rgb('#5a2a40')), ('SPIDER_EYE', rgb('#ff3030')), ('HEART', rgb('#ff3a70')),
    ('KEY', rgb('#ffe020')), ('WHITE', rgb('#ffffff')), ('SHADOW', rgb('#100820')),
]
