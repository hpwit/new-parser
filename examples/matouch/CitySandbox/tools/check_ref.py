#!/usr/bin/env python3
"""Compiles tools/city.sc as C on this computer with the sketch's scripted
tour (200 frames, a simulated clock of 50 ms a frame) and prints the checksum
of every 10th frame: CitySandbox.ino prints the same on the board.

  ./check_ref.py            # checksums
  ./check_ref.py --png      # also city_59.png ... from four frames (needs Pillow)
"""
import os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
script = open(os.path.join(HERE, "city.sc")).read()
script = re.sub(r"^external .*\n", "", script, flags=re.M).replace("int main() {", "int script_main() {")
harness = r'''#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
uint16_t fb[96000];
static int frameNo = 0;
static unsigned char wadBytes[8192];
static int wadSize;
int millis() { return frameNo * 50; }
#define TOUR 200
/* the same tour as CitySandbox.ino's tourKey() */
static int tourKey(int f, int key) {
  switch (key) {
  case 3: return f < 90 || (f >= 106 && f < 130) || f >= 180;
  case 2: return f >= 60 && f < 90;
  case 8: return f >= 90 && f < 105;
  case 6: return f == 105 || f == 179;
  case 4: return f >= 131 && f < 179;
  case 7: return f == 130 || f == 185 || f == 195;
  }
  return 0;
}
int keyDown(int key) { return frameNo < TOUR ? tourKey(frameNo, key) : 0; }
int wadLoad(uint8_t *dst, int maxlen) { int n = wadSize < maxlen ? wadSize : maxlen; memcpy(dst, wadBytes, n); return n; }
static int dumpFrames = 0;
void present() {
  uint32_t h = 2166136261u;
  const uint8_t *b = (const uint8_t *)fb;
  for (int i = 0; i < 192000; i++) h = (h ^ b[i]) * 16777619u;
  if (frameNo % 10 == 9) printf("FRAME %d checksum %08x\n", frameNo, h);
  if (dumpFrames && (frameNo == 59 || frameNo == 120 || frameNo == 170 || frameNo == 199)) {
    char n[32]; sprintf(n, "city_%d.raw", frameNo); FILE *o = fopen(n, "wb"); fwrite(fb, 2, 96000, o); fclose(o);
  }
  frameNo++;
  if (frameNo == TOUR) exit(0);
}
'''
main = r'''
int main(int argc, char **argv) {
  dumpFrames = argc > 1;
  FILE *f = fopen(WADPATH, "rb");
  wadSize = fread(wadBytes, 1, sizeof(wadBytes), f);
  fclose(f);
  return script_main();
}
'''
work = tempfile.mkdtemp()
src = os.path.join(work, "city_ref.c")
open(src, "w").write(harness + script + main)
exe = os.path.join(work, "city_ref")
wad = os.path.join(HERE, "..", "data", "city.wad")
subprocess.run(["cc", "-O1", "-fwrapv", "-w", '-DWADPATH="%s"' % wad, "-o", exe, src], check=True)
png = "--png" in sys.argv
out = subprocess.run([exe] + (["dump"] if png else []), capture_output=True, text=True, cwd=work).stdout
print(out, end="")
if png:
    from PIL import Image
    import struct
    for n in (59, 120, 170, 199):
        px = struct.unpack("<96000H", open(os.path.join(work, "city_%d.raw" % n), "rb").read())
        im = Image.new("RGB", (400, 240))
        im.putdata([(((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31) for p in px])
        im.resize((800, 480), Image.NEAREST).save("city_%d.png" % n)
