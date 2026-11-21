"""Valmora's pictures: every 32x32 tile and creature, drawn here.

Shapes are drawn 4x larger with antialiasing and scaled down; textures are
computed per pixel, tileable. Each tile is then reduced to at most 16
colours of the 4096 (4 bits a channel): the WAD stores 4 bits a pixel and
a palette per tile (make_wad.py).
"""
import math
import random

from PIL import Image, ImageDraw, ImageFilter

T = 32          # tile size
S = 4           # supersampling for shapes
B = T * S


def rgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def clamp(v):
    return max(0, min(255, int(v)))


def shade(c, f):
    return tuple(clamp(x * f) for x in c)


# ------------------------------------------------------------ noise

class Noise:
    """Tileable value noise over a period of `period` pixels."""

    def __init__(self, seed, cells, period=T):
        r = random.Random(seed)
        self.cells = cells
        self.period = period
        self.v = [[r.random() for _ in range(cells)] for _ in range(cells)]

    def at(self, x, y):
        fx = (x % self.period) * self.cells / self.period
        fy = (y % self.period) * self.cells / self.period
        x0, y0 = int(fx), int(fy)
        tx, ty = fx - x0, fy - y0
        tx = tx * tx * (3 - 2 * tx)
        ty = ty * ty * (3 - 2 * ty)
        c = self.cells
        a = self.v[y0 % c][x0 % c]
        b = self.v[y0 % c][(x0 + 1) % c]
        d = self.v[(y0 + 1) % c][x0 % c]
        e = self.v[(y0 + 1) % c][(x0 + 1) % c]
        return (a * (1 - tx) + b * tx) * (1 - ty) + (d * (1 - tx) + e * tx) * ty


def fbm(seed, x, y, octaves=((4, .55), (8, .3), (16, .15))):
    key = (seed, octaves)
    if key not in _fbm_cache:
        _fbm_cache[key] = [Noise(seed * 31 + i, c) for i, (c, _) in enumerate(octaves)]
    ns = _fbm_cache[key]
    return sum(n.at(x, y) * w for n, (_, w) in zip(ns, octaves))


_fbm_cache = {}


def ramp(colours, t):
    """t in 0..1 over a list of colours."""
    t = max(0.0, min(0.999, t))
    f = t * (len(colours) - 1)
    i = int(f)
    return mix(colours[i], colours[i + 1], f - i)


def texture(seed, colours, octaves=((4, .55), (8, .3), (16, .15)), speck=0.0, contrast=1.0):
    img = Image.new('RGB', (T, T))
    px = img.load()
    r = random.Random(seed)
    for y in range(T):
        for x in range(T):
            v = fbm(seed, x, y, octaves)
            v = 0.5 + (v - 0.5) * contrast
            if speck and r.random() < speck:
                v += r.choice((-0.25, 0.25))
            px[x, y] = ramp(colours, v)
    return img


# ------------------------------------------------------------ shapes at 4x

def canvas():
    return Image.new('RGBA', (B, B), (0, 0, 0, 0))


def down(big):
    return big.resize((T, T), Image.LANCZOS)


def over(base, big):
    """Composites a 4x RGBA drawing on a 32x32 RGB(A) tile."""
    small = down(big)
    out = base.convert('RGBA')
    out.alpha_composite(small)
    return out.convert('RGB')


def ellipse(d, cx, cy, rx, ry, fill):
    d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=fill)


def shaded_blob(d, cx, cy, r, dark, mid, light, hi=None, outline=None):
    """A round shape lit from the top left: canopy, bush, boulder."""
    if outline:
        ellipse(d, cx, cy, r + 3, r + 3, outline)
    ellipse(d, cx, cy, r, r, dark)
    ellipse(d, cx - r * .12, cy - r * .14, r * .86, r * .84, mid)
    ellipse(d, cx - r * .3, cy - r * .32, r * .52, r * .48, light)
    if hi:
        ellipse(d, cx - r * .42, cy - r * .44, r * .2, r * .18, hi)


def outline_sprite(img, colour=(20, 16, 24)):
    """Adds a 1-pixel dark outline around the opaque part of a 32x32 sprite."""
    px = img.load()
    w, h = img.size
    solid = [[px[x, y][3] >= 128 for x in range(w)] for y in range(h)]
    out = img.copy()
    op = out.load()
    for y in range(h):
        for x in range(w):
            if solid[y][x]:
                op[x, y] = px[x, y][:3] + (255,)
                continue
            near = False
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h and solid[ny][nx]:
                    near = True
            op[x, y] = colour + (255,) if near else (0, 0, 0, 0)
    return out


def sprite_from(big, outline=True):
    small = down(big)
    px = small.load()
    for y in range(T):
        for x in range(T):
            r, g, b, a = px[x, y]
            if a >= 110:
                # undo the premultiplied look of the edge pixels
                px[x, y] = (r, g, b, 255)
            else:
                px[x, y] = (0, 0, 0, 0)
    return outline_sprite(small) if outline else small


# ------------------------------------------------------------ palettes

GRASS = [rgb('#2b5e27'), rgb('#3a7a31'), rgb('#4a933b'), rgb('#62aa48'), rgb('#86c460')]
GRASS_DARK = [rgb('#24501f'), rgb('#2f6a2a'), rgb('#3d8034'), rgb('#4f963f')]
SAND = [rgb('#a88d55'), rgb('#c4a96d'), rgb('#d6bf86'), rgb('#e6d6a6')]
WET_SAND = [rgb('#8a7448'), rgb('#9e875a')]
SHALLOW = [rgb('#2a63a6'), rgb('#3576bd'), rgb('#4489cf'), rgb('#5ea2df')]
DEEP = [rgb('#14346b'), rgb('#1a427f'), rgb('#225093'), rgb('#2c5fa6')]
ROCK = [rgb('#3e362f'), rgb('#5b5147'), rgb('#7a6e60'), rgb('#9a8d7c'), rgb('#bdb09e')]
SNOW = [rgb('#aebccb'), rgb('#cdd8e3'), rgb('#eef3f8')]
SWAMP = [rgb('#2f3a22'), rgb('#3c4a2a'), rgb('#4b5b32'), rgb('#5e6e3c')]
LAVA = [rgb('#5a140a'), rgb('#a8260e'), rgb('#e0541a'), rgb('#ff9a2e'), rgb('#ffe27a')]
ASH = [rgb('#2e2a28'), rgb('#433d39'), rgb('#57504a'), rgb('#6c645c')]
DIRT = [rgb('#6e5232'), rgb('#86663f'), rgb('#9d7d52'), rgb('#b59666')]
COBBLE = [rgb('#4d4a48'), rgb('#6a6662'), rgb('#86817b'), rgb('#a29c94')]
WOOD = [rgb('#4a2e18'), rgb('#6a4424'), rgb('#875a32'), rgb('#a57444')]
OUTLINE = (20, 16, 24)


# ------------------------------------------------------------ terrain

def grass(seed, flowers=0, tufts=6):
    img = texture(seed, GRASS, speck=0.05)
    px = img.load()
    r = random.Random(seed)
    for _ in range(tufts):           # blades
        x, y = r.randrange(T), r.randrange(T)
        for k in range(r.randint(2, 3)):
            xx = (x + k - 1) % T
            for j in range(r.randint(2, 3)):
                px[xx, (y - j) % T] = GRASS[4] if j == 0 else GRASS[3]
            px[xx, (y + 1) % T] = GRASS[0]
    cols = [rgb('#f2e45a'), rgb('#f6f6f0'), rgb('#e0505a'), rgb('#b48cf0')]
    for _ in range(flowers):
        x, y = r.randrange(1, T - 1), r.randrange(1, T - 1)
        c = r.choice(cols)
        px[x, y] = c
        px[x + 1, y] = shade(c, .8)
        px[x, y + 1] = GRASS[0]
    return img


def brush(seed):
    img = grass(seed, tufts=3)
    big = canvas()
    d = ImageDraw.Draw(big)
    r = random.Random(seed)
    for _ in range(5):
        cx, cy = r.randrange(16, 112), r.randrange(20, 112)
        shaded_blob(d, cx, cy, r.randrange(12, 18), GRASS_DARK[0], GRASS_DARK[2], GRASS[3], outline=GRASS_DARK[0])
    return over(img, big)


def trees(seed, n, pine=False, dark=False):
    img = grass(seed + 7, tufts=2) if not dark else texture(seed, GRASS_DARK, speck=.04)
    big = canvas()
    d = ImageDraw.Draw(big)
    r = random.Random(seed)
    spots = [(40, 40), (92, 36), (64, 84), (28, 100), (100, 96), (64, 30)][:n] if n > 1 else [(64, 52)]
    spots.sort(key=lambda p: p[1])
    for (x, y) in spots:
        x += r.randint(-6, 6)
        y += r.randint(-6, 6)
        if pine:
            ellipse(d, x + 6, y + 30, 22, 8, (0, 0, 0, 70))
            d.rectangle((x - 3, y + 18, x + 3, y + 30), fill=WOOD[0])
            for k, (w, top) in enumerate(((30, -6), (24, -22), (16, -36))):
                pts = [(x, y + top - 22), (x - w, y + top + 14), (x + w, y + top + 14)]
                d.polygon(pts, fill=rgb('#163826'))
                d.polygon([(x, y + top - 20), (x - w + 6, y + top + 11), (x, y + top + 11)],
                          fill=rgb('#2a5e40'))
                d.polygon([(x - 2, y + top - 14), (x - w + 14, y + top + 6), (x - 4, y + top + 6)],
                          fill=rgb('#3f7d50'))
        else:
            ellipse(d, x + 8, y + 26, 26, 10, (0, 0, 0, 80))
            d.rectangle((x - 4, y + 10, x + 4, y + 30), fill=WOOD[1])
            d.rectangle((x - 4, y + 10, x - 1, y + 30), fill=WOOD[2])
            shaded_blob(d, x, y, r.randrange(22, 28) if n > 1 else 40, rgb('#1b4520'), rgb('#2c6b2c'),
                        rgb('#47963a'), rgb('#7cc456'), outline=rgb('#123015'))
    return over(img, big)


def hills(seed):
    img = grass(seed, tufts=3)
    big = canvas()
    d = ImageDraw.Draw(big)
    for (cx, cy, rx, ry) in ((44, 70, 40, 26), (92, 92, 34, 22)):
        d.pieslice((cx - rx, cy - ry, cx + rx, cy + ry * 2), 180, 360, fill=rgb('#2e6a2a'))
        d.pieslice((cx - rx + 6, cy - ry + 4, cx + rx - 10, cy + ry * 2 - 8), 180, 300, fill=rgb('#5aa245'))
        d.pieslice((cx - rx + 14, cy - ry + 8, cx + 4, cy + ry), 190, 270, fill=rgb('#7cbc5a'))
        d.line((cx - rx, cy + 1, cx + rx, cy + 1), fill=rgb('#24561f'), width=4)
    return over(img, big)


def mountain(seed, snow=False):
    img = grass(seed, tufts=1) if not snow else texture(seed, ROCK[1:4], speck=.05)
    big = canvas()
    d = ImageDraw.Draw(big)
    r = random.Random(seed)
    peaks = [(44, 22, 44), (92, 34, 38)] if r.random() < .5 else [(64, 14, 52), (26, 50, 30), (104, 54, 28)]
    for (px_, top, w) in sorted(peaks, key=lambda p: p[1]):
        base = 124
        pts = [(px_, top), (px_ - w, base), (px_ + w, base)]
        d.polygon(pts, fill=ROCK[1])
        d.polygon([(px_, top), (px_ - w, base), (px_ - w * .15, base), (px_ + 4, top + 30)], fill=ROCK[3])
        d.polygon([(px_, top), (px_ - w * .55, top + (base - top) * .55), (px_ - 4, top + 26)], fill=ROCK[4])
        d.polygon([(px_ + 6, top + 18), (px_ + w * .7, base), (px_ + w, base)], fill=ROCK[0])
        for k in range(4):
            y = top + 20 + k * 22
            d.line((px_ - w * .2 + k * 4, y, px_ + w * .25, y + 10), fill=ROCK[0], width=3)
        if snow or top < 30:
            sh = (base - top) * (.42 if snow else .25)
            ratio = sh / (base - top)
            d.polygon([(px_, top), (px_ - w * ratio, top + sh), (px_ - w * ratio * .3, top + sh * .75),
                       (px_ + w * ratio * .2, top + sh), (px_ + w * ratio, top + sh)], fill=SNOW[2])
            d.polygon([(px_, top), (px_ + w * ratio, top + sh), (px_ + w * ratio * .4, top + sh * .8),
                       (px_ + 4, top + 10)], fill=SNOW[0])
        d.line((px_ - w, base, px_, top, px_ + w, base), fill=OUTLINE, width=3)
    return over(img, big)


def sand(seed):
    return texture(seed, SAND, speck=.12, contrast=.8)


def water(seed, frame, deep=False):
    cols = DEEP if deep else SHALLOW
    img = Image.new('RGB', (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            ph = frame * T / 4
            v = fbm(seed, x + ph * .5, y, ((4, .6), (8, .4)))
            w = math.sin((x + y * .5) * .45 + frame * math.pi / 2 + v * 4) * .5 + .5
            px[x, y] = ramp(cols, v * .65 + w * .35)
    r = random.Random(seed * 10 + frame)
    hi = rgb('#a8d4f4') if not deep else rgb('#7aa8e0')
    for k in range(5 if not deep else 3):
        x = (k * 13 + frame * 3) % T
        y = (k * 7 + 5) % T
        for j in range(4):
            px[(x + j) % T, (y + (1 if j in (0, 3) else 0)) % T] = hi
    return img


def swamp(seed):
    img = texture(seed, SWAMP, speck=.08)
    px = img.load()
    for y in range(T):
        for x in range(T):
            v = fbm(seed + 3, x, y, ((4, .7), (8, .3)))
            if v > .56:
                px[x, y] = mix(rgb('#25351e'), rgb('#3a5030'), (v - .56) * 3)
    big = canvas()
    d = ImageDraw.Draw(big)
    r = random.Random(seed)
    for _ in range(6):
        x, y = r.randrange(10, 118), r.randrange(30, 120)
        for k in range(3):
            d.line((x + k * 5, y, x + k * 5 + r.randint(-4, 4), y - r.randint(18, 30)),
                   fill=rgb('#7a9a42'), width=3)
        ellipse(d, x + 6, y - 28, 3, 6, rgb('#6a4a28'))
    return over(img, big)


def lava(seed, frame):
    img = Image.new('RGB', (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            v = fbm(seed, x + frame * 4, y + frame * 2, ((4, .6), (8, .4)))
            c = fbm(seed + 9, x - frame * 2, y, ((4, .7), (8, .3)))
            t = v * .7 + (math.sin(c * 9 + frame * math.pi) * .5 + .5) * .3
            px[x, y] = ramp(LAVA, t)
            if c > .63:
                px[x, y] = mix(rgb('#3a2018'), rgb('#5a2c1c'), (c - .63) * 3)
    return img


def ash(seed):
    img = texture(seed, ASH, speck=.1)
    px = img.load()
    r = random.Random(seed)
    for _ in range(4):
        x, y = r.randrange(T), r.randrange(T)
        for j in range(r.randint(3, 7)):
            px[(x + j) % T, (y + j // 3) % T] = rgb('#1e1a18')
    return img


def shore(mask, seed=900):
    """Grass with a beach on the sides facing water: mask bits N1 E2 S4 W8."""
    img = grass(seed + mask, tufts=4)
    px = img.load()
    n = Noise(seed + 1, 4)
    for y in range(T):
        for x in range(T):
            d = 99
            if mask & 1:
                d = min(d, y)
            if mask & 2:
                d = min(d, T - 1 - x)
            if mask & 4:
                d = min(d, T - 1 - y)
            if mask & 8:
                d = min(d, x)
            edge = 9 + n.at(x * 2, y * 2) * 6
            if d < edge - 2:
                c = ramp(SAND, fbm(seed, x, y))
                if d < 2.5:
                    c = WET_SAND[1] if d >= 1 else WET_SAND[0]
                px[x, y] = c
            elif d < edge:
                px[x, y] = mix(SAND[0], GRASS[1], .5)
    return img


def road(mask, seed=500):
    """A dirt road on grass toward the sides in mask (N1 E2 S4 W8)."""
    img = grass(seed + mask, tufts=3)
    px = img.load()
    n = Noise(seed + 2, 8)
    c = (T - 1) / 2
    for y in range(T):
        for x in range(T):
            best = 99
            dc = math.hypot(x - c, y - c)
            best = dc - 2 if mask == 0 else dc
            if mask & 1 and y <= c:
                best = min(best, abs(x - c))
            if mask & 4 and y >= c:
                best = min(best, abs(x - c))
            if mask & 2 and x >= c:
                best = min(best, abs(y - c))
            if mask & 8 and x <= c:
                best = min(best, abs(y - c))
            w = 6.5 + n.at(x, y) * 2
            if best < w:
                v = fbm(seed, x, y)
                col = ramp(DIRT, v)
                if best > w - 1.5:
                    col = mix(DIRT[0], GRASS[1], .4)
                px[x, y] = col
    r = random.Random(seed + mask)
    for _ in range(8):
        x, y = r.randrange(T), r.randrange(T)
        if px[x, y] in [ramp(DIRT, i / 10) for i in range(10)] or px[x, y][0] > 120:
            px[x, y] = DIRT[3]
    return img


def bridge(vertical):
    img = water(77, 0)
    big = canvas()
    d = ImageDraw.Draw(big)
    for k in range(8):
        y = 4 + k * 15
        c = WOOD[2] if k % 2 else WOOD[3]
        d.rectangle((14, y, 114, y + 12), fill=c)
        d.line((14, y + 12, 114, y + 12), fill=WOOD[0], width=3)
        d.ellipse((22, y + 4, 28, y + 9), fill=WOOD[0])
        d.ellipse((100, y + 4, 106, y + 9), fill=WOOD[0])
    d.rectangle((6, 0, 16, 128), fill=WOOD[1])
    d.rectangle((112, 0, 122, 128), fill=WOOD[1])
    d.line((6, 0, 6, 128), fill=OUTLINE, width=3)
    d.line((122, 0, 122, 128), fill=OUTLINE, width=3)
    if not vertical:
        big = big.rotate(90)
    return over(img, big)


def house(d, x, y, w, h, roof, wall=rgb('#d8c8a0')):
    d.rectangle((x, y + h * .45, x + w, y + h), fill=wall)
    d.rectangle((x, y + h * .45, x + w * .15, y + h), fill=shade(wall, .8))
    d.polygon([(x - 6, y + h * .5), (x + w / 2, y), (x + w + 6, y + h * .5)], fill=roof)
    d.polygon([(x + w / 2, y), (x + w + 6, y + h * .5), (x + w / 2, y + h * .5)], fill=shade(roof, .75))
    d.rectangle((x + w * .4, y + h * .7, x + w * .6, y + h), fill=WOOD[0])
    d.rectangle((x + w * .12, y + h * .6, x + w * .3, y + h * .75), fill=rgb('#f0d060'))
    d.line((x - 6, y + h * .5, x + w / 2, y, x + w + 6, y + h * .5), fill=OUTLINE, width=3)


def village():
    img = grass(31, flowers=3)
    big = canvas()
    d = ImageDraw.Draw(big)
    ellipse(d, 64, 100, 54, 20, DIRT[1])
    house(d, 10, 22, 46, 52, rgb('#b0402c'))
    house(d, 70, 14, 44, 50, rgb('#8a5a2c'))
    house(d, 40, 62, 48, 54, rgb('#a8342a'))
    return over(img, big)


def tower(d, x, y, w, h, stone, roof=None):
    d.rectangle((x, y, x + w, y + h), fill=stone[2])
    d.rectangle((x, y, x + w * .3, y + h), fill=stone[3])
    d.rectangle((x + w * .75, y, x + w, y + h), fill=stone[1])
    for k in range(0, int(w), 10):
        d.rectangle((x + k, y - 8, x + k + 5, y), fill=stone[2])
    if roof:
        d.polygon([(x - 4, y), (x + w / 2, y - h * .6), (x + w + 4, y)], fill=roof)
        d.polygon([(x + w / 2, y - h * .6), (x + w + 4, y), (x + w / 2, y)], fill=shade(roof, .7))
    d.rectangle((x + w * .4, y + h * .3, x + w * .6, y + h * .45), fill=OUTLINE)


def castle():
    img = grass(41, tufts=2)
    big = canvas()
    d = ImageDraw.Draw(big)
    stone = [rgb('#4a4c58'), rgb('#6a6c78'), rgb('#8e909c'), rgb('#b4b6c0')]
    d.rectangle((14, 58, 114, 120), fill=stone[1])
    d.rectangle((14, 58, 114, 66), fill=stone[3])
    for k in range(14, 114, 12):
        d.rectangle((k, 50, k + 6, 58), fill=stone[2])
    tower(d, 6, 40, 26, 80, stone, rgb('#2c4c9c'))
    tower(d, 96, 40, 26, 80, stone, rgb('#2c4c9c'))
    tower(d, 48, 24, 32, 96, stone, rgb('#3a5cb4'))
    d.line((64, 4, 64, 18), fill=OUTLINE, width=3)
    d.polygon([(64, 4), (84, 9), (64, 14)], fill=rgb('#e8c040'))
    d.rectangle((54, 92, 74, 120), fill=OUTLINE)
    d.pieslice((54, 84, 74, 100), 180, 360, fill=OUTLINE)
    d.rectangle((14, 116, 114, 122), fill=stone[0])
    return over(img, big)


def port():
    img = Image.new('RGB', (T, T))
    top = grass(51, tufts=2)
    wat = water(52, 0)
    for y in range(T):
        for x in range(T):
            img.putpixel((x, y), top.getpixel((x, y)) if y < 20 else wat.getpixel((x, y)))
    big = canvas()
    d = ImageDraw.Draw(big)
    d.rectangle((0, 74, 128, 82), fill=SAND[2])
    d.rectangle((70, 70, 84, 128), fill=WOOD[2])
    for k in range(70, 128, 10):
        d.line((70, k, 84, k), fill=WOOD[0], width=2)
    house(d, 8, 10, 44, 50, rgb('#3a6a9a'), rgb('#e0d4b0'))
    house(d, 80, 2, 40, 46, rgb('#9a4a2a'))
    d.polygon([(22, 100), (58, 100), (52, 114), (28, 114)], fill=WOOD[1])
    d.line((40, 100, 40, 70), fill=WOOD[0], width=3)
    d.polygon([(42, 72), (58, 94), (42, 94)], fill=rgb('#f0ece0'))
    return over(img, big)


def cave_entrance():
    img = grass(61, tufts=2)
    big = canvas()
    d = ImageDraw.Draw(big)
    d.polygon([(4, 124), (18, 50), (46, 20), (84, 16), (112, 46), (124, 124)], fill=ROCK[2])
    d.polygon([(4, 124), (18, 50), (46, 20), (50, 60), (36, 124)], fill=ROCK[3])
    d.polygon([(84, 16), (112, 46), (124, 124), (96, 124)], fill=ROCK[1])
    d.pieslice((36, 60, 92, 160), 180, 360, fill=rgb('#0c0a0c'))
    d.rectangle((36, 110, 92, 124), fill=rgb('#0c0a0c'))
    d.pieslice((42, 70, 86, 160), 180, 360, fill=rgb('#000000'))
    for (x, y) in ((24, 90), (100, 80), (60, 34)):
        ellipse(d, x, y, 7, 4, rgb('#4f8a3a'))
    d.line((4, 124, 18, 50, 46, 20, 84, 16, 112, 46, 124, 124), fill=OUTLINE, width=3)
    return over(img, big)


def crypt_entrance():
    img = swamp(71)
    big = canvas()
    d = ImageDraw.Draw(big)
    st = [rgb('#3e4440'), rgb('#5c645e'), rgb('#7c8680'), rgb('#a0aaa2')]
    d.rectangle((16, 50, 112, 122), fill=st[1])
    d.polygon([(8, 52), (64, 14), (120, 52)], fill=st[2])
    d.polygon([(64, 14), (120, 52), (64, 52)], fill=st[1])
    for x in (24, 98):
        d.rectangle((x - 6, 52, x + 6, 122), fill=st[3])
    d.rectangle((46, 70, 82, 122), fill=rgb('#0a0c0a'))
    d.pieslice((46, 56, 82, 88), 180, 360, fill=rgb('#0a0c0a'))
    d.line((64, 24, 64, 46), fill=st[3], width=4)
    d.line((56, 32, 72, 32), fill=st[3], width=4)
    for (x, y) in ((20, 64), (104, 100), (60, 54)):
        ellipse(d, x, y, 8, 4, rgb('#4a6a2c'))
    d.line((8, 52, 64, 14, 120, 52), fill=OUTLINE, width=3)
    return over(img, big)


def mine_entrance():
    img = hills(81)
    big = canvas()
    d = ImageDraw.Draw(big)
    d.polygon([(0, 124), (20, 40), (64, 18), (108, 40), (128, 124)], fill=ROCK[1])
    d.polygon([(0, 124), (20, 40), (64, 18), (60, 50), (30, 124)], fill=ROCK[2])
    d.rectangle((38, 64, 90, 124), fill=rgb('#0a0806'))
    d.rectangle((32, 58, 42, 124), fill=WOOD[2])
    d.rectangle((86, 58, 96, 124), fill=WOOD[1])
    d.rectangle((28, 54, 100, 66), fill=WOOD[3])
    d.line((28, 66, 100, 66), fill=WOOD[0], width=3)
    for k in range(48, 84, 12):
        d.rectangle((k, 112, k + 6, 124), fill=rgb('#7a7068'))
    d.line((0, 124, 20, 40, 64, 18, 108, 40, 128, 124), fill=OUTLINE, width=3)
    return over(img, big)


def spire():
    img = ash(91)
    big = canvas()
    d = ImageDraw.Draw(big)
    ob = [rgb('#140e1c'), rgb('#261a34'), rgb('#3a2850'), rgb('#56407a')]
    d.polygon([(30, 126), (44, 30), (64, 0), (84, 30), (98, 126)], fill=ob[1])
    d.polygon([(30, 126), (44, 30), (64, 0), (60, 126)], fill=ob[2])
    d.polygon([(52, 40), (64, 6), (60, 60)], fill=ob[3])
    for y in (50, 78, 104):
        d.rectangle((56, y, 70, y + 10), fill=rgb('#c060ff'))
        d.rectangle((58, y + 2, 68, y + 8), fill=rgb('#f0c0ff'))
    ellipse(d, 64, 4, 8, 8, rgb('#d080ff'))
    d.line((30, 126, 44, 30, 64, 0, 84, 30, 98, 126), fill=OUTLINE, width=3)
    return over(img, big)


def causeway():
    img = ash(95)
    px = img.load()
    for y in range(T):
        for x in range(5, 27):
            v = fbm(96, x, y)
            px[x, y] = ramp([rgb('#3a3640'), rgb('#55505c'), rgb('#6e6878')], v)
            if (y % 8 == 0) or ((x + (y // 8) * 4) % 11 == 0):
                px[x, y] = rgb('#26222a')
        px[4, y] = OUTLINE
        px[27, y] = OUTLINE
    return img


def gate(open_):
    img = causeway()
    big = canvas()
    d = ImageDraw.Draw(big)
    st = [rgb('#2a2630'), rgb('#4a4454'), rgb('#6a6478')]
    for x in (8, 98):
        d.rectangle((x, 8, x + 22, 124), fill=st[1])
        d.rectangle((x, 8, x + 8, 124), fill=st[2])
        d.rectangle((x - 2, 2, x + 24, 12), fill=st[2])
    if not open_:
        for x in range(36, 96, 12):
            d.rectangle((x, 16, x + 4, 120), fill=rgb('#8a8070'))
        for y in range(24, 120, 20):
            d.rectangle((32, y, 98, y + 4), fill=rgb('#6a6050'))
        ellipse(d, 64, 64, 12, 12, rgb('#c060ff'))
        ellipse(d, 64, 64, 6, 6, rgb('#f0c0ff'))
    return over(img, big)


# ------------------------------------------------------------ town and dungeon

def cobble(seed):
    img = Image.new('RGB', (T, T))
    px = img.load()
    r = random.Random(seed)
    stones = []
    for gy in range(4):
        for gx in range(4):
            stones.append((gx * 8 + 4 + r.uniform(-1.5, 1.5) + (4 if gy % 2 else 0), gy * 8 + 4 + r.uniform(-1, 1),
                           r.random()))
    for y in range(T):
        for x in range(T):
            ds = sorted(((((x - sx + 16) % 32 - 16) ** 2 + ((y - sy + 16) % 32 - 16) ** 2) ** .5, v)
                        for sx, sy, v in stones)
            (d0, v0), (d1, _) = ds[0], ds[1]
            if d1 - d0 < 1.2:
                px[x, y] = COBBLE[0]
            else:
                t = .35 + v0 * .4 - d0 * .03
                px[x, y] = ramp(COBBLE, t + (fbm(seed, x, y) - .5) * .3)
    return img


def planks(seed, cols=WOOD, vertical=False):
    img = Image.new('RGB', (T, T))
    px = img.load()
    r = random.Random(seed)
    shifts = [r.randrange(32) for _ in range(4)]
    for y in range(T):
        for x in range(T):
            a, b = (x, y) if not vertical else (y, x)
            row = b // 8
            v = fbm(seed + row, a * .5, b * 3, ((4, .6), (8, .4)))
            c = ramp(cols[1:], v * .9)
            if b % 8 == 7:
                c = cols[0]
            if (a + shifts[row]) % 32 == 0:
                c = cols[0]
            px[x, y] = c
    return img


def flagstone(seed, cols):
    img = Image.new('RGB', (T, T))
    px = img.load()
    r = random.Random(seed)
    tint = [r.uniform(-.12, .12) for _ in range(4)]
    for y in range(T):
        for x in range(T):
            q = (x // 16) + (y // 16) * 2
            v = fbm(seed, x, y) * .6 + .3 + tint[q]
            c = ramp(cols[1:], v)
            if x % 16 == 0 or y % 16 == 0:
                c = cols[0]
            elif x % 16 == 1 or y % 16 == 1:
                c = shade(c, 1.12)
            px[x, y] = c
    return img


def carpet():
    img = flagstone(300, COBBLE)
    px = img.load()
    red = [rgb('#5a1018'), rgb('#8a1c24'), rgb('#a82a30')]
    for y in range(T):
        for x in range(T):
            v = fbm(301, x, y)
            c = ramp(red, v)
            if x in (2, 29) or ((x + y) % 8 == 0 and 6 < x < 26):
                c = rgb('#d8a840')
            px[x, y] = c
    return img


def bricks(seed, cols, mortar, bw=16, bh=8, top=True):
    img = Image.new('RGB', (T, T))
    px = img.load()
    r = random.Random(seed)
    tints = {}
    for y in range(T):
        for x in range(T):
            row = y // bh
            off = (bw // 2) if row % 2 else 0
            bx = (x + off) // bw
            key = (row, bx % (T // bw))
            if key not in tints:
                tints[key] = r.uniform(-.15, .15)
            lx = (x + off) % bw
            ly = y % bh
            if lx == 0 or ly == 0:
                px[x, y] = mortar
                continue
            v = fbm(seed, x, y) * .5 + .35 + tints[key]
            c = ramp(cols, v)
            if ly == 1 or lx == 1:
                c = shade(c, 1.15)
            if ly == bh - 1:
                c = shade(c, .75)
            px[x, y] = c
    return img


def wall_with(base, kind):
    big = canvas()
    d = ImageDraw.Draw(big)
    if kind == 'window':
        d.rectangle((36, 30, 92, 96), fill=WOOD[0])
        d.rectangle((42, 36, 86, 90), fill=rgb('#f0d878'))
        d.rectangle((42, 36, 86, 50), fill=rgb('#fff0b0'))
        d.line((64, 36, 64, 90), fill=WOOD[0], width=4)
        d.line((42, 62, 86, 62), fill=WOOD[0], width=4)
    elif kind == 'door':
        d.rectangle((26, 18, 102, 128), fill=WOOD[0])
        d.rectangle((32, 24, 96, 128), fill=WOOD[2])
        for x in (48, 64, 80):
            d.line((x, 24, x, 128), fill=WOOD[1], width=3)
        d.rectangle((32, 50, 96, 56), fill=rgb('#3a3a40'))
        d.rectangle((32, 96, 96, 102), fill=rgb('#3a3a40'))
        ellipse(d, 86, 78, 5, 5, rgb('#e0c050'))
    elif kind == 'torch':
        pass
    return over(base, big)


def torch_wall(base, frame):
    big = canvas()
    d = ImageDraw.Draw(big)
    d.rectangle((58, 58, 70, 100), fill=WOOD[1])
    d.rectangle((52, 54, 76, 62), fill=rgb('#5a5a62'))
    fl = [(64, 14 + frame * 6), (78, 46), (70, 58), (58, 58), (50, 46)]
    ellipse(d, 64, 44, 30, 30, (255, 180, 60, 60))
    d.polygon(fl, fill=rgb('#ff8a20'))
    d.polygon([(64, 28 + frame * 4), (72, 48), (64, 56), (56, 48)], fill=rgb('#ffe060'))
    return over(base, big)


def furniture(kind, floor):
    return over(floor, furniture_big(kind))


def overlay(kind):
    """A transparent picture drawn over the map's own floor at run time."""
    return sprite_from(furniture_big(kind), outline=False)


def furniture_big(kind):
    big = canvas()
    d = ImageDraw.Draw(big)
    if kind == 'counter':
        d.rectangle((0, 30, 128, 104), fill=WOOD[1])
        d.rectangle((0, 30, 128, 50), fill=WOOD[3])
        d.line((0, 50, 128, 50), fill=WOOD[0], width=3)
        d.line((0, 104, 128, 104), fill=OUTLINE, width=4)
        for x in range(10, 128, 30):
            d.rectangle((x, 60, x + 18, 96), fill=WOOD[2])
    elif kind == 'table':
        ellipse(d, 64, 96, 50, 12, (0, 0, 0, 70))
        d.rectangle((18, 40, 110, 92), fill=WOOD[2])
        d.rectangle((18, 40, 110, 52), fill=WOOD[3])
        d.rectangle((24, 92, 34, 110), fill=WOOD[0])
        d.rectangle((94, 92, 104, 110), fill=WOOD[0])
        ellipse(d, 50, 56, 10, 6, rgb('#e8e0d0'))
        d.rectangle((76, 44, 84, 60), fill=rgb('#c03030'))
        d.rectangle((18, 40, 110, 92), outline=OUTLINE, width=3)
    elif kind == 'bed':
        d.rectangle((24, 6, 104, 122), fill=WOOD[1])
        d.rectangle((30, 12, 98, 116), fill=rgb('#e8e4dc'))
        d.rectangle((30, 40, 98, 116), fill=rgb('#3a5ab0'))
        d.rectangle((30, 40, 98, 50), fill=rgb('#5a7ad0'))
        d.rectangle((36, 16, 92, 34), fill=rgb('#ffffff'))
        d.rectangle((24, 6, 104, 122), outline=OUTLINE, width=3)
    elif kind == 'barrel':
        ellipse(d, 64, 110, 34, 10, (0, 0, 0, 80))
        d.rounded_rectangle((32, 24, 96, 112), 20, fill=WOOD[2])
        d.rounded_rectangle((32, 24, 56, 112), 14, fill=WOOD[3])
        for y in (40, 92):
            d.rectangle((30, y, 98, y + 6), fill=rgb('#4a4a52'))
        ellipse(d, 64, 30, 30, 10, WOOD[1])
        d.rounded_rectangle((32, 24, 96, 112), 20, outline=OUTLINE, width=3)
    elif kind == 'crate':
        d.rectangle((24, 28, 104, 108), fill=WOOD[2])
        d.rectangle((24, 28, 104, 40), fill=WOOD[3])
        d.line((24, 28, 104, 108), fill=WOOD[0], width=6)
        d.line((104, 28, 24, 108), fill=WOOD[0], width=6)
        d.rectangle((24, 28, 104, 108), outline=OUTLINE, width=4)
    elif kind == 'shelf':
        d.rectangle((8, 4, 120, 124), fill=WOOD[1])
        for i, y in enumerate((10, 48, 86)):
            d.rectangle((14, y, 114, y + 32), fill=WOOD[0])
            x = 16
            r = random.Random(i)
            while x < 108:
                w = r.randint(6, 12)
                c = r.choice([rgb('#a83030'), rgb('#3050a0'), rgb('#308040'), rgb('#c0a040'), rgb('#704090')])
                d.rectangle((x, y + r.randint(2, 8), x + w, y + 32), fill=c)
                x += w + 2
        d.rectangle((8, 4, 120, 124), outline=OUTLINE, width=3)
    elif kind == 'anvil':
        ellipse(d, 64, 108, 40, 10, (0, 0, 0, 80))
        d.rectangle((48, 76, 80, 108), fill=rgb('#3a3a44'))
        d.polygon([(14, 46), (114, 46), (100, 76), (28, 76)], fill=rgb('#5a5a68'))
        d.rectangle((24, 40, 106, 50), fill=rgb('#8a8a98'))
        d.polygon([(14, 46), (114, 46), (100, 76), (28, 76)], outline=OUTLINE, width=3)
    elif kind == 'throne':
        d.rectangle((24, 4, 104, 120), fill=rgb('#c8a030'))
        d.rectangle((34, 14, 94, 120), fill=rgb('#8a1c24'))
        d.rectangle((20, 66, 108, 122), fill=rgb('#c8a030'))
        d.rectangle((32, 70, 96, 112), fill=rgb('#a82a30'))
        for x in (28, 64, 100):
            ellipse(d, x, 8, 7, 7, rgb('#f0e070'))
        d.rectangle((24, 4, 104, 120), outline=OUTLINE, width=3)
    elif kind == 'well':
        ellipse(d, 64, 72, 52, 46, rgb('#6a6662'))
        ellipse(d, 64, 72, 40, 34, rgb('#1a2a40'))
        ellipse(d, 58, 66, 16, 10, rgb('#3a5a8a'))
        d.rectangle((14, 14, 22, 80), fill=WOOD[1])
        d.rectangle((106, 14, 114, 80), fill=WOOD[1])
        d.rectangle((8, 8, 120, 20), fill=rgb('#8a3a2a'))
        d.ellipse((12, 26, 116, 118), outline=OUTLINE, width=3)
    elif kind == 'pillar':
        ellipse(d, 64, 110, 34, 12, (0, 0, 0, 90))
        d.rectangle((40, 10, 88, 112), fill=rgb('#8a8c96'))
        d.rectangle((40, 10, 56, 112), fill=rgb('#aeb0ba'))
        d.rectangle((78, 10, 88, 112), fill=rgb('#5e606a'))
        d.rectangle((32, 4, 96, 16), fill=rgb('#b4b6c0'))
        d.rectangle((32, 104, 96, 116), fill=rgb('#6a6c76'))
        d.rectangle((32, 4, 96, 116), outline=OUTLINE, width=3)
    elif kind == 'altar':
        d.rectangle((18, 50, 110, 112), fill=rgb('#6a6c78'))
        d.rectangle((12, 40, 116, 56), fill=rgb('#9a9cac'))
        ellipse(d, 64, 30, 22, 22, (255, 230, 120, 90))
        d.polygon([(64, 10), (76, 30), (64, 48), (52, 30)], fill=rgb('#ffe070'))
        d.polygon([(64, 10), (76, 30), (64, 30)], fill=rgb('#fff8c0'))
        d.rectangle((18, 50, 110, 112), outline=OUTLINE, width=3)
    elif kind == 'altar_empty':
        d.rectangle((18, 50, 110, 112), fill=rgb('#6a6c78'))
        d.rectangle((12, 40, 116, 56), fill=rgb('#9a9cac'))
        d.rectangle((18, 50, 110, 112), outline=OUTLINE, width=3)
    elif kind == 'bones':
        r = random.Random(4)
        for _ in range(4):
            x, y = r.randrange(20, 100), r.randrange(30, 110)
            a = r.uniform(0, math.pi)
            dx, dy = math.cos(a) * 18, math.sin(a) * 18
            d.line((x - dx, y - dy, x + dx, y + dy), fill=rgb('#d8d0bc'), width=6)
            ellipse(d, x - dx, y - dy, 5, 5, rgb('#e8e0cc'))
            ellipse(d, x + dx, y + dy, 5, 5, rgb('#e8e0cc'))
        ellipse(d, 80, 60, 14, 12, rgb('#e8e0cc'))
        ellipse(d, 75, 58, 3, 3, OUTLINE)
        ellipse(d, 85, 58, 3, 3, OUTLINE)
    elif kind == 'web':
        for a in range(0, 360, 45):
            d.line((64, 64, 64 + math.cos(math.radians(a)) * 64, 64 + math.sin(math.radians(a)) * 64),
                   fill=(230, 230, 240, 200), width=2)
        for rr in (14, 28, 44, 58):
            pts = [(64 + math.cos(math.radians(a)) * rr, 64 + math.sin(math.radians(a)) * rr)
                   for a in range(0, 405, 45)]
            d.line(pts, fill=(230, 230, 240, 200), width=2)
    elif kind == 'rubble':
        r = random.Random(9)
        for _ in range(9):
            x, y = r.randrange(16, 112), r.randrange(24, 112)
            shaded_blob(d, x, y, r.randrange(8, 16), ROCK[1], ROCK[2], ROCK[3], outline=OUTLINE)
    elif kind == 'flowerbed':
        r = random.Random(3)
        d.rectangle((6, 6, 122, 122), fill=rgb('#5a3a22'))
        for _ in range(26):
            x, y = r.randrange(14, 114), r.randrange(14, 114)
            ellipse(d, x, y, 8, 8, GRASS[2])
            ellipse(d, x, y - 2, 5, 5, r.choice([rgb('#f04050'), rgb('#f8e050'), rgb('#f0f0f0'), rgb('#c070f0')]))
        d.rectangle((6, 6, 122, 122), outline=rgb('#8a7a6a'), width=6)
    elif kind == 'hedge':
        d.rounded_rectangle((2, 14, 126, 114), 26, fill=GRASS_DARK[1])
        for x in range(14, 128, 26):
            shaded_blob(d, x, 54, 22, GRASS_DARK[0], GRASS_DARK[2], GRASS[3], outline=GRASS_DARK[0])
    elif kind == 'fence':
        for x in (10, 60, 110):
            d.rectangle((x - 5, 30, x + 5, 104), fill=WOOD[2])
        d.rectangle((0, 44, 128, 54), fill=WOOD[3])
        d.rectangle((0, 78, 128, 88), fill=WOOD[3])
    elif kind == 'stairs_down':
        for k in range(6):
            c = shade(rgb('#9a9488'), 1 - k * .14)
            d.rectangle((16 + k * 6, 14 + k * 17, 112 - k * 6, 30 + k * 17), fill=c)
        d.rectangle((16, 14, 112, 116), outline=OUTLINE, width=4)
    elif kind == 'stairs_up':
        for k in range(6):
            c = shade(rgb('#9a9488'), .45 + k * .11)
            d.rectangle((16 + (5 - k) * 6, 14 + k * 17, 112 - (5 - k) * 6, 30 + k * 17), fill=c)
        d.line((64, 6, 64, 24), fill=rgb('#f0e070'), width=4)
        d.polygon([(52, 18), (64, 4), (76, 18)], fill=rgb('#f0e070'))
    elif kind in ('chest', 'chest_open'):
        ellipse(d, 64, 108, 44, 10, (0, 0, 0, 80))
        d.rectangle((20, 54, 108, 106), fill=WOOD[2])
        d.rectangle((20, 54, 108, 62), fill=WOOD[3])
        if kind == 'chest':
            d.pieslice((20, 26, 108, 82), 180, 360, fill=WOOD[3])
            d.rectangle((20, 50, 108, 56), fill=rgb('#c8a030'))
            d.rectangle((56, 50, 72, 72), fill=rgb('#e8c040'))
            d.rectangle((60, 58, 68, 66), fill=OUTLINE)
        else:
            d.rectangle((20, 20, 108, 40), fill=WOOD[1])
            d.rectangle((26, 54, 102, 72), fill=rgb('#1a1008'))
        for x in (20, 100):
            d.rectangle((x, 54, x + 8, 106), fill=rgb('#c8a030'))
        d.rectangle((20, 54, 108, 106), outline=OUTLINE, width=3)
    elif kind == 'sign_weapon' or kind == 'sign_armor' or kind == 'sign_potion' or kind == 'sign_inn' \
            or kind == 'sign_heal':
        d.rectangle((60, 70, 68, 124), fill=WOOD[0])
        d.rectangle((16, 16, 112, 76), fill=WOOD[3])
        d.rectangle((16, 16, 112, 76), outline=OUTLINE, width=4)
        if kind == 'sign_weapon':
            d.line((36, 66, 92, 24), fill=rgb('#d0d4e0'), width=8)
            d.line((40, 44, 56, 62), fill=rgb('#8a6a2a'), width=6)
        elif kind == 'sign_armor':
            d.polygon([(44, 24), (84, 24), (84, 48), (64, 70), (44, 48)], fill=rgb('#5a70b0'))
            d.line((64, 26, 64, 64), fill=rgb('#e0c050'), width=4)
        elif kind == 'sign_potion':
            ellipse(d, 64, 54, 16, 16, rgb('#d03040'))
            d.rectangle((58, 24, 70, 40), fill=rgb('#a0c0d0'))
        elif kind == 'sign_inn':
            d.rectangle((38, 40, 90, 60), fill=rgb('#3a5ab0'))
            d.rectangle((38, 34, 52, 46), fill=rgb('#ffffff'))
        else:
            d.rectangle((58, 24, 70, 70), fill=rgb('#e03030'))
            d.rectangle((42, 40, 86, 52), fill=rgb('#e03030'))
    return big


def fountain(frame):
    base = cobble(400)
    big = canvas()
    d = ImageDraw.Draw(big)
    ellipse(d, 64, 68, 60, 54, rgb('#8a8680'))
    ellipse(d, 64, 68, 50, 44, SHALLOW[1])
    ellipse(d, 56 + frame * 4, 60, 20, 12, SHALLOW[3])
    ellipse(d, 64, 64, 12, 12, rgb('#a6a29a'))
    for k in range(6):
        a = math.radians(k * 60 + frame * 30)
        x, y = 64 + math.cos(a) * 26, 58 + math.sin(a) * 20
        ellipse(d, x, y, 4, 4, rgb('#d8f0ff'))
    d.line((64, 58, 64, 22 + frame * 4), fill=rgb('#d8f0ff'), width=6)
    d.ellipse((4, 14, 124, 122), outline=OUTLINE, width=3)
    return over(base, big)


def dungeon_floor(seed, cols, cracks=True):
    img = flagstone(seed, cols)
    px = img.load()
    r = random.Random(seed)
    if cracks:
        x, y = r.randrange(T), r.randrange(T)
        for _ in range(10):
            px[x % T, y % T] = cols[0]
            x += r.choice((-1, 0, 1))
            y += 1
    return img


def cave_floor(seed, cols):
    img = texture(seed, cols, speck=.1)
    return img


def rock_wall(seed, cols, moss=None, beams=False):
    img = Image.new('RGB', (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            v = fbm(seed, x, y, ((4, .5), (8, .35), (16, .15)))
            c = ramp(cols, v * 1.1 - .05)
            e = fbm(seed + 5, x, y, ((8, 1.0),))
            if abs(e - .5) < .03:
                c = cols[0]
            if moss and fbm(seed + 9, x, y) > .58:
                c = mix(c, moss, .7)
            px[x, y] = c
    for x in range(T):
        px[x, T - 1] = shade(px[x, T - 1], .6)
        px[x, T - 2] = shade(px[x, T - 2], .8)
    if beams:
        big = canvas()
        d = ImageDraw.Draw(big)
        d.rectangle((0, 8, 128, 22), fill=WOOD[2])
        d.rectangle((10, 0, 24, 128), fill=WOOD[1])
        d.rectangle((104, 0, 118, 128), fill=WOOD[1])
        d.line((0, 22, 128, 22), fill=WOOD[0], width=3)
        img = over(img, big)
    return img


# ------------------------------------------------------------ creatures

SKIN = [rgb('#f0c8a0'), rgb('#d8a878'), rgb('#b07a50')]


def humanoid(frame, tunic, legs=rgb('#4a3a2a'), skin=SKIN, hair=rgb('#5a3a1e'), hat=None,
             cape=None, held=None, shield=None, robe=False, beard=None, boots=rgb('#3a2414'),
             eyes=OUTLINE, belt=rgb('#6a4a1a'), big_=False, glow=None, hood=None):
    big = canvas()
    d = ImageDraw.Draw(big)
    bob = 2 if frame else 0
    sw = 6 if frame else -6
    cx = 64
    k = 1.15 if big_ else 1.0

    def X(v):
        return cx + (v - cx) * k

    if glow:
        ellipse(d, cx, 70, 56, 60, glow)
    if cape:
        d.polygon([(X(40), 48 + bob), (X(88), 48 + bob), (X(96), 112), (X(32), 112)], fill=cape)
        d.polygon([(X(40), 48 + bob), (X(52), 48 + bob), (X(42), 112), (X(32), 112)], fill=shade(cape, 1.25))
    # legs
    if not robe:
        d.rectangle((X(46), 88, X(60), 116 - (4 if frame else 0)), fill=legs)
        d.rectangle((X(68), 88, X(82), 116 - (0 if frame else 4)), fill=shade(legs, .8))
        d.rectangle((X(44), 108 - (4 if frame else 0), X(62), 120 - (4 if frame else 0)), fill=boots)
        d.rectangle((X(66), 108 - (0 if frame else 4), X(84), 120 - (0 if frame else 4)), fill=boots)
    # body
    if robe:
        d.polygon([(X(42), 52 + bob), (X(86), 52 + bob), (X(96), 118), (X(32), 118)], fill=tunic)
        d.polygon([(X(42), 52 + bob), (X(56), 52 + bob), (X(46), 118), (X(32), 118)], fill=shade(tunic, 1.25))
        d.polygon([(X(76), 52 + bob), (X(86), 52 + bob), (X(96), 118), (X(82), 118)], fill=shade(tunic, .72))
        d.rectangle((X(38), 114, X(90), 120), fill=shade(tunic, .6))
    else:
        d.rounded_rectangle((X(40), 50 + bob, X(88), 94 + bob), 10, fill=tunic)
        d.rounded_rectangle((X(40), 50 + bob, X(54), 94 + bob), 8, fill=shade(tunic, 1.28))
        d.rectangle((X(76), 54 + bob, X(88), 92 + bob), fill=shade(tunic, .72))
        d.rectangle((X(40), 80 + bob, X(88), 86 + bob), fill=belt)
    # arms
    d.rounded_rectangle((X(30), 54 + bob + sw // 3, X(42), 88 + bob + sw // 2), 6, fill=shade(tunic, .9))
    d.rounded_rectangle((X(86), 54 + bob - sw // 3, X(98), 88 + bob - sw // 2), 6, fill=shade(tunic, .8))
    ellipse(d, X(36), 90 + bob + sw // 2, 6, 6, skin[1])
    ellipse(d, X(92), 90 + bob - sw // 2, 6, 6, skin[1])
    # held things
    hy = 90 + bob - sw // 2
    if held == 'sword':
        d.line((X(92), hy, X(110), hy - 46), fill=rgb('#e0e4f0'), width=6)
        d.line((X(94), hy - 2, X(110), hy - 44), fill=rgb('#a0a8bc'), width=2)
        d.line((X(84), hy - 6, X(102), hy + 4), fill=rgb('#c8a030'), width=5)
    elif held == 'spear':
        d.line((X(94), 120, X(100), 10), fill=WOOD[2], width=5)
        d.polygon([(X(100), 0), (X(106), 18), (X(94), 18)], fill=rgb('#d0d4e0'))
    elif held == 'staff':
        d.line((X(94), 122, X(98), 14), fill=WOOD[1], width=6)
        ellipse(d, X(98), 12, 9, 9, glow and rgb('#e070ff') or rgb('#60c0ff'))
        ellipse(d, X(96), 10, 4, 4, rgb('#ffffff'))
    elif held == 'hammer':
        d.line((X(92), hy, X(104), hy - 34), fill=WOOD[1], width=5)
        d.rectangle((X(94), hy - 44, X(116), hy - 30), fill=rgb('#6a6a76'))
    elif held == 'dagger':
        d.line((X(92), hy, X(102), hy - 22), fill=rgb('#d0d4e0'), width=5)
    elif held == 'club':
        d.line((X(92), hy, X(108), hy - 38), fill=WOOD[1], width=9)
        ellipse(d, X(108), hy - 40, 9, 9, WOOD[0])
    elif held == 'scythe':
        d.line((X(94), 120, X(100), 12), fill=rgb('#3a3030'), width=5)
        d.arc((X(60), 4, X(124), 40), 180, 300, fill=rgb('#c0c4d0'), width=6)
    elif held == 'mug':
        d.rectangle((X(88), hy - 8, X(100), hy + 6), fill=rgb('#c8a050'))
    if shield:
        d.polygon([(X(18), 58 + bob), (X(46), 58 + bob), (X(46), 88 + bob), (X(32), 102 + bob), (X(18), 88 + bob)],
                  fill=shield)
        d.polygon([(X(18), 58 + bob), (X(30), 58 + bob), (X(30), 98 + bob), (X(18), 88 + bob)],
                  fill=shade(shield, 1.3))
        d.line((X(32), 62 + bob, X(32), 96 + bob), fill=rgb('#e8c850'), width=4)
    # head
    hy0 = 14 + bob
    if hood:
        ellipse(d, cx, hy0 + 22, 24, 26, hood)
    ellipse(d, cx, hy0 + 20, 18, 19, skin[0])
    ellipse(d, cx + 6, hy0 + 24, 12, 14, skin[1])
    ellipse(d, cx - 3, hy0 + 15, 10, 9, skin[0])
    if hair and not hat:
        d.pieslice((cx - 20, hy0 - 2, cx + 20, hy0 + 34), 180, 360, fill=hair)
        d.rectangle((cx - 20, hy0 + 14, cx - 14, hy0 + 30), fill=hair)
        d.rectangle((cx + 14, hy0 + 14, cx + 20, hy0 + 30), fill=hair)
    if hood:
        d.pieslice((cx - 24, hy0 - 4, cx + 24, hy0 + 40), 180, 360, fill=hood)
        d.rectangle((cx - 24, hy0 + 16, cx - 16, hy0 + 44), fill=hood)
        d.rectangle((cx + 16, hy0 + 16, cx + 24, hy0 + 44), fill=shade(hood, .75))
    d.rectangle((cx - 9, hy0 + 20, cx - 4, hy0 + 26), fill=eyes)
    d.rectangle((cx + 4, hy0 + 20, cx + 9, hy0 + 26), fill=eyes)
    if beard:
        d.polygon([(cx - 16, hy0 + 26), (cx + 16, hy0 + 26), (cx + 8, hy0 + 52), (cx, hy0 + 58), (cx - 8, hy0 + 52)],
                  fill=beard)
    if hat == 'helmet':
        d.pieslice((cx - 21, hy0 - 4, cx + 21, hy0 + 36), 180, 360, fill=rgb('#a0a4b0'))
        d.pieslice((cx - 21, hy0 - 4, cx + 21, hy0 + 36), 200, 260, fill=rgb('#d0d4e0'))
        d.rectangle((cx - 22, hy0 + 14, cx + 22, hy0 + 19), fill=rgb('#6a6e7a'))
        d.rectangle((cx - 3, hy0 + 14, cx + 3, hy0 + 30), fill=rgb('#6a6e7a'))
    elif hat == 'crown':
        d.pieslice((cx - 20, hy0 - 2, cx + 20, hy0 + 34), 180, 360, fill=hair)
        d.polygon([(cx - 18, hy0 + 10), (cx - 18, hy0 - 8), (cx - 9, hy0 + 2), (cx, hy0 - 12), (cx + 9, hy0 + 2),
                   (cx + 18, hy0 - 8), (cx + 18, hy0 + 10)], fill=rgb('#f0c830'))
        ellipse(d, cx, hy0 + 2, 4, 4, rgb('#e03040'))
    elif hat == 'wizard':
        d.polygon([(cx - 28, hy0 + 12), (cx + 6, hy0 - 30), (cx + 28, hy0 + 12)], fill=tunic)
        d.polygon([(cx + 6, hy0 - 30), (cx + 28, hy0 + 12), (cx + 6, hy0 + 12)], fill=shade(tunic, .7))
        d.rectangle((cx - 30, hy0 + 8, cx + 30, hy0 + 14), fill=shade(tunic, .6))
    elif hat == 'cap':
        d.pieslice((cx - 20, hy0 - 4, cx + 20, hy0 + 30), 180, 360, fill=rgb('#3a6a3a'))
        d.rectangle((cx - 24, hy0 + 10, cx + 10, hy0 + 15), fill=rgb('#2a4a2a'))
        d.line((cx + 10, hy0, cx + 26, hy0 - 12), fill=rgb('#e04040'), width=3)
    elif hat == 'scarf':
        d.pieslice((cx - 20, hy0 - 4, cx + 20, hy0 + 32), 180, 360, fill=rgb('#c04060'))
    elif hat == 'horns':
        d.polygon([(cx - 16, hy0 + 6), (cx - 30, hy0 - 14), (cx - 8, hy0 + 2)], fill=rgb('#e8e0c8'))
        d.polygon([(cx + 16, hy0 + 6), (cx + 30, hy0 - 14), (cx + 8, hy0 + 2)], fill=rgb('#e8e0c8'))
    return sprite_from(big)


def skeleton(frame):
    big = canvas()
    d = ImageDraw.Draw(big)
    bone = rgb('#e8e2cc')
    dark = rgb('#a8a290')
    bob = 2 if frame else 0
    sw = 6 if frame else -6
    d.line((54, 88, 50, 118 - (4 if frame else 0)), fill=bone, width=6)
    d.line((74, 88, 78, 118 - (0 if frame else 4)), fill=bone, width=6)
    d.rectangle((48, 82, 80, 92), fill=dark)
    d.line((64, 46 + bob, 64, 86), fill=bone, width=6)
    for k in range(4):
        y = 52 + bob + k * 8
        d.arc((44, y - 6, 84, y + 8), 0, 180, fill=bone, width=4)
    d.line((46, 52 + bob, 34, 86 + sw // 2), fill=bone, width=5)
    d.line((82, 52 + bob, 94, 86 - sw // 2), fill=bone, width=5)
    d.line((94, 86 - sw // 2, 108, 40 - sw // 2), fill=rgb('#a0a4b0'), width=6)
    ellipse(d, 64, 30 + bob, 17, 18, bone)
    ellipse(d, 70, 34 + bob, 10, 12, dark)
    ellipse(d, 57, 28 + bob, 5, 6, OUTLINE)
    ellipse(d, 71, 28 + bob, 5, 6, OUTLINE)
    ellipse(d, 57, 28 + bob, 2, 2, rgb('#ff4030'))
    ellipse(d, 71, 28 + bob, 2, 2, rgb('#ff4030'))
    d.rectangle((56, 42 + bob, 72, 46 + bob), fill=OUTLINE)
    return sprite_from(big)


def rat(frame):
    big = canvas()
    d = ImageDraw.Draw(big)
    body = rgb('#7a6050')
    o = 4 if frame else 0
    d.line((20, 92, 6, 70 + o), fill=rgb('#c09080'), width=4)
    ellipse(d, 56, 90, 36, 22, body)
    ellipse(d, 50, 84, 26, 14, rgb('#9a8070'))
    ellipse(d, 92, 84, 18, 15, body)
    d.polygon([(104, 80), (120, 88), (104, 94)], fill=rgb('#9a8070'))
    ellipse(d, 120, 88, 3, 3, rgb('#e08090'))
    ellipse(d, 86, 66, 9, 10, rgb('#c09080'))
    ellipse(d, 98, 80, 3, 3, rgb('#ff3020'))
    for x in (38 + o, 70 - o):
        d.line((x, 104, x - 4, 116), fill=rgb('#c09080'), width=4)
    return sprite_from(big)


def bat(frame):
    big = canvas()
    d = ImageDraw.Draw(big)
    w = rgb('#4a3a5a')
    y = 30 if frame else 60
    d.polygon([(64, 64), (14, y), (24, y + 20), (34, y + 10), (44, y + 26), (60, 70)], fill=w)
    d.polygon([(64, 64), (114, y), (104, y + 20), (94, y + 10), (84, y + 26), (68, 70)], fill=shade(w, .8))
    ellipse(d, 64, 68, 14, 16, rgb('#3a2a48'))
    d.polygon([(54, 54), (56, 40), (62, 54)], fill=rgb('#3a2a48'))
    d.polygon([(66, 54), (72, 40), (74, 54)], fill=rgb('#3a2a48'))
    ellipse(d, 58, 64, 3, 3, rgb('#ff4030'))
    ellipse(d, 70, 64, 3, 3, rgb('#ff4030'))
    return sprite_from(big)


def slime(frame, col=rgb('#4ac040')):
    big = canvas()
    d = ImageDraw.Draw(big)
    h = 44 if frame else 52
    w = 50 if frame else 44
    d.pieslice((64 - w, 118 - h * 2, 64 + w, 118), 180, 360, fill=shade(col, .7))
    d.pieslice((64 - w + 6, 118 - h * 2 + 6, 64 + w - 14, 118 - 4), 180, 360, fill=col)
    ellipse(d, 48, 118 - h + 4, 10, 7, shade(col, 1.5))
    ellipse(d, 54, 104 - h // 2, 5, 6, OUTLINE)
    ellipse(d, 74, 104 - h // 2, 5, 6, OUTLINE)
    return sprite_from(big)


def wolf(frame, col=rgb('#7a7a86')):
    big = canvas()
    d = ImageDraw.Draw(big)
    o = 6 if frame else 0
    d.line((24, 70, 6, 52 + o), fill=col, width=10)
    ellipse(d, 58, 74, 38, 20, col)
    ellipse(d, 54, 66, 30, 10, shade(col, 1.25))
    for x in (34 + o, 46 - o, 70 + o, 82 - o):
        d.rectangle((x, 84, x + 8, 116), fill=shade(col, .75))
    ellipse(d, 96, 58, 18, 16, col)
    d.polygon([(106, 56), (126, 66), (106, 74)], fill=shade(col, 1.1))
    d.polygon([(86, 46), (90, 26), (98, 44)], fill=col)
    d.polygon([(98, 44), (104, 28), (108, 48)], fill=shade(col, .8))
    ellipse(d, 102, 56, 3, 3, rgb('#ffd030'))
    d.line((110, 72, 122, 70), fill=rgb('#ffffff'), width=2)
    return sprite_from(big)


def spider(frame):
    big = canvas()
    d = ImageDraw.Draw(big)
    o = 6 if frame else -6
    for k in range(4):
        y = 54 + k * 12
        d.line((64, 70, 18, y - 18 + (o if k % 2 else -o)), fill=rgb('#2a2028'), width=5)
        d.line((18, y - 18 + (o if k % 2 else -o), 8, y + 14), fill=rgb('#2a2028'), width=4)
        d.line((64, 70, 110, y - 18 + (-o if k % 2 else o)), fill=rgb('#2a2028'), width=5)
        d.line((110, y - 18 + (-o if k % 2 else o), 120, y + 14), fill=rgb('#2a2028'), width=4)
    ellipse(d, 64, 84, 26, 28, rgb('#3a2a38'))
    d.polygon([(54, 80), (74, 80), (64, 100)], fill=rgb('#c02030'))
    ellipse(d, 64, 54, 16, 14, rgb('#2a2028'))
    for x in (56, 62, 66, 72):
        ellipse(d, x, 50, 2, 2, rgb('#ff4030'))
    return sprite_from(big)


def ghost(frame, col=rgb('#b8d0f0')):
    big = canvas()
    d = ImageDraw.Draw(big)
    y0 = 12 if frame else 18
    ellipse(d, 64, 64, 54, 54, (col[0], col[1], col[2], 50))
    d.pieslice((28, y0, 100, y0 + 76), 180, 360, fill=col)
    d.rectangle((28, y0 + 38, 100, y0 + 86), fill=col)
    for k in range(5):
        x = 28 + k * 18 + (4 if frame else 0)
        d.polygon([(x, y0 + 86), (x + 9, y0 + 100), (x + 18, y0 + 86)], fill=col)
    d.rectangle((28, y0 + 38, 44, y0 + 86), fill=shade(col, 1.1))
    ellipse(d, 52, y0 + 36, 7, 10, rgb('#1a2040'))
    ellipse(d, 76, y0 + 36, 7, 10, rgb('#1a2040'))
    ellipse(d, 64, y0 + 62, 8, 10, rgb('#1a2040'))
    return sprite_from(big)


def drake(frame, col=rgb('#b83a28'), size=1.0, wings=True):
    big = canvas()
    d = ImageDraw.Draw(big)
    o = 10 if frame else 0

    def P(x, y):
        return (64 + (x - 64) * size, 70 + (y - 70) * size)

    if wings:
        d.polygon([P(54, 56), P(10, 20 + o), P(18, 60), P(30, 54), P(40, 72)], fill=shade(col, .7))
        d.polygon([P(74, 56), P(118, 20 + o), P(110, 60), P(98, 54), P(88, 72)], fill=shade(col, .6))
    d.line((P(40, 96), P(14, 112), P(4, 96)), fill=col, width=int(9 * size))
    d.ellipse((*P(32, 62), *P(96, 112)), fill=col)
    d.ellipse((*P(40, 70), *P(80, 100)), fill=rgb('#e0a050'))
    d.ellipse((*P(48, 22), *P(84, 58)), fill=col)
    d.polygon([P(76, 34), P(104, 44), P(80, 54)], fill=col)
    d.polygon([P(54, 26), P(48, 4), P(62, 22)], fill=rgb('#e8e0c8'))
    d.polygon([P(72, 26), P(80, 4), P(78, 26)], fill=rgb('#e8e0c8'))
    d.ellipse((*P(66, 32), *P(74, 40)), fill=rgb('#ffe040'))
    for x in (44, 76):
        d.rectangle((*P(x, 104), *P(x + 10, 120)), fill=shade(col, .7))
    return sprite_from(big)


def golem(frame):
    big = canvas()
    d = ImageDraw.Draw(big)
    bob = 3 if frame else 0
    st = [rgb('#4a4a40'), rgb('#6a6a5a'), rgb('#8a8a76')]
    moss = rgb('#4a8a32')
    d.rectangle((38, 92, 58, 122), fill=st[0])
    d.rectangle((70, 92, 90, 122), fill=st[0])
    d.rounded_rectangle((26, 36 + bob, 102, 100 + bob), 14, fill=st[1])
    d.rounded_rectangle((26, 36 + bob, 54, 100 + bob), 12, fill=st[2])
    d.rounded_rectangle((6, 40 + bob, 28, 104 + bob), 8, fill=st[1])
    d.rounded_rectangle((100, 40 + bob, 122, 104 + bob), 8, fill=st[0])
    d.rounded_rectangle((46, 6 + bob, 82, 40 + bob), 8, fill=st[2])
    ellipse(d, 56, 22 + bob, 4, 4, rgb('#80ff60'))
    ellipse(d, 72, 22 + bob, 4, 4, rgb('#80ff60'))
    for (x, y, r) in ((40, 44, 12), (90, 60, 10), (60, 8, 10), (14, 44, 8), (76, 92, 9)):
        ellipse(d, x, y + bob, r, r * .6, moss)
    d.line((50, 60 + bob, 70, 76 + bob, 64, 92 + bob), fill=st[0], width=3)
    return sprite_from(big)


def wyrm(frame):
    big = canvas()
    d = ImageDraw.Draw(big)
    o = 6 if frame else -6
    col = rgb('#d84a18')
    pts = [(12, 110), (30, 96 + o), (50, 110 - o), (72, 92 + o), (88, 70), (92, 46)]
    for i in range(len(pts) - 1):
        d.line((pts[i], pts[i + 1]), fill=col, width=18 - i)
    for i in range(len(pts) - 1):
        d.line((pts[i], pts[i + 1]), fill=rgb('#ffa040'), width=6)
    ellipse(d, 92, 36, 22, 18, col)
    d.polygon([(100, 34), (126, 44), (102, 52)], fill=col)
    d.polygon([(82, 22), (70, 0), (90, 18)], fill=rgb('#ffe070'))
    d.polygon([(96, 20), (102, 0), (104, 22)], fill=rgb('#ffe070'))
    ellipse(d, 96, 32, 4, 4, rgb('#fff080'))
    if frame:
        d.polygon([(122, 46), (128, 40), (128, 54)], fill=rgb('#ffe070'))
    for k in range(5):
        ellipse(d, 30 + k * 16, 60 - k * 6 + o, 4, 4, rgb('#ffd040'))
    return sprite_from(big)


def serpent_king(frame):
    return humanoid(frame, rgb('#2a6a6a'), robe=True, skin=[rgb('#8ab8a8'), rgb('#6a9a8a'), rgb('#4a7a6a')],
                    hat='crown', hair=rgb('#2a4a3a'), held='staff', beard=rgb('#3a7a5a'),
                    eyes=rgb('#a0ffe0'), big_=True)


def make_creatures():
    """(name, [frame0, frame1]) for every creature, in the WAD's order."""
    C = []

    def add(name, f):
        C.append((name, [f(0), f(1)]))

    add('hero', lambda f: humanoid(f, rgb('#2c5ab4'), cape=rgb('#a82a2a'), held='sword', shield=rgb('#3a4a9a'),
                                   hair=rgb('#c08a3a'), legs=rgb('#4a4040')))
    add('guard', lambda f: humanoid(f, rgb('#8a8c98'), hat='helmet', held='spear', shield=rgb('#a83030'),
                                    legs=rgb('#5a5c68')))
    add('merchant', lambda f: humanoid(f, rgb('#3a8a4a'), hat='cap', robe=True, hair=rgb('#3a2a1a'),
                                       beard=rgb('#3a2a1a')))
    add('man', lambda f: humanoid(f, rgb('#8a5a2a'), hair=rgb('#3a2414'), legs=rgb('#4a4a5a')))
    add('woman', lambda f: humanoid(f, rgb('#b04a6a'), robe=True, hair=rgb('#d8a040'), hat='scarf'))
    add('king', lambda f: humanoid(f, rgb('#6a2a9a'), robe=True, hat='crown', hair=rgb('#c8c8d0'),
                                   beard=rgb('#e0e0e8'), cape=rgb('#a82a2a')))
    add('healer', lambda f: humanoid(f, rgb('#e8e4d8'), robe=True, hair=rgb('#a07040'), hood=rgb('#c8c0b0')))
    add('innkeeper', lambda f: humanoid(f, rgb('#e0d8c0'), hair=rgb('#5a3a1e'), beard=rgb('#5a3a1e'),
                                        held='mug', legs=rgb('#3a3a4a')))
    add('sage', lambda f: humanoid(f, rgb('#2a3a8a'), robe=True, hat='wizard', beard=rgb('#e8e8f0'),
                                   held='staff', hair=rgb('#d0d0d8')))
    add('smith', lambda f: humanoid(f, rgb('#5a4030'), hair=rgb('#1a1a1a'), held='hammer', beard=rgb('#2a2a2a'),
                                    skin=[rgb('#d8a878'), rgb('#b88858'), rgb('#906040')]))
    add('child', lambda f: humanoid(f, rgb('#d8a030'), hair=rgb('#6a3a1a'), legs=rgb('#3a5a9a')))
    add('rat', rat)
    add('bat', bat)
    add('slime', slime)
    add('wolf', wolf)
    add('goblin', lambda f: humanoid(f, rgb('#6a4a2a'), skin=[rgb('#8ac050'), rgb('#6aa040'), rgb('#4a8030')],
                                     hair=None, hat='horns', held='dagger', eyes=rgb('#ffe020'),
                                     legs=rgb('#4a3a2a')))
    add('skeleton', skeleton)
    add('orc', lambda f: humanoid(f, rgb('#6a5a4a'), skin=[rgb('#7a9a5a'), rgb('#5a7a40'), rgb('#3a5a2a')],
                                  hair=rgb('#1a1a1a'), held='club', big_=True, eyes=rgb('#ff3020'),
                                  shield=rgb('#5a4030')))
    add('spider', spider)
    add('ghoul', lambda f: humanoid(f, rgb('#4a5a4a'), skin=[rgb('#a8b898'), rgb('#88987a'), rgb('#68785a')],
                                    hair=rgb('#2a2a2a'), eyes=rgb('#f0f040'), legs=rgb('#3a3a30')))
    add('wraith', lambda f: ghost(f, rgb('#9a8ad0')))
    add('troll', lambda f: humanoid(f, rgb('#5a4a3a'), skin=[rgb('#8a9aa8'), rgb('#6a7a88'), rgb('#4a5a68')],
                                    hair=None, held='club', big_=True, eyes=rgb('#ffd020'), legs=rgb('#4a3a2a')))
    add('drake', drake)
    add('golem', golem)
    add('drowned', serpent_king)
    add('wyrm', wyrm)
    add('morvath', lambda f: humanoid(f, rgb('#2a1a3a'), robe=True, hood=rgb('#1a1024'), hair=None,
                                      skin=[rgb('#c0b0d0'), rgb('#a090b8'), rgb('#806898')], held='staff',
                                      eyes=rgb('#ff40ff'), glow=(200, 80, 255, 70), big_=True))
    add('bandit', lambda f: humanoid(f, rgb('#3a3a3a'), hood=rgb('#2a2a2a'), hair=None, held='dagger',
                                     legs=rgb('#2a2a2a'), cape=rgb('#4a2a2a')))
    add('blueslime', lambda f: slime(f, rgb('#4a80e0')))
    add('ghost', ghost)
    return C


def make_terrain():
    """(name, image) pairs, animated ones as name#frame."""
    L = []
    for i in range(3):
        L.append(('grass%d' % i, grass(10 + i, flowers=0 if i < 2 else 2)))
    L.append(('meadow', grass(20, flowers=12)))
    L.append(('brush', brush(21)))
    L.append(('forest', trees(22, 4)))
    L.append(('forest2', trees(23, 5)))
    L.append(('pines', trees(24, 5, pine=True, dark=True)))
    L.append(('hills', hills(25)))
    L.append(('mountain', mountain(26)))
    L.append(('mountain2', mountain(27)))
    L.append(('peak', mountain(28, snow=True)))
    L.append(('sand', sand(29)))
    for f in range(4):
        L.append(('shallow#%d' % f, water(30, f)))
    for f in range(4):
        L.append(('deep#%d' % f, water(31, f, deep=True)))
    L.append(('swamp', swamp(32)))
    for f in range(4):
        L.append(('lava#%d' % f, lava(33, f)))
    L.append(('ash', ash(34)))
    for m in range(1, 16):
        L.append(('shore%d' % m, shore(m)))
    for m in range(16):
        L.append(('road%d' % m, road(m)))
    L.append(('bridgeh', bridge(False)))
    L.append(('bridgev', bridge(True)))
    L.append(('village', village()))
    L.append(('castle', castle()))
    L.append(('port', port()))
    L.append(('cave', cave_entrance()))
    L.append(('crypt', crypt_entrance()))
    L.append(('mine', mine_entrance()))
    L.append(('spire', spire()))
    L.append(('causeway', causeway()))
    L.append(('gate', gate(False)))
    L.append(('gateopen', gate(True)))
    # town
    cob = cobble(100)
    L.append(('cobble', cob))
    L.append(('cobble2', cobble(101)))
    wood = planks(102)
    L.append(('woodfloor', wood))
    stone = flagstone(103, COBBLE)
    L.append(('stonefloor', stone))
    L.append(('carpet', carpet()))
    brick = bricks(104, [rgb('#7a3a2a'), rgb('#9a4a32'), rgb('#b4603e')], rgb('#4a3a32'))
    L.append(('brick', brick))
    L.append(('brickwin', wall_with(brick, 'window')))
    L.append(('brickdoor', wall_with(brick, 'door')))
    sw = bricks(105, [rgb('#5a5c66'), rgb('#7a7c88'), rgb('#9a9ca8')], rgb('#34343c'), bw=16, bh=10)
    L.append(('stonewall', sw))
    L.append(('stonedoor', wall_with(sw, 'door')))
    for f in range(2):
        L.append(('torch#%d' % f, torch_wall(sw, f)))
    ww = planks(106, [rgb('#3a2412'), rgb('#5a3a1e'), rgb('#7a5030'), rgb('#946640')], vertical=True)
    L.append(('woodwall', ww))
    L.append(('woodwin', wall_with(ww, 'window')))
    L.append(('wooddoor', wall_with(ww, 'door')))
    for k in ('counter', 'table', 'bed', 'barrel', 'crate', 'shelf', 'anvil'):
        L.append((k, furniture(k, wood)))
    L.append(('throne', furniture('throne', carpet())))
    L.append(('well', furniture('well', cob)))
    for f in range(2):
        L.append(('fountain#%d' % f, fountain(f)))
    L.append(('flowerbed', furniture('flowerbed', grass(110))))
    L.append(('hedge', furniture('hedge', grass(111))))
    L.append(('fence', furniture('fence', grass(112))))
    L.append(('tree', trees(113, 1)))
    for k in ('sign_weapon', 'sign_armor', 'sign_potion', 'sign_inn', 'sign_heal'):
        L.append((k, furniture(k, cob)))
    L.append(('pillar', furniture('pillar', stone)))
    # dungeons: four looks
    cave_f = [rgb('#2a2620'), rgb('#3a352c'), rgb('#4a4438'), rgb('#5a5446')]
    L.append(('cavefloor', cave_floor(120, cave_f)))
    L.append(('cavewall', rock_wall(121, [rgb('#1e1c18'), rgb('#3a3630'), rgb('#56504a'), rgb('#746c62')],
                                    moss=rgb('#3a7a2a'))))
    crypt_f = [rgb('#1e2420'), rgb('#343c36'), rgb('#4a544c'), rgb('#5e6a60')]
    L.append(('cryptfloor', dungeon_floor(122, crypt_f)))
    L.append(('cryptwall', bricks(123, [rgb('#2e3a34'), rgb('#40504a'), rgb('#54665e')], rgb('#141a18'))))
    mine_f = [rgb('#2a2018'), rgb('#3e3024'), rgb('#524030'), rgb('#66503c')]
    L.append(('minefloor', cave_floor(124, mine_f)))
    L.append(('minewall', rock_wall(125, [rgb('#24180e'), rgb('#40301e'), rgb('#5a4630'), rgb('#7a6044')],
                                    beams=True)))
    sp_f = [rgb('#120c18'), rgb('#22182c'), rgb('#302240'), rgb('#3e2e52')]
    L.append(('spirefloor', dungeon_floor(126, sp_f)))
    L.append(('spirewall', bricks(127, [rgb('#1e1428'), rgb('#2e2040'), rgb('#443060')], rgb('#0a060e'))))
    return L


OVERLAYS = ('stairs_down', 'stairs_up', 'chest', 'chest_open', 'bones', 'web', 'rubble', 'altar', 'altar_empty')


def make_overlays():
    """Drawn over the map's own floor at run time (transparent)."""
    return [(k, overlay(k)) for k in OVERLAYS]
