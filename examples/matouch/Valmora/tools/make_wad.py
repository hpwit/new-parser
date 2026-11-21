"""Builds valmora.wad, the data next to valmora.sc, and the #define block the
script uses (between its "generated" markers).

    python make_wad.py ../data/valmora.wad valmora.sc [preview-dir]
    python make_wad.py --low valmoralow.wad valmoralow.sc

--low: Valmora Low, for SolarOS at 400x240: 16x16 tiles (the same art made
smaller), a font of 8 rows, and valmoralow.sc written from valmora.sc with
the pixel size taken from the command line (asm run valmoralow.bin 1 or 2).

The WAD is a PWAD of named lumps (asm reads it next to the script, wadFind()
gives where a lump starts):

  TILES  u16 count, then per tile: u8 flags (1 = colour 0 transparent),
         u8 size (32, or 16 for Valmora Low), 16 x u16 colours (RGB565, byte-swapped for the panel, each
         one of the 4096 colours of 4 bits a channel), size*size/2 bytes of
         4-bit pixels (left pixel in the high nibble)
  TPROP  u8 per tile: 1 blocks, 2 burns, 4 water, 8 lights the dark,
         zone << 4 (what roams there)
  TANIM  u8 per tile: frames (1 = still; the frames follow the first)
  FONT   u8 height, u8 first, u8 count, then per character u8 width,
         u16 offset in the lump; 2 bits a pixel (4 shades), 4 pixels a byte
  LOGO   u16 width, u16 height, 16 x u16 colours, 4-bit pixels
  TEXT   zero-terminated strings; \n a line, \f a page
  MAP00..MAP11  u8 w, u8 h, u8 kind (0 world, 1 town, 2 dungeon), u8 floor,
         u8 light, u16 name, u8 exit map, u8 exit x, u8 exit y, u8 entry x,
         u8 entry y, u8 objects, 3 x u8 0; w*h tiles; objects of 6 bytes:
         type, x, y, a, b, c
  NPCS   u8 count; per person 12 bytes: map, x, y, sprite, role, wander,
         u16 name, u8 dialogue, u8 p1, u8 p2, u8 0
  DLG    u8 count; per dialogue: u8 variants, then (u8 min shards, u16 text)
  MONST  u8 count; per monster 16 bytes: sprite, u16 hp, attack, defence,
         u16 xp, u16 gold, u16 name, 5 x 0
  ITEMS  10 x (u16 name, u8 power, u16 price): weapons then armours
"""
import heapq
import math
import os
import random
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

import art
import content as C
import font6

KEY = 0x0008  # never a colour: RGB565 with red 1 is out of the 4096


# ------------------------------------------------------------ colours

def c12(c):
    return tuple(min(15, int(round(v / 17.0))) for v in c[:3])


def sw565(c4):
    r, g, b = c4
    r5 = (r << 1) | (r >> 3)
    g6 = (g << 2) | (g >> 2)
    b5 = (b << 1) | (b >> 3)
    v = (r5 << 11) | (g6 << 5) | b5
    return ((v & 255) << 8) | (v >> 8)


def from12(c4):
    return tuple(v * 17 for v in c4)


def quantize(img, ncol):
    """Pixels (list) of an RGB/RGBA tile reduced to ncol colours of 4096:
    returns (palette of 12-bit colours, index per pixel or -1 transparent)."""
    rgba = img.convert('RGBA')
    px = list(rgba.get_flattened_data())
    solid = [from12(c12(p)) for p in px if p[3] >= 128]
    distinct = sorted(set(solid))
    if len(distinct) <= ncol:
        pal = [c12(c) for c in distinct]
    else:
        strip = Image.new('RGB', (len(solid), 1))
        strip.putdata(solid)
        q = strip.quantize(colors=ncol, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        raw = q.getpalette()[:ncol * 3]
        pal = []
        for i in range(ncol):
            c = c12(raw[i * 3:i * 3 + 3])
            if c not in pal:
                pal.append(c)
    full = [from12(c) for c in pal]
    cache = {}
    out = []
    for p in px:
        if p[3] < 128:
            out.append(-1)
            continue
        k = c12(p)
        if k not in cache:
            c = from12(k)
            cache[k] = min(range(len(full)),
                           key=lambda i: sum((full[i][j] - c[j]) ** 2 * (2, 3, 1)[j] for j in range(3)))
        out.append(cache[k])
    return pal, out


def pack_picture(img, transparent):
    """(flags+palette+pixels bytes, preview RGBA image) for a 4-bit picture."""
    pal, idx = quantize(img, 15 if transparent else 16)
    if transparent:
        pal = [(0, 0, 0)] + pal
        idx = [0 if i < 0 else i + 1 for i in idx]
    pal = pal + [(0, 0, 0)] * (16 - len(pal))
    w, h = img.size
    data = bytearray()
    for i in range(0, w * h, 2):
        data.append((idx[i] << 4) | idx[i + 1])
    cols = b''.join(struct.pack('<H', sw565(c)) for c in pal)
    prev = Image.new('RGBA', (w, h))
    prev.putdata([(0, 0, 0, 0) if (transparent and i == 0) else from12(pal[i]) + (255,) for i in idx])
    return cols, bytes(data), prev


# ------------------------------------------------------------ tiles

class Tiles:
    def __init__(self):
        self.names = []       # one per stored picture
        self.images = []
        self.flags = []
        self.frames = []
        self.index = {}       # base name -> first index

    def add(self, name, img, transparent=False):
        base, _, frame = name.partition('#')
        i = len(self.names)
        if not frame or frame == '0':
            self.index[base] = i
            self.frames.append(1)
        else:
            self.frames.append(0)
            self.frames[self.index[base]] += 1
        self.names.append(name)
        self.images.append(img)
        self.flags.append(1 if transparent else 0)
        return i

    def __getitem__(self, base):
        return self.index[base]


def build_tiles():
    t = Tiles()
    for n, im in art.make_terrain():
        t.add(n, im)
    for n, im in art.make_overlays():
        t.add(n, im, True)
    for n, fs in art.make_creatures():
        t.add('spr_' + n, fs[0], True)
        t.add('spr_' + n + '#1', fs[1], True)
    return t


BLOCK = {'shallow', 'deep', 'mountain', 'mountain2', 'peak', 'lava', 'gate', 'brick', 'brickwin',
         'stonewall', 'torch', 'woodwall', 'woodwin', 'counter', 'table', 'bed', 'barrel', 'crate', 'shelf',
         'anvil', 'throne', 'well', 'fountain', 'flowerbed', 'hedge', 'fence', 'tree', 'sign_weapon',
         'sign_armor', 'sign_potion', 'sign_inn', 'sign_heal', 'pillar', 'cavewall', 'cryptwall', 'minewall',
         'spirewall', 'chest', 'chest_open', 'rubble', 'altar', 'altar_empty', 'pillar_d', 'void'}
ZONE = {'grass0': 1, 'grass1': 1, 'grass2': 1, 'meadow': 1, 'brush': 1, 'sand': 1, 'forest': 2, 'forest2': 2,
        'pines': 2, 'hills': 3, 'swamp': 4, 'ash': 5}


def tile_props(t):
    props = []
    for n in t.names:
        base = n.partition('#')[0]
        p = 0
        if base in BLOCK:
            p |= 1
        if base == 'lava':
            p |= 2
        if base in ('shallow', 'deep'):
            p |= 4
        if base in ('torch', 'lava'):
            p |= 8
        if base.startswith('shore'):
            p |= 1 << 4
        p |= ZONE.get(base, 0) << 4
        props.append(p)
    return props


# ------------------------------------------------------------ the world

def make_world(t):
    rows = [r for r in C.WORLD.strip('\n').split('\n')]
    W, H = 48, 40
    rows = [(r + '~' * W)[:W] for r in rows][:H]
    rnd = random.Random(7)
    kind = [[rows[y // 2][x // 2] for x in range(W * 2)] for y in range(H * 2)]
    # organic edges: a cell on a border sometimes takes its neighbour's ground
    fixed = [row[:] for row in kind]
    for y in range(1, H * 2 - 1):
        for x in range(1, W * 2 - 1):
            ns = [kind[y + dy][x + dx] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))]
            other = [n for n in ns if n != kind[y][x]]
            if other and rnd.random() < .3:
                fixed[y][x] = rnd.choice(other)
    kind = fixed
    w, h = W * 2, H * 2
    water = lambda k: k in '~r'
    # the sea is shallow near the land
    for y in range(h):
        for x in range(w):
            if kind[y][x] == '~':
                near = any(0 <= x + dx < w and 0 <= y + dy < h and not water(kind[y + dy][x + dx])
                           for dx in range(-2, 3) for dy in range(-2, 3))
                if near:
                    kind[y][x] = 'r'
    pick = {
        '.': ['grass0'] * 5 + ['grass1'] * 4 + ['grass2'] * 2 + ['brush'],
        'o': ['meadow'] * 3 + ['grass2'],
        'f': ['forest', 'forest2', 'forest'],
        'F': ['pines'],
        'h': ['hills'] * 5 + ['grass1'],
        'm': ['mountain', 'mountain2'],
        'M': ['peak', 'peak', 'mountain'],
        's': ['sand'],
        'w': ['swamp'] * 4 + ['grass1'],
        'l': ['lava'],
        'a': ['ash'],
        'r': ['shallow'],
        '~': ['deep'],
    }
    grid = [[pick[kind[y][x]][rnd.randrange(len(pick[kind[y][x]]))] for x in range(w)] for y in range(h)]

    places = {}
    for name, ((x, y), tile, target) in C.PLACES.items():
        places[name] = (x, y)
    # spire: in the lava, a causeway south to the gate at the lava's edge
    sx, sy = places['spire']
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            grid[sy + dy][sx + dx] = 'lava'
    y = sy + 1
    while grid[y][sx] == 'lava' or grid[y + 1][sx] == 'lava':
        grid[y][sx] = 'causeway'
        grid[y][sx - 1] = grid[y][sx - 1] if grid[y][sx - 1] != 'causeway' else 'lava'
        y += 1
    gate = (sx, y - 1)
    grid[y - 1][sx] = 'gate'
    for yy in range(sy + 1, y - 1):
        for xx in (sx - 1, sx + 1):
            grid[yy][xx] = 'lava'
    places['gate'] = (sx, y)          # the road ends below the gate
    for name, ((x, y), tile, target) in C.PLACES.items():
        if name != 'spire':
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if grid[y + dy][x + dx] in BLOCK or grid[y + dy][x + dx] in ('swamp',):
                        if name != 'mines' and name != 'crypt':
                            grid[y + dy][x + dx] = 'grass0'
        grid[y][x] = tile
    # the mine's door must be reachable: hills around it
    mx, my = places['mines']
    for dy in (1, 2):
        for dx in (-1, 0, 1):
            if grid[my + dy][mx + dx] in BLOCK:
                grid[my + dy][mx + dx] = 'hills'
    # roads
    cost = {'grass0': 1, 'grass1': 1, 'grass2': 1, 'meadow': 1.2, 'brush': 1.5, 'sand': 1.4, 'hills': 4,
            'forest': 4, 'forest2': 4, 'pines': 5, 'swamp': 5, 'ash': 3, 'shallow': 14}

    def route(a, b):
        start, goal = places[a], places[b]
        dist = {start: 0}
        prev = {}
        q = [(0, start)]
        while q:
            d, p = heapq.heappop(q)
            if p == goal:
                break
            if d > dist[p]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (p[0] + dx, p[1] + dy)
                if not (0 < n[0] < w - 1 and 0 < n[1] < h - 1):
                    continue
                g = grid[n[1]][n[0]]
                if n != goal and g not in cost and not g.startswith('road'):
                    continue
                c = cost.get(g, .5)
                if g == 'shallow':  # a bridge only across a river: land both sides
                    if dy == 0 and not (grid[n[1]][n[0] + dx] not in ('shallow', 'deep')
                                        or grid[n[1]][n[0] + dx] == 'shallow'):
                        continue
                nd = d + c
                if nd < dist.get(n, 1e9):
                    dist[n] = nd
                    prev[n] = p
                    heapq.heappush(q, (nd, n))
        path = []
        p = goal
        while p != start:
            path.append(p)
            p = prev[p]
        return path[1:]

    road = set()
    bridge = set()
    for a, b in C.ROADS:
        for (x, y) in route(a, b):
            if grid[y][x] == 'shallow':
                bridge.add((x, y))
            elif grid[y][x] not in ('village', 'castle', 'port', 'gate', 'causeway'):
                road.add((x, y))
            grid[y][x] = 'roadX' if (x, y) in road else grid[y][x]
    gx, gy = places['gate']
    road.add((gx, gy))
    links = road | bridge | {places[n] for n in places} | {gate}
    for (x, y) in road:
        m = 0
        for bit, (dx, dy) in ((1, (0, -1)), (2, (1, 0)), (4, (0, 1)), (8, (-1, 0))):
            if (x + dx, y + dy) in links:
                m |= bit
        grid[y][x] = 'road%d' % m
    for (x, y) in bridge:
        grid[y][x] = 'bridgeh' if (x - 1, y) in links or (x + 1, y) in links else 'bridgev'
    # beaches where grass meets water
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            if grid[y][x] in ('grass0', 'grass1', 'grass2', 'meadow', 'brush', 'sand'):
                m = 0
                for bit, (dx, dy) in ((1, (0, -1)), (2, (1, 0)), (4, (0, 1)), (8, (-1, 0))):
                    if grid[y + dy][x + dx] in ('shallow', 'deep'):
                        m |= bit
                if m:
                    grid[y][x] = 'shore%d' % m
    objs = []
    for name, ((x, y), tile, target) in C.PLACES.items():
        objs.append((6, x, y, target, 0, 0))
    tiles = bytes(t[g] for row in grid for g in row)
    return w, h, tiles, objs, grid, places, gate


def reachable(grid, start, extra_ok=()):
    h, w = len(grid), len(grid[0])
    seen = {start}
    st = [start]
    while st:
        x, y = st.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + dx, y + dy)
            if 0 <= n[0] < w and 0 <= n[1] < h and n not in seen:
                g = grid[n[1]][n[0]]
                if g not in BLOCK or g in extra_ok:
                    seen.add(n)
                    st.append(n)
    return seen


# ------------------------------------------------------------ towns

def make_town(t, mid, name, text, people, rnd):
    rows = text.strip('\n').split('\n')
    w = max(len(r) for r in rows)
    # a short row is padded with its last ground before its edge
    rows = [r[:-1] + r[-2] * (w - len(r)) + r[-1] for r in rows]
    h = len(rows)
    grid = []
    entry = None
    persons = []
    for y, r in enumerate(rows):
        row = []
        for x, ch in enumerate(r):
            if ch in C.TOWN_TILES and C.TOWN_TILES[ch] is not None:
                tile = C.TOWN_TILES[ch]
            else:
                if ch == '@':
                    entry = (x, y)
                elif ch in people:
                    persons.append((ch, x, y))
                else:
                    print('  %s: unknown %r at %d,%d' % (name, ch, x, y))
                tile = None
            if tile == 'grass':
                tile = rnd.choice(['grass0', 'grass0', 'grass1', 'grass2'])
            if tile == 'cobble' and rnd.random() < .3:
                tile = 'cobble2'
            row.append(tile)
        grid.append(row)
    # a person stands on the floor around them
    for y in range(h):
        for x in range(w):
            if grid[y][x] is None:
                ns = [grid[y + dy][x + dx] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
                      if 0 <= x + dx < w and 0 <= y + dy < h and grid[y + dy][x + dx] not in BLOCK
                      and grid[y + dy][x + dx] is not None and 'door' not in grid[y + dy][x + dx]]
                grid[y][x] = max(sorted(set(ns)), key=ns.count) if ns else 'cobble'
    seen = reachable(grid, entry)
    for ch, x, y in persons:
        ok = (x, y) in seen or any((x + dx, y + dy) in seen for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2))
        if not ok:
            print('  %s: %s at %d,%d cannot be reached' % (name, people[ch][0], x, y))
    return w, h, grid, entry, persons


# ------------------------------------------------------------ dungeons

LOOKS = {'cave': ('cavefloor', 'cavewall', 5), 'crypt': ('cryptfloor', 'cryptwall', 4),
         'mine': ('minefloor', 'minewall', 5), 'spire': ('spirefloor', 'spirewall', 6)}


def make_dungeon(mid, look, floor_no, kinds, boss, rnd, special):
    w, h = 40, 30
    fl, wl, light = LOOKS[look]
    g = [[wl] * w for _ in range(h)]
    rooms = []
    tries = 0
    while len(rooms) < 9 and tries < 400:
        tries += 1
        rw, rh = rnd.randint(4, 8), rnd.randint(4, 6)
        rx, ry = rnd.randint(1, w - rw - 2), rnd.randint(1, h - rh - 2)
        if any(rx < a + aw + 2 and a < rx + rw + 2 and ry < b + bh + 2 and b < ry + rh + 2 for a, b, aw, bh in rooms):
            continue
        rooms.append((rx, ry, rw, rh))
    rooms.sort(key=lambda r: (r[0] + r[2] / 2) + (r[1] + r[3] / 2) * .7)
    if boss:  # a big room for the last fight, at the end
        last = rooms[-1]
        rx = max(1, min(w - 12, last[0] - 1))
        ry = max(1, min(h - 10, last[1] - 1))
        rooms[-1] = (rx, ry, 10, 8)
    for rx, ry, rw, rh in rooms:
        for y in range(ry, ry + rh):
            for x in range(rx, rx + rw):
                g[y][x] = fl
    centre = lambda r: (r[0] + r[2] // 2, r[1] + r[3] // 2)
    for a, b in zip(rooms, rooms[1:]):
        (x0, y0), (x1, y1) = centre(a), centre(b)
        if rnd.random() < .5:
            for x in range(min(x0, x1), max(x0, x1) + 1):
                g[y0][x] = fl
            for y in range(min(y0, y1), max(y0, y1) + 1):
                g[y][x1] = fl
        else:
            for y in range(min(y0, y1), max(y0, y1) + 1):
                g[y][x0] = fl
            for x in range(min(x0, x1), max(x0, x1) + 1):
                g[y1][x] = fl
    # a few loops
    for _ in range(2):
        a, b = rnd.sample(rooms, 2)
        (x0, y0), (x1, y1) = centre(a), centre(b)
        for x in range(min(x0, x1), max(x0, x1) + 1):
            g[y0][x] = fl
        for y in range(min(y0, y1), max(y0, y1) + 1):
            g[y][x1] = fl
    # walls nobody can see become nothing
    out = [[None] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if g[y][x] == wl:
                near = any(0 <= x + dx < w and 0 <= y + dy < h and g[y + dy][x + dx] == fl
                           for dx in (-1, 0, 1) for dy in (-1, 0, 1))
                out[y][x] = wl if near else 'void'
            else:
                out[y][x] = fl
    objs = []
    up = centre(rooms[0])
    out[up[1]][up[0]] = 'stairs_up'
    last = rooms[-1]
    lc = centre(last)
    if boss:
        out[last[1] + 1][lc[0]] = 'altar'
        objs.append((5, lc[0], last[1] + 1, 0, boss[1], 0))
        objs.append((4, lc[0], last[1] + 4, boss[0], boss[1], 0))
        for x in (last[0] + 1, last[0] + last[2] - 2):
            for y in (last[1] + 1, last[1] + last[3] - 2):
                out[y][x] = 'pillar_d'
    else:
        out[lc[1]][lc[0]] = 'stairs_down'
    # chests in corners of middle rooms, monsters around
    chests = 0
    for r in rooms[1:-1]:
        if rnd.random() < .7:
            x, y = r[0] + rnd.choice((0, r[2] - 1)), r[1] + rnd.choice((0, r[3] - 1))
            if out[y][x] == fl:
                out[y][x] = 'chest'
                if special and chests == 0:
                    kind, amount = special
                    objs.append((2, x, y, 2 if kind == 'weapon' else 3, amount, 0))
                elif rnd.random() < .45:
                    objs.append((2, x, y, 1, rnd.randint(1, 2), 0))
                else:
                    objs.append((2, x, y, 0, rnd.randint(15, 40) * (1 + mid // 3), 0))
                chests += 1
        for _ in range(rnd.randint(1, 3)):
            x, y = rnd.randint(r[0], r[0] + r[2] - 1), rnd.randint(r[1], r[1] + r[3] - 1)
            if out[y][x] == fl and not any(o[1] == x and o[2] == y for o in objs):
                objs.append((1, x, y, rnd.choice(kinds), 0, 0))
    for _ in range(8):  # decoration
        x, y = rnd.randrange(w), rnd.randrange(h)
        if out[y][x] == fl and not any(o[1] == x and o[2] == y for o in objs):
            out[y][x] = rnd.choice(['bones', 'web', 'bones', 'rubble'] if look != 'spire' else ['bones', 'web'])
    return w, h, out, objs, up, light, fl


# ------------------------------------------------------------ font and logo

def make_font(size=15, H=16, dy=-3, crisp=0):
    """crisp > 0: every pixel on or off (on from that much ink of 255),
    for the small font shown at pixel size 2, where grey edges blur."""
    font = ImageFont.load_default(size)
    widths, glyphs = [], []
    for ch in range(32, 127):
        c = chr(ch)
        adv = int(round(font.getlength(c))) + (1 if c != ' ' else 0)
        adv = max(adv, 3)
        im = Image.new('L', (adv + 2, H), 0)
        ImageDraw.Draw(im).text((0, dy), c, font=font, fill=255)
        im = im.crop((0, 0, adv, H))
        widths.append(adv)
        glyphs.append(im)
    data = bytearray()
    table = bytearray()
    base = 3 + 95 * 3
    for adv, im in zip(widths, glyphs):
        table += struct.pack('<BH', adv, base + len(data))
        px = im.load()
        for y in range(H):
            bits = []
            for x in range(adv):
                v = px[x, y]
                if crisp:
                    bits.append(3 if v >= crisp else 0)
                else:
                    bits.append(0 if v < 48 else 1 if v < 120 else 2 if v < 190 else 3)
            while len(bits) % 4:
                bits.append(0)
            for i in range(0, len(bits), 4):
                data.append((bits[i] << 6) | (bits[i + 1] << 4) | (bits[i + 2] << 2) | bits[i + 3])
    lump = struct.pack('<BBB', H, 32, 95) + table + data
    return lump, widths, font


def make_pixel_font():
    """Valmora Low's font (font6.py): 8 rows, each pixel on or off."""
    H = 8
    widths, rows_of = [], []
    for ch in range(32, 127):
        w, rows = font6.glyph(chr(ch))
        adv = w + 1                      # a column of space after it
        widths.append(adv)
        rows_of.append([r + [0] for r in rows])
    data = bytearray()
    table = bytearray()
    base = 3 + 95 * 3
    for adv, rows in zip(widths, rows_of):
        table += struct.pack('<BH', adv, base + len(data))
        for r in rows:
            bits = [3 if v else 0 for v in r]
            while len(bits) % 4:
                bits.append(0)
            for i in range(0, len(bits), 4):
                data.append((bits[i] << 6) | (bits[i + 1] << 4) | (bits[i + 2] << 2) | bits[i + 3])
    return struct.pack('<BBB', H, 32, 95) + table + data, widths, None


def wrap(text, widths, maxw):
    """Pages of lines, wrapped with the WAD font's widths."""
    pages = []
    for page in text.split('|'):
        lines, line = [], ''
        for word in page.split():
            t = (line + ' ' + word) if line else word
            if sum(widths[ord(c) - 32] for c in t) > maxw and line:
                lines.append(line)
                line = word
            else:
                line = t
        if line:
            lines.append(line)
        pages.append('\n'.join(lines))
    return '\f'.join(pages)


def make_logo():
    font = ImageFont.load_default(96)
    text = 'VALMORA'
    w = int(font.getlength(text)) + 24
    h = 120
    mask = Image.new('L', (w, h), 0)
    ImageDraw.Draw(mask).text((12, 4), text, font=font, fill=255, stroke_width=3, stroke_fill=255)
    inner = Image.new('L', (w, h), 0)
    ImageDraw.Draw(inner).text((12, 4), text, font=font, fill=255, stroke_width=1, stroke_fill=255)
    bb = mask.getbbox()
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    px = img.load()
    mp, ip = mask.load(), inner.load()
    top, bot = bb[1], bb[3]
    gold = [art.rgb('#fff6c0'), art.rgb('#ffd860'), art.rgb('#e8a020'), art.rgb('#a85a10')]
    for y in range(h):
        for x in range(w):
            if mp[x, y] > 100:
                if ip[x, y] > 100:
                    t = (y - top) / max(1, bot - top)
                    c = art.ramp(gold, t)
                    if (x + y) % 7 == 0 and t < .4:
                        c = art.mix(c, (255, 255, 255), .5)
                    px[x, y] = c + (255,)
                else:
                    px[x, y] = (40, 16, 8, 255)
    img = img.crop(bb)
    if img.size[0] % 2:
        img = img.crop((0, 0, img.size[0] + 1, img.size[1]))
    return img


# ------------------------------------------------------------ packing

class Text:
    def __init__(self):
        self.data = bytearray()
        self.at = {}

    def add(self, s):
        if s in self.at:
            return self.at[s]
        o = len(self.data)
        self.data += s.encode('latin-1') + b'\0'
        self.at[s] = o
        return o


def wad(lumps):
    out = bytearray(b'PWAD' + struct.pack('<ii', len(lumps), 0))
    dirs = []
    for name, data in lumps:
        while len(out) % 4:
            out.append(0)
        dirs.append((len(out), len(data), name))
        out += data
    while len(out) % 4:
        out.append(0)
    d = len(out)
    for o, n, name in dirs:
        out += struct.pack('<ii8s', o, n, name.encode())
    struct.pack_into('<i', out, 8, d)
    return bytes(out)


LOW_PIXEL_SIZE = """   // --- the pixel size (make_wad.py --low changes these lines) ---
   // Valmora Low: asm run valmoralow.bin 2 fills the panel with its 400x240
   // (the default), 1 shows them as they are, in the middle of it
   pixelSize = 2;
   if (argc >= 1 && arg(0) == 1)
      pixelSize = 1;
   // --- end of the pixel size ---"""


def main():
    low = '--low' in sys.argv
    args = [a for a in sys.argv[1:] if a != '--low']
    out_wad, out_sc = args[0], args[1]
    prev_dir = args[2] if len(args) > 2 and not low else None
    size = 16 if low else 32
    if low:
        art.SPR = 16
    rnd = random.Random(1987)
    t = build_tiles()
    t.add('void', Image.new('RGB', (32, 32), (0, 0, 0)))
    t.add('pillar_d', art.overlay('pillar'), True)
    props = tile_props(t)
    T = t.index
    text = Text()
    font_lump, widths, _ = make_pixel_font() if low else make_font()
    if low:
        # the layout is in 800x480 coordinates: the small font counts double
        widths = [w * 2 for w in widths]

    tile_data = bytearray(struct.pack('<H', len(t.names)))
    previews = []
    for img, fl in zip(t.images, t.flags):
        if img.size[0] != size:
            img = img.resize((size, size), Image.LANCZOS)
        cols, pix, pv = pack_picture(img, fl == 1)
        tile_data += struct.pack('<BB', fl, size) + cols + pix
        previews.append(pv)

    maps = {}
    # the world
    w, h, tiles, objs, grid, places, gate = make_world(t)
    seen = reachable(grid, places['brindle'], extra_ok=('village', 'castle', 'port', 'cave', 'crypt', 'mine'))
    for n, p in places.items():
        if n not in ('spire',) and p not in seen:
            print('  world: %s at %s cannot be reached' % (n, p))
    maps[0] = dict(w=w, h=h, kind=0, floor=T['grass0'], light=0, name=text.add('Valmora'), exit=(0, 0, 0),
                   entry=places['brindle'], tiles=tiles, objs=objs, grid=grid)
    world_places = {n: p for n, p in places.items()}
    # towns
    npcs = []
    dialog_ids = {}
    town_rnd = random.Random(5)
    for mid, name, txt, people in C.TOWNS:
        tw, th, grid, entry, persons = make_town(t, mid, name, txt, people, town_rnd)
        place = [n for n, (p, tl, m) in C.PLACES.items() if m == mid][0]
        px_, py_ = world_places[place]
        maps[mid] = dict(w=tw, h=th, kind=1, floor=T['cobble'], light=0, name=text.add(name),
                         exit=(0, px_, py_), entry=entry, tiles=bytes(T[g] for row in grid for g in row),
                         objs=[], grid=grid)
        for ch, x, y in persons:
            pname, spr, role, (p1, p2), dlg, wander = people[ch]
            if dlg not in dialog_ids:
                dialog_ids[dlg] = len(dialog_ids)
            roles = ['talk', 'weapons', 'armour', 'potions', 'inn', 'healer', 'king']
            npcs.append((mid, x, y, T['spr_' + spr], roles.index(role), 1 if wander else 0, text.add(pname),
                         dialog_ids[dlg], p1, p2))
    # dungeons
    mon_index = {m[0]: i for i, m in enumerate(C.MONSTERS)}
    for mid, name, look, floor_no, kinds, boss, place in C.DUNGEONS:
        d_rnd = random.Random(mid * 101 + 3)
        b = (mon_index[boss[0]], boss[1]) if boss else None
        dw, dh, grid, objs, up, light, fl = make_dungeon(mid, look, floor_no, [mon_index[k] for k in kinds], b,
                                                       d_rnd, C.SPECIAL_CHESTS.get(mid))
        px_, py_ = world_places[place]
        maps[mid] = dict(w=dw, h=dh, kind=2, floor=T[fl], light=light, name=text.add(name),
                         exit=(0, px_, py_), entry=up, tiles=None, objs=objs, grid=grid)
    # stairs between floors
    for mid, name, look, floor_no, kinds, boss, place in C.DUNGEONS:
        m = maps[mid]
        g = m['grid']
        for y in range(m['h']):
            for x in range(m['w']):
                if g[y][x] == 'stairs_down':
                    nxt = maps[mid + 1]
                    m['objs'].append((3, x, y, mid + 1, nxt['entry'][0], nxt['entry'][1]))
                if g[y][x] == 'stairs_up':
                    if floor_no == 1:
                        m['objs'].append((3, x, y, 0, m['exit'][1], m['exit'][2]))
                    else:
                        prv = maps[mid - 1]
                        sd = [(xx, yy) for yy in range(prv['h']) for xx in range(prv['w'])
                              if prv['grid'][yy][xx] == 'stairs_down'][0]
                        m['objs'].append((3, x, y, mid - 1, sd[0], sd[1]))
        m['tiles'] = bytes(T[c] for row in g for c in row)
        if len(m['objs']) > 60:
            print('  map %d has %d objects' % (mid, len(m['objs'])))
    lumps = []
    lumps.append(('TILES', bytes(tile_data)))
    lumps.append(('TPROP', bytes(props)))
    lumps.append(('TANIM', bytes(t.frames)))
    lumps.append(('FONT', font_lump))
    logo = make_logo()
    cols, pix, logo_prev = pack_picture(logo, True)
    lumps.append(('LOGO', struct.pack('<HH', *logo.size) + cols + pix))
    for mid in sorted(maps):
        m = maps[mid]
        hdr = struct.pack('<BBBBBHBBBBBB3x', m['w'], m['h'], m['kind'], m['floor'], m['light'], m['name'],
                          m['exit'][0], m['exit'][1], m['exit'][2], m['entry'][0], m['entry'][1], len(m['objs']))
        body = b''.join(struct.pack('<6B', *o) for o in m['objs'])
        lumps.append(('MAP%02d' % mid, hdr + m['tiles'] + body))
    npc_lump = bytearray([len(npcs)])
    for (mid, x, y, spr, role, wander, name, dlg, p1, p2) in npcs:
        npc_lump += struct.pack('<6BHBBBx', mid, x, y, spr, role, wander, name, dlg, p1, p2)
    lumps.append(('NPCS', bytes(npc_lump)))
    BOXW = 440
    dlg_lump = bytearray([len(dialog_ids)])
    for key, _ in sorted(dialog_ids.items(), key=lambda kv: kv[1]):
        vs = C.DIALOG[key]
        dlg_lump.append(len(vs))
        for minimum, s in vs:
            dlg_lump += struct.pack('<BH', minimum, text.add(wrap(s, widths, BOXW)))
    lumps.append(('DLG', bytes(dlg_lump)))
    mon = bytearray([len(C.MONSTERS)])
    for key, name, spr, hp, atk, df, xp, gold in C.MONSTERS:
        mon += struct.pack('<BHBBHHH5x', T['spr_' + spr], hp, atk, df, xp, gold, text.add(name))
    lumps.append(('MONST', bytes(mon)))
    items = bytearray()
    for name, power, price in C.WEAPONS + C.ARMOURS:
        items += struct.pack('<HBH', text.add(name), power, price)
    lumps.append(('ITEMS', bytes(items)))
    intro = text.add(wrap(C.INTRO, widths, 560))
    victory = text.add(wrap(C.VICTORY, widths, 560))
    lumps.append(('TEXT', bytes(text.data)))
    data = wad(lumps)
    open(out_wad, 'wb').write(data)
    print('%s: %d bytes, %d tiles, %d people, %d text bytes' % (out_wad, len(data), len(t.names), len(npcs),
                                                               len(text.data)))
    # the script's names
    defs = ['// --- generated by valmora-tools/make_wad.py: do not edit by hand ---']
    for base, i in sorted(t.index.items(), key=lambda kv: kv[1]):
        defs.append('#define T_%s %d' % (base.upper(), i))
    defs.append('#define NTILES %d' % len(t.names))
    defs.append('#define PAL_WORDS %d' % (len(t.names) * 32))
    for i, m in enumerate(C.MONSTERS):
        defs.append('#define M_%s %d' % (m[0].upper(), i))
    defs.append('#define NMONSTERS %d' % len(C.MONSTERS))
    mon_index = {m[0]: i for i, m in enumerate(C.MONSTERS)}
    zm, zl = [], []
    for z in range(6):
        lst = C.ZONES.get(z, [])
        for i in range(5):
            k, lv = lst[i] if i < len(lst) else (None, 99)
            zm.append(mon_index[k] if k else 0)
            zl.append(lv)
    defs.append('int zoneMon[30] = {%s};' % ', '.join(map(str, zm)))
    defs.append('int zoneLvl[30] = {%s};' % ', '.join(map(str, zl)))
    defs.append('#define TX_INTRO %d' % intro)
    defs.append('#define TX_VICTORY %d' % victory)
    gx, gy = gate
    defs.append('#define GATE_X %d' % gx)
    defs.append('#define GATE_Y %d' % gy)
    bx, by = world_places['brindle']
    defs.append('#define HOME_X %d' % bx)
    defs.append('#define HOME_Y %d' % by)
    defs.append('// --- end of generated ---')
    if low:
        # valmoralow.sc: valmora.sc with the pixel size from the command line
        sc = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'valmora.sc')).read()
        a = sc.find('   // --- the pixel size')
        b = sc.find('   // --- end of the pixel size ---')
        sc = sc[:a] + LOW_PIXEL_SIZE + sc[b + len('   // --- end of the pixel size ---'):]
        sc = sc.replace('printfln("valmora.wad must be next to the script");',
                        'printfln("valmoralow.wad must be next to the script");')
        open(out_sc, 'w').write(sc)
    sc = open(out_sc).read() if os.path.exists(out_sc) else ''
    a = sc.find('// --- generated by')
    b = sc.find('// --- end of generated ---')
    if a >= 0 and b >= 0:
        sc = sc[:a] + '\n'.join(defs) + sc[b + len('// --- end of generated ---'):]
        open(out_sc, 'w').write(sc)
        print('%s: defines updated' % out_sc)
    else:
        print('\n'.join(defs))
    if prev_dir:
        os.makedirs(prev_dir, exist_ok=True)
        render_previews(prev_dir, t, previews, maps, logo_prev)


def render_previews(d, t, previews, maps, logo_prev):
    pv = {}
    for name, im in zip(t.names, previews):
        pv[name] = im
    def tile_img(i):
        return previews[i]
    for mid in (0, 1, 2, 3, 4, 7, 9):
        m = maps[mid]
        scale = 8 if mid == 0 else 32
        img = Image.new('RGB', (m['w'] * scale, m['h'] * scale))
        floor = previews[m['floor']]
        for y in range(m['h']):
            for x in range(m['w']):
                i = m['tiles'][y * m['w'] + x]
                im = previews[i]
                if t.flags[i]:
                    base = floor.copy()
                    base.alpha_composite(im)
                    im = base
                img.paste(im.convert('RGB').resize((scale, scale), Image.BOX), (x * scale, y * scale))
        img.save(os.path.join(d, 'map%02d.png' % mid))
    logo_prev.save(os.path.join(d, 'logo.png'))


if __name__ == '__main__':
    main()
