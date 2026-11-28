#!/usr/bin/env python3
"""Compares the board with this computer on one cartridge.

Build GameBoy.ino with -DGB_TEST=N and a test_rom.h holding the cartridge
(`const uint8_t test_rom[] = {...};`), upload it, then:

  ./board_check.py ROM.gb N FRAMES PORT

runs tools/host_check.py for the checksums of the picture every N frames,
resets the board, reads its "GBTEST frame F checksum H" lines and says
which match. Needs pyserial.
"""
import re, subprocess, sys, time
import serial

rom, every, frames, port = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
here = sys.path[0]
out = subprocess.run([sys.executable, here + "/host_check.py", rom, str(frames), "--every", str(every)],
                     capture_output=True, text=True, errors="replace").stdout
want = dict(re.findall(r"GBTEST frame (\d+) checksum ([0-9a-f]{8})", out))
print("computer: %d checksums" % len(want))

s = serial.Serial(port, 115200, timeout=1)
s.rts = True
time.sleep(0.1)
s.rts = False
got = {}
start = time.time()
while time.time() - start < 600 and len(got) < len(want):
    line = s.readline().decode(errors="replace").strip()
    if "compiled in" in line or "rror" in line:
        print(line)
    m = re.match(r"GBTEST frame (\d+) checksum ([0-9a-f]{8})", line)
    if m:
        got[m.group(1)] = m.group(2)
        print("frame %5s  board %s  computer %s  %s" % (m.group(1), m.group(2), want.get(m.group(1)),
                                                    "ok" if want.get(m.group(1)) == m.group(2) else "DIFFERENT"))
print("ALL %d MATCH" % len(want) if got == want else "MISMATCH or incomplete (%d of %d)" % (len(got), len(want)))
