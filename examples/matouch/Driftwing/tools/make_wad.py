"""Builds driftwing.wad and refreshes the generated part of driftwing.sc.

    python make_wad.py [out.wad] [driftwing.sc]

The WAD ("PWAD", u32 count, u32 directory, 16-byte entries, as the other
matouch games'):

  PAL      256 colours of the ground (r g b bytes)
  HMAP     1024x1024 heights, a byte a cell               (compressed)
  CMAP     1024x1024 colours, a byte a cell into PAL      (compressed)
  MESH     the plane in 3D (sprites.mesh_lump)
  BALLOON, BIRD, LETTER   sprites                         (images)
  MINI     128x128 colours, one every 8 cells (the minimap) (compressed)
  RMAP     128x128 regions, the same                      (compressed)
  FONT     95 widths, then 8 row bytes a character
  TEXT     the story, zero-terminated strings
  ROUTES   u16 routes; per route: u16 gates, u16 letters, u16 balloons,
           u16 start x, u16 start y, u16 start altitude, u16 heading,
           u16 seconds, sky top r g b, sky bottom r g b, fog r g b, light
           (percent), each a u16; then the gates (x, y, altitude,
           heading), the letters (x, y, altitude), the balloons (x, y,
           altitude), u16 each

An image lump (as Lumenfall's): u16 width, height, frames, colours, 4 x u16
0, the colours (RGB565 byte-swapped; colour 0 transparent), a byte a
pixel, the frames one under the other. Compressed lumps: u32 size, then
tokens (t < 128: t+1 bytes follow; t >= 128: (t & 127) + 3 bytes from the
u16 distance back).
"""
import math
import os
import random
import struct
import sys

from PIL import Image

import font6
import sprites
import terrain

HERE = os.path.dirname(os.path.abspath(__file__))
N = terrain.N
KEY = 0x1FF8


def lz(data):
    out = bytearray(struct.pack('<I', len(data)))
    n = len(data)
    head = {}
    lit = bytearray()

    def flush():
        i = 0
        while i < len(lit):
            k = min(128, len(lit) - i)
            out.append(k - 1)
            out.extend(lit[i:i + k])
            i += k
        lit.clear()

    i = 0
    while i < n:
        best_len, best_d = 0, 0
        if i + 3 <= n:
            for p in reversed(head.get(bytes(data[i:i + 3]), [])[-24:]):
                d = i - p
                if d > 65535:
                    break
                l = 3
                while l < 130 and i + l < n and data[p + l] == data[i + l]:
                    l += 1
                if l > best_len:
                    best_len, best_d = l, d
                    if l == 130:
                        break
        if best_len >= 3:
            flush()
            out.append(128 + best_len - 3)
            out += struct.pack('<H', best_d)
            for k in range(best_len):
                if i + k + 3 <= n:
                    head.setdefault(bytes(data[i + k:i + k + 3]), []).append(i + k)
            i += best_len
        else:
            if i + 3 <= n:
                head.setdefault(bytes(data[i:i + 3]), []).append(i)
            lit.append(data[i])
            i += 1
    flush()
    return bytes(out)


def swap565(c):
    r, g, b = c[:3]
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    v = ((v & 0xFF) << 8) | (v >> 8)
    if v == KEY:
        v ^= 0x0100
    return v


def image_lump(frames):
    w, h = frames[0].size
    sheet = Image.new('RGBA', (w, h * len(frames)))
    for i, f in enumerate(frames):
        sheet.paste(f, (0, i * h))
    px = sheet.load()
    cols = {}
    for y in range(sheet.height):
        for x in range(w):
            p = px[x, y]
            if p[3] >= 128 and p[:3] not in cols:
                cols[p[:3]] = len(cols) + 1
    if len(cols) > 255:
        rgbimg = Image.new('RGB', sheet.size)
        rgbimg.paste(sheet.convert('RGB'))
        q = rgbimg.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        pal = q.getpalette()[:255 * 3]
        qp = q.load()
        palette = [tuple(pal[i * 3:i * 3 + 3]) for i in range(255)]
        index = lambda x, y: qp[x, y] + 1
    else:
        palette = list(cols)
        index = lambda x, y: cols[px[x, y][:3]]
    raw = bytearray(struct.pack('<8H', w, h, len(frames), len(palette) + 1, 0, 0, 0, 0))
    raw += struct.pack('<H', KEY)
    for c in palette:
        raw += struct.pack('<H', swap565(c))
    for y in range(sheet.height):
        for x in range(w):
            raw.append(index(x, y) if px[x, y][3] >= 128 else 0)
    return lz(bytes(raw))


def font_lump():
    widths = bytearray()
    rows = bytearray()
    for ch in range(32, 127):
        wd, g = font6.glyph(chr(ch))
        widths.append(wd + 1)
        for r in g:
            b = 0
            for i, v in enumerate(r[:8]):
                if v:
                    b |= 0x80 >> i
            rows.append(b)
    return bytes(widths + rows)


STORY = [
    "The Hundred Isles have no roads between them, only the wind.|"
    "Every day the courier planes of Driftwing Post carry the letters "
    "from village to village: over the Hundred Isles, the Red Canyons, "
    "the Frost Peaks, the Ember Coast where the volcanoes glow, and home "
    "to the lights of Posthaven, the city of the post itself.|"
    "Today the old courier is ill, and his little red plane is yours. "
    "Fly through the gates of each route before the time runs out, and "
    "gather the letters the wind has scattered on the way.",
    "The last letter lands in the last village, and the bells ring.|"
    "From now on the Hundred Isles have a new courier: you.|"
    "THE END - thank you for playing Driftwing",
]

# the four routes: name, time of day, sky top, sky bottom, fog, light
ROUTES = [
    ('Morning Post', (70, 130, 230), (255, 200, 150), (230, 190, 170), 108),
    ('The Frost Run', (40, 110, 240), (170, 220, 255), (190, 220, 245), 118),
    ('Canyon Sunset', (60, 40, 120), (255, 120, 80), (230, 120, 110), 92),
    ('Ember Night Mail', (6, 10, 40), (40, 50, 110), (30, 40, 90), 52),
    ('City Lights', (30, 22, 70), (255, 140, 90), (140, 90, 120), 80),
]


def text_lump():
    out = bytearray()
    offs = []
    for s in STORY:
        offs.append(len(out))
        out += s.encode('ascii') + b'\0'
    return bytes(out), offs


def ground_max(h, x, y, r):
    m = 0
    for dy in range(-r, r + 1, 2):
        for dx in range(-r, r + 1, 2):
            m = max(m, h[((y + dy) % N) * N + (x + dx) % N])
    return m


def make_routes(h, villages, seed=11):
    """Each route: a loop of gates over the isles, from village to village,
    high enough over what is under them, letters on the way."""
    r = random.Random(seed)
    routes = []
    for ri, (name, top, bot, fog, light) in enumerate(ROUTES):
        n_gates = 10 + ri * 2
        # each route in its region: the Isles, the Peaks, the Canyons, the
        # Ember Coast
        reg = (terrain.ISLES, terrain.PEAKS, terrain.CANYONS, terrain.EMBER, terrain.CITY)[ri]
        order = [(x, y) for (x, y, g) in villages if g == reg]
        r.shuffle(order)
        pts = []
        if reg == terrain.CITY:
            # a round over Posthaven: a ring of gates, above the towers
            n_gates = 12
            for k in range(n_gates):
                a = 2 * math.pi * k / n_gates
                rad = 40 + 18 * math.sin(k * 2.1)
                pts.append(((terrain.CITY_X + math.cos(a) * rad) % N, (terrain.CITY_Y + math.sin(a) * rad) % N))
            sx, sy = int(pts[0][0]), int((pts[0][1] + 30) % N)
        else:
            # a path through some villages, gates along it every ~45 cells
            sx, sy = order[0]
            cx, cy = float(sx), float(sy)
            targets = order[1:1 + 3 + ri]
            for tx, ty in targets + [order[0]]:
                dx = ((tx - cx + N / 2) % N) - N / 2
                dy = ((ty - cy + N / 2) % N) - N / 2
                dist = math.hypot(dx, dy)
                steps = max(1, int(dist / 45))
                for k in range(1, steps + 1):
                    pts.append(((cx + dx * k / steps) % N, (cy + dy * k / steps) % N))
                cx, cy = float(tx), float(ty)
        pts = pts[:n_gates] if len(pts) >= n_gates else pts
        gates = []
        prev = (sx, sy)
        for (gx, gy) in pts:
            gx, gy = int(gx), int(gy)
            alt = max(terrain.SEA + 22, ground_max(h, gx, gy, 6) + 18 + (ri == 1) * 6)
            dx = ((gx - prev[0] + N / 2) % N) - N / 2
            dy = ((gy - prev[1] + N / 2) % N) - N / 2
            heading = int(round(math.atan2(dx, dy) / (2 * math.pi) * 1024)) & 1023
            gates.append((gx, gy, min(250, alt), heading))
            prev = (gx, gy)
        letters = []
        last = (sx, sy, gates[0][2])
        for (gx, gy, ga, _) in gates:
            for k in (1, 2, 3):
                t = k / 4
                dx = ((gx - last[0] + N / 2) % N) - N / 2
                dy = ((gy - last[1] + N / 2) % N) - N / 2
                lx = int((last[0] + dx * t + r.uniform(-6, 6)) % N)
                ly = int((last[1] + dy * t + r.uniform(-6, 6)) % N)
                la = int(max(ground_max(h, lx, ly, 3) + 10, last[2] + (ga - last[2]) * t + r.uniform(-6, 6)))
                letters.append((lx, ly, min(250, la)))
            last = (gx, gy, ga)
        balloons = []
        while len(balloons) < 10:
            gx, gy = gates[r.randrange(len(gates))][:2]
            bx, by = (gx + r.randrange(-40, 41)) % N, (gy + r.randrange(-40, 41)) % N
            balloons.append((bx, by, min(250, ground_max(h, bx, by, 4) + 30 + r.randrange(40))))
        first = gates[0]
        dx = ((first[0] - sx + N / 2) % N) - N / 2
        dy = ((first[1] - sy + N / 2) % N) - N / 2
        heading = int(round(math.atan2(dx, dy) / (2 * math.pi) * 1024)) & 1023
        start_alt = max(terrain.SEA + 30, ground_max(h, sx, sy, 8) + 25)
        seconds = 14 + len(gates) * 2
        routes.append(dict(name=name, gates=gates, letters=letters, balloons=balloons, start=(sx, sy, start_alt, heading),
                           seconds=seconds, sky=(top, bot), fog=fog, light=light))
    return routes


def routes_lump(routes):
    out = bytearray(struct.pack('<H', len(routes)))
    for rt in routes:
        sx, sy, sa, hd = rt['start']
        out += struct.pack('<8H', len(rt['gates']), len(rt['letters']), len(rt['balloons']), sx, sy, sa, hd,
                           rt['seconds'])
        out += struct.pack('<10H', *rt['sky'][0], *rt['sky'][1], *rt['fog'], rt['light'])
        for g in rt['gates']:
            out += struct.pack('<4H', *g)
        for l in rt['letters']:
            out += struct.pack('<3H', *l)
        for b in rt['balloons']:
            out += struct.pack('<3H', *b)
    return bytes(out)


def build(out_path, sc_path):
    heights, cols, villages, houses, river, region, towers = terrain.build()
    img = Image.new('RGB', (N, N))
    img.putdata([tuple(int(v) for v in c) for c in cols])
    q = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = bytes(q.getpalette()[:768])
    cmap = bytes(q.getdata())
    routes = make_routes(heights, villages)
    lumps = [('PAL', pal), ('HMAP', lz(heights)), ('CMAP', lz(cmap)),
             ('MESH', sprites.mesh_lump(sprites.plane_mesh())[0]),
             ('BALLOON', image_lump([sprites.balloon()])),
             ('BIRD', image_lump([sprites.bird(0), sprites.bird(1)])),
             ('LETTER', image_lump([sprites.letter(0), sprites.letter(1)])),
             ('FONT', font_lump())]
    # the minimap: a colour every 8 cells (an index into PAL), and the
    # regions the same (the one most of the 8x8 cells are in)
    mini = bytearray(128 * 128)
    rmap = bytearray(128 * 128)
    for y in range(128):
        for x in range(128):
            mini[y * 128 + x] = cmap[(y * 8 + 4) * N + x * 8 + 4]
            cnt = [0, 0, 0, 0, 0]
            for yy in range(0, 8, 2):
                for xx in range(0, 8, 2):
                    cnt[region[(y * 8 + yy) * N + x * 8 + xx]] += 1
            rmap[y * 128 + x] = cnt.index(max(cnt))
    lumps.append(('CITY', struct.pack('<H', len(towers)) + b''.join(struct.pack('<3H', *t) for t in towers)))
    lumps.append(('MINI', lz(bytes(mini))))
    lumps.append(('RMAP', lz(bytes(rmap))))
    tdata, toffs = text_lump()
    lumps.append(('TEXT', tdata))
    lumps.append(('ROUTES', routes_lump(routes)))
    body = bytearray()
    entries = []
    pos = 12
    for name, data in lumps:
        entries.append((pos, len(data), name))
        body += data
        pos += len(data)
        while pos % 4:
            body.append(0)
            pos += 1
    out = bytearray(b'PWAD' + struct.pack('<II', len(lumps), pos)) + body
    for p, n, name in entries:
        out += struct.pack('<II', p, n) + name.encode().ljust(8, b'\0')
    open(out_path, 'wb').write(out)
    print('%s: %d bytes' % (out_path, len(out)))
    for name, data in lumps:
        print('  %-8s %7d' % (name, len(data)))
    for rt in routes:
        print('  route %-18s %2d gates %2d letters %3d s' % (rt['name'], len(rt['gates']), len(rt['letters']),
                                                            rt['seconds']))
    img.save(os.path.join(os.path.dirname(out_path), 'map_preview.png'))

    gen = ['// --- generated by tools/make_wad.py: do not edit by hand ---',
           '#define MAPN %d' % N, '#define SEA %d' % terrain.SEA, '#define NROUTES %d' % len(routes),
           '#define STORY_INTRO %d' % toffs[0], '#define STORY_END %d' % toffs[1],
           '#define KEY 0x%04X' % KEY,
           '#define CITY_X %d' % terrain.CITY_X, '#define CITY_Y %d' % terrain.CITY_Y,
           '#define CITY_R %d' % terrain.CITY_R, '// --- end of the generated part ---']
    if sc_path and os.path.exists(sc_path):
        sc = open(sc_path).read()
        a = sc.index('// --- generated by tools/make_wad.py')
        b = sc.index('// --- end of the generated part ---') + len('// --- end of the generated part ---')
        open(sc_path, 'w').write(sc[:a] + '\n'.join(gen) + sc[b:])
        print('%s: generated part refreshed' % sc_path)
    else:
        print('\n'.join(gen))
    return routes


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'data', 'driftwing.wad')
    sc = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'driftwing.sc')
    build(out, sc)
