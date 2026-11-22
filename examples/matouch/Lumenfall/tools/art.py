"""Lumenfall's pictures, drawn here: the parallax layers of the four lands,
their 16x16 tiles, Wren and every creature.

Everything is drawn at its real size, pixel by pixel, in RGB565: gradients
are dithered on the 565 steps (an ordered 4x4 pattern), so the skies keep
their bands soft on the panel. Each picture is then reduced to at most 255
of its colours plus "transparent" (make_wad.py stores a palette of RGB565
colours and one byte a pixel). The background layers wrap: every pattern
across them repeats every W pixels.
"""
import math
import random

from PIL import Image

W = 512            # the width of every background layer (they wrap)
T = 16             # tile size
SCREEN_W, SCREEN_H = 400, 240

# where each layer sits on the screen, and its height
L0_Y, L0_H = 0, 152
L1_Y, L1_H = 64, 128
L2_Y, L2_H = 120, 120

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


# ------------------------------------------------------------ colours

def rgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def shade(c, f):
    return tuple(max(0, min(255, c[i] * f)) for i in range(3))


def add(c, d):
    return tuple(max(0, min(255, c[i] + d[i])) for i in range(3))


def ramp(cols, t):
    t = max(0.0, min(0.9999, t))
    f = t * (len(cols) - 1)
    i = int(f)
    return mix(cols[i], cols[i + 1], f - i)


def q565(c, x=0, y=0, dither=True):
    """The colour as the panel shows it: 5, 6 and 5 bits, ordered-dithered
    between the two nearest steps when `dither`."""
    b = (BAYER[y & 3][x & 3] + 0.5) / 16.0 if dither else 0.5
    out = []
    for v, bits in zip(c, (5, 6, 5)):
        top = (1 << bits) - 1
        n = int(max(0.0, min(255.0, v)) / 255.0 * top + b)
        n = max(0, min(top, n))
        out.append(int(round(n * 255 / top)))
    return tuple(out)


# ------------------------------------------------------------ noise

class Noise1:
    """Smooth periodic noise in x: period `period` pixels, `cells` knots."""

    def __init__(self, seed, cells, period=W):
        r = random.Random(seed)
        self.c = cells
        self.p = period
        self.v = [r.random() for _ in range(cells)]

    def at(self, x):
        f = (x % self.p) * self.c / self.p
        i = int(f)
        t = f - i
        t = t * t * (3 - 2 * t)
        return self.v[i % self.c] * (1 - t) + self.v[(i + 1) % self.c] * t


class Noise2:
    """Smooth noise periodic in x (period px) and y (period py)."""

    def __init__(self, seed, cx, cy, px=W, py=256):
        r = random.Random(seed)
        self.cx, self.cy, self.px, self.py = cx, cy, px, py
        self.v = [[r.random() for _ in range(cx)] for _ in range(cy)]

    def at(self, x, y):
        fx = (x % self.px) * self.cx / self.px
        fy = (y % self.py) * self.cy / self.py
        x0, y0 = int(fx), int(fy)
        tx, ty = fx - x0, fy - y0
        tx = tx * tx * (3 - 2 * tx)
        ty = ty * ty * (3 - 2 * ty)
        v = self.v
        a = v[y0 % self.cy][x0 % self.cx]
        b = v[y0 % self.cy][(x0 + 1) % self.cx]
        c = v[(y0 + 1) % self.cy][x0 % self.cx]
        d = v[(y0 + 1) % self.cy][(x0 + 1) % self.cx]
        return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


def fbm1(seed, x, octaves):
    key = ('1', seed, octaves)
    if key not in _cache:
        _cache[key] = [Noise1(seed * 17 + i, c) for i, (c, _) in enumerate(octaves)]
    return sum(n.at(x) * w for n, (_, w) in zip(_cache[key], octaves))


def fbm2(seed, x, y, octaves, px=W, py=256):
    key = ('2', seed, octaves, px, py)
    if key not in _cache:
        _cache[key] = [Noise2(seed * 13 + i, c, max(1, c * py // px), px, py)
                       for i, (c, _) in enumerate(octaves)]
    return sum(n.at(x, y) * w for n, (_, w) in zip(_cache[key], octaves))


_cache = {}


def wrapdx(a, b, period=W):
    """The shortest x distance from a to b on a wrapping layer."""
    d = (b - a) % period
    return d - period if d > period / 2 else d


# ------------------------------------------------------------ a layer

class Layer:
    """A float RGB picture with coverage, drawn into, then made 565."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.c = [[None] * w for _ in range(h)]

    def put(self, x, y, col):
        x, y = int(math.floor(x)) % self.w, int(math.floor(y))
        if 0 <= y < self.h:
            self.c[y][x] = col

    def get(self, x, y):
        x, y = int(math.floor(x)), int(math.floor(y))
        if 0 <= y < self.h:
            return self.c[y][x % self.w]
        return None

    def image(self, dither=True):
        img = Image.new('RGBA', (self.w, self.h), (0, 0, 0, 0))
        px = img.load()
        for y in range(self.h):
            row = self.c[y]
            for x in range(self.w):
                c = row[x]
                if c is not None:
                    px[x, y] = q565(c, x, y, dither) + (255,)
        return img


def outline_layer(L, col, test=None):
    """Darkens the outermost opaque pixels against transparency (a rim)."""
    marks = []
    for y in range(L.h):
        for x in range(L.w):
            if L.c[y][x] is None:
                continue
            for dx, dy in ((0, -1), (-1, 0), (1, 0)):
                if L.get(x + dx, y + dy) is None and 0 <= y + dy:
                    marks.append((x, y))
                    break
    for x, y in marks:
        L.c[y][x] = mix(L.c[y][x], col, 0.55)


# ------------------------------------------------------------ pieces

def sky(L, stops, y0=0, y1=None):
    y1 = L.h if y1 is None else y1
    for y in range(y0, y1):
        t = (y - y0) / max(1, (y1 - y0 - 1))
        c = ramp(stops, t)
        for x in range(L.w):
            L.c[y][x] = c


def glow(L, cx, cy, r, col, strength):
    for y in range(max(0, int(cy - r)), min(L.h, int(cy + r) + 1)):
        for x in range(int(cx - r), int(cx + r) + 1):
            d = math.hypot(x - cx, y - cy) / r
            if d < 1 and L.c[y][x % L.w] is not None:
                f = (1 - d) ** 2 * strength
                L.c[y][x % L.w] = mix(L.c[y][x % L.w], col, f)


def disc(L, cx, cy, r, inner, outer, rim=None):
    for y in range(int(cy - r) - 1, int(cy + r) + 2):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            d = math.hypot(x - cx, y - cy)
            if d <= r:
                t = d / r
                c = mix(inner, outer, t ** 2)
                if rim is not None and d > r - 1.2:
                    c = mix(c, rim, 0.5)
                L.put(x, y, c)


def stars(L, seed, n, y1, cols):
    r = random.Random(seed)
    for _ in range(n):
        x, y = r.randrange(L.w), r.randrange(y1)
        c = r.choice(cols)
        if L.get(x, y) is not None:
            L.put(x, y, mix(L.get(x, y), c, r.uniform(0.5, 1.0)))
            if r.random() < 0.12:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if L.get(x + dx, y + dy) is not None:
                        L.put(x + dx, y + dy, mix(L.get(x + dx, y + dy), c, 0.35))


def cloud(L, seed, cx, cy, w, h, lit, mid, dark, rim=None):
    """A puffy cloud: overlapping circles, lit from above."""
    r = random.Random(seed)
    blobs = []
    n = max(3, int(w / (h * 0.7)))
    for i in range(n):
        bx = cx - w / 2 + (i + 0.5) * w / n + r.uniform(-3, 3)
        top = 1 - abs((i + 0.5) / n - 0.5) * 2
        br = h * (0.45 + 0.55 * top) * r.uniform(0.8, 1.05)
        blobs.append((bx, cy + h * 0.25 - br * 0.6, br))
    base = cy + h * 0.45
    for y in range(int(cy - h * 1.3), int(base) + 1):
        for x in range(int(cx - w / 2 - h), int(cx + w / 2 + h)):
            inside = None
            for bx, by, br in blobs:
                d = math.hypot(wrapdx(bx, x, L.w), y - by)
                if d < br and y <= base:
                    k = (y - (by - br)) / (2 * br)
                    if inside is None or k < inside:
                        inside = k
            if inside is None or L.get(x, y) is None:
                continue
            t = (y - (cy - h)) / (base - (cy - h) + 1)
            c = ramp([lit, mid, dark], max(0, min(1, t * 0.7 + inside * 0.3)))
            if rim is not None and y >= base - 1:
                c = mix(c, rim, 0.5)
            L.put(x, y, c)


def ridge(L, seed, base, amp, octaves, cols, light=-1, haze=None, haze_amt=0.0,
          snow=None, snow_line=None, top_rim=None, y_end=None, solid_from=None):
    """A mountain or hill range: heights from noise, shaded by slope."""
    y_end = L.h if y_end is None else y_end
    hs = [base - amp * fbm1(seed, x, octaves) for x in range(L.w)]
    top = min(hs)
    for x in range(L.w):
        h = hs[x]
        slope = (hs[(x + 1) % L.w] - hs[(x - 1) % L.w]) * light
        for y in range(int(math.ceil(h)), y_end):
            depth = y - h
            f = 0.55 + max(-0.45, min(0.45, slope * 0.35))
            f -= min(0.25, depth / 160.0)
            tex = fbm2(seed + 3, x, y, ((32, .6), (96, .4)))
            f += (tex - 0.5) * 0.18
            c = ramp(cols, max(0, min(1, f)))
            if snow is not None and h < snow_line and depth < (snow_line - h) * 0.6 + tex * 4:
                c = mix(snow[0] if slope > -0.1 else snow[1], c, depth / 14.0)
            if haze is not None:
                t = (y - top) / max(1, (y_end - top))
                c = mix(c, haze, haze_amt * (0.35 + 0.65 * t))
            if top_rim is not None and depth < 1.0:
                c = mix(c, top_rim, 0.6)
            L.c[y][x] = c
    return hs


def tree_blob(L, seed, cx, base, height, width, leaf, trunk, light=-1, rim=None, dark=None,
              trunk_w=3):
    """A round broadleaf tree: a canopy of shaded leaf clusters on a trunk."""
    r = random.Random(seed)
    th = int(height * 0.38)
    for y in range(int(base - th - 4), int(base)):
        for x in range(int(cx - trunk_w / 2), int(cx + trunk_w / 2 + 1)):
            k = (x - (cx - trunk_w / 2)) / max(1, trunk_w)
            c = ramp(trunk, 0.25 + 0.6 * (k if light > 0 else 1 - k))
            L.put(x, y, c)
    blobs = []
    ctop = base - height
    for i in range(7 + int(width / 10)):
        bx = cx + r.uniform(-width * 0.36, width * 0.36)
        by = ctop + r.uniform(height * 0.18, height * 0.5)
        br = r.uniform(width * 0.2, width * 0.32)
        blobs.append((bx, by, br))
    for y in range(int(ctop - 2), int(base - th + 3)):
        for x in range(int(cx - width), int(cx + width)):
            best = None
            for bx, by, br in blobs:
                dx, dy = wrapdx(bx, x, L.w), y - by
                d = math.hypot(dx, dy)
                if d < br:
                    nx, ny = dx / br, dy / br
                    lit = -(nx * light * 0.7 + ny * 0.7)
                    if best is None or lit > best[0] - 0.4:
                        best = (lit, d / br)
            if best is None:
                continue
            n = fbm2(seed, x, y, ((128, .5), (256, .5)))
            v = 0.5 + best[0] * 0.38 + (n - 0.5) * 0.45
            c = ramp(leaf, max(0, min(1, v)))
            if rim is not None and best[1] > 0.86 and best[0] > 0.1:
                c = mix(c, rim, 0.6)
            L.put(x, y, c)


def pine(L, cx, base, height, width, cols, light=-1, rim=None, trunk=None):
    """A conifer: stacked jagged triangles."""
    if trunk is not None:
        for y in range(int(base - 5), int(base)):
            for x in range(int(cx - 1), int(cx + 2)):
                L.put(x, y, trunk)
    top = base - height
    for y in range(int(top), int(base - 3)):
        t = (y - top) / (height - 3)
        tier = (t * 4) % 1.0
        half = width * (0.15 + 0.85 * t) * (0.55 + 0.45 * tier) / 2
        for x in range(int(cx - half), int(cx + half) + 1):
            k = (x - cx) / max(1, half)
            v = 0.5 - k * light * 0.35 - (1 - tier) * 0.15
            c = ramp(cols, max(0, min(1, v)))
            if rim is not None and k * light < -0.75:
                c = mix(c, rim, 0.5)
            L.put(x, y, c)


def rect(L, x0, y0, x1, y1, col):
    for y in range(int(y0), int(y1)):
        for x in range(int(x0), int(x1)):
            L.put(x, y, col)


def band(L, y0, cols, seed, bumps=6.0, light=-1):
    """An opaque band to the layer's bottom, with a bumpy top (bushes)."""
    hs = [y0 - bumps * fbm1(seed, x, ((16, .5), (64, .35), (128, .15))) for x in range(L.w)]
    for x in range(L.w):
        for y in range(int(hs[x]), L.h):
            n = fbm2(seed + 1, x, y, ((64, .5), (256, .5)))
            v = 0.62 - (y - hs[x]) / (L.h - y0 + bumps + 1) * 0.5 + (n - 0.5) * 0.4
            L.c[y][x] = ramp(cols, max(0, min(1, v)))
    return hs


# ------------------------------------------------------------ the four lands

def dawnmere():
    """Sunrise over meadows: a violet-to-gold sky, the sun on the horizon,
    blue mountains, green hills with cottages, oaks and hedges."""
    L0 = Layer(W, L0_H)
    sky(L0, [rgb('#2a2c6e'), rgb('#5a4a9a'), rgb('#c06a9a'), rgb('#f0a070'), rgb('#ffd48a')], 0, L0_H)
    glow(L0, 300, 128, 120, rgb('#fff0b0'), 0.75)
    disc(L0, 300, 118, 17, rgb('#fffbe8'), rgb('#ffe07a'))
    r = random.Random(7)
    for i in range(9):
        cx = r.randrange(W)
        cy = r.randrange(14, 84)
        w = r.randrange(40, 90)
        h = r.randrange(7, 13)
        lit = mix(rgb('#ffe0c0'), rgb('#fff4e0'), cy / 84)
        cloud(L0, 100 + i, cx, cy, w, h, lit, rgb('#e8a0b0'), rgb('#8a6aa8'), rim=rgb('#ffd0a0'))
    ridge(L0, 11, 132, 46, ((4, .5), (9, .3), (23, .2)),
          [rgb('#3a3a78'), rgb('#5a5aa0'), rgb('#8a80c0')], light=1,
          haze=rgb('#e0a0a0'), haze_amt=0.45, snow=(rgb('#fff0f0'), rgb('#c0b0e0')), snow_line=100)
    ridge(L0, 12, 146, 22, ((6, .6), (17, .4)),
          [rgb('#4a4a88'), rgb('#6a6aa8'), rgb('#9a90c8')], light=1,
          haze=rgb('#f0b0a0'), haze_amt=0.55)

    L1 = Layer(W, L1_H)
    hs = ridge(L1, 21, 70, 34, ((3, .55), (7, .3), (16, .15)),
               [rgb('#2e6a40'), rgb('#4a9a50'), rgb('#8ac860')], light=1,
               haze=rgb('#c0a0a0'), haze_amt=0.25, top_rim=rgb('#d0e890'))
    r = random.Random(22)
    for i in range(26):
        x = r.randrange(W)
        h = hs[x]
        if r.random() < 0.25:
            # a cottage: wall, roof, a lit window
            y = h + 3
            rect(L1, x - 5, y - 6, x + 5, y + 1, rgb('#e8d0b0'))
            rect(L1, x - 5, y - 6, x - 4, y + 1, rgb('#b8a080'))
            for k in range(6):
                rect(L1, x - 6 + k, y - 7 - k, x + 7 - k, y - 6 - k, mix(rgb('#a04a3a'), rgb('#e07a5a'), k / 6))
            L1.put(x + 1, y - 3, rgb('#ffe070'))
            L1.put(x + 2, y - 3, rgb('#ffe070'))
        else:
            s = r.uniform(0.6, 1.1)
            tree_blob(L1, 300 + i, x, h + 4, 16 * s, 11 * s,
                      [rgb('#1e4a30'), rgb('#2e7040'), rgb('#5aa050'), rgb('#a0d070')],
                      [rgb('#3a2a20'), rgb('#6a4a30')], light=1, rim=rgb('#f0e0a0'))
    band(L1, 112, [rgb('#2a5a30'), rgb('#3a7a3a'), rgb('#5a9a48')], 23, bumps=5)

    L2 = Layer(W, L2_H)
    r = random.Random(31)
    xs = sorted(r.sample(range(0, W, 8), 7))
    for i, x in enumerate(xs):
        tree_blob(L2, 400 + i, x, 86, r.uniform(64, 84), r.uniform(34, 44),
                  [rgb('#10301e'), rgb('#1e5030'), rgb('#3a8040'), rgb('#7ab858'), rgb('#c0e080')],
                  [rgb('#2a1a14'), rgb('#5a3a26'), rgb('#8a6040')], light=1,
                  rim=rgb('#fff0a0'), trunk_w=6)
    band(L2, 72, [rgb('#103018'), rgb('#1e5028'), rgb('#3a7838'), rgb('#6aa048')], 33, bumps=9)
    flowers(L2, 34, 74, [rgb('#ffd0e0'), rgb('#fff0a0'), rgb('#c0d0ff')])
    return L0, L1, L2


def flowers(L, seed, y0, cols):
    r = random.Random(seed)
    for _ in range(140):
        x = r.randrange(L.w)
        y = r.randrange(y0, L.h)
        if L.get(x, y) is not None and L.get(x, y - 1) is not None:
            L.put(x, y, r.choice(cols))


def mirewood():
    """Dusk in an old forest: an amber-to-plum sky, a pale moon, misty ranks
    of pines, mossy giants and glowing mushrooms."""
    L0 = Layer(W, L0_H)
    sky(L0, [rgb('#1a1438'), rgb('#3a2458'), rgb('#7a3a6a'), rgb('#c8605a'), rgb('#f09a5a')], 0, L0_H)
    disc(L0, 90, 40, 11, rgb('#fff8e8'), rgb('#e0d8c8'))
    glow(L0, 90, 40, 46, rgb('#f0e0d0'), 0.35)
    stars(L0, 41, 90, 70, [rgb('#ffffff'), rgb('#ffe0c0'), rgb('#c0d0ff')])
    r = random.Random(42)
    for i in range(6):
        cloud(L0, 140 + i, r.randrange(W), r.randrange(50, 96), r.randrange(50, 110), r.randrange(4, 7),
              rgb('#f0a080'), rgb('#a05070'), rgb('#5a2a5a'))
    for k, (b, a, hz) in enumerate(((128, 26, 0.65), (142, 22, 0.5))):
        hs = [b - a * fbm1(50 + k, x, ((5, .6), (13, .4))) for x in range(W)]
        for x in range(W):
            for y in range(int(hs[x]), L0_H):
                L0.c[y][x] = mix(rgb('#3a2a5a'), rgb('#c07a7a'), hz * (1 - (y - hs[x]) / 60))
        rr = random.Random(60 + k)
        for i in range(70):
            x = rr.randrange(W)
            pine(L0, x, hs[x] + 6, rr.uniform(12, 22), rr.uniform(7, 10),
                 [mix(rgb('#2a1e48'), rgb('#c07a7a'), hz), mix(rgb('#4a3060'), rgb('#d08a80'), hz)])

    L1 = Layer(W, L1_H)
    r = random.Random(70)
    for i in range(34):
        x = r.randrange(W)
        h = r.uniform(70, 110)
        pine(L1, x, 122, h, r.uniform(18, 26),
             [rgb('#1e1638'), rgb('#3a2a58'), rgb('#6a4a7a')], light=-1, rim=rgb('#e08a7a'),
             trunk=rgb('#241a30'))
    band(L1, 90, [rgb('#1a1430'), rgb('#2a2044'), rgb('#3a2c58')], 71, bumps=8)
    fog(L1, 78, 128, rgb('#c08aa0'), 0.35, 72)

    L2 = Layer(W, L2_H)
    r = random.Random(80)
    for i in range(16):
        x = r.randrange(W)
        fern(L2, 820 + i, x, 76, r.uniform(16, 30), r)
    for i in range(3):
        x = r.randrange(W)
        log(L2, x, 70, r.randrange(40, 70))
    band(L2, 72, [rgb('#0a0a14'), rgb('#141628'), rgb('#1e2a2a'), rgb('#2a4030')], 81, bumps=10)
    for i in range(46):
        x = r.randrange(W)
        y = r.randrange(76, 110)
        if L2.get(x, y) is not None:
            mushroom(L2, x, y, r.choice([rgb('#60f0d0'), rgb('#a0f070'), rgb('#f0a0f0')]))
    return L0, L1, L2


def fog(L, y0, y1, col, amt, seed):
    for y in range(y0, min(y1, L.h)):
        for x in range(L.w):
            if L.c[y][x] is not None:
                n = fbm2(seed, x, y, ((16, .6), (48, .4)))
                L.c[y][x] = mix(L.c[y][x], col, amt * n * (1 - abs((y - (y0 + y1) / 2) / ((y1 - y0) / 2 + 1))))


def trunk(L, seed, cx, y0, y1, w, cols, moss=None):
    for y in range(y0, y1):
        wob = int(2 * (fbm1(seed, y * 4, ((8, 1.0),)) - 0.5) * 2)
        for x in range(cx - w // 2 + wob, cx + w // 2 + wob + 1):
            k = (x - (cx - w // 2 + wob)) / max(1, w)
            n = fbm2(seed, x * 3, y, ((32, .5), (128, .5)))
            v = 0.15 + 0.6 * math.sin(k * math.pi) * (1 - k * 0.5) + (n - 0.5) * 0.3
            c = ramp(cols, max(0, min(1, v)))
            if moss is not None and n > 0.62 and k < 0.6:
                c = mix(c, moss, 0.6)
            L.put(x, y, c)


def fern(L, seed, x, base, size, r):
    """A clump of arching fronds."""
    for k in range(7):
        ang = math.pi * (0.15 + 0.7 * k / 6) + r.uniform(-0.1, 0.1)
        length = size * r.uniform(0.7, 1.0)
        for i in range(int(length * 1.5)):
            t = i / (length * 1.5)
            px = x + math.cos(ang) * length * t
            py = base - math.sin(ang) * length * t + (t ** 2) * length * 0.5
            c = ramp([rgb('#0e1a14'), rgb('#1e3a26'), rgb('#3a6a3a')], 0.3 + 0.5 * (1 - t))
            L.put(px, py, c)
            if i % 2 == 0 and t > 0.15:
                for s_ in (-1, 1):
                    L.put(px + s_, py + 1, mix(c, rgb('#0a120e'), 0.3))


def log(L, x, y, length):
    for i in range(length):
        for dy in range(-4, 5):
            v = 0.5 - dy / 10 + (fbm2(851, (x + i) * 2, y + dy, ((64, 1.0),)) - 0.5) * 0.3
            c = ramp([rgb('#120c14'), rgb('#2a1e24'), rgb('#4a3a34')], max(0, min(1, v)))
            if dy <= -3 and (i * 7) % 5 < 3:
                c = mix(c, rgb('#3a7a3a'), 0.6)
            L.put(x + i, y + dy, c)
    for dy in range(-4, 5):
        for dx in range(-2, 1):
            if dx * dx + dy * dy / 4 <= 4:
                L.put(x + length + dx, y + dy, rgb('#6a5040') if abs(dy) > 1 else rgb('#8a7050'))


def mushroom(L, x, y, col):
    for dx in range(-2, 3):
        L.put(x + dx, y - 2, mix(col, rgb('#ffffff'), 0.2 if abs(dx) < 2 else 0))
    for dx in range(-1, 2):
        L.put(x + dx, y - 3, mix(col, rgb('#ffffff'), 0.5))
    L.put(x, y - 1, rgb('#c0b0a0'))
    L.put(x, y, rgb('#a09080'))
    for yy in range(y - 6, y + 3):
        for xx in range(x - 5, x + 6):
            d = math.hypot(xx - x, yy - (y - 2)) / 6
            if d < 1 and L.get(xx, yy) is not None and not (abs(xx - x) <= 2 and y - 3 <= yy <= y):
                L.put(xx, yy, mix(L.get(xx, yy), col, (1 - d) * 0.35))


def glimmerdeep():
    """Crystal caves: a dark vault with stalactites, veins of glowing crystal,
    rock pillars and sharp foreground spires."""
    L0 = Layer(W, L0_H)
    for y in range(L0_H):
        for x in range(W):
            n = fbm2(90, x, y, ((16, .45), (48, .35), (160, .2)), py=L0_H * 2)
            v = 0.25 + (n - 0.5) * 0.7 + 0.25 * math.sin(y / L0_H * math.pi)
            L0.c[y][x] = ramp([rgb('#06081a'), rgb('#101a34'), rgb('#1e2e4e'), rgb('#2e4a6a')], max(0, min(1, v)))
    r = random.Random(91)
    for i in range(46):
        x = r.randrange(W)
        h = r.uniform(10, 46)
        w = r.uniform(4, 10)
        for y in range(0, int(h)):
            half = w / 2 * max(0.0, 1 - y / h) ** 0.8
            for xx in range(int(x - half), int(x + half) + 1):
                k = (xx - x) / max(1, half)
                L0.put(xx, y, ramp([rgb('#0a0e20'), rgb('#22304a'), rgb('#3a5070')], 0.5 + k * 0.4))
    for i in range(30):
        x, y = r.randrange(W), r.randrange(40, 140)
        crystal(L0, x, y, r.uniform(4, 9), r.choice([(rgb('#40f0e0'), rgb('#1a6a8a')), (rgb('#c080ff'), rgb('#4a2a8a')),
                                                      (rgb('#80c0ff'), rgb('#2a4a9a'))]), glow_amt=0.4)
    ridge(L0, 93, 150, 22, ((5, .6), (13, .4)), [rgb('#0a1020'), rgb('#18243a'), rgb('#2a3a56')], light=1)

    L1 = Layer(W, L1_H)
    r = random.Random(95)
    for i in range(9):
        x = r.randrange(W)
        w = r.randrange(14, 26)
        pillar(L1, 960 + i, x, w, [rgb('#0c1424'), rgb('#1e2c44'), rgb('#3a4e6a'), rgb('#5a7090')])
    ridge(L1, 96, 86, 22, ((4, .5), (11, .3), (27, .2)),
          [rgb('#0e1628'), rgb('#22324c'), rgb('#3e5674')], light=1, top_rim=rgb('#60c0d0'))
    for i in range(26):
        x = r.randrange(W)
        y = r.randrange(60, 112)
        if L1.get(x, y) is not None:
            crystal(L1, x, y, r.uniform(5, 11), r.choice([(rgb('#50ffe8'), rgb('#1a7a8a')), (rgb('#d090ff'), rgb('#5a2a9a'))]),
                    glow_amt=0.5)

    L2 = Layer(W, L2_H)
    r = random.Random(97)
    for i in range(22):
        x = r.randrange(W)
        h = r.uniform(30, 90)
        w = r.uniform(10, 22)
        for y in range(int(L2_H - h), L2_H):
            t = max(0.0, (y - (L2_H - h)) / h)
            half = w / 2 * t ** 0.7
            for xx in range(int(x - half), int(x + half) + 1):
                k = (xx - x) / max(1, half)
                v = 0.3 - k * 0.3 + fbm2(98, xx, y, ((64, 1.0),)) * 0.2
                c = ramp([rgb('#04060e'), rgb('#0e1626'), rgb('#1e2c40')], max(0, min(1, v)))
                if k < -0.8:
                    c = mix(c, rgb('#40d0e0'), 0.55)
                L2.put(xx, y, c)
    band(L2, 72, [rgb('#03050c'), rgb('#0a1020'), rgb('#142036')], 99, bumps=7)
    for i in range(14):
        x = r.randrange(W)
        crystal(L2, x, r.randrange(80, 104), r.uniform(6, 12), (rgb('#60fff0'), rgb('#1a5a7a')), glow_amt=0.6)
    return L0, L1, L2


def crystal(L, x, y, s, cols, glow_amt=0.0):
    """A cluster of three faceted crystals, lit on their left faces."""
    hi, lo = cols
    if glow_amt:
        for yy in range(int(y - s * 2), int(y + s)):
            for xx in range(int(x - s * 2), int(x + s * 2)):
                d = math.hypot(xx - x, yy - (y - s * 0.6)) / (s * 2)
                if d < 1 and L.get(xx, yy) is not None:
                    L.put(xx, yy, mix(L.get(xx, yy), hi, (1 - d) ** 2 * glow_amt))
    for k, (ox, hh, ww) in enumerate(((0, 1.0, 0.42), (-s * 0.45, 0.65, 0.32), (s * 0.45, 0.55, 0.3))):
        h = s * 1.6 * hh
        w = s * ww
        for yy in range(int(y - h), int(y) + 1):
            t = (yy - (y - h)) / h
            half = w * min(1, t * 2.2)
            for xx in range(int(x + ox - half), int(x + ox + half) + 1):
                kx = (xx - (x + ox)) / max(0.5, half)
                c = mix(hi, lo, 0.15 + (kx + 1) * 0.35 + t * 0.2)
                if abs(kx) < 0.18:
                    c = mix(c, rgb('#ffffff'), 0.35 * (1 - t))
                L.put(xx, yy, c)


def pillar(L, seed, cx, w, cols):
    for y in range(L.h):
        wob = (fbm1(seed, y * 3, ((6, .6), (19, .4))) - 0.5) * 6
        mid = 0.6 + 0.4 * abs(math.cos(y / L.h * math.pi))
        half = w / 2 * mid
        for x in range(int(cx + wob - half), int(cx + wob + half) + 1):
            k = (x - (cx + wob)) / max(1, half)
            n = fbm2(seed, x * 2, y, ((32, .5), (128, .5)))
            v = 0.45 - k * 0.35 + (n - 0.5) * 0.35
            L.put(x, y, ramp(cols, max(0, min(1, v))))


def hollow_spire():
    """A moonlit night at the dark citadel: stars, a great moon behind thin
    clouds, far spires with lit windows, battlements, dead trees and railings."""
    L0 = Layer(W, L0_H)
    sky(L0, [rgb('#05061a'), rgb('#0e1236'), rgb('#1e2052'), rgb('#3a3068'), rgb('#5a3a6a')], 0, L0_H)
    stars(L0, 101, 260, 120, [rgb('#ffffff'), rgb('#d0e0ff'), rgb('#ffe8d0')])
    glow(L0, 380, 52, 80, rgb('#c8d0ff'), 0.45)
    moon(L0, 380, 52, 26)
    r = random.Random(102)
    for i in range(7):
        cloud(L0, 150 + i, r.randrange(W), r.randrange(30, 100), r.randrange(60, 130), r.randrange(3, 6),
              rgb('#8a90c0'), rgb('#4a4a80'), rgb('#26285a'))
    hs = [140 - 14 * fbm1(103, x, ((6, .6), (15, .4))) for x in range(W)]
    for x in range(W):
        for y in range(int(hs[x]), L0_H):
            L0.c[y][x] = rgb('#1a1a3e')
    for i in range(14):
        x = r.randrange(W)
        spire_tower(L0, x, hs[x] + 2, r.uniform(26, 70), r.randrange(5, 11), rgb('#14143a'), rgb('#ffd070'), r)

    L1 = Layer(W, L1_H)
    r = random.Random(110)
    wall_top = 76
    for x in range(W):
        merlon = (x // 6) % 2 == 0
        for y in range(wall_top - (4 if merlon else 0), L1_H):
            n = fbm2(111, x, y, ((64, .5), (256, .5)))
            brick = ((y - wall_top) % 6 == 0) or ((x + (6 if ((y - wall_top) // 6) % 2 else 0)) % 12 == 0)
            v = 0.45 + (n - 0.5) * 0.3 - (0.12 if brick else 0)
            L1.c[y][x] = ramp([rgb('#141432'), rgb('#26284e'), rgb('#3e3e6a')], max(0, min(1, v)))
    for i in range(7):
        x = r.randrange(W)
        tw = r.randrange(18, 28)
        tower(L1, x, tw, r.randrange(12, 40), r)
    for i in range(18):
        x = r.randrange(W)
        y = r.randrange(wall_top + 10, L1_H - 20)
        window(L1, x, y, rgb('#ffd070') if r.random() < 0.6 else rgb('#3a3a5a'))

    L2 = Layer(W, L2_H)
    r = random.Random(120)
    for i in range(5):
        dead_tree(L2, 1200 + i, r.randrange(W), 76, r.uniform(60, 90), r)
    band(L2, 72, [rgb('#05040c'), rgb('#0e0c1e'), rgb('#1a1832')], 121, bumps=5)
    for x in range(W):
        if x % 7 == 0:
            for y in range(46, 76):
                L2.put(x, y, rgb('#0a0a16'))
            L2.put(x, 45, rgb('#5a5a80'))
            L2.put(x - 1, 46, rgb('#14142a'))
            L2.put(x + 1, 46, rgb('#14142a'))
        L2.put(x, 52, rgb('#0e0e1e'))
        L2.put(x, 66, rgb('#0e0e1e'))
    return L0, L1, L2


def moon(L, cx, cy, r):
    for y in range(int(cy - r) - 1, int(cy + r) + 2):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            d = math.hypot(x - cx, y - cy)
            if d <= r:
                n = fbm2(130, x * 4, y * 4, ((64, .6), (160, .4)))
                v = 0.85 - (d / r) ** 3 * 0.35 - (0.25 if n > 0.6 else 0) - ((x - cx) / r) * 0.08
                L.put(x, y, ramp([rgb('#8890b8'), rgb('#c8d0f0'), rgb('#f4f6ff')], max(0, min(1, v))))


def spire_tower(L, x, base, h, w, col, lit, r):
    for y in range(int(base - h), int(base)):
        for xx in range(int(x - w / 2), int(x + w / 2) + 1):
            L.put(xx, y, col)
    for k in range(int(w * 1.6)):
        half = w / 2 * (1 - k / (w * 1.6))
        for xx in range(int(x - half), int(x + half) + 1):
            L.put(xx, int(base - h - k), col)
    for _ in range(int(h / 14) + 1):
        if r.random() < 0.7:
            L.put(x, int(base - h + r.uniform(4, h - 4)), lit)


def tower(L, cx, w, rise, r):
    top = 76 - rise
    for y in range(top, L.h):
        for x in range(cx - w // 2, cx + w // 2 + 1):
            k = (x - (cx - w // 2)) / w
            n = fbm2(140, x, y, ((64, .5), (256, .5)))
            brick = ((y - top) % 6 == 0) or ((x + (5 if ((y - top) // 6) % 2 else 0)) % 10 == 0)
            v = 0.62 - k * 0.4 + (n - 0.5) * 0.25 - (0.1 if brick else 0)
            L.put(x, y, ramp([rgb('#121230'), rgb('#2a2a52'), rgb('#4a4a78')], max(0, min(1, v))))
    for k in range(w):
        half = (w / 2 + 2) * (1 - k / w)
        for x in range(int(cx - half), int(cx + half) + 1):
            kk = (x - cx) / max(1, half)
            L.put(x, top - k, ramp([rgb('#2a1430'), rgb('#5a2a4a'), rgb('#8a3a5a')], 0.5 - kk * 0.4))
    window(L, cx, top + 10, rgb('#ffe080'))


def window(L, x, y, col):
    for yy in range(y, y + 7):
        for xx in range(x - 1, x + 2):
            v = col if yy > y or xx == x else None
            if v is not None:
                L.put(xx, yy, mix(col, rgb('#ffffff'), 0.25) if yy < y + 3 else col)
    for yy in range(y - 1, y + 8):
        L.put(x - 2, yy, rgb('#0a0a1a'))
        L.put(x + 2, yy, rgb('#0a0a1a'))


def dead_tree(L, seed, x, base, h, r):
    def branch(x0, y0, ang, length, width, depth):
        if depth == 0 or length < 3:
            return
        x1 = x0 + math.cos(ang) * length
        y1 = y0 + math.sin(ang) * length
        steps = int(length * 2)
        for i in range(steps):
            t = i / steps
            px = x0 + (x1 - x0) * t
            py = y0 + (y1 - y0) * t
            ww = width * (1 - t * 0.5)
            for dx in range(-int(ww / 2), int(ww / 2) + 1):
                c = rgb('#141020') if dx < ww / 4 else rgb('#2a2234')
                L.put(int(px + dx), int(py), c)
        for _ in range(2):
            branch(x1, y1, ang + r.uniform(-0.8, 0.8), length * r.uniform(0.55, 0.75), width * 0.6, depth - 1)
    branch(x, base, -math.pi / 2 + r.uniform(-0.15, 0.15), h * 0.45, 7, 6)


LANDS = [
    ('Dawnmere Fields', dawnmere),
    ('Mirewood', mirewood),
    ('Glimmerdeep', glimmerdeep),
    ('The Hollow Spire', hollow_spire),
]


def layers(land):
    L0, L1, L2 = LANDS[land][1]()
    return [L.image() for L in (L0, L1, L2)]
