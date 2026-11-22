"""Lumenfall's sprites: Wren, the creatures of the four lands, their four
guardians, and the small things (shots, pickups, sparks, the lanterns).

Wren's head and shoulders are drawn by hand below (a letter a colour);
her legs, cloak and arm are posed per frame. Creatures are built from
shaded shapes and outlined. Every sprite faces right; the script flips.
"""
import math
import random

from PIL import Image

from art import rgb, mix, ramp, shade, q565

OUTLINE = rgb('#1a1222')


class Sprite:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.c = [[None] * w for _ in range(h)]

    def put(self, x, y, col):
        x, y = int(math.floor(x)), int(math.floor(y))
        if 0 <= x < self.w and 0 <= y < self.h and col is not None:
            self.c[y][x] = col

    def get(self, x, y):
        x, y = int(x), int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.c[y][x]
        return None

    def outline(self, col=OUTLINE, diag=False):
        marks = []
        for y in range(self.h):
            for x in range(self.w):
                if self.c[y][x] is not None:
                    continue
                n = ((1, 0), (-1, 0), (0, 1), (0, -1))
                if diag:
                    n += ((1, 1), (-1, 1), (1, -1), (-1, -1))
                for dx, dy in n:
                    if self.get(x + dx, y + dy) is not None and self.get(x + dx, y + dy) != col:
                        marks.append((x, y))
                        break
        for x, y in marks:
            self.c[y][x] = col
        return self

    def image(self):
        img = Image.new('RGBA', (self.w, self.h), (0, 0, 0, 0))
        px = img.load()
        for y in range(self.h):
            for x in range(self.w):
                if self.c[y][x] is not None:
                    px[x, y] = q565(self.c[y][x], x, y, dither=False) + (255,)
        return img


def ellipse(s, cx, cy, rx, ry, cols, light=(-0.6, -0.8), spec=None, cut=None):
    """A shaded ellipse: cols dark..light by how its surface faces the light."""
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            nx, ny = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            d = nx * nx + ny * ny
            if d > 1 or (cut is not None and not cut(x, y)):
                continue
            nz = math.sqrt(max(0, 1 - d))
            v = 0.5 + 0.5 * (-(nx * light[0] + ny * light[1]) * 0.8 + nz * 0.4) - 0.15
            c = ramp(cols, max(0, min(1, v)))
            if spec is not None and v > 0.82:
                c = mix(c, spec, 0.6)
            s.put(x, y, c)


def line(s, x0, y0, x1, y1, col, w=1):
    n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
    for i in range(n + 1):
        t = i / n
        x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
        for dx in range(w):
            for dy in range(w):
                s.put(x + dx - w // 2, y + dy - w // 2, col)


def from_rows(rows, pal, w=None, h=None, ox=0, oy=0, s=None):
    w = w or len(rows[0])
    h = h or len(rows)
    s = s or Sprite(w, h)
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != '.' and ch != ' ':
                s.put(x + ox, y + oy, pal[ch])
    return s


# ------------------------------------------------------------ Wren

WREN = {
    'K': OUTLINE,
    'h': rgb('#6a3418'), 'H': rgb('#a8582a'), 'J': rgb('#d88a48'),
    's': rgb('#f4c49a'), 'S': rgb('#d0946e'),
    'e': rgb('#1a1428'), 'w': rgb('#ffffff'),
    'r': rgb('#a82a2a'), 'R': rgb('#e8503a'), 'Q': rgb('#ff8a5a'),
    'c': rgb('#14566a'), 'C': rgb('#1e8a90'), 'L': rgb('#5ac8b8'),
    'p': rgb('#2a2a52'), 'P': rgb('#44487a'),
    'b': rgb('#4a2a1c'), 'B': rgb('#7a4a2e'),
    'g': rgb('#4a5068'), 'G': rgb('#9aa4c0'),
    'y': rgb('#ffb030'), 'Y': rgb('#fff0a0'),
    'm': rgb('#c08a4a'),
}

# head and shoulders, facing right; the hair's tail streams behind (left)
HEAD = [
    "......KKKKK.....",
    ".....KHJJHHK....",
    "....KhHHHHHHK...",
    "...KhhhhHHHHK...",
    "..KhhhhhsssshK..",
    "..KhhhhssssesK..",
    ".KhKhhSssssssK..",
    ".KK.KhSSsssSK...",
    "....KrKKSSKK....",
    "...KrRRQQRRK....",
]
HEAD_BLINK = HEAD[:5] + ["..KhhhhssssSsK.."] + HEAD[6:]


def wren_frame(kind, t=0.0, aim=False, blink=False, hurt=False):
    """kind: stand, run (t: 0..1 of the stride), jump, fall, crouch, hurt."""
    s = Sprite(16, 24)
    bob = 0
    if kind == 'run':
        bob = -1 if (t * 2) % 1.0 < 0.5 else 0
    if kind == 'crouch':
        bob = 5
    hip_y = 17 + bob
    hip_x = 7

    # legs: thigh and shin per leg, back leg darker; boots
    def leg(ang_thigh, ang_knee, back):
        cols = (WREN['p'], WREN['p']) if back else (WREN['p'], WREN['P'])
        kx = hip_x + math.sin(ang_thigh) * 3.6
        ky = hip_y + math.cos(ang_thigh) * 3.6
        fx = kx + math.sin(ang_thigh - ang_knee) * 3.4
        fy = ky + math.cos(ang_thigh - ang_knee) * 3.4
        line(s, hip_x, hip_y, kx, ky, cols[0], 2)
        line(s, hip_x + 1, hip_y, kx + 1, ky, cols[1])
        line(s, kx, ky, fx, fy, cols[0], 2)
        bc = WREN['b'] if back else WREN['B']
        bx, by = int(round(fx)), int(round(fy))
        for dx in range(-1, 3):
            s.put(bx + dx, by, WREN['b'])
            s.put(bx + dx, by - 1, bc)
        s.put(bx + 2, by - 1, WREN['b'])

    if kind == 'stand':
        leg(-0.12, 0, True)
        leg(0.14, 0, False)
    elif kind == 'run':
        a = math.sin(t * 2 * math.pi)
        leg(-a * 0.75, max(0, math.sin(t * 2 * math.pi + 1.6)) * 1.3, True)
        leg(a * 0.75, max(0, math.sin(t * 2 * math.pi + 1.6 + math.pi)) * 1.3, False)
    elif kind == 'jump':
        leg(-0.5, 1.2, True)
        leg(0.7, 1.6, False)
    elif kind == 'fall':
        leg(-0.3, 0.4, True)
        leg(0.35, 0.3, False)
    elif kind == 'crouch':
        leg(-0.9, 1.9, True)
        leg(1.1, 2.2, False)
    elif kind == 'hurt':
        leg(-0.6, 0.5, True)
        leg(-0.2, 0.9, False)

    # the cloak: from the shoulders to just above the knees, flaring back
    top = 9 + bob
    flare = {'run': 2 + (1 if (t * 4) % 1.0 < 0.5 else 0), 'jump': 1, 'fall': 3, 'hurt': 2}.get(kind, 0)
    bottom = top + 9 - (1 if kind == 'crouch' else 0)
    for y in range(top, bottom):
        k = (y - top) / (bottom - top)
        x0 = 4 - int(k * (1.5 + flare))
        x1 = 10 + int(k * 1.2)
        for x in range(x0, x1 + 1):
            v = 0.62 - (x - x0) / max(1, (x1 - x0)) * 0.1 + (0.25 if x == x1 or x == x1 - 1 else 0) - k * 0.2
            if (x - x0) % 3 == 0 and k > 0.4:
                v -= 0.18
            s.put(x, y, ramp([WREN['c'], WREN['C'], WREN['L']], max(0, min(1, v))))
    # the satchel strap and belt
    for x in range(5, 11):
        s.put(x, top + 5, WREN['b'])
    s.put(9, top + 5, WREN['m'])
    for k in range(5):
        s.put(5 + k, top + k, WREN['B'])

    # the arm and the lantern-caster
    if aim:
        for x in range(9, 13):
            s.put(x, top + 3, WREN['C'])
            s.put(x, top + 4, WREN['c'])
        s.put(13, top + 3, WREN['s'])
        s.put(13, top + 4, WREN['S'])
        for x in range(12, 16):
            s.put(x, top + 2, WREN['g'])
            s.put(x, top + 5, WREN['g'])
        for x in range(13, 16):
            s.put(x, top + 3, WREN['G'])
            s.put(x, top + 4, WREN['y'])
        s.put(15, top + 3, WREN['Y'])
    else:
        swing = {'run': int(round(math.sin(t * 2 * math.pi) * 1.5))}.get(kind, 0)
        hx, hy = 11 + swing, top + 6
        for y in range(top + 1, hy):
            s.put(10 + (swing if y > top + 3 else 0), y, WREN['C'])
        s.put(hx, hy, WREN['s'])
        # the lantern hangs from the hand
        for x in range(hx - 1, hx + 2):
            s.put(x, hy + 1, WREN['g'])
            s.put(x, hy + 4, WREN['g'])
        for y in range(hy + 2, hy + 4):
            s.put(hx - 1, y, WREN['g'])
            s.put(hx, y, WREN['Y'])
            s.put(hx + 1, y, WREN['y'])

    from_rows(HEAD_BLINK if blink else HEAD, WREN, s=s, oy=bob + (1 if kind == 'crouch' else 0) - 1 + (1 if kind != 'crouch' else 0) - 1)
    if hurt:
        for y in range(s.h):
            for x in range(s.w):
                if s.c[y][x] is not None:
                    s.c[y][x] = mix(s.c[y][x], rgb('#ffffff'), 0.55)
    s.outline()
    return s


WREN_FRAMES = [
    ('STAND', lambda: wren_frame('stand')),
    ('BLINK', lambda: wren_frame('stand', blink=True)),
] + [('RUN%d' % i, (lambda i=i: wren_frame('run', i / 6))) for i in range(6)] + [
    ('JUMP', lambda: wren_frame('jump')),
    ('FALL', lambda: wren_frame('fall')),
    ('CROUCH', lambda: wren_frame('crouch')),
    ('HURT', lambda: wren_frame('hurt', hurt=True)),
    ('AIM', lambda: wren_frame('stand', aim=True)),
] + [('ARUN%d' % i, (lambda i=i: wren_frame('run', i / 6, aim=True))) for i in range(6)] + [
    ('AJUMP', lambda: wren_frame('jump', aim=True)),
    ('AFALL', lambda: wren_frame('fall', aim=True)),
]


# ------------------------------------------------------------ eyes and feet

def eyes(s, x, y, angry=True, col=rgb('#ffffff'), pupil=OUTLINE, gap=4):
    for ex in (x, x + gap):
        s.put(ex, y, col)
        s.put(ex + 1, y, col)
        s.put(ex, y + 1, col)
        s.put(ex + 1, y + 1, pupil)
    if angry:
        s.put(x - 1, y - 1, OUTLINE)
        s.put(x, y - 1, OUTLINE)
        s.put(x + gap + 1, y - 1, OUTLINE)
        s.put(x + gap + 2, y - 1, OUTLINE)


def feet(s, x, y, frame, col):
    for k, fx in enumerate((x, x + 6)):
        lift = 1 if (frame + k) % 2 else 0
        for dx in range(3):
            s.put(fx + dx, y - lift, col)


# ------------------------------------------------------------ the creatures (16x16)

def burrlet(f):
    """Dawnmere's walker: a spiky chestnut burr on two stubby feet."""
    s = Sprite(16, 16)
    if f == 2:  # stomped flat
        ellipse(s, 8, 13, 7, 2.5, [rgb('#4a2a10'), rgb('#8a5a24'), rgb('#c8903a')])
        return s.outline()
    body = [rgb('#3a2410'), rgb('#7a5020'), rgb('#b8803a'), rgb('#e8c070')]
    for k in range(12):
        a = k / 12 * 2 * math.pi + f * 0.2
        line(s, 8 + math.cos(a) * 4, 8 + math.sin(a) * 4, 8 + math.cos(a) * 7.2, 8 + math.sin(a) * 6.5, rgb('#6a8a2a'))
    ellipse(s, 8, 8, 5.2, 5, body, spec=rgb('#fff0c0'))
    eyes(s, 6, 7, gap=3)
    feet(s, 4, 14, f, rgb('#4a2a10'))
    return s.outline()


def puffwing(f):
    """Dawnmere's flyer: a dandelion puff that beats two tiny wings."""
    s = Sprite(16, 16)
    for k in range(16):
        a = k / 16 * 2 * math.pi
        for r in range(3, 7):
            s.put(8 + math.cos(a) * r, 8 + math.sin(a) * r, rgb('#f0f0e8') if r > 4 else rgb('#d8d8d0'))
    ellipse(s, 8, 8, 3.2, 3.2, [rgb('#b0a888'), rgb('#e8e0c0'), rgb('#ffffff')])
    eyes(s, 6, 7, angry=False, gap=3)
    wy = 4 if f == 0 else 9
    for side in (-1, 1):
        for k in range(4):
            s.put(8 + side * (6 + k * 0.5), wy + k * (1 if f == 0 else -1) * 0.5, rgb('#ff9ab0'))
            s.put(8 + side * (6 + k * 0.5), wy + 1 + k * (1 if f == 0 else -1) * 0.5, rgb('#d06a88'))
    return s.outline()


def hopkin(f):
    """Dawnmere's hopper: a round green frog that leaps at you."""
    s = Sprite(16, 16)
    g = [rgb('#1a4a1a'), rgb('#2e7a2a'), rgb('#5ab040'), rgb('#a8e070')]
    if f == 0:
        ellipse(s, 8, 11, 6.5, 4.5, g, spec=rgb('#e0ffb0'))
        for x in (3, 10):
            for dx in range(4):
                s.put(x + dx, 15, g[0])
    else:
        ellipse(s, 8, 8, 5.5, 5.5, g, spec=rgb('#e0ffb0'))
        line(s, 4, 12, 1, 15, g[1], 2)
        line(s, 12, 12, 15, 15, g[1], 2)
    yb = 7 if f == 0 else 4
    for ex in (5, 10):
        ellipse(s, ex, yb, 2, 2, [rgb('#c8e080'), rgb('#ffffff')])
        s.put(ex, yb, OUTLINE)
    for x in range(5, 11):
        s.put(x, yb + 5 if f == 0 else yb + 6, rgb('#8a2a2a'))
    return s.outline()


def mosslug(f):
    """Mirewood's walker: a slug carrying a mossy stone on its back."""
    s = Sprite(16, 16)
    if f == 2:
        ellipse(s, 8, 14, 7, 2, [rgb('#3a2a40'), rgb('#7a5a80'), rgb('#b08ab0')])
        return s.outline()
    stretch = 1 if f else 0
    ellipse(s, 8, 12.5, 7 + stretch, 3, [rgb('#3a2a40'), rgb('#7a5a80'), rgb('#c09ac0')], spec=rgb('#ffe0ff'))
    ellipse(s, 7, 8, 5, 4.5, [rgb('#1a2a14'), rgb('#3a5a2a'), rgb('#6a9a44'), rgb('#a8d070')])
    for k in range(5):
        s.put(4 + k * 2, 5 + (k % 2), rgb('#a8e070'))
    for side in (0, 1):
        line(s, 13 + side, 11, 14 + side * 1.5, 7, rgb('#7a5a80'))
        s.put(14 + side * 1.5, 6, rgb('#ffe060'))
    return s.outline()


def lampmoth(f):
    """Mirewood's flyer: a moth whose wings glow like paper lanterns."""
    s = Sprite(16, 16)
    up = f == 0
    for side in (-1, 1):
        for y in range(16):
            for x in range(8):
                xx = 8 + side * (x + 1)
                if up:
                    inside = ((x - 3.5) / 4) ** 2 + ((y - 5) / 4.5) ** 2 < 1
                else:
                    inside = ((x - 3.5) / 4) ** 2 + ((y - 9) / 3.5) ** 2 < 1
                if inside:
                    d = math.hypot(x - 3.5, y - (5 if up else 9)) / 4
                    s.put(xx, y, mix(rgb('#fff0a0'), rgb('#e07a30'), d))
        s.put(8 + side * 4, 5 if up else 9, rgb('#ffffff'))
    for y in range(4, 13):
        s.put(7, y, rgb('#4a3020'))
        s.put(8, y, rgb('#6a4a30'))
    line(s, 7, 4, 5, 1, rgb('#4a3020'))
    line(s, 8, 4, 10, 1, rgb('#4a3020'))
    return s.outline()


def sporecap(f):
    """Mirewood's turret: a squat toadstool that puffs spores at you."""
    s = Sprite(16, 16)
    squash = 1 if f else 0
    for y in range(10, 16):
        for x in range(5, 11):
            s.put(x, y, mix(rgb('#f0e8d0'), rgb('#a89880'), (x - 5) / 6))
    eyes(s, 6, 11, gap=3)
    ellipse(s, 8, 8 + squash, 7.5, 4.5 - squash, [rgb('#4a1a4a'), rgb('#8a3a8a'), rgb('#d070c0'), rgb('#ffb0f0')],
            cut=lambda x, y: y <= 10)
    for x, y in ((4, 7), (8, 5), (11, 8)):
        s.put(x, y + squash, rgb('#a0fff0'))
    return s.outline()


def shardbug(f):
    """Glimmerdeep's walker: a beetle with crystals growing from its shell."""
    s = Sprite(16, 16)
    if f == 2:
        ellipse(s, 8, 14, 7, 2, [rgb('#0a1a3a'), rgb('#2a4a7a'), rgb('#5a8ac0')])
        return s.outline()
    ellipse(s, 8, 10, 7, 4.5, [rgb('#0a1430'), rgb('#1e3a6a'), rgb('#3a6aa8'), rgb('#7ab0e8')], spec=rgb('#e0f0ff'))
    for cx, h in ((5, 5), (8, 7), (11, 4)):
        for y in range(h):
            half = (y + 1) / h * 1.5
            for x in range(int(cx - half), int(cx + half) + 1):
                s.put(x, 6 - h + y + 1, mix(rgb('#b0fff8'), rgb('#2a8aa0'), (x - cx + half) / (2 * half + 1)))
    eyes(s, 12, 9, gap=2, col=rgb('#80fff0'))
    for k in range(3):
        lift = (f + k) % 2
        s.put(4 + k * 3, 15 - lift, OUTLINE)
        s.put(5 + k * 3, 15 - lift, OUTLINE)
    return s.outline()


def gloomflit(f):
    """Glimmerdeep's flyer: a cave bat with glowing eyes."""
    s = Sprite(16, 16)
    body = [rgb('#1a1030'), rgb('#3a2a60'), rgb('#6a5aa0')]
    ellipse(s, 8, 8, 3, 3.5, body)
    for side in (-1, 1):
        for k in range(7):
            yy = (3 + k * 0.5) if f == 0 else (11 - k * 0.5)
            for y in range(int(yy), 9):
                s.put(8 + side * (3 + k), y if f == 0 else 16 - y, body[1] if k % 2 else body[0])
    s.put(7, 7, rgb('#ff5060'))
    s.put(9, 7, rgb('#ff5060'))
    s.put(6, 4, body[0])
    s.put(10, 4, body[0])
    return s.outline()


def geodrop(f):
    """Glimmerdeep's hopper: a geode that springs open, crystals inside."""
    s = Sprite(16, 16)
    rock = [rgb('#1a1a2a'), rgb('#3a3a52'), rgb('#62627e')]
    if f == 0:
        ellipse(s, 8, 10, 6.5, 5.5, rock)
        line(s, 2, 10, 14, 10, rgb('#a0fff8'))
        eyes(s, 5, 7, gap=4, col=rgb('#a0fff8'))
    else:
        ellipse(s, 8, 12, 6.5, 3.5, rock, cut=lambda x, y: y >= 11)
        ellipse(s, 8, 6, 6.5, 3.5, rock, cut=lambda x, y: y <= 6)
        for x in range(3, 14):
            s.put(x, 9, mix(rgb('#f0b0ff'), rgb('#40f0e0'), (x - 3) / 10))
            s.put(x, 8 + (x % 2), rgb('#ffffff') if x % 3 == 0 else rgb('#c080ff'))
    return s.outline()


def clank(f):
    """The Spire's walker: a small iron knight that takes two hits."""
    s = Sprite(16, 16)
    if f == 2:
        ellipse(s, 8, 13, 7, 2.5, [rgb('#2a2a3a'), rgb('#5a5a72'), rgb('#9a9ab8')])
        return s.outline()
    iron = [rgb('#2a2a3a'), rgb('#4e4e66'), rgb('#7a7a98'), rgb('#c0c0dc')]
    ellipse(s, 8, 8, 5, 5.5, iron, spec=rgb('#ffffff'))
    for x in range(5, 12):
        s.put(x, 6, OUTLINE)
    s.put(9, 6, rgb('#ff4030'))
    s.put(10, 6, rgb('#ff8060'))
    for y in range(1, 4):
        s.put(8, y, rgb('#c02a3a'))
        s.put(7 + (y % 2), y - 1, rgb('#e04a5a'))
    for k in range(2):
        lift = (f + k) % 2
        for y in range(13, 16 - lift):
            s.put(5 + k * 5, y, iron[1])
            s.put(6 + k * 5, y, iron[0])
    for y in range(6, 12):  # a round shield
        s.put(13, y, iron[3] if y < 9 else iron[2])
        s.put(14, y, iron[1])
    return s.outline()


def wisp(f):
    """The Spire's flyer: a cold blue flame with a face."""
    s = Sprite(16, 16)
    for y in range(16):
        for x in range(16):
            dx = (x - 8) / (4.6 - (y < 8) * (8 - y) * 0.45)
            dy = (y - 10) / 5.5
            if dx * dx + dy * dy < 1 or (y < 9 and abs(x - 8 + (1 if f else -1) * (9 - y) * 0.3) < (y - 1) * 0.45):
                d = math.hypot(x - 8, y - 10) / 6
                s.put(x, y, ramp([rgb('#ffffff'), rgb('#90e0ff'), rgb('#3a7aff'), rgb('#2a2ab0')], d))
    eyes(s, 6, 10, angry=True, col=rgb('#1a1a5a'), pupil=rgb('#1a1a5a'), gap=3)
    return s.outline(rgb('#10104a'))


def gargoyle(f):
    """The Spire's turret: a stone head that breathes fire."""
    s = Sprite(16, 16)
    stone = [rgb('#1e1a2a'), rgb('#3e3850'), rgb('#6a6280'), rgb('#9a90b0')]
    ellipse(s, 8, 9, 6.5, 6, stone)
    for side in (-1, 1):
        line(s, 8 + side * 4, 4, 8 + side * 6.5, 0, stone[2], 2)
    eyes(s, 5, 7, gap=5, col=rgb('#ff6020') if f else rgb('#a03010'), pupil=rgb('#ffe060') if f else rgb('#601808'))
    for x in range(5, 12):
        s.put(x, 12, OUTLINE)
        if f:
            s.put(x, 13, rgb('#ff8a20'))
    return s.outline()


CREATURES = [
    (burrlet, puffwing, hopkin),
    (mosslug, lampmoth, sporecap),
    (shardbug, gloomflit, geodrop),
    (clank, wisp, gargoyle),
]


# ------------------------------------------------------------ the guardians (48x48)

def bramble_boar(f):
    """Dawnmere's guardian: a great boar grown over with thorny briars."""
    s = Sprite(48, 48)
    hide = [rgb('#2a1408'), rgb('#5a2e14'), rgb('#8a4a22'), rgb('#c07a3e')]
    lunge = 2 if f == 1 else 0
    ellipse(s, 22, 26, 18, 13, hide, spec=rgb('#f0c080'))
    ellipse(s, 38 + lunge, 28, 9, 8, hide)
    ellipse(s, 45 + lunge, 31, 3, 3, [rgb('#8a4a4a'), rgb('#e09090')])
    for k in range(18):
        a = math.pi * (1.05 + k / 17 * 0.95)
        x0, y0 = 22 + math.cos(a) * 15, 26 + math.sin(a) * 11
        line(s, x0, y0, x0 + math.cos(a) * 6, y0 + math.sin(a) * 6, rgb('#2e5a1a'), 2)
        s.put(x0 + math.cos(a) * 6, y0 + math.sin(a) * 6, rgb('#ff5a6a') if k % 4 == 0 else rgb('#6a9a3a'))
    line(s, 40 + lunge, 34, 46 + lunge, 28, rgb('#fff8e0'), 2)  # tusk
    s.put(40 + lunge, 25, rgb('#ffd040'))
    s.put(41 + lunge, 25, rgb('#ff4020'))
    for k, lx in enumerate((10, 18, 28, 34)):
        lift = 2 if (k + f) % 2 else 0
        for y in range(36, 46 - lift):
            s.put(lx, y, hide[1])
            s.put(lx + 1, y, hide[0])
            s.put(lx + 2, y, hide[1])
    if f == 2:
        for y in range(48):
            for x in range(48):
                if s.c[y][x] is not None:
                    s.c[y][x] = mix(s.c[y][x], rgb('#ffffff'), 0.6)
    return s.outline()


def moth_matriarch(f):
    """Mirewood's guardian: the queen of the lantern moths."""
    s = Sprite(48, 48)
    up = f != 1
    for side in (-1, 1):
        for y in range(48):
            for x in range(24):
                cy = 16 if up else 26
                inside = ((x - 11) / 12) ** 2 + ((y - cy) / 13) ** 2 < 1
                inside2 = ((x - 8) / 8) ** 2 + ((y - (cy + 14)) / 7) ** 2 < 1
                if inside or inside2:
                    d = math.hypot(x - 11, y - cy) / 13
                    c = ramp([rgb('#fff8c0'), rgb('#ffc050'), rgb('#e0602a'), rgb('#6a1a3a')], d)
                    if abs(math.hypot(x - 12, y - cy) - 5) < 1.2:
                        c = rgb('#3a0a2a')
                    s.put(24 + side * (x + 2), y, c)
    for y in range(12, 40):
        w = 3 - abs(y - 24) / 10
        for x in range(int(24 - w), int(24 + w) + 1):
            s.put(x, y, ramp([rgb('#2a1a14'), rgb('#6a4a30'), rgb('#a8805a')], 0.5 + (x - 24) / 8))
    for side in (-1, 1):
        line(s, 24, 12, 24 + side * 6, 3, rgb('#6a4a30'))
        s.put(24 + side * 6, 2, rgb('#fff0a0'))
    s.put(22, 15, rgb('#ff4060'))
    s.put(26, 15, rgb('#ff4060'))
    if f == 2:
        for y in range(48):
            for x in range(48):
                if s.c[y][x] is not None:
                    s.c[y][x] = mix(s.c[y][x], rgb('#ffffff'), 0.6)
    return s.outline()


def geode_golem(f):
    """Glimmerdeep's guardian: a walking hill of rock, crystals for a crown."""
    s = Sprite(48, 48)
    rock = [rgb('#10121e'), rgb('#262a40'), rgb('#40466a'), rgb('#6a7298')]
    raise_ = 6 if f == 1 else 0
    ellipse(s, 24, 28, 15, 14, rock, spec=rgb('#a0a8d0'))
    ellipse(s, 9, 26 - raise_, 6, 9, rock)
    ellipse(s, 39, 26 - raise_, 6, 9, rock)
    ellipse(s, 9, 37 - raise_ // 2, 6, 5, rock)
    ellipse(s, 39, 37 - raise_ // 2, 6, 5, rock)
    for cx, h, col in ((16, 10, rgb('#80fff0')), (22, 14, rgb('#c0a0ff')), (28, 12, rgb('#80fff0')), (33, 8, rgb('#a0d0ff'))):
        for y in range(h):
            half = (y + 1) / h * 2.6
            for x in range(int(cx - half), int(cx + half) + 1):
                s.put(x, 16 - h + y, mix(col, rgb('#1a3a6a'), (x - cx + half) / (2 * half + 1) * 0.8))
    for x in (18, 28):
        s.put(x, 26, rgb('#a0fff8'))
        s.put(x + 1, 26, rgb('#ffffff'))
        s.put(x, 27, rgb('#40c0d0'))
        s.put(x + 1, 27, rgb('#40c0d0'))
    for k, lx in enumerate((14, 30)):
        for y in range(40, 47):
            for dx in range(5):
                s.put(lx + dx, y, rock[1] if dx else rock[0])
    if f == 2:
        for y in range(48):
            for x in range(48):
                if s.c[y][x] is not None:
                    s.c[y][x] = mix(s.c[y][x], rgb('#ffffff'), 0.6)
    return s.outline()


def gloam_king(f):
    """The Hollow Spire's master: a crowned shadow in a cloak of night."""
    s = Sprite(48, 48)
    for y in range(10, 48):
        k = (y - 10) / 38
        half = 7 + k * 14 + (math.sin(y * 0.6 + f * 2) * 1.5 if y > 34 else 0)
        for x in range(int(24 - half), int(24 + half) + 1):
            v = 0.55 - abs(x - 24) / (half + 1) * 0.4 - k * 0.2
            c = ramp([rgb('#06040e'), rgb('#1a1030'), rgb('#3a2460'), rgb('#6a4a9a')], max(0, min(1, v)))
            if y > 44 and (x + y) % 3 == 0:
                continue
            s.put(x, y, c)
    ellipse(s, 24, 12, 6, 6.5, [rgb('#05040a'), rgb('#140e22'), rgb('#2a2040')])
    for x in range(18, 31):  # the crown
        h = 3 + (2 if x % 3 == 0 else 0)
        for y in range(6 - h, 6):
            s.put(x, y, rgb('#e0b040') if y > 2 else rgb('#fff0a0'))
    s.put(24, 2, rgb('#ff4080'))
    glow_ = rgb('#a0f0ff') if f != 1 else rgb('#ffffff')
    for ex in (21, 26):
        s.put(ex, 12, glow_)
        s.put(ex + 1, 12, glow_)
    for side in (-1, 1):  # hands holding orbs of shadow
        hx = 24 + side * (14 + (3 if f == 1 else 0))
        ellipse(s, hx, 26, 3.5, 3.5, [rgb('#3a1a6a'), rgb('#9a5aff'), rgb('#f0d0ff')])
    if f == 2:
        for y in range(48):
            for x in range(48):
                if s.c[y][x] is not None:
                    s.c[y][x] = mix(s.c[y][x], rgb('#ffffff'), 0.6)
    return s.outline()


BOSSES = [bramble_boar, moth_matriarch, geode_golem, gloam_king]


# ------------------------------------------------------------ small things

def bolt(f):
    """Wren's shot: a dart of lantern light (8x6)."""
    s = Sprite(8, 6)
    for x in range(8):
        t = x / 7
        for y in range(6):
            d = abs(y - 2.5) / (0.6 + t * 2.2)
            if d < 1:
                s.put(x, y, ramp([rgb('#ffffff'), rgb('#fff0a0'), rgb('#ffb030'), rgb('#c06010')], d * 0.6 + (1 - t) * 0.4))
    if f:
        s.put(0, 1, rgb('#ffd060'))
        s.put(0, 4, rgb('#ffd060'))
    return s


def big_bolt(f):
    """The prism-charged shot (8x6), white-violet."""
    s = Sprite(8, 6)
    for x in range(8):
        t = x / 7
        for y in range(6):
            d = abs(y - 2.5) / (0.8 + t * 2.4)
            if d < 1:
                s.put(x, y, ramp([rgb('#ffffff'), rgb('#e0c0ff'), rgb('#a060ff'), rgb('#5020c0')], d * 0.6 + (1 - t) * 0.4))
    if f:
        s.put(1, 0, rgb('#ffffff'))
        s.put(1, 5, rgb('#ffffff'))
    return s


def orb(cols):
    def make(f):
        s = Sprite(8, 6)
        ellipse(s, 4, 3, 3 - f * 0.4, 2.6 - f * 0.4, cols)
        return s
    return make


def heart_pickup(f):
    s = Sprite(12, 12)
    for y in range(12):
        for x in range(12):
            u, v = (x - 5.5) / 5.0, -(y - 5.0) / 5.0
            if (u * u + v * v - 1) ** 3 - u * u * v ** 3 < 0:
                s.put(x, y, mix(rgb('#ff8a9a'), rgb('#b01a3a'), (x + y) / 22))
    s.put(3, 3, rgb('#ffffff'))
    s.put(3, 4 - f, rgb('#ffe0e8'))
    return s.outline()


def prism_pickup(f):
    s = Sprite(12, 12)
    for y in range(12):
        half = (5.5 - abs(y - 5.5)) if True else 0
        for x in range(int(6 - half), int(6 + half) + 1):
            hue = (x + y + f * 3) % 6
            s.put(x, y, [rgb('#ff6a6a'), rgb('#ffc040'), rgb('#f0ff60'), rgb('#60ffa0'), rgb('#60c0ff'), rgb('#c080ff')][hue])
    s.put(5, 3, rgb('#ffffff'))
    return s.outline()


def lantern_pickup(f):
    """An extra life: a little lantern of Wren's own."""
    s = Sprite(12, 12)
    for x in range(3, 9):
        s.put(x, 1, rgb('#6a5a4a'))
    for y in range(2, 11):
        for x in range(2, 10):
            edge = x in (2, 9) or y in (2, 10)
            s.put(x, y, rgb('#4a3a2a') if edge else (rgb('#fff0a0') if (x - 5.5) ** 2 + (y - 6) ** 2 < 4 + f else rgb('#ffb030')))
    s.put(5, 0, rgb('#6a5a4a'))
    s.put(6, 0, rgb('#6a5a4a'))
    return s.outline()


def puff(f):
    """A burst of light when something is beaten (4 frames, 16x16)."""
    s = Sprite(16, 16)
    r0 = 2 + f * 2
    for k in range(10):
        a = k / 10 * 2 * math.pi + f * 0.3
        x, y = 8 + math.cos(a) * r0, 8 + math.sin(a) * r0
        size = max(1, 3 - f)
        for dx in range(size):
            for dy in range(size):
                s.put(x + dx - size // 2, y + dy - size // 2,
                      [rgb('#ffffff'), rgb('#fff0a0'), rgb('#ffc040'), rgb('#c07020')][f])
    if f < 2:
        ellipse(s, 8, 8, 3 - f, 3 - f, [rgb('#ffd060'), rgb('#ffffff')])
    return s


def spark(f):
    s = Sprite(8, 8)
    c = [rgb('#ffffff'), rgb('#fff0a0'), rgb('#ffb030')][f]
    r = 1 + f
    for k in range(4):
        a = k * math.pi / 2 + (math.pi / 4 if f == 1 else 0)
        line(s, 4, 4, 4 + math.cos(a) * r * 1.4, 4 + math.sin(a) * r * 1.4, c)
    s.put(4, 4, rgb('#ffffff'))
    return s


def checkpoint(f):
    """A lantern on a post (16x24): dark until Wren touches it; then it burns."""
    s = Sprite(16, 24)
    post = [rgb('#2a1a10'), rgb('#5a3a22'), rgb('#8a5a34')]
    for y in range(8, 24):
        s.put(7, y, post[2])
        s.put(8, y, post[1])
    for x in range(4, 12):
        s.put(x, 23, post[0])
    for x in range(5, 11):
        s.put(x, 1, rgb('#5a5a6a'))
    for y in range(2, 9):
        for x in range(4, 12):
            edge = x in (4, 11) or y in (2, 8)
            if f == 0:
                c = rgb('#3a3a4a') if edge else rgb('#1a1a2a')
            else:
                fl = ramp([rgb('#ffffff'), rgb('#fff0a0'), rgb('#ffb030'), rgb('#c06010')],
                          math.hypot(x - 7.5, (y - 5.5) * (1.2 if f == 1 else 0.9)) / 4)
                c = rgb('#6a5a4a') if edge else fl
            s.put(x, y, c)
    return s.outline()


def beacon(f):
    """The great beacon at each level's end (32x48): Wren lights it."""
    s = Sprite(32, 48)
    stone = [rgb('#2a2a3a'), rgb('#5a5a72'), rgb('#8a8aa8'), rgb('#c0c0d8')]
    for y in range(20, 48):
        half = 5 + (y - 20) / 28 * 5
        for x in range(int(16 - half), int(16 + half) + 1):
            v = 0.6 - (x - 16) / (half + 1) * 0.35 - (0.15 if (y % 6 == 0) else 0)
            s.put(x, y, ramp(stone, v))
    for x in range(6, 27):
        for y in range(16, 21):
            s.put(x, y, ramp(stone, 0.85 - (y - 16) * 0.12))
    if f > 0:
        for y in range(0, 17):
            for x in range(6, 27):
                w = (17 - y) / 17
                dx = (x - 16 - math.sin(y * 0.5 + f * 2) * 2 * w) / (9 * (0.3 + 0.7 * (1 - w)))
                if abs(dx) < 1 and (y > 2 or abs(dx) < 0.4):
                    s.put(x, y, ramp([rgb('#ffffff'), rgb('#fff0a0'), rgb('#ffb030'), rgb('#e05a10')], abs(dx) * 0.7 + w * 0.3))
    else:
        for x in range(9, 24):
            s.put(x, 15, rgb('#3a3030'))
            s.put(x, 14, rgb('#2a2020') if x % 2 else rgb('#4a3a30'))
    return s.outline()


def platform(land):
    """A moving platform for each land (48x8)."""
    def make(f):
        from tiles import MATERIALS
        m = MATERIALS[land]
        pl = m['plank'] if land != 2 else m['stone']
        s = Sprite(48, 8)
        for x in range(48):
            for y in range(8):
                v = 0.7 - y * 0.08
                if y == 0:
                    v = 0.95
                if y == 7 or x in (0, 47):
                    v = 0.05
                if land in (0, 1) and x % 12 == 0:
                    v = 0.2
                if land == 3 and y in (2, 5):
                    v = 0.25
                s.put(x, y, ramp(pl, v))
        if land == 2:
            for x in range(4, 44, 8):
                s.put(x, 3, rgb('#80fff0'))
                s.put(x + 1, 3, rgb('#ffffff'))
        return s
    return make


def hud(f):
    """HUD icons (8x8): 0 full heart, 1 empty heart, 2 glint, 3 Wren's head."""
    s = Sprite(8, 8)
    if f in (0, 1):
        for y in range(8):
            for x in range(8):
                u, v = (x - 3.5) / 3.4, -(y - 3.4) / 3.4
                if (u * u + v * v - 1) ** 3 - u * u * v ** 3 < 0:
                    s.put(x, y, mix(rgb('#ff8a9a'), rgb('#c01a3a'), (x + y) / 14) if f == 0 else rgb('#3a2a3a'))
        if f == 0:
            s.put(2, 2, rgb('#ffffff'))
    elif f == 2:
        for y in range(8):
            half = 3.2 - abs(y - 3.5)
            for x in range(int(4 - half), int(4 + half) + 1):
                s.put(x, y, rgb('#fff0a0') if x < 4 else rgb('#ffb030'))
    else:
        rows = ["..KKKK..", ".KHJHHK.", "KhhhsssK", "KhhsssesK"[:8], "KhhSsssK", ".KKrRRK.", "..KRRK..", "........"]
        from_rows(rows, WREN, s=s)
    return s


SMALL = {
    'BOLT': (bolt, 2), 'BIGBOLT': (big_bolt, 2),
    'ORB': (orb([rgb('#6a1a8a'), rgb('#d070ff'), rgb('#ffe0ff')]), 2),
    'SPORE': (orb([rgb('#3a6a2a'), rgb('#a0e060'), rgb('#f0ffc0')]), 2),
    'SHARD': (orb([rgb('#1a5a8a'), rgb('#60f0ff'), rgb('#ffffff')]), 2),
    'FIRE': (orb([rgb('#a02a08'), rgb('#ff8a20'), rgb('#fff0a0')]), 2),
    'HEART': (heart_pickup, 2), 'PRISM': (prism_pickup, 2), 'LIFE': (lantern_pickup, 2),
    'PUFF': (puff, 4), 'SPARK': (spark, 3),
    'CHECK': (checkpoint, 3), 'BEACON': (beacon, 3), 'HUD': (hud, 4),
}

CREATURE_FRAMES = []
for land in range(4):
    for kind, fn in zip(('W', 'F', 'S'), CREATURES[land]):
        CREATURE_FRAMES.append(('%s%d' % (kind, land), (lambda fn=fn: fn(0))))
        CREATURE_FRAMES.append(('%s%db' % (kind, land), (lambda fn=fn: fn(1))))
BOSS_FRAMES = [('B%d_%d' % (i, f), (lambda fn=fn, f=f: fn(f))) for i, fn in enumerate(BOSSES) for f in range(3)]
SMALL_FRAMES = [('%s%d' % (n, f), (lambda fn=fn, f=f: fn(f))) for n, (fn, k) in SMALL.items() for f in range(k)]
