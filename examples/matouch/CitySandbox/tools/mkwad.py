#!/usr/bin/env python3
"""Builds city.wad for the city sandbox (style C, clear day downtown).

Layout (little-endian):
  0   'CWAD'
  4   uint32 lump count
  8   directory: per lump  name[8] (zero padded), uint32 offset, uint32 size
  ... lump data

Lumps
  PALETTE  24 colours x 5 shades x 8 fog levels, RGB565   index = (colour*5+shade)*8+fog
  SKYCOLS  8 x RGB565: 6 sky bands (top to horizon), fog colour, spare
  SINTAB   1024 x int16, sin(2*pi*i/1024) * 4096
  PATTERNS 8 window styles x 8 rows; bit c of row (floor & 7) = window present in column c & 7
  CARMESH  int16 box count, then per box x0 x1 z0 z1 y0 y1 colour (24.8; x to the right,
           z forward, y up, around the car's centre on the ground)
  PEDMESH  the same for the person on foot
Edit the tables below and re-run, from tools/:
  python3 mkwad.py ../data/city.wad ../city_wad.h
(the second file is the WAD as a C array, which CitySandbox.ino compiles in)
"""
import math, struct, sys

FOG = (206, 226, 246)
SHADES = (1.00, 0.90, 0.78, 0.66, 0.54)          # top, +x, +z, -z, -x faces
COLOURS = [
    (86, 88, 96),     # 0 road
    (176, 176, 170),  # 1 sidewalk
    (150, 150, 146),  # 2 lot paving
    (96, 160, 84),    # 3 grass
    (240, 240, 240),  # 4 lane paint
    (64, 124, 190),   # 5 water
    (110, 80, 56),    # 6 tree trunk
    (70, 140, 70),    # 7 leaves
    (90, 94, 100),    # 8 lamp pole
    (230, 230, 220),  # 9 lamp head
    (70, 110, 150),   # 10 window glass
    (140, 180, 214),  # 11 window glass, sky reflection
    (200, 196, 186),  # 12 building: concrete
    (168, 176, 188),  # 13 building: blue-grey
    (188, 120, 96),   # 14 building: brick
    (224, 214, 190),  # 15 building: sandstone
    (130, 150, 172),  # 16 building: steel
    (150, 156, 150),  # 17 building: grey-green
    (88, 156, 78),    # 18 leaves, light
    (36, 36, 40),     # 19 dark trim
    (184, 34, 44),    # 20 car paint
    (240, 70, 60),    # 21 tail light
    (58, 92, 168),    # 22 shirt
    (232, 190, 158),  # 23 skin
]
SKY = [(60, 130, 220), (86, 152, 230), (116, 174, 238), (150, 196, 244),
       (184, 216, 248), (206, 226, 246)]
PATTERNS = [
    [255] * 8,                       # 0 full rows
    [219] * 8,                       # 1 pairs with a gap
    [102] * 8,                       # 2 two pairs
    [255, 126] * 4,                  # 3 alternating wide / narrow floors
    [170] * 8,                       # 4 every other column
    [255, 255, 255, 0] * 2,          # 5 blank service floor every 4th
    [126] * 8,                       # 6 inset
    [231] * 8,                       # 7 split centre
]

U = 256  # one unit, about 0.5 m
# boxes: x0, x1, z0, z1, y0, y1 in units, colour
CAR = [
    (-2.0, 2.0, -4.5, 4.5, 0.6, 2.2, 20),    # body
    (-1.7, 1.7, -2.4, 1.4, 2.2, 3.4, 10),    # cabin, glass
    (-1.8, 1.8, -2.2, 1.2, 3.4, 3.6, 20),    # roof
    (-2.2, -1.6, 2.4, 3.8, 0.0, 1.3, 19),    # wheels
    (1.6, 2.2, 2.4, 3.8, 0.0, 1.3, 19),
    (-2.2, -1.6, -3.8, -2.4, 0.0, 1.3, 19),
    (1.6, 2.2, -3.8, -2.4, 0.0, 1.3, 19),
    (-1.6, 1.6, 4.5, 4.7, 1.2, 1.7, 9),      # headlights
    (-1.6, 1.6, -4.7, -4.5, 1.2, 1.7, 21),   # tail lights
]
PERSON = [
    (-0.5, -0.1, -0.3, 0.3, 0.0, 1.8, 19),   # legs
    (0.1, 0.5, -0.3, 0.3, 0.0, 1.8, 19),
    (-0.6, 0.6, -0.35, 0.35, 1.8, 3.0, 22),  # torso
    (-0.3, 0.3, -0.3, 0.3, 3.0, 3.6, 23),    # head
]

def mesh(boxes):
    out = struct.pack("<h", len(boxes))
    for b in boxes:
        out += struct.pack("<7h", *[int(round(v * U)) for v in b[:6]] + [b[6]])
    return out

def rgb565(c):
    r, g, b = (max(0, min(255, int(round(v)))) for v in c)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def build():
    assert len(COLOURS) == 24 and len(PATTERNS) == 8
    pal = []
    for col in COLOURS:
        for sh in SHADES:
            for f in range(8):
                t = f / 7.0
                pal.append(rgb565([col[i] * sh * (1 - t) + FOG[i] * t for i in range(3)]))
    sky = [rgb565(c) for c in SKY] + [rgb565(FOG), 0]
    sint = [int(round(math.sin(2 * math.pi * i / 1024) * 4096)) for i in range(1024)]
    lumps = [
        (b"PALETTE", struct.pack("<%dH" % len(pal), *pal)),
        (b"SKYCOLS", struct.pack("<8H", *sky)),
        (b"SINTAB", struct.pack("<1024h", *sint)),
        (b"PATTERNS", bytes(v for row in PATTERNS for v in row)),
        (b"CARMESH", mesh(CAR)),
        (b"PEDMESH", mesh(PERSON)),
    ]
    off = 8 + 16 * len(lumps)
    head = b"CWAD" + struct.pack("<I", len(lumps))
    body = b""
    for name, data in lumps:
        head += name.ljust(8, b"\0") + struct.pack("<II", off + len(body), len(data))
        body += data
    return head + body

def header(data):
    h = "// city.wad (mkwad.py), %d bytes\nstatic const uint8_t cityWad[%d] = {\n" % (len(data), len(data))
    for i in range(0, len(data), 16):
        h += "  " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",\n"
    return h + "};\n"

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "city.wad"
    data = build()
    open(out, "wb").write(data)
    print("%s: %d bytes" % (out, len(data)))
    if len(sys.argv) > 2:
        open(sys.argv[2], "w").write(header(data))
        print("%s: the same as a C array" % sys.argv[2])
