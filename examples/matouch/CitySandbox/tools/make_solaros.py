#!/usr/bin/env python3
"""Writes gt32.sc, GT 32 for SolarOS's `asm run`, from tools/city.sc.

The game is the same; only its bindings change. CitySandbox.ino gives the
script fb[] with frameOffset(), present(), keyDown(), wadLoad() and
setWindow(); SolarOS has display (the back buffer, 800 wide), updateDisplay(),
setPixelsize(), keyHeld() and wad[] (gt32.wad, read next to the script).
The script gets small versions of the sketch's functions written with those.

  ./make_solaros.py [out.sc]     # default: gt32.sc next to this file
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, "city.sc")).read()


def rgb565_swapped(r, g, b):
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return ((v & 255) << 8) | (v >> 8)


BINDINGS = '''// ===== BINDINGS: SolarOS =====
// Generated from tools/city.sc by tools/make_solaros.py: the same game, with
// SolarOS's display, updateDisplay(), setPixelsize(), keyHeld() and wad[]
// (gt32.wad, next to the script) in place of CitySandbox.ino's functions.
// Ctrl-] stops it.
'''

HELPERS = '''// ===== SolarOS versions of CitySandbox.ino's functions =====
int winOn = 0;       // 1: the 400x240 picture in the middle of the panel, at pixel size 1
int backdrops = 0;   // frame buffers still to get the window's surroundings

// where pixel (0, 0) of this frame goes in display
int frameOffset() {
  if (winOn == 1) return 120 * 800 + 200;
  return 0;
}

// Around the window: dark purple fading to black, a pink frame and a cyan one.
void paintBackdrop() {
  int c;
  int v;
  int o;
  int d;
  for (int y = 0; y < 480; y++) {
    c = 0;
    if (winOn == 1) {
      v = ((24 - y / 20) / 8) * 2048 + ((6 - y / 80) / 4) * 32 + (60 - y / 8) / 8;
      c = (v & 255) * 256 + v / 256;
    }
    o = y * 800;
    for (int x = 0; x < 800; x++) display[o + x] = c;
  }
  if (winOn == 0) return;
  for (int f = 0; f < 2; f++) {
    d = 3;
    c = PINK_SW;
    if (f == 1) {
      d = 7;
      c = CYAN_SW;
    }
    for (int t = 0; t < 2; t++) {
      for (int x = 200 - d - t; x < 600 + d + t; x++) {
        display[(120 - d - t) * 800 + x] = c;
        display[(359 + d + t) * 800 + x] = c;
      }
      for (int y = 120 - d - t; y < 360 + d + t; y++) {
        display[y * 800 + 200 - d - t] = c;
        display[y * 800 + 599 + d + t] = c;
      }
    }
  }
}

// the frame is already in the back buffer: show it, the other one is next
void present() {
  updateDisplay();
  if (backdrops > 0) {
    paintBackdrop();
    backdrops = backdrops - 1;
  }
}

// full screen (pixel size 2) or a window in the middle (pixel size 1)
void setWindow(int on) {
  winOn = on;
  if (on == 1) {
    setPixelsize(1);
  } else {
    setPixelsize(2);
  }
  backdrops = 2;
}

// KEY_LEFT 1, KEY_RIGHT 2, KEY_UP 3, KEY_DOWN 4, KEY_QUIT 5, KEY_ENTER 6,
// KEY_VIEW 7, KEY_BRAKE 8, KEY_WANTED 9, KEY_TIME 10 (SolarOS gives the arrows as z q s d)
int keyDown(int key) {
  if (key == 1) return keyHeld(113);
  if (key == 2) return keyHeld(100);
  if (key == 3) return keyHeld(122);
  if (key == 4) return keyHeld(115);
  if (key == 5) return keyHeld(27);
  if (key == 6) return keyHeld(13) | keyHeld(101);
  if (key == 7) return keyHeld(118);
  if (key == 8) return keyHeld(32);
  if (key == 9) return keyHeld(119);
  if (key == 10) return keyHeld(116);
  return 0;
}

// gt32.wad into cwad[]: its length is where its last lump ends
int wadLoad() {
  int n;
  int e;
  int o;
  if (wadLoaded == 0) return 0;
  n = wad[4] + wad[5] * 256;
  e = 8 + n * 16;
  for (int i = 0; i < n; i++) {
    o = 8 + i * 16;
    o = wad[o + 8] + wad[o + 9] * 256 + wad[o + 10] * 65536 + wad[o + 12] + wad[o + 13] * 256 + wad[o + 14] * 65536;
    if (o > e) e = o;
  }
  if (e > WAD_MAX) e = WAD_MAX;
  for (int i = 0; i < e; i++) cwad[i] = wad[i];
  return e;
}

'''.replace("PINK_SW", str(rgb565_swapped(255, 40, 200))).replace("CYAN_SW", str(rgb565_swapped(80, 240, 255)))


def sub1(pattern, repl, s, flags=0):
    out, n = re.subn(pattern, repl, s, count=1, flags=flags)
    assert n == 1, pattern
    return out


out = src
# the sketch's declarations go, SolarOS binds its own names
out = sub1(r"// ===== BINDINGS[^\n]*\n(?:external [^\n]*\n)+", BINDINGS, out)
# the script's copy of the WAD is cwad[]: wad is SolarOS's
out, n = re.subn(r"(?<![.\w])wad(?=\s*[\[,)])", "cwad", out)
assert n > 10, n
out = sub1(r"n = wadLoad\(cwad, WAD_MAX\);", "n = wadLoad();", out)
# drawn straight into SolarOS's back buffer (read again at every frame swap)
out, n = re.subn(r"(?<![.\w])fb\[", "display[", out)
assert n > 3, n
out = sub1(r"(?=// Loads palette)", HELPERS, out)
# SolarOS: say why when the WAD is missing, then take the screen
out = sub1(r"  if \(loadAssets\(\) == 0\) return 1;\n",
           '  if (loadAssets() == 0) {\n'
           '    printfln("gt32: gt32.wad must be next to the script");\n'
           '    return 1;\n'
           '  }\n'
           '  enableGraphicMode();\n', out)
assert "external" not in re.sub(r"//[^\n]*", "", out)

dst = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "gt32.sc")
open(dst, "w").write(out)
print("wrote", dst, len(out), "bytes")
