#!/usr/bin/env python3
"""Builds city.wad for the city sandbox (style C, clear day downtown).

Layout (little-endian):
  0   'CWAD'
  4   uint32 lump count
  8   directory: per lump  name[8] (zero padded), uint32 offset, uint32 size
  ... lump data

Lumps
  PALETTE  47 colours x 5 shades x 8 fog levels, RGB565   index = (colour*5+shade)*8+fog
  SKYCOLS  8 x RGB565: 6 sky bands (top to horizon), fog colour, spare
  SINTAB   1024 x int16, sin(2*pi*i/1024) * 4096
  PATTERNS 8 window styles x 8 rows; bit c of row (floor & 7) = window present in column c & 7
  CARMESH  int16 box count, then per box x0 x1 z0 z1 y0 y1 colour (24.8; x to the right,
           z forward, y up, around the car's centre on the ground)
  PEDMESH  the same for the person on foot
  CARLOD   the car from afar, a few boxes; PEDLOD  the person from afar
  COPMESH  the police car: the car, black below, white above (colour 20,
           painted white by the script), a light bar red (21) and blue (5)
  COPLOD   the police car from afar
  WOMMESH  a woman (the person on foot is a man); WOMLOD from afar
  SEDMESH, SEDLOD, VANMESH, VANLOD  a saloon and a van of the traffic
  COLRGB   the 47 colours as r g b bytes: the script relights the palette by the time of day
  FONT     5x7 characters 32..95, 7 rows each, bit 4 = leftmost column
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
    (170, 34, 34),    # 21 tail light
    (58, 92, 168),    # 22 shirt
    (232, 190, 158),  # 23 skin
    (196, 48, 52),    # 24 awning: red
    (36, 128, 84),    # 25 awning: green
    (44, 86, 160),    # 26 awning: blue
    (226, 170, 48),   # 27 awning: ochre
    (62, 84, 128),    # 28 trousers or skirt (recoloured per person)
    (70, 48, 32),     # 29 hair (recoloured per person)
    (236, 230, 214),  # 30 cream: table tops, stripes
    (84, 58, 40),     # 31 dark wood: chairs, café fascias
    (244, 206, 176),  # 32 skin: fair
    (204, 154, 112),  # 33 skin: tan
    (150, 100, 68),   # 34 skin: brown
    (96, 62, 44),     # 35 skin: dark
    (222, 186, 108),  # 36 hair: blond
    (30, 28, 30),     # 37 hair: black
    (150, 68, 38),    # 38 hair: auburn
    (172, 172, 172),  # 39 hair: grey
    (60, 110, 255),   # 40 police light, blue (lights up at night, as 9 and 21 do)
    (255, 206, 120),  # 41 a lit window
    (112, 96, 66),    # 42 the light under a street lamp
    (196, 186, 146),  # 43 headlights on the road
    (60, 230, 90),    # 44 marker: a mission starts here
    (255, 214, 40),   # 45 marker: the mission's target
    (255, 60, 50),    # 46 brake lights on, and the police light's red
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
CAR = [  # a low sports coupe
    (-2.0, 2.0, -4.6, 4.6, 0.45, 1.2, 20),     # lower body
    (-1.9, 1.9, 4.6, 4.85, 0.35, 0.75, 19),    # front splitter
    (-1.9, 1.9, -4.85, -4.6, 0.35, 0.8, 19),   # rear diffuser
    (-1.9, 1.9, 1.2, 4.6, 1.2, 1.45, 20),      # long low bonnet
    (-2.0, -1.55, 2.4, 4.2, 1.2, 1.6, 20),     # front wheel arches
    (1.55, 2.0, 2.4, 4.2, 1.2, 1.6, 20),
    (-2.05, -1.5, -3.9, -2.0, 1.2, 1.75, 20),  # rear haunches
    (1.5, 2.05, -3.9, -2.0, 1.2, 1.75, 20),
    (-1.9, 1.9, -4.6, -2.0, 1.2, 1.6, 20),     # rear deck
    (-1.6, 1.6, -2.0, 1.2, 1.45, 2.2, 10),     # glasshouse
    (-1.45, 1.45, -1.7, 0.5, 2.2, 2.45, 10),   # windscreen, stepped back
    (-1.4, 1.4, -1.4, 0.2, 2.45, 2.6, 20),     # roof
    (-1.6, -1.4, 0.9, 1.2, 1.45, 2.2, 20),     # windscreen pillars
    (1.4, 1.6, 0.9, 1.2, 1.45, 2.2, 20),
    (-1.9, 1.9, -4.5, -3.9, 2.0, 2.15, 20),    # rear wing
    (-1.2, -1.0, -4.3, -4.1, 1.6, 2.0, 19),    # its struts
    (1.0, 1.2, -4.3, -4.1, 1.6, 2.0, 19),
    (-2.15, -1.55, 2.6, 3.9, 0.0, 1.3, 19),    # wheels
    (1.55, 2.15, 2.6, 3.9, 0.0, 1.3, 19),
    (-2.15, -1.55, -3.9, -2.6, 0.0, 1.3, 19),
    (1.55, 2.15, -3.9, -2.6, 0.0, 1.3, 19),
    (-2.22, -2.15, 2.95, 3.55, 0.35, 0.95, 8),  # rims
    (2.15, 2.22, 2.95, 3.55, 0.35, 0.95, 8),
    (-2.22, -2.15, -3.55, -2.95, 0.35, 0.95, 8),
    (2.15, 2.22, -3.55, -2.95, 0.35, 0.95, 8),
    (-1.8, -1.0, 4.55, 4.65, 0.95, 1.15, 9),   # slim headlights
    (1.0, 1.8, 4.55, 4.65, 0.95, 1.15, 9),
    (-1.8, -0.6, -4.65, -4.55, 1.25, 1.45, 21),  # tail lights
    (0.6, 1.8, -4.65, -4.55, 1.25, 1.45, 21),
    (-0.5, 0.5, -4.9, -4.85, 0.5, 0.75, 4),    # number plate
    (-1.2, -0.9, -4.95, -4.85, 0.4, 0.55, 8),  # exhausts
    (0.9, 1.2, -4.95, -4.85, 0.4, 0.55, 8),
    (-2.2, -1.9, 0.7, 0.95, 1.55, 1.75, 20),   # door mirrors
    (1.9, 2.2, 0.7, 0.95, 1.55, 1.75, 20),
]
PERSON = [
    (-0.5, -0.1, -0.3, 0.45, 0.0, 0.25, 19),   # shoes
    (0.1, 0.5, -0.3, 0.45, 0.0, 0.25, 19),
    (-0.48, -0.1, -0.25, 0.25, 0.25, 1.75, 28),  # trouser legs
    (0.1, 0.48, -0.25, 0.25, 0.25, 1.75, 28),
    (-0.52, 0.52, -0.28, 0.28, 1.7, 1.85, 19),  # belt
    (-0.55, 0.55, -0.3, 0.3, 1.85, 2.95, 22),   # shirt
    (-0.8, -0.55, -0.18, 0.18, 2.2, 2.95, 22),  # upper arms
    (0.55, 0.8, -0.18, 0.18, 2.2, 2.95, 22),
    (-0.78, -0.57, -0.15, 0.15, 1.6, 2.2, 23),  # forearms and hands
    (0.57, 0.78, -0.15, 0.15, 1.6, 2.2, 23),
    (-0.15, 0.15, -0.12, 0.12, 2.95, 3.1, 23),  # neck
    (-0.28, 0.28, -0.28, 0.3, 3.1, 3.62, 23),   # head
    (-0.3, 0.3, -0.3, 0.3, 3.62, 3.72, 29),     # hair on top
    (-0.3, 0.3, -0.34, -0.28, 3.3, 3.62, 29),   # and at the back
    (-0.2, -0.08, 0.3, 0.32, 3.36, 3.44, 19),   # eyes
    (0.08, 0.2, 0.3, 0.32, 3.36, 3.44, 19),
]
# a woman: the same 16 parts in the same order (legs 0-3 and arms 6-9 swing),
# slimmer, bare legs under a skirt, long hair at the back
WOMAN = [
    (-0.42, -0.1, -0.25, 0.35, 0.0, 0.2, 19),   # shoes
    (0.1, 0.42, -0.25, 0.35, 0.0, 0.2, 19),
    (-0.38, -0.12, -0.15, 0.15, 0.2, 1.4, 23),  # legs
    (0.12, 0.38, -0.15, 0.15, 0.2, 1.4, 23),
    (-0.6, 0.6, -0.38, 0.38, 1.3, 2.1, 28),     # skirt
    (-0.48, 0.48, -0.26, 0.26, 2.1, 2.9, 22),   # top
    (-0.7, -0.48, -0.15, 0.15, 2.25, 2.9, 22),  # upper arms
    (0.48, 0.7, -0.15, 0.15, 2.25, 2.9, 22),
    (-0.68, -0.5, -0.13, 0.13, 1.7, 2.25, 23),  # forearms and hands
    (0.5, 0.68, -0.13, 0.13, 1.7, 2.25, 23),
    (-0.13, 0.13, -0.1, 0.1, 2.9, 3.02, 23),    # neck
    (-0.26, 0.26, -0.26, 0.28, 3.02, 3.5, 23),  # head
    (-0.29, 0.29, -0.3, 0.3, 3.5, 3.6, 29),     # hair on top
    (-0.34, 0.34, -0.4, -0.26, 2.6, 3.56, 29),  # long at the back
    (-0.18, -0.07, 0.28, 0.3, 3.24, 3.31, 19),  # eyes
    (0.07, 0.18, 0.28, 0.3, 3.24, 3.31, 19),
]
# traffic: a four-door saloon and a van (colour 20 is painted per car)
SEDAN = [
    (-1.9, 1.9, -4.4, 4.4, 0.45, 1.3, 20),     # lower body
    (-1.9, 1.9, 4.4, 4.6, 0.4, 0.75, 19),      # bumpers
    (-1.9, 1.9, -4.6, -4.4, 0.4, 0.75, 19),
    (-1.8, 1.8, 2.0, 4.4, 1.3, 1.6, 20),       # bonnet
    (-1.8, 1.8, -4.4, -2.6, 1.3, 1.65, 20),    # boot
    (-1.65, 1.65, -2.6, 2.0, 1.3, 2.6, 10),    # windows all round
    (-1.55, 1.55, -2.2, 1.4, 2.6, 2.8, 20),    # roof
    (-1.67, -1.65, -0.4, 0.0, 1.3, 2.6, 20),   # door pillars
    (1.65, 1.67, -0.4, 0.0, 1.3, 2.6, 20),
    (-2.0, -1.5, 2.4, 3.6, 0.0, 1.15, 19),     # wheels
    (1.5, 2.0, 2.4, 3.6, 0.0, 1.15, 19),
    (-2.0, -1.5, -3.6, -2.4, 0.0, 1.15, 19),
    (1.5, 2.0, -3.6, -2.4, 0.0, 1.15, 19),
    (-1.7, -0.8, 4.4, 4.5, 0.95, 1.2, 9),      # headlights
    (0.8, 1.7, 4.4, 4.5, 0.95, 1.2, 9),
    (-1.7, -0.9, -4.5, -4.4, 1.0, 1.3, 21),    # tail lights
    (0.9, 1.7, -4.5, -4.4, 1.0, 1.3, 21),
    (-0.5, 0.5, -4.65, -4.6, 0.5, 0.72, 4),    # number plate
]
SEDAN_LOD = [
    (-1.9, 1.9, -4.4, 4.4, 0.35, 1.5, 20),
    (-1.65, 1.65, -2.6, 2.0, 1.5, 2.75, 10),
    (-2.0, 2.0, 2.4, 3.6, 0.0, 0.9, 19),
    (-2.0, 2.0, -3.6, -2.4, 0.0, 0.9, 19),
]
VAN = [
    (-2.0, 2.0, -4.8, 4.8, 0.5, 1.6, 20),      # lower body
    (-2.0, 2.0, 4.8, 5.0, 0.4, 0.8, 19),       # bumpers
    (-2.0, 2.0, -5.0, -4.8, 0.4, 0.8, 19),
    (-2.0, 2.0, -4.8, 2.4, 1.6, 3.6, 20),      # the box
    (-1.9, 1.9, 2.4, 3.1, 1.6, 3.3, 10),       # windscreen
    (-1.9, 1.9, 3.1, 4.8, 1.6, 1.9, 20),       # short bonnet
    (-2.03, -2.0, 1.0, 2.3, 2.2, 3.0, 10),     # side windows
    (2.0, 2.03, 1.0, 2.3, 2.2, 3.0, 10),
    (-2.03, -2.0, -4.6, 0.8, 2.0, 2.3, 4),     # a stripe along each side
    (2.0, 2.03, -4.6, 0.8, 2.0, 2.3, 4),
    (-2.1, -1.6, 2.6, 3.8, 0.0, 1.2, 19),      # wheels
    (1.6, 2.1, 2.6, 3.8, 0.0, 1.2, 19),
    (-2.1, -1.6, -4.0, -2.8, 0.0, 1.2, 19),
    (1.6, 2.1, -4.0, -2.8, 0.0, 1.2, 19),
    (-1.8, -1.0, 4.8, 4.9, 1.0, 1.3, 9),       # headlights
    (1.0, 1.8, 4.8, 4.9, 1.0, 1.3, 9),
    (-1.95, -1.6, -4.9, -4.8, 0.8, 1.8, 21),   # tail lights
    (1.6, 1.95, -4.9, -4.8, 0.8, 1.8, 21),
]
VAN_LOD = [
    (-2.0, 2.0, -4.8, 2.4, 0.4, 3.6, 20),
    (-2.0, 2.0, 2.4, 4.8, 0.4, 1.9, 20),
    (-1.9, 1.9, 2.4, 3.1, 1.9, 3.3, 10),
    (-2.1, 2.1, 2.6, 3.8, 0.0, 0.9, 19),
    (-2.1, 2.1, -4.0, -2.8, 0.0, 0.9, 19),
]

CAR_LOD = [
    (-2.0, 2.0, -4.6, 4.6, 0.35, 1.5, 20),     # body
    (-1.6, 1.6, -2.0, 1.2, 1.5, 2.4, 10),      # glasshouse
    (-2.15, 2.15, 2.6, 3.9, 0.0, 1.0, 19),     # front wheels
    (-2.15, 2.15, -3.9, -2.6, 0.0, 1.0, 19),   # rear wheels
]
PERSON_LOD = [
    (-0.48, 0.48, -0.25, 0.25, 0.0, 1.8, 28),  # legs
    (-0.6, 0.6, -0.3, 0.3, 1.8, 3.0, 22),      # shirt and arms
    (-0.28, 0.28, -0.28, 0.3, 3.0, 3.7, 23),   # head
]
WOMAN_LOD = [
    (-0.38, 0.38, -0.15, 0.15, 0.0, 1.3, 23),  # legs
    (-0.6, 0.6, -0.38, 0.38, 1.3, 2.1, 28),    # skirt
    (-0.55, 0.55, -0.26, 0.26, 2.1, 2.9, 22),  # top and arms
    (-0.3, 0.3, -0.4, -0.26, 2.7, 3.6, 29),    # hair
    (-0.26, 0.26, -0.26, 0.3, 2.95, 3.5, 23),  # face
]

# the police car: the car with its lower body black, and a light bar
COP = [((b[0], b[1], b[2], b[3], b[4], b[5], 19) if i == 0 else b) for i, b in enumerate(CAR)] + [
    (-1.1, 1.1, -0.7, -0.1, 2.6, 2.7, 19),     # light bar
    (-1.0, -0.05, -0.65, -0.15, 2.7, 2.95, 46),  # red
    (0.05, 1.0, -0.65, -0.15, 2.7, 2.95, 40),  # blue
]
COP_LOD = [
    (-2.0, 2.0, -4.6, 4.6, 0.35, 1.0, 19),     # black below
    (-2.0, 2.0, -4.6, 4.6, 1.0, 1.5, 20),      # white above
    (-1.6, 1.6, -2.0, 1.2, 1.5, 2.4, 10),      # glasshouse
    (-1.0, 0.0, -0.65, -0.15, 2.4, 2.7, 46),   # lights
    (0.0, 1.0, -0.65, -0.15, 2.4, 2.7, 40),
]

def mesh(boxes):
    out = struct.pack("<h", len(boxes))
    for b in boxes:
        out += struct.pack("<7h", *[int(round(v * U)) for v in b[:6]] + [b[6]])
    return out

def rgb565(c):
    r, g, b = (max(0, min(255, int(round(v)))) for v in c)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

# 5x7 capitals, digits and a little punctuation, ASCII 32..95; '#' is ink
GLYPHS = {
    "A": [" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    "B": ["#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "],
    "C": [" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "],
    "D": ["#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "],
    "E": ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"],
    "F": ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "],
    "G": [" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####"],
    "H": ["#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    "I": [" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    "J": ["  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "],
    "K": ["#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"],
    "L": ["#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"],
    "M": ["#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"],
    "N": ["#   #", "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #"],
    "O": [" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    "P": ["#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "],
    "Q": [" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"],
    "R": ["#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"],
    "S": [" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "],
    "T": ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "],
    "U": ["#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    "V": ["#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "],
    "W": ["#   #", "#   #", "#   #", "# # #", "# # #", "# # #", " # # "],
    "X": ["#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"],
    "Y": ["#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "],
    "Z": ["#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"],
    "0": [" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "],
    "1": ["  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    "2": [" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"],
    "3": ["#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "],
    "4": ["   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "],
    "5": ["#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "],
    "6": ["  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "],
    "7": ["#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "],
    "8": [" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "],
    "9": [" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "],
    ".": ["     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "],
    "!": ["  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "     ", "  #  "],
    "-": ["     ", "     ", "     ", "#####", "     ", "     ", "     "],
    ":": ["     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     "],
    "$": ["  #  ", " ####", "# #  ", " ### ", "  # #", "#### ", "  #  "],
    "+": ["     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "],
    "/": ["    #", "    #", "   # ", "  #  ", " #   ", "#    ", "#    "],
    "'": ["  #  ", "  #  ", " #   ", "     ", "     ", "     ", "     "],
}

def font():
    out = bytearray(64 * 7)
    for ch, rows in GLYPHS.items():
        for r, row in enumerate(rows):
            out[(ord(ch) - 32) * 7 + r] = sum(16 >> c for c in range(5) if row[c] == "#")
    return bytes(out)

def build():
    assert len(COLOURS) == 47 and len(PATTERNS) == 8
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
        (b"CARLOD", mesh(CAR_LOD)),
        (b"PEDLOD", mesh(PERSON_LOD)),
        (b"COPMESH", mesh(COP)),
        (b"COPLOD", mesh(COP_LOD)),
        (b"FONT", font()),
        (b"COLRGB", bytes(v for c in COLOURS for v in c)),
        (b"WOMMESH", mesh(WOMAN)),
        (b"WOMLOD", mesh(WOMAN_LOD)),
        (b"SEDMESH", mesh(SEDAN)),
        (b"SEDLOD", mesh(SEDAN_LOD)),
        (b"VANMESH", mesh(VAN)),
        (b"VANLOD", mesh(VAN_LOD)),
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
