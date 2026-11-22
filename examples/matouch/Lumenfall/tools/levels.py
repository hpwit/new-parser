"""Lumenfall's eight levels, built from pieces: each level is a run of
segments (flat ground, steps, gaps of water or of nothing, floating
platforms, rows of blocks and lumen boxes, spikes, moving platforms...)
picked from the level's own seed, harder as the game goes on.

Every gap and step is chosen within what Wren can jump (about 5 tiles
across and 4 up), and each piece leaves her on solid ground.

A level in the WAD (LEVEL1..LEVEL8):
  u16 width, u8 land, u8 boss, u8 start x, u8 start y, u16 count,
  width*15 tiles (row by row), then count things of 5 bytes:
  type, x low, x high, y, value
"""
import random
import struct

from tiles import ID

ROWS = 15

# what stands in a level (the script has the same numbers)
THINGS = ['NONE', 'WALKER', 'FLYER', 'SPECIAL', 'BOSS', 'CHECK', 'BEACON',
          'PLATH', 'PLATV', 'HEART', 'PRISM', 'LIFE', 'BOXITEM']
TH = {n: i for i, n in enumerate(THINGS)}

NAMES = [
    'Dawnmere Fields', 'The Bramble Gate',
    'Mirewood', 'The Moth Hollow',
    'Glimmerdeep', 'The Geode Throne',
    'The Hollow Spire', 'The Gloam Crown',
]


class Builder:
    def __init__(self, land, seed, difficulty):
        self.land = land
        self.r = random.Random(seed)
        self.d = difficulty
        self.cols = []       # per column: ground row (None: no ground), kind
        self.extra = {}      # (x, y) -> tile above the ground
        self.things = []
        self.g = 12

    # --- columns

    def ground(self, n, g=None):
        g = self.g if g is None else g
        for _ in range(n):
            self.cols.append((g, 'ground'))

    def x(self):
        return len(self.cols)

    def put(self, x, y, tile):
        self.extra[(x, y)] = tile

    def thing(self, kind, x, y, value=0):
        self.things.append((TH[kind], x, y, value))

    # --- pieces

    def flat(self, n, enemies=True):
        x0 = self.x()
        self.ground(n)
        if enemies and n >= 5 and self.r.random() < 0.75 + self.d * 0.03:
            self.thing('WALKER', x0 + self.r.randrange(3, n - 1), self.g - 1)
        self.decorate(x0, n)

    def decorate(self, x0, n):
        for x in range(x0, x0 + n):
            if self.r.random() < 0.22 and (x, self.g - 1) not in self.extra:
                self.put(x, self.g - 1, ID['DECO1'] + self.r.randrange(8))

    def step(self):
        dh = self.r.choice((-2, -1, -1, 1, 1, 2))
        ng = max(8, min(12, self.g + dh))
        if ng == self.g:
            ng = self.g - 1 if self.g > 9 else self.g + 1
        self.g = ng
        self.flat(self.r.randrange(4, 8))

    def gap(self, liquid=None):
        n = self.r.randrange(2, 4 + (1 if self.d >= 3 else 0))
        liquid = self.r.random() < 0.5 if liquid is None else liquid
        x0 = self.x()
        for _ in range(n):
            self.cols.append((None, 'liquid' if liquid else 'pit'))
        # glints in an arc over it
        for i in range(n):
            self.put(x0 + i, self.g - 3 - (1 if 0 < i < n - 1 else 0), ID['GLINT_A'])
        if self.d >= 1 and self.r.random() < 0.35 + self.d * 0.05:
            self.thing('FLYER', x0 + n // 2, self.g - 5)
        self.ground(self.r.randrange(3, 6))

    def floaters(self):
        n = self.r.randrange(12, 18)
        x0 = self.x()
        self.ground(n)
        self.decorate(x0, n)
        px = x0 + 2
        while px < x0 + n - 4:
            w = self.r.randrange(3, 6)
            y = self.g - self.r.choice((3, 4))
            self.platform_row(px, y, w)
            for i in range(w):
                self.put(px + i, y - 1, ID['GLINT_A'])
            if self.r.random() < 0.4 + self.d * 0.05:
                self.thing('WALKER', px + w // 2, y - 1)
            px += w + self.r.randrange(2, 4)

    def platform_row(self, x, y, w):
        for i in range(w):
            t = 'PLAT_M'
            if i == 0:
                t = 'PLAT_L'
            if i == w - 1:
                t = 'PLAT_R'
            self.put(x + i, y, ID[t])

    def boxes(self):
        n = self.r.randrange(10, 15)
        x0 = self.x()
        self.ground(n)
        y = self.g - 4
        pattern = self.r.choice(['BXB', 'BXXB', 'XBX', 'BBXBB', 'X', 'XX'])
        bx = x0 + (n - len(pattern)) // 2
        for i, ch in enumerate(pattern):
            if ch == 'B':
                self.put(bx + i, y, ID['BLOCK'] if self.land != 0 or self.r.random() < 0.5 else ID['CRATE'])
            else:
                self.put(bx + i, y, ID['BOX_A'])
                roll = self.r.random()
                if roll < 0.18:
                    self.thing('BOXITEM', bx + i, y, TH['PRISM'])
                elif roll < 0.34:
                    self.thing('BOXITEM', bx + i, y, TH['HEART'])
                elif roll < 0.38:
                    self.thing('BOXITEM', bx + i, y, TH['LIFE'])
        if self.r.random() < 0.5:
            self.thing('WALKER', x0 + n - 2, self.g - 1)

    def spikes(self):
        n = self.r.randrange(4, 7)
        self.ground(2)
        x0 = self.x()
        self.ground(n)
        for i in range(n):
            self.put(x0 + i, self.g - 1, ID['SPIKES'])
        self.platform_row(x0, self.g - 4, n)
        for i in range(1, n - 1):
            self.put(x0 + i, self.g - 5, ID['GLINT_A'])
        self.ground(2)

    def pillar(self):
        self.ground(3)
        x0 = self.x()
        h = self.r.randrange(2, 4)
        self.ground(2)
        for k in range(h):
            for i in range(2):
                self.put(x0 + i, self.g - 1 - k, ID['COLUMN_TOP'] if k == h - 1 else ID['COLUMN'])
        self.put(x0, self.g - 1 - h, ID['GLINT_A'])
        self.put(x0 + 1, self.g - 1 - h, ID['GLINT_A'])
        self.ground(3)
        if self.r.random() < 0.6:
            self.thing('SPECIAL', x0 + 4, self.g - 1)

    def mover(self):
        self.ground(2)
        x0 = self.x()
        n = self.r.randrange(7, 10)
        for _ in range(n):
            self.cols.append((None, 'pit' if self.land in (0, 2) else 'liquid'))
        if self.r.random() < 0.5 or self.land == 2:
            self.thing('PLATH', x0, self.g - 1, n - 3)
        else:
            self.thing('PLATV', x0 + n // 2 - 1, self.g - 1, 4)
        for i in range(1, n - 1, 2):
            self.put(x0 + i, self.g - 5, ID['GLINT_A'])
        self.ground(3)

    def stairs(self):
        n = self.r.randrange(3, 5)
        x0 = self.x()
        self.ground(n * 2 + 4)
        for k in range(n):
            for h in range(k + 1):
                self.put(x0 + 2 + k * 2, self.g - 1 - h, ID['BLOCK'])
                self.put(x0 + 3 + k * 2, self.g - 1 - h, ID['BLOCK'])
        self.put(x0 + 2 + n * 2 - 1, self.g - 2 - n, ID['GLINT_A'])

    def hoppers(self):
        n = self.r.randrange(9, 13)
        x0 = self.x()
        self.ground(n)
        self.decorate(x0, n)
        self.thing('SPECIAL', x0 + n // 2, self.g - 1)
        if self.d >= 4:
            self.thing('FLYER', x0 + n - 2, self.g - 6)

    # --- the level

    def build(self, length, boss):
        self.ground(12)
        self.decorate(4, 8)
        pieces = ['flat', 'step', 'gap', 'floaters', 'boxes', 'spikes', 'pillar', 'mover', 'stairs', 'hoppers']
        weights = [2, 3, 3, 3, 3, 2 + self.d // 2, 2, 1 + self.d // 3, 1, 2]
        half_done = False
        while self.x() < length:
            if not half_done and self.x() > length // 2:
                self.ground(3)
                self.thing('CHECK', self.x() - 2, self.g - 1)
                half_done = True
            p = self.r.choices(pieces, weights)[0]
            if p == 'flat':
                self.flat(self.r.randrange(6, 12))
            else:
                getattr(self, p)()
        # the end: a run to the beacon, or the guardian's arena
        if boss:
            if self.g != 12:
                self.g = 12
            self.ground(4)
            self.thing('CHECK', self.x() - 2, self.g - 1)
            x0 = self.x()
            self.ground(26)
            self.decorate(x0 + 2, 6)
            self.thing('BOSS', x0 + 18, self.g - 1, self.land)
            for y in range(0, self.g):
                self.put(x0 + 25, y, ID['BLOCK'])
            self.thing('BEACON', x0 + 24, self.g - 1, 1)
            self.ground(1)
        else:
            self.ground(6)
            self.thing('BEACON', self.x() - 4, self.g - 1)
            self.ground(14)

    def tiles(self):
        w = len(self.cols)
        grid = [[ID['EMPTY']] * w for _ in range(ROWS)]
        for x, (g, kind) in enumerate(self.cols):
            if g is not None:
                for y in range(g, ROWS):
                    if y == g:
                        grid[y][x] = ID['TOP']
                    else:
                        grid[y][x] = ID['FILL2'] if self.r.random() < 0.12 else ID['FILL']
            elif kind == 'liquid':
                grid[13][x] = ID['LIQTOP_A']
                grid[14][x] = ID['LIQ_A']
        # rounded tops and edges where the ground ends
        for y in range(ROWS):
            for x in range(w):
                t = grid[y][x]
                if t not in (ID['TOP'], ID['FILL'], ID['FILL2']):
                    continue
                rgt = x < w - 1 and self.cols[x + 1][0] is not None and self.cols[x + 1][0] <= y
                lft = x == 0 or (self.cols[x - 1][0] is not None and self.cols[x - 1][0] <= y)
                if t == ID['TOP']:
                    if not lft:
                        grid[y][x] = ID['TOP_L']
                    elif not rgt:
                        grid[y][x] = ID['TOP_R']
                else:
                    if not lft:
                        grid[y][x] = ID['FILL_L']
                    elif not rgt:
                        grid[y][x] = ID['FILL_R']
        for (x, y), t in self.extra.items():
            if 0 <= x < w and 0 <= y < ROWS and grid[y][x] == ID['EMPTY']:
                grid[y][x] = t
        return grid


def build_level(i):
    land = i // 2
    boss = i % 2 == 1
    b = Builder(land, 4242 + i * 101, i)
    b.build(200 + i * 16, boss)
    grid = b.tiles()
    w = len(grid[0])
    assert w <= 400, w
    data = bytearray(struct.pack('<HBBBBH', w, land, 1 if boss else 0, 3, 11, len(b.things)))
    for row in grid:
        data += bytes(row)
    for kind, x, y, v in b.things:
        data += struct.pack('<BHBB', kind, x, y, v)
    return bytes(data), grid, b.things


if __name__ == '__main__':
    for i in range(8):
        data, grid, things = build_level(i)
        print(i, NAMES[i], len(grid[0]), 'tiles,', len(things), 'things,', len(data), 'bytes')
