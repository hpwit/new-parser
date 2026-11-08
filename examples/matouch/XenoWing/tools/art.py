#!/usr/bin/env python3
"""Original pixel art for the shooter: a small raster toolkit (no PIL),
three player ships, three enemy sets, three backgrounds, and preview sheets."""
import math, random, struct, zlib, colorsys

# ---------------------------------------------------------------- raster
class Img:
    def __init__(s, w, h, fill=None):
        s.w, s.h = w, h
        s.p = [fill] * (w * h)          # None = transparent
    def get(s, x, y):
        if 0 <= x < s.w and 0 <= y < s.h:
            return s.p[y * s.w + x]
        return None
    def put(s, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < s.w and 0 <= y < s.h and c is not None:
            s.p[y * s.w + x] = c
    def blit(s, o, ox, oy):
        for y in range(o.h):
            for x in range(o.w):
                c = o.p[y * o.w + x]
                if c is not None:
                    s.put(ox + x, oy + y, c)
    def scaled(s, k):
        o = Img(s.w * k, s.h * k)
        for y in range(o.h):
            for x in range(o.w):
                o.p[y * o.w + x] = s.p[(y // k) * s.w + x // k]
        return o
    def save(s, path, bg=(0, 0, 0)):
        raw = bytearray()
        for y in range(s.h):
            raw.append(0)
            for x in range(s.w):
                c = s.p[y * s.w + x]
                raw += bytes(c if c is not None else bg)
        def chunk(t, d):
            return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
        open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", s.w, s.h, 8, 2, 0, 0, 0))
                               + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))

def hsv(h, s, v):
    r, g, b = colorsys.hsv_to_rgb((h % 360) / 360.0, max(0, min(1, s)), max(0, min(1, v)))
    return (int(r * 255), int(g * 255), int(b * 255))

def mix(a, b, t):
    t = max(0, min(1, t))
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c)

def poly(img, pts, colfn):
    """fills a polygon; colfn(x, y) gives each pixel's colour"""
    ys = [p[1] for p in pts]
    for y in range(int(min(ys)), int(max(ys)) + 1):
        xs = []
        n = len(pts)
        for i in range(n):
            x1, y1 = pts[i]; x2, y2 = pts[(i + 1) % n]
            if (y1 <= y + 0.5 < y2) or (y2 <= y + 0.5 < y1):
                xs.append(x1 + (y + 0.5 - y1) * (x2 - x1) / (y2 - y1))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            for x in range(int(math.ceil(xs[i] - 0.5)), int(math.floor(xs[i + 1] - 0.5)) + 1):
                img.put(x, y, colfn(x, y))

def disc(img, cx, cy, r, colfn):
    for y in range(int(cy - r - 1), int(cy + r + 2)):
        for x in range(int(cx - r - 1), int(cx + r + 2)):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if d <= r:
                img.put(x, y, colfn(x, y, d / r if r else 0))

def outline(img, col):
    """a dark 1-pixel outline around the sprite (the pixel-art look)"""
    add = []
    for y in range(img.h):
        for x in range(img.w):
            if img.get(x, y) is None:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if img.get(x + dx, y + dy) is not None:
                        add.append((x, y)); break
    for x, y in add:
        img.put(x, y, col)

def lit(base, x, y, cx, cy, h):
    """lighter at the top, darker at the bottom (light from above)"""
    t = (y - (cy - h / 2)) / max(1, h)
    return shade(base, 1.35 - 0.75 * t)

# ---------------------------------------------------------------- noise
random.seed(7)
PERM = list(range(256)); random.shuffle(PERM); PERM += PERM
GRAD = [random.random() for _ in range(256)]
def vnoise(x, y):
    xi, yi = int(math.floor(x)) & 255, int(math.floor(y)) & 255
    xf, yf = x - math.floor(x), y - math.floor(y)
    u, v = xf * xf * (3 - 2 * xf), yf * yf * (3 - 2 * yf)
    a = GRAD[PERM[PERM[xi] + yi]]; b = GRAD[PERM[PERM[xi + 1] + yi]]
    c = GRAD[PERM[PERM[xi] + yi + 1]]; d = GRAD[PERM[PERM[xi + 1] + yi + 1]]
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v
def fbm(x, y, oct=5):
    s, a, f, n = 0, 0.5, 1, 0
    for _ in range(oct):
        s += a * vnoise(x * f, y * f); n += a; a *= 0.5; f *= 2
    return s / n

# ---------------------------------------------------------------- ships
OUT = (12, 10, 24)

def ship_interceptor():
    """A: a sleek silver-blue interceptor, nose to the right"""
    s = Img(36, 18)
    hull = (170, 190, 215); blue = (40, 110, 230); dark = (60, 70, 100)
    # swept wings
    poly(s, [(6, 9), (16, 9), (10, 1), (6, 1)], lambda x, y: lit(blue, x, y, 9, 5, 9))
    poly(s, [(6, 9), (16, 9), (10, 17), (6, 17)], lambda x, y: lit(shade(blue, 0.7), x, y, 9, 13, 9))
    # hull: a long wedge
    poly(s, [(3, 6), (24, 7), (34, 9), (24, 11), (3, 12)], lambda x, y: lit(hull, x, y, 18, 9, 7))
    # engine block and stripe
    poly(s, [(1, 6), (6, 6), (6, 12), (1, 12)], lambda x, y: lit(dark, x, y, 3, 9, 6))
    for x in range(8, 30):
        s.put(x, 10, shade(blue, 1.2))
    # cockpit
    disc(s, 21, 8, 2.6, lambda x, y, d: mix((180, 240, 255), (20, 80, 160), d))
    s.put(20, 7, (255, 255, 255))
    outline(s, OUT)
    return s

def ship_manta():
    """B: a wide organic manta wing, glowing teal core"""
    s = Img(36, 22)
    skin = (120, 60, 170); belly = (70, 30, 110)
    poly(s, [(2, 11), (14, 2), (26, 6), (34, 11), (26, 16), (14, 20)],
         lambda x, y: lit(skin if y <= 11 else belly, x, y, 18, 11, 18))
    # ribs on the wings
    for k in range(3):
        for t in range(8):
            s.put(10 + k * 4 + t // 2, 4 + t, shade(skin, 1.5))
            s.put(10 + k * 4 + t // 2, 18 - t, shade(belly, 1.4))
    # the glowing core
    disc(s, 19, 11, 4, lambda x, y, d: mix((220, 255, 250), (0, 170, 170), d))
    disc(s, 19, 11, 1.5, lambda x, y, d: (255, 255, 255))
    # tail fins
    poly(s, [(0, 9), (5, 10), (5, 12), (0, 13)], lambda x, y: (200, 90, 220))
    outline(s, OUT)
    return s

def ship_gunship():
    """C: a heavy red gunship with two side pods and cannons"""
    s = Img(38, 22)
    red = (210, 50, 40); steel = (150, 150, 160); dark = (80, 40, 40)
    # pods
    for py in (3, 15):
        poly(s, [(4, py), (24, py), (27, py + 2), (24, py + 4), (4, py + 4)],
             lambda x, y, py=py: lit(steel, x, y, 15, py + 2, 4))
        for x in range(27, 34):
            s.put(x, py + 2, (90, 90, 100))
        s.put(34, py + 2, (255, 230, 120))
    # core hull
    poly(s, [(2, 7), (22, 6), (32, 10), (32, 12), (22, 16), (2, 15)],
         lambda x, y: lit(red, x, y, 17, 11, 10))
    poly(s, [(2, 9), (8, 9), (8, 13), (2, 13)], lambda x, y: dark)
    # armour plates
    for x in (10, 15, 20):
        for y in range(8, 15):
            s.put(x, y, shade(red, 0.6))
    disc(s, 26, 10, 2.4, lambda x, y, d: mix((255, 240, 160), (230, 120, 0), d))
    outline(s, OUT)
    return s

def flame(s, x, y, length, cold=False):
    for i in range(length):
        t = i / max(1, length - 1)
        c = mix((255, 255, 220), (60, 140, 255) if cold else (255, 90, 20), t)
        w = 2 - int(t * 2)
        for dy in range(-w, w + 1):
            s.put(x - i, y + dy, c)

# ---------------------------------------------------------------- enemies (facing left)
def enemy_drones():
    out = []
    # 1 a metal orb with a red eye and fins
    s = Img(20, 20)
    disc(s, 10, 10, 7, lambda x, y, d: mix((220, 225, 235), (60, 65, 80), d * 0.6 + (y - 3) / 30))
    poly(s, [(12, 2), (18, 0), (16, 5)], lambda x, y: (120, 125, 140))
    poly(s, [(12, 18), (18, 20), (16, 15)], lambda x, y: (100, 105, 120))
    disc(s, 7, 10, 3, lambda x, y, d: mix((255, 220, 200), (200, 0, 0), d))
    s.put(6, 9, (255, 255, 255))
    outline(s, OUT); out.append(s)
    # 2 a spinning blade drone
    s = Img(22, 22)
    for k in range(4):
        a = k * math.pi / 2 + 0.4
        poly(s, [(11, 11), (11 + 10 * math.cos(a), 11 + 10 * math.sin(a)), (11 + 7 * math.cos(a + 0.5), 11 + 7 * math.sin(a + 0.5))],
             lambda x, y: (190, 195, 210))
    disc(s, 11, 11, 4, lambda x, y, d: mix((255, 120, 120), (140, 0, 30), d))
    outline(s, OUT); out.append(s)
    # 3 a heavy turret drone
    s = Img(26, 20)
    poly(s, [(4, 4), (22, 2), (24, 10), (22, 18), (4, 16)], lambda x, y: lit((150, 155, 170), x, y, 14, 10, 16))
    for x in range(0, 6):
        s.put(x, 8, (80, 80, 90)); s.put(x, 12, (80, 80, 90))
    disc(s, 13, 10, 3.5, lambda x, y, d: mix((255, 200, 120), (220, 40, 0), d))
    for y in range(4, 17, 4):
        for x in range(16, 22):
            s.put(x, y, (90, 95, 110))
    outline(s, OUT); out.append(s)
    return out

def enemy_bio():
    out = []
    # 1 a green bug with legs
    s = Img(22, 18)
    disc(s, 12, 9, 6.5, lambda x, y, d: mix((170, 255, 90), (30, 120, 30), d * 0.7 + (y - 3) / 25))
    for k in range(3):
        s.put(14 + k * 2, 2, (60, 140, 40)); s.put(15 + k * 2, 1, (60, 140, 40))
        s.put(14 + k * 2, 16, (60, 140, 40)); s.put(15 + k * 2, 17, (60, 140, 40))
    disc(s, 7, 7, 2, lambda x, y, d: (255, 230, 40)); disc(s, 7, 11, 2, lambda x, y, d: (255, 230, 40))
    s.put(6, 7, OUT); s.put(6, 11, OUT)
    outline(s, OUT); out.append(s)
    # 2 a purple jelly with tentacles
    s = Img(20, 24)
    disc(s, 11, 9, 7, lambda x, y, d: mix((240, 170, 255), (110, 30, 160), d))
    for k in range(4):
        for t in range(8):
            s.put(14 - k * 3 + int(math.sin(t * 0.9 + k) * 1.2), 15 + t, mix((200, 120, 230), (90, 20, 130), t / 8))
    disc(s, 9, 8, 1.6, lambda x, y, d: (255, 255, 255))
    outline(s, OUT); out.append(s)
    # 3 a horned crawler
    s = Img(28, 20)
    poly(s, [(4, 10), (12, 3), (26, 5), (24, 15), (12, 17)], lambda x, y: lit((190, 90, 40), x, y, 15, 10, 14))
    for k in range(4):
        disc(s, 13 + k * 3.5, 10, 1.4, lambda x, y, d: (240, 170, 80))
    poly(s, [(5, 8), (0, 2), (8, 6)], lambda x, y: (240, 230, 200))
    poly(s, [(5, 12), (0, 18), (8, 14)], lambda x, y: (240, 230, 200))
    disc(s, 8, 10, 1.6, lambda x, y, d: (255, 40, 40))
    outline(s, OUT); out.append(s)
    return out

def enemy_fighters():
    out = []
    # 1 a dart with neon edges
    s = Img(24, 14)
    poly(s, [(0, 7), (20, 1), (16, 7), (20, 13)], lambda x, y: lit((60, 60, 80), x, y, 10, 7, 12))
    for t in range(18):
        s.put(1 + t, int(7 - t * 6 / 19), (255, 40, 200)); s.put(1 + t, int(7 + t * 6 / 19), (255, 40, 200))
    disc(s, 9, 7, 1.6, lambda x, y, d: (255, 230, 60))
    outline(s, OUT); out.append(s)
    # 2 a wide bomber
    s = Img(28, 20)
    poly(s, [(2, 10), (14, 2), (26, 4), (22, 10), (26, 16), (14, 18)], lambda x, y: lit((80, 70, 100), x, y, 14, 10, 16))
    for x in range(8, 24):
        s.put(x, 10, (0, 230, 255))
    disc(s, 8, 10, 2.2, lambda x, y, d: mix((200, 255, 255), (0, 120, 200), d))
    outline(s, OUT); out.append(s)
    # 3 a crescent ace
    s = Img(24, 24)
    disc(s, 12, 12, 10, lambda x, y, d: lit((110, 90, 140), x, y, 12, 12, 20))
    disc(s, 16, 12, 8, lambda x, y, d: None)
    for y in range(24):
        for x in range(24):
            if s.get(x, y) is not None and math.hypot(x + 0.5 - 16, y + 0.5 - 12) <= 8:
                s.p[y * 24 + x] = None
    disc(s, 6, 12, 2.5, lambda x, y, d: mix((255, 255, 200), (255, 60, 0), d))
    outline(s, OUT); out.append(s)
    return out

# ---------------------------------------------------------------- backgrounds
W, H = 400, 240

def bg_nebula():
    """1: deep space: stars, coloured nebula clouds, a ringed planet"""
    img = Img(W, H)
    for y in range(H):
        for x in range(W):
            n = fbm(x / 90.0, y / 70.0)
            m = fbm(x / 40.0 + 9, y / 40.0 + 3, 4)
            base = mix((4, 2, 18), (10, 6, 40), y / H)
            neb = mix((120, 20, 120), (20, 60, 160), m)
            img.p[y * W + x] = mix(base, neb, max(0, n - 0.45) * 2.2)
    random.seed(3)
    for _ in range(420):
        x, y = random.randrange(W), random.randrange(H)
        b = random.random()
        img.put(x, y, mix((90, 90, 130), (255, 255, 255), b))
        if b > 0.93:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                img.put(x + dx, y + dy, (150, 150, 200))
    # a planet with rings
    cx, cy, r = 300, 70, 38
    for y in range(H):
        for x in range(W):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            d = math.hypot(dx, dy)
            if d <= r:
                band = math.sin((dy + fbm(x / 12, y / 6) * 8) / 4) * 0.5 + 0.5
                c = mix((230, 150, 80), (150, 70, 50), band)
                light = max(0.15, min(1, (-dx * 0.6 - dy * 0.5) / r + 0.75))
                img.p[y * W + x] = shade(c, light)
            e = (dx * dx) / (r * 1.9) ** 2 + (dy * 3.2 + dx * 0.9) ** 2 / (r * 1.9) ** 2
            if 0.72 < e < 1.0 and not (d <= r and dy * 3.2 + dx * 0.9 < 0):
                img.p[y * W + x] = mix(img.p[y * W + x], (220, 200, 170), 0.65)
    return img

def bg_alien():
    """2: an alien world: violet sky, two moons, crystal mountains, a glowing valley"""
    img = Img(W, H)
    for y in range(H):
        for x in range(W):
            img.p[y * W + x] = mix((30, 10, 60), (200, 70, 120), (y / 150.0) ** 1.6) if y < 150 else (40, 10, 50)
    random.seed(5)
    for _ in range(150):
        img.put(random.randrange(W), random.randrange(110), (230, 220, 255))
    disc(img, 80, 50, 22, lambda x, y, d: shade(mix((250, 240, 220), (200, 180, 200), d), 1.0 - 0.4 * max(0, (x - 70) / 30)))
    disc(img, 130, 34, 9, lambda x, y, d: mix((180, 255, 230), (60, 160, 160), d))
    # far mountains
    for x in range(W):
        h = int(60 + fbm(x / 60.0, 1.5) * 50)
        for y in range(H - h - 40, H):
            img.put(x, y, mix((90, 40, 120), (50, 20, 80), (y - (H - h - 40)) / 80))
    # near crystal ridge
    for x in range(W):
        h = int(30 + abs(math.sin(x / 23.0)) * 30 + fbm(x / 20.0, 7) * 25)
        for y in range(H - h, H):
            t = (y - (H - h)) / max(1, h)
            c = mix((120, 240, 255), (30, 60, 120), t ** 0.5) if (x // 23) % 2 == 0 else mix((80, 180, 230), (20, 40, 90), t ** 0.5)
            img.put(x, y, c)
    # a glowing valley line
    for x in range(W):
        img.put(x, H - 4, (255, 120, 200)); img.put(x, H - 3, (180, 60, 160))
    return img

def bg_station():
    """3: a space station: dark hull panels, pipes, lit windows, a starry gap"""
    img = Img(W, H, (6, 6, 16))
    random.seed(11)
    for _ in range(200):
        img.put(random.randrange(W), random.randrange(60, 180), (200, 200, 255))
    def panel(y0, y1, mirror):
        for y in range(y0, y1):
            for x in range(W):
                k = (y - y0) / max(1, y1 - y0)
                if mirror:
                    k = 1 - k
                c = mix((70, 80, 100), (25, 28, 40), k)
                if x % 48 == 0 or (y - y0) % 20 == 0:
                    c = (20, 22, 32)
                if x % 48 == 1:
                    c = (110, 120, 140)
                img.put(x, y, c)
        # lit windows and warning lights
        for x in range(10, W, 48):
            for wx in range(0, 18, 6):
                for wy in range(3):
                    yy = (y0 + 6 + wy) if not mirror else (y1 - 9 + wy)
                    img.put(x + wx, yy, (255, 210, 90)); img.put(x + wx + 1, yy, (255, 210, 90))
            yy = y1 - 3 if not mirror else y0 + 2
            disc(img, x + 30, yy, 2, lambda xx, yy2, d: mix((255, 255, 255), (255, 30, 30), d))
    panel(0, 50, False)
    panel(190, 240, True)
    # pipes and girders across the gap
    for x0 in range(30, W, 120):
        for y in range(50, 190):
            for dx in range(6):
                img.put(x0 + dx, y, mix((150, 160, 180), (50, 55, 70), abs(dx - 2) / 3))
        for y in (90, 150):
            for x in range(x0 - 20, x0 + 26):
                img.put(x, y, (100, 110, 130)); img.put(x, y + 1, (60, 65, 80))
    for y0 in (46, 186):
        for x in range(W):
            for dy in range(4):
                img.put(x, y0 + dy, mix((180, 190, 210), (60, 65, 80), dy / 3))
    return img

# ---------------------------------------------------------------- previews
def mockup(bg, player, enemies, cold=False):
    m = Img(W, H); m.blit(bg, 0, 0)
    m.blit(player, 60, 110)
    flame(m, 59, 110 + player.h // 2, 9, cold)
    # player shots
    for k in range(4):
        for x in range(8):
            m.put(110 + k * 34 + x, 119, (255, 255, 160)); m.put(110 + k * 34 + x, 120, (255, 200, 60))
    spots = [(250, 60), (300, 120), (240, 170), (330, 190), (210, 95)]
    for i, (x, y) in enumerate(spots):
        m.blit(enemies[i % 3], x, y)
    # enemy bullets
    for (x, y) in ((230, 70), (285, 128), (225, 176), (195, 100)):
        disc(m, x, y, 2.5, lambda xx, yy, d: mix((255, 255, 255), (255, 60, 160), d))
    # an explosion
    disc(m, 360, 75, 9, lambda xx, yy, d: mix((255, 255, 210), (255, 80, 0), d))
    disc(m, 356, 72, 4, lambda xx, yy, d: (255, 255, 255))
    return m

def sheet(items, scale, title_rows=None):
    pad = 8
    w = sum(i.w * scale + pad for i in items) + pad
    h = max(i.h for i in items) * scale + pad * 2
    s = Img(w, h, (18, 16, 30))
    x = pad
    for i in items:
        s.blit(i.scaled(scale), x, pad)
        x += i.w * scale + pad
    return s

if __name__ == "__main__":
    import os, sys
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    ships = [ship_interceptor(), ship_manta(), ship_gunship()]
    for s, cold in zip(ships, (True, False, False)):
        flame(s, s.w - s.w, s.h // 2, 0)
    sets = [enemy_drones(), enemy_bio(), enemy_fighters()]
    bgs = [bg_nebula(), bg_alien(), bg_station()]
    sheet(ships, 6).save(os.path.join(out, "choice_ships.png"))
    for k, st in enumerate(sets):
        sheet(st, 6).save(os.path.join(out, "choice_enemies_%d.png" % (k + 1)))
    for k, bg in enumerate(bgs):
        mockup(bg, ships[k], sets[k], k == 0).scaled(2).save(os.path.join(out, "choice_background_%d.png" % (k + 1)))
    print("done")
