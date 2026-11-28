#!/usr/bin/env python3
"""Runs tools/gameboy.sc compiled as C on this computer with a cartridge:
prints what it sends through the serial port (where Blargg's test
cartridges write their results) and a checksum of the last picture.

  ./host_check.py ROM.gb [frames] [--png out.png] [--keys F:KEYS,...]

--every N prints the picture's checksum every N frames and runs on past
"Passed" (tools/board_check.py compares them with the board's).
--keys holds buttons at given frames, e.g. 120:start,200:a (a b start
select up down left right), each for 5 frames.
"""
import os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
args = [a for a in sys.argv[1:]]
png = None
keys = ""
every = 0
script_path = os.path.join(HERE, "gameboy.sc")
if "--script" in args:
    i = args.index("--script")
    script_path = args[i + 1]
    del args[i:i + 2]
if "--every" in args:
    i = args.index("--every")
    every = int(args[i + 1])
    del args[i:i + 2]
if "--png" in args:
    i = args.index("--png")
    png = args[i + 1]
    del args[i:i + 2]
if "--keys" in args:
    i = args.index("--keys")
    keys = args[i + 1]
    del args[i:i + 2]
rom = os.path.abspath(args[0])
frames = int(args[1]) if len(args) > 1 else 600

KEYCODE = {"a": 107, "b": 106, "start": 13, "select": 9, "up": 122, "down": 115, "left": 113, "right": 100}
holds = []
for k in filter(None, keys.split(",")):
    f, names = k.split(":")
    for n in names.split("+"):
        holds.append((int(f), KEYCODE[n]))

script = open(script_path).read().replace("int main() {", "int script_main() {")
hold_c = " ".join("if (c == %d && frameNo >= %d && frameNo < %d) return 1;" % (c, f, f + 5) for f, c in holds)
harness = r'''#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
/* two buffers swapped at every updateDisplay(), as on the panel: a frame
 * that draws nothing (the LCD off) leaves the one before last behind */
static uint16_t fbufs[2][800 * 480];
uint16_t *display = fbufs[0];
#define fbuf display
static uint8_t romBuf[8 << 20];
uint8_t *rom = romBuf;
static int romBytes;
static int frameNo = 0, menuFrames = 0, inGame = 0;
static int FRAMES = %d, EVERY = %d;
extern uint8_t serBuf[];
extern int serN;
extern int picOff;   /* where the script puts the picture */
/* for a script with trace() calls added (state after each frame) */
static int traceN = 0;
void trace(int a, int b, int c, int d) { if (traceN < 1200) printf("T %%d %%d %%d %%d %%d\n", traceN, a, b, c, d); traceN++; }
int millis() { return frameNo * 17; }
void delay(int ms) { }
void enableGraphicMode() { }
void setPixelsize(int s) { }
int romCount() { return 1; }
int romNameChar(int i, int j) { const char *n = "TEST.GB"; return j < 7 ? n[j] : 0; }
int romLoad(int i) { inGame = 1; return romBytes; }
int keyHeld(int c) {
  if (!inGame) return c == 13 && menuFrames == 1;
  %s
  return 0;
}
static uint32_t pictureSum() {
  uint32_t h = 2166136261u;
  for (int y = 0; y < 144; y++)
    for (int x = 0; x < 160; x++) {
      uint16_t v = fbuf[picOff + y * 800 + x];
      h = (h ^ (v & 255)) * 16777619u;
      h = (h ^ (v >> 8)) * 16777619u;
    }
  return h;
}
static void finish() {
  fwrite(serBuf, 1, serN, stdout);
  printf("\n--- %%d frames\n", frameNo);
  uint32_t h = 2166136261u;
  for (int y = 0; y < 144; y++)
    for (int x = 0; x < 160; x++) {
      uint16_t v = fbuf[picOff + y * 800 + x];
      h = (h ^ (v & 255)) * 16777619u;
      h = (h ^ (v >> 8)) * 16777619u;
    }
  printf("picture checksum %%08x\n", h);
  FILE *o = fopen("last.raw", "wb");
  for (int y = 0; y < 144; y++) fwrite(fbuf + picOff + y * 800, 2, 160, o);
  fclose(o);
  exit(0);
}
static void swapBuffers() { display = display == fbufs[0] ? fbufs[1] : fbufs[0]; }
void updateDisplay() {
  if (!inGame) { menuFrames++; swapBuffers(); return; }
  frameNo++;
  if (EVERY && frameNo %% EVERY == 0) printf("GBTEST frame %%d checksum %%08x\n", frameNo, pictureSum());
  if (frameNo >= FRAMES) finish();
  /* Blargg's cartridges end with "Passed" or "Failed" */
  if (!EVERY && serN > 6 && (memmem(serBuf, serN, "Passed", 6) || memmem(serBuf, serN, "Failed", 6))) finish();
  swapBuffers();
}
''' % (frames, every, hold_c)
main = r'''
int main() {
  FILE *f = fopen(ROMPATH, "rb");
  romBytes = fread(romBuf, 1, sizeof(romBuf), f);
  fclose(f);
  return script_main();
}
'''
work = tempfile.mkdtemp()
src = os.path.join(work, "gb.c")
open(src, "w").write(harness + script + main)
exe = os.path.join(work, "gb")
subprocess.run(["cc", "-O2", "-fwrapv", "-w", '-DROMPATH="%s"' % rom, "-o", exe, src], check=True)
out = subprocess.run([exe], capture_output=True, text=True, cwd=work, errors="replace").stdout
print(out, end="")
if png:
    from PIL import Image
    import struct
    px = struct.unpack("<%dH" % (160 * 144), open(os.path.join(work, "last.raw"), "rb").read())
    im = Image.new("RGB", (160, 144))
    def rgb(v):
        v = ((v & 255) << 8) | (v >> 8)
        return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    im.putdata([rgb(p) for p in px])
    im.resize((480, 432), Image.NEAREST).save(png)
