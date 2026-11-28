#!/usr/bin/env python3
"""Writes the generated tables of gameboy.sc in place: the four greens of the
first Game Boy (RGB565 with the two bytes swapped, as the MaTouch panel
wants), and the 5x7 font of the cartridge list (GT 32's letters, plus the
signs that cartridge file names often have).

  ./make_tables.py
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "CitySandbox", "tools"))
from mkwad import GLYPHS  # noqa: E402

EXTRA = {
    "(": ["   # ", "  #  ", " #   ", " #   ", " #   ", "  #  ", "   # "],
    ")": [" #   ", "  #  ", "   # ", "   # ", "   # ", "  #  ", " #   "],
    "[": [" ### ", " #   ", " #   ", " #   ", " #   ", " #   ", " ### "],
    "]": [" ### ", "   # ", "   # ", "   # ", "   # ", "   # ", " ### "],
    ",": ["     ", "     ", "     ", "     ", " ##  ", "  #  ", " #   "],
    "_": ["     ", "     ", "     ", "     ", "     ", "     ", "#####"],
    "&": [" ##  ", "#  # ", "# #  ", " #   ", "# # #", "#  # ", " ## #"],
    "=": ["     ", "     ", "#####", "     ", "#####", "     ", "     "],
    "?": [" ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  "],
}


def swapped565(r, g, b):
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return ((v & 255) << 8) | (v >> 8)


def font():
    out = [0] * (64 * 7)
    for ch, rows in list(GLYPHS.items()) + list(EXTRA.items()):
        for r, row in enumerate(rows):
            out[(ord(ch) - 32) * 7 + r] = sum(16 >> c for c in range(5) if row[c] == "#")
    return out


SHADES = [(155, 188, 15), (139, 172, 15), (48, 98, 48), (15, 56, 15)]
COLOURS = {
    "C_FRAME": (32, 40, 36),     # the frame around the picture
    "C_LIST": (15, 56, 15),      # the list's background
    "C_TEXT": (139, 172, 15),
    "C_SEL": (155, 188, 15),
    "C_BAR": (48, 98, 48),       # behind the cartridge chosen
}

p = os.path.join(HERE, "gameboy.sc")
s = open(p).read()
s, n = re.subn(r"uint16_t shade\[4\] = \{[^}]*\};",
               "uint16_t shade[4] = {%s};" % ",".join(str(swapped565(*c)) for c in SHADES), s)
assert n == 1
s, n = re.subn(r"uint8_t font\[\d+\] = \{[^}]*\};", "uint8_t font[448] = {%s};" % ",".join(map(str, font())), s)
assert n == 1
for name, c in COLOURS.items():
    s, n = re.subn(r"#define %s \d+" % name, "#define %s %d" % (name, swapped565(*c)), s)
    assert n == 1, name
open(p, "w").write(s)
print("tables written into", p)
