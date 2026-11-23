"""Driftwing's world: a 1024x1024 heightmap and colour map that wrap (the
plane can fly on for ever), four regions joined into one, made here from
noise:

  the Hundred Isles   islands, mountains with snow, rivers, forests,
                      villages with red and blue roofs, fields, roads
  the Red Canyons     mesas in layers of red rock, canyons carved down to a
                      river, cacti and shrubs, adobe villages, flat roofs
  the Frost Peaks     high ridged mountains, snowfields, frozen lakes and
                      ice floes, dark pines dusted with snow, timber villages
  the Ember Coast     volcanic islands, craters glowing with lava, black
                      sand, turquoise lagoons, palms and jungle, thatched huts

Each region is a quarter of the world (the Isles round (0, 0), the
Canyons beside them, the Peaks under them, the Coast diagonal), their
borders wavy and blended over a few dozen cells.

The light is baked into the colours: the sun from the south-east (a
slope facing it is brighter), the shadows the mountains cast traced cell
by cell toward the sun; the lava is not lit, it glows.

make_wad.py stores the heights (a byte a cell), the colours (a byte a
cell into a palette of 256 colours), and a small map of the regions.
"""
import math
import random

N = 1024
SEA = 70           # height of the sea's surface (0..255)
SUN = (-0.55, 0.62, -0.56)   # toward the sun: x, up, z (normalised below)

ISLES, CANYONS, PEAKS, EMBER, CITY = 0, 1, 2, 3, 4
REGION_NAMES = ['THE HUNDRED ISLES', 'THE RED CANYONS', 'THE FROST PEAKS', 'THE EMBER COAST', 'POSTHAVEN CITY']

# Posthaven, the city where Driftwing Post has its home: its middle and
# its size (cells). It sits on the Hundred Isles' coast.
CITY_X, CITY_Y, CITY_R = 48, 848, 78


def wrap(i):
    return i & (N - 1)


class Noise:
    """Value noise, periodic over N, at `cells` knots across."""

    def __init__(self, seed, cells):
        r = random.Random(seed)
        self.c = cells
        self.v = [r.random() for _ in range(cells * cells)]

    def at(self, x, y):
        c = self.c
        fx = x * c / N
        fy = y * c / N
        x0 = int(fx)
        y0 = int(fy)
        tx = fx - x0
        ty = fy - y0
        tx = tx * tx * (3 - 2 * tx)
        ty = ty * ty * (3 - 2 * ty)
        x0 %= c
        y0 %= c
        x1 = (x0 + 1) % c
        y1 = (y0 + 1) % c
        v = self.v
        a = v[y0 * c + x0]
        b = v[y0 * c + x1]
        d = v[y1 * c + x0]
        e = v[y1 * c + x1]
        return (a + (b - a) * tx) * (1 - ty) + (d + (e - d) * tx) * ty


def fbm(seed, octaves):
    ns = [Noise(seed * 31 + i, c) for i, (c, w) in enumerate(octaves)]
    ws = [w for c, w in octaves]
    out = [0.0] * (N * N)
    for y in range(N):
        row = y * N
        for x in range(N):
            s = 0.0
            for n, w in zip(ns, ws):
                s += n.at(x, y) * w
            out[row + x] = s
    return out


def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def rgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def ramp(cols, t):
    t = max(0.0, min(0.9999, t))
    f = t * (len(cols) - 1)
    i = int(f)
    return mix(cols[i], cols[i + 1], f - i)


def smooth(e0, e1, v):
    t = max(0.0, min(1.0, (v - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def dist(ax, ay, bx, by):
    return math.hypot(min(abs(ax - bx), N - abs(ax - bx)), min(abs(ay - by), N - abs(ay - by)))


def build(seed=7, progress=print):
    r = random.Random(seed)
    progress('terrain: noise')
    base = fbm(seed, ((8, .5), (16, .27), (32, .13), (64, .07), (128, .03)))
    ridge_src = fbm(seed + 1, ((16, .55), (32, .3), (64, .15)))
    detail = fbm(seed + 2, ((128, .6), (256, .4)))
    moist = fbm(seed + 3, ((12, .6), (24, .4)))
    warp = fbm(seed + 4, ((6, .6), (12, .4)))

    # the regions' weights: the Isles round x = 0 and y = 0, the borders
    # waved by noise
    progress('terrain: regions')
    weights = [None] * (N * N)
    region = bytearray(N * N)
    for y in range(N):
        for x in range(N):
            i = y * N + x
            wv = (warp[i] - 0.5) * 0.9
            a = 0.5 + 0.5 * math.cos(2 * math.pi * x / N + wv)
            b = 0.5 + 0.5 * math.cos(2 * math.pi * y / N - wv)
            a = smooth(0.38, 0.62, a)
            b = smooth(0.38, 0.62, b)
            w = (a * b, (1 - a) * b, a * (1 - b), (1 - a) * (1 - b))
            weights[i] = w
            # the colours' region: the heaviest, dithered at the borders
            t = r.random() * sum(w)
            k = 0
            while k < 3 and t > w[k]:
                t -= w[k]
                k += 1
            region[i] = k

    progress('terrain: heights')
    # the Ember Coast's volcanoes: cones with a crater
    volcanoes = []
    while len(volcanoes) < 7:
        x, y = r.randrange(N), r.randrange(N)
        if weights[y * N + x][EMBER] > 0.9 and all(dist(x, y, vx, vy) > 90 for vx, vy, _ in volcanoes):
            volcanoes.append((x, y, 28 + r.random() * 16))
    h = [0.0] * (N * N)
    for i in range(N * N):
        b = base[i]
        rg = 1.0 - abs(ridge_src[i] - 0.5) * 2.0
        dt = (detail[i] - 0.5) * 6
        w = weights[i]
        hh = 0.0
        if w[ISLES] > 0.001:
            v = (b - 0.42) * 3.0
            if v > 0:
                v = v + max(0.0, v - 0.25) * rg * rg * 1.6
            hh += w[ISLES] * (SEA + v * 150 + dt)
        if w[PEAKS] > 0.001:
            v = (b - 0.36) * 3.0
            if v > 0:
                v = v + max(0.0, v - 0.15) * rg * rg * 2.4
            hh += w[PEAKS] * (SEA + v * 165 + dt * 1.4)
        if w[CANYONS] > 0.001:
            v = max(0.0, (b - 0.22) * 3.0)
            hc = 30 + v * 110
            # terraces: flat steps, steep risers
            t = hc / 16
            f = t - math.floor(t)
            hc = (math.floor(t) + smooth(0.72, 1.0, f)) * 16
            # canyons: where the ridged noise is lowest, cut to the river
            cut = abs(ridge_src[i] - 0.5)
            if cut < 0.07:
                hc = hc + (2 - hc) * (1 - cut / 0.07) ** 0.6
            hh += w[CANYONS] * (SEA + hc + dt * 0.6)
        if w[EMBER] > 0.001:
            v = (b - 0.57) * 4.0
            hh += w[EMBER] * (SEA + v * 80 + dt)
        h[i] = hh
    for (vx, vy, rad) in volcanoes:
        R = int(rad * 2.2)
        top = SEA + 150
        rim = top * (1 - rad * 0.28 / R) ** 1.3 + SEA * 0.4
        for dy in range(-R, R + 1):
            for dx in range(-R, R + 1):
                d = math.hypot(dx, dy)
                if d > R:
                    continue
                j = wrap(vy + dy) * N + wrap(vx + dx)
                cone = top * max(0.0, 1 - d / R) ** 1.3 + SEA * 0.4
                if d < rad * 0.28:
                    cone = rim - 14 * (1 - d / (rad * 0.28))
                h[j] = max(h[j], cone)
    # the highest mountains rounded off below the bytes' 255, not cut flat
    for i in range(N * N):
        t = h[i] - SEA
        if t > 100:
            t = 100 + 78 * math.tanh((t - 100) / 78)
        h[i] = max(4.0, min(250.0, SEA + t))

    # rivers: from high springs downhill to the sea (the Isles, the Peaks);
    # the canyons' floors have theirs
    progress('terrain: rivers')
    river = bytearray(N * N)
    for i in range(N * N):
        if region[i] == CANYONS and h[i] < SEA + 6 and abs(ridge_src[i] - 0.5) < 0.03:
            river[i] = 1
            h[i] = SEA - 1
    springs = [i for i in r.sample(range(N * N), 16000)
               if h[i] > SEA + 70 and region[i] in (ISLES, PEAKS)][:30]
    for s in springs:
        x, y = s % N, s // N
        for _ in range(1400):
            i = y * N + x
            river[i] = 1
            if h[i] <= SEA + 0.5:
                break
            h[i] = max(SEA - 2, h[i] - 3)
            best = None
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, 1), (1, -1), (-1, -1)):
                j = wrap(y + dy) * N + wrap(x + dx)
                if best is None or h[j] < h[best[0]]:
                    best = (j, dx, dy)
            j, dx, dy = best
            if h[j] > h[i]:
                h[j] = h[i] - 0.3
            x, y = wrap(x + dx), wrap(y + dy)
    for i in range(N * N):
        if river[i]:
            x, y = i % N, i // N
            for dx, dy in ((1, 0), (0, 1)):
                j = wrap(y + dy) * N + wrap(x + dx)
                if h[j] > SEA and not river[j]:
                    h[j] = (h[j] + h[i]) / 2

    # Posthaven: the ground flattened (the deeper bays stay, as the
    # harbour), then streets every 14 cells, avenues every 42, and the
    # blocks between them: towers downtown (taller near the middle),
    # lower blocks further out, parks and plazas; the tallest towers get
    # a blinking light (the CITY lump)
    progress('terrain: the city')
    rc = random.Random(seed * 7 + 3)
    kind = bytearray(N * N)
    for dy in range(-CITY_R, CITY_R + 1):
        for dx in range(-CITY_R, CITY_R + 1):
            d = math.hypot(dx, dy)
            x, y = wrap(CITY_X + dx), wrap(CITY_Y + dy)
            j = y * N + x
            if d > CITY_R * (0.82 + 0.3 * warp[j]):
                continue
            region[j] = CITY
            if h[j] < SEA - 6:
                continue
            # flat ground; at the rim the land outside blends in
            flat = SEA + 8
            e2 = CITY_R * (0.82 + 0.3 * warp[j])
            k_ = smooth(e2, e2 * 0.86, d)
            h[j] = h[j] + (flat - h[j]) * k_
            river[j] = 0
            if k_ < 0.99:
                continue
            if x % 14 <= 1 or y % 14 <= 1:
                kind[j] = 1
            elif x % 42 <= 2 or y % 42 <= 2:
                kind[j] = 1
    towers = []
    for by in range((CITY_Y - CITY_R) // 14 - 1, (CITY_Y + CITY_R) // 14 + 2):
        for bx in range((CITY_X - CITY_R) // 14 - 1, (CITY_X + CITY_R) // 14 + 2):
            x0, y0 = bx * 14 + 2, by * 14 + 2
            d = math.hypot(x0 + 6 - CITY_X, y0 + 6 - CITY_Y)
            if d > CITY_R * 0.88:
                continue
            ok = True
            for (px_, py_) in ((x0, y0), (x0 + 11, y0), (x0, y0 + 11), (x0 + 11, y0 + 11), (x0 + 6, y0 + 6)):
                j = wrap(py_) * N + wrap(px_)
                if region[j] != CITY or h[j] <= SEA:
                    ok = False
            if not ok:
                continue
            t = max(0.0, 1 - d / CITY_R)
            roll = rc.random()
            if roll < 0.13:
                # a park: grass, a few trees
                for yy in range(12):
                    for xx in range(12):
                        kind[wrap(y0 + yy) * N + wrap(x0 + xx)] = 15
                for _ in range(5):
                    tx, ty = wrap(x0 + rc.randrange(2, 10)), wrap(y0 + rc.randrange(2, 10))
                    j = ty * N + tx
                    h[j] += 3 + rc.random() * 2
                    kind[j] = 5
            elif roll < 0.18:
                # a plaza: pale paving
                for yy in range(12):
                    for xx in range(12):
                        kind[wrap(y0 + yy) * N + wrap(x0 + xx)] = 16
            else:
                # 2x2 lots, most with a building on them
                for (lx, ly) in ((0, 0), (6, 0), (0, 6), (6, 6)):
                    if rc.random() > 0.88:
                        continue
                    fw = rc.randrange(4, 6)
                    fd = rc.randrange(4, 6)
                    H = 4 + rc.random() * 10 + t * t * rc.random() * 80
                    top = SEA + 8 + H
                    for yy in range(fd):
                        for xx in range(fw):
                            j = wrap(y0 + ly + yy) * N + wrap(x0 + lx + xx)
                            h[j] = top
                            kind[j] = 14
                    if H > 50:
                        towers.append((wrap(x0 + lx + fw // 2), wrap(y0 + ly + fd // 2), top, H))
    towers.sort(key=lambda t_: -t_[3])
    towers = [(x, y, int(round(top))) for (x, y, top, _) in towers[:8]]

    def slope(i):
        x, y = i % N, i // N
        dx = h[y * N + wrap(x + 1)] - h[y * N + wrap(x - 1)]
        dy = h[wrap(y + 1) * N + x] - h[wrap(y - 1) * N + x]
        return math.hypot(dx, dy) / 2

    # 0 land, 1 road, 2 roof red, 3 roof blue, 4 field, 5 tree, 6 tower,
    # 7 timber roof (snowy), 8 adobe roof, 9 thatch, 10 pine, 11 palm,
    # 12 cactus, 13 shrub
    progress('terrain: villages, roads, fields')
    villages = []          # (x, y, region)
    per = [0, 0, 0, 0]
    tries = 0
    while min(per) < 5 and tries < 60000:
        tries += 1
        x, y = r.randrange(N), r.randrange(N)
        i = y * N + x
        g = region[i]
        if g >= CITY or per[g] >= 5 or weights[i][g] < 0.85:
            continue
        lo, hi = SEA + 6, SEA + 45
        if g == CANYONS:
            lo, hi = SEA + 20, SEA + 140
        if g == PEAKS:
            hi = SEA + 70
        if lo < h[i] < hi and slope(i) < 1.2 and all(dist(x, y, vx, vy) > 60 for vx, vy, _ in villages):
            villages.append((x, y, g))
            per[g] += 1
    houses = []
    for (vx, vy, g) in villages:
        ground = h[vy * N + vx]
        if g in (ISLES, PEAKS):
            for fy in range(-22, 23):
                for fx in range(-22, 23):
                    if fx * fx + fy * fy > 22 * 22:
                        continue
                    j = wrap(vy + fy) * N + wrap(vx + fx)
                    if h[j] > SEA + 2 and abs(h[j] - ground) < 14 and not river[j]:
                        kind[j] = 4
        for _ in range(r.randrange(9, 16)):
            hx = wrap(vx + r.randrange(-9, 10))
            hy = wrap(vy + r.randrange(-9, 10))
            w, d = r.randrange(2, 4), r.randrange(3, 5)
            if g == ISLES:
                roof = 2 if r.random() < 0.6 else 3
            else:
                roof = {PEAKS: 7, CANYONS: 8, EMBER: 9}[g]
            top = h[hy * N + hx] + (3 if g == CANYONS else 4) + r.random() * 2
            for yy in range(d):
                for xx in range(w):
                    j = wrap(hy + yy) * N + wrap(hx + xx)
                    if h[j] > SEA + 1:
                        ridge = 0 if g == CANYONS else (0.8 if xx == w // 2 else 0)
                        h[j] = top + ridge
                        kind[j] = roof
            houses.append((hx, hy))
        if g in (ISLES, PEAKS):
            tx, ty = wrap(vx + 2), wrap(vy - 3)
            j = ty * N + tx
            h[j] = h[j] + 14
            kind[j] = 6
    # roads between neighbouring villages of a region
    for a in range(len(villages)):
        ax, ay, ga = villages[a]
        others = sorted([b for b in range(len(villages)) if villages[b][2] == ga],
                        key=lambda b: dist(villages[b][0], villages[b][1], ax, ay))
        for b in others[1:3]:
            bx, by, _ = villages[b]
            dx = ((bx - ax + N // 2) % N) - N // 2
            dy = ((by - ay + N // 2) % N) - N // 2
            steps = max(abs(dx), abs(dy))
            for s in range(steps):
                x = wrap(int(ax + dx * s / steps + math.sin(s * 0.07) * 3))
                y = wrap(int(ay + dy * s / steps + math.cos(s * 0.05) * 3))
                j = y * N + x
                if h[j] > SEA + 1 and kind[j] not in (2, 3, 6, 7, 8, 9):
                    kind[j] = 1

    # trees: broadleaf in the Isles, pines in the Peaks, palms and jungle
    # on the Ember Coast, cacti and shrubs in the Canyons
    progress('terrain: forests')
    forest = fbm(seed + 5, ((32, .6), (64, .4)))
    for y in range(0, N, 2):
        for x in range(0, N, 2):
            i = y * N + x
            if kind[i] or river[i]:
                continue
            g = region[i]
            if g == CITY:
                continue
            if g == CANYONS:
                if h[i] > SEA + 3 and r.random() < 0.05:
                    j = wrap(y + r.randrange(2)) * N + wrap(x + r.randrange(2))
                    if not kind[j]:
                        tall = r.random() < 0.4
                        h[j] += 3 if tall else 1.5
                        kind[j] = 12 if tall else 13
                continue
            top = SEA + 140 if g == PEAKS else SEA + 110
            if not (SEA + 4 < h[i] < top):
                continue
            need = 0.78 if g == ISLES else (0.7 if g == PEAKS else 0.66)
            if forest[i] + moist[i] * 0.4 < need or r.random() < 0.35:
                continue
            tx, ty = wrap(x + r.randrange(2)), wrap(y + r.randrange(2))
            j = ty * N + tx
            kd = {ISLES: 5, PEAKS: 10, EMBER: 11}[g]
            th = 3 + r.random() * 4 + (2 if g == PEAKS else 0)
            h[j] += th
            kind[j] = kd
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                k = wrap(ty + dy) * N + wrap(tx + dx)
                if not kind[k]:
                    h[k] += th * (0.4 if g == PEAKS else 0.55)
                    kind[k] = kd

    progress('terrain: light and shadows')
    sl = math.sqrt(sum(c * c for c in SUN))
    sun = tuple(c / sl for c in SUN)
    shadow = bytearray(N * N)
    sx, sy = sun[0], sun[2]
    step_h = sun[1] / math.hypot(sx, sy)
    for y in range(N):
        for x in range(N):
            i = y * N + x
            hh = h[i]
            if hh <= SEA:
                continue
            px, py = float(x), float(y)
            ray = hh
            for k in range(60):
                px += sx * 1.5
                py += sy * 1.5
                ray += step_h * 1.5 * 1.2
                if ray > 255:
                    break
                if h[wrap(int(py)) * N + wrap(int(px))] > ray:
                    shadow[i] = 1
                    break

    progress('terrain: colours')
    cols = [None] * (N * N)
    sand = [rgb('#d8c088'), rgb('#f0dca8')]
    grass = [rgb('#2e7a28'), rgb('#4aa03a'), rgb('#86c850')]
    grass_dry = [rgb('#6a8a30'), rgb('#a8b850'), rgb('#d8d070')]
    rock = [rgb('#5a5048'), rgb('#8a7e70'), rgb('#b8ac98')]
    snow = [rgb('#c8d0e8'), rgb('#ffffff')]
    tree = [rgb('#0e3a14'), rgb('#1e5a20'), rgb('#3a8a30')]
    pine = [rgb('#0a2a20'), rgb('#18402c'), rgb('#2a5a3a')]
    palm = [rgb('#2a8a20'), rgb('#4ab030'), rgb('#7ad040')]
    jungle = [rgb('#145a1c'), rgb('#2a7a24'), rgb('#4a9a30')]
    tundra = [rgb('#6a7a60'), rgb('#8a9a80'), rgb('#b0b8a8')]
    canyon = [rgb('#7a2e18'), rgb('#a8461e'), rgb('#c86a2c'), rgb('#e09048'), rgb('#f0c088')]
    black_sand = [rgb('#282428'), rgb('#4a4448')]
    basalt = [rgb('#2a2224'), rgb('#4a3c38'), rgb('#6a5a50')]
    field_cols = [rgb('#c8b040'), rgb('#88b040'), rgb('#a07a30'), rgb('#70a838'), rgb('#d8c860')]
    water_deep, water_mid, water_shallow = rgb('#0e2a6a'), rgb('#1e5aa8'), rgb('#3ab0c8')
    lagoon = rgb('#40e0d0')
    for i in range(N * N):
        x, y = i % N, i // N
        hh = h[i]
        g = region[i]
        dxh = h[y * N + wrap(x + 1)] - h[y * N + wrap(x - 1)]
        dyh = h[wrap(y + 1) * N + x] - h[wrap(y - 1) * N + x]
        nx, ny, nz = -dxh, 2.0, -dyh
        nl = math.sqrt(nx * nx + ny * ny + nz * nz)
        lit = (nx * sun[0] + ny * sun[1] + nz * sun[2]) / nl
        light = 0.55 + 0.55 * max(0.0, lit)
        if shadow[i]:
            light *= 0.62
        k = kind[i]
        sl_ = math.hypot(dxh, dyh) / 2
        dtl = detail[i]
        if hh <= SEA + 0.6 or river[i] and hh <= SEA + 4:
            depth = SEA - hh
            c = mix(water_shallow, water_mid, depth / 14)
            c = mix(c, water_deep, (depth - 14) / 30)
            if g == EMBER:
                c = mix(lagoon, c, depth / 10)
            if river[i]:
                c = mix(water_shallow, water_mid, 0.4)
            if g == PEAKS:
                # frozen lakes, the sea full of ice floes
                if depth < 10 or dtl > 0.58:
                    c = mix(rgb('#d8e8f8'), rgb('#a8c8e8'), dtl)
                else:
                    c = mix(c, rgb('#5a7aa0'), 0.4)
            near = any(h[wrap(y + dy) * N + wrap(x + dx)] > SEA + 0.6
                       for dx, dy in ((2, 0), (-2, 0), (0, 2), (0, -2)))
            if near and r.random() < 0.6 and g != PEAKS:
                c = mix(c, (240, 250, 255), 0.6)
            if r.random() < 0.006:
                c = mix(c, (220, 240, 255), 0.4)
            cols[i] = c
            continue
        glow = False
        if k == 1:
            c = mix(rgb('#a8865a'), rgb('#c8a878'), dtl)
            if g == CANYONS:
                c = mix(rgb('#c89060'), rgb('#e0b080'), dtl)
            if g == PEAKS:
                c = mix(c, (240, 240, 250), 0.5)
            if g == CITY:
                # asphalt, a pale lane mark here and there
                c = mix(rgb('#3a3a42'), rgb('#50505a'), dtl)
                if (x % 14 == 1 or y % 14 == 1) and (x + y) % 5 == 0:
                    c = rgb('#b8b8a8')
        elif k == 14:
            # a building's roof: its block picks the style
            bid = ((x // 14) * 7349 + (y // 14) * 523) % 97
            if bid < 22:
                c = mix(rgb('#8a8a92'), rgb('#a8a8b0'), dtl)      # concrete
            elif bid < 44:
                c = mix(rgb('#8a5038'), rgb('#aa6a48'), dtl)      # brick
            elif bid < 66:
                c = mix(rgb('#3a6a9a'), rgb('#5a9ac8'), dtl)      # glass
            elif bid < 82:
                c = mix(rgb('#d8d4c8'), rgb('#f0ece0'), dtl)      # white
            else:
                c = mix(rgb('#3a7a68'), rgb('#52a088'), dtl)      # teal
            if (x * 7 + y * 11) % 23 == 0:
                c = mix(c, (0, 0, 0), 0.45)                        # a vent
        elif k == 15:
            c = mix(rgb('#3a8a34'), rgb('#5aaa48'), dtl)
        elif k == 16:
            c = mix(rgb('#c0b8a8'), rgb('#d8d0c0'), dtl)
        elif k == 2:
            c = mix(rgb('#b02818'), rgb('#e85030'), (x % 3) / 2)
        elif k == 3:
            c = mix(rgb('#284898'), rgb('#4878d0'), (x % 3) / 2)
        elif k == 6:
            c = rgb('#e8e0d0')
        elif k == 7:
            # timber houses, snow on the roofs
            c = rgb('#f4f8ff') if (x + y) % 3 else rgb('#6a4430')
        elif k == 8:
            c = mix(rgb('#d89a68'), rgb('#e8b888'), (x * 7 + y * 3) % 5 / 4)
        elif k == 9:
            c = mix(rgb('#b08a40'), rgb('#d8b060'), (x + y) % 2)
        elif k == 5:
            c = ramp(tree, 0.3 + dtl * 0.7)
        elif k == 10:
            c = ramp(pine, 0.2 + dtl * 0.8)
            if hh > SEA + 60 and (x + y) % 2 == 0:
                c = mix(c, (240, 245, 255), 0.6)
        elif k == 11:
            c = ramp(palm, dtl) if (x * 3 + y) % 4 else ramp(jungle, dtl)
        elif k == 12:
            c = rgb('#3a7a30')
        elif k == 13:
            c = rgb('#7a8a40')
        elif k == 4:
            patch = (((x + 3) // 6) * 7 + ((y + 1) // 5) * 13) % len(field_cols)
            c = field_cols[patch]
            if (x + y) % 3 == 0:
                c = mix(c, (0, 0, 0), 0.08)
            if g == PEAKS:
                c = mix(c, (220, 230, 240), 0.35)
        elif g == CANYONS:
            # layered red rock; sand on the flats
            band = (hh * 0.33 + dtl * 2.5) % 1.0
            c = ramp(canyon, band)
            if sl_ < 1.2 and hh > SEA + 3:
                c = mix(c, rgb('#e8b878'), 0.55)
            if hh < SEA + 4:
                c = ramp(sand, dtl)
        elif g == PEAKS:
            if hh < SEA + 3:
                c = mix(rgb('#8a8a90'), rgb('#c0c4cc'), dtl)
            elif hh > SEA + 110 or (sl_ < 2 and hh > SEA + 75):
                c = ramp(snow, dtl)
                if sl_ > 6:
                    c = mix(c, ramp(rock, dtl), 0.6)
            elif sl_ > 6:
                c = ramp(rock, dtl)
            else:
                c = ramp(tundra, dtl)
                c = mix(c, (240, 245, 255), max(0.0, (hh - SEA - 50) / 70))
        elif g == EMBER:
            # the volcanoes: basalt, the crater's lava glowing, lava runs
            vd = None
            for (vx, vy, rad) in volcanoes:
                d = dist(x, y, vx, vy)
                if d < rad * 2.3:
                    vd = (d, rad)
                    break
            if vd and vd[0] < vd[1] * 0.28:
                c = mix(rgb('#ff4010'), rgb('#ffd040'), dtl)
                glow = True
            elif vd and vd[0] < vd[1] * 1.6:
                c = ramp(basalt, dtl)
                if ((x * 13 + y * 7) % 97) < 3 and r.random() < 0.5:
                    c = rgb('#ff6020')
                    glow = True
            elif hh < SEA + 4:
                c = ramp(black_sand, dtl)
            elif sl_ > 7:
                c = ramp(basalt, dtl)
            else:
                c = ramp(jungle, dtl * 0.8 + 0.1)
        elif g == CITY:
            c = mix(rgb('#9a9488'), rgb('#b0aa9c'), dtl)
            if hh < SEA + 4:
                c = ramp(sand, dtl)
        elif hh < SEA + 4:
            c = ramp(sand, dtl)
        elif sl_ > 7 or hh > SEA + 120:
            c = ramp(rock, dtl + (hh - SEA) / 400)
            if hh > SEA + 165 and sl_ < 9:
                c = ramp(snow, dtl)
        else:
            dry = 1 - moist[i]
            gr = ramp(grass, dtl * 0.8 + 0.1)
            c = mix(gr, ramp(grass_dry, dtl), dry * 0.8)
            if (hh - SEA) > 80:
                c = mix(c, ramp(rock, dtl), (hh - SEA - 80) / 40)
        if glow:
            cols[i] = c
        else:
            cols[i] = tuple(min(255.0, v * light) for v in c)
    heights = bytes(int(max(0, min(255, round(v)))) for v in h)
    return heights, cols, villages, houses, river, region, towers


if __name__ == '__main__':
    import sys
    from PIL import Image
    hs, cs, villages, houses, river, region, towers = build()
    img = Image.new('RGB', (N, N))
    img.putdata([tuple(int(v) for v in c) for c in cs])
    img.save(sys.argv[1] if len(sys.argv) > 1 else 'map.png')
    print('villages', villages)
