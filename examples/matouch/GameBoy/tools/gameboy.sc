// ============================================================
// GAME BOY  -  an emulator of the first Game Boy (DMG), as one script
//
// The CPU (all of its instructions, timed in clock cycles), the memory map
// with the cartridge chips MBC1, MBC3 and MBC5, the timer, the interrupts,
// the picture (background, window and sprites, a line at a time) and the
// buttons. No sound. 160x144 in the middle of the panel, at the pixel size
// below (3: shown 3x, 480x432).
//
// The host gives it, as SolarOS does (and GameBoy.ino the same names):
//   display, updateDisplay(), setPixelsize(n), enableGraphicMode(),
//   keyHeld(c), millis(), delay(ms)
// and for the cartridges:
//   romCount()            how many .gb files there are
//   romNameChar(i, j)     the j-th letter of the i-th name, 0 at its end
//   romLoad(i)            reads the i-th into rom[], returns its size (0: failed)
//   rom                   the bytes of the cartridge loaded
//
// Keys: arrows (or Z Q S D) the cross, K or Space A, J or B B, Enter Start,
// Tab or Backspace Select, Escape back to the list of cartridges.
//
// Language notes: ^ is a power, so xor is written (a | b) - (a & b), and
// there is no ~ (not v is 255 - v); functions come before their use.
// ============================================================

// the panel's pixel size: 3 shows the Game Boy's 160x144 three times as
// big (480x432), 2 twice (320x288), 1 as it is; the picture is centred
#define pixelsize 3

#define STRIDE 800
#define FRAME_CYC 70224
// a frame of the Game Boy lasts 70224 / 4194304 s = 16.742 ms: 16 ms and
// 742 thousandths, carried from frame to frame (59.73 frames a second)
#define FRAME_MS 16
#define FRAME_FRAC 742
// colours of the frame and the list (tools/make_tables.py)
#define C_FRAME 17441
#define C_LIST 49417
#define C_TEXT 24973
#define C_SEL 57757
#define C_BAR 1587

#define KEY_A1 107
#define KEY_A2 32
#define KEY_B1 106
#define KEY_B2 98
#define KEY_START 13
#define KEY_SEL1 9
#define KEY_SEL2 8

// the screen of that pixel size, and where the picture goes in it
int scrW = 266;
int scrH = 160;
int picX = 53;
int picY = 8;
int picOff = 6453;

// ===== the machine =====
int r[8];            // B C D E H L - A: r[6] is never used ((HL) is memory)
int F = 0;           // flags: Z 128, N 64, H 32, C 16
int SP = 0;
int PC = 0;
int IME = 0;
int imeNext = 0;     // EI: on after the next instruction
int halted = 0;
int cyc = 0;         // clock cycles of the instruction just run

uint8_t vram[8192];
uint8_t wram[8192];
uint8_t oam[160];
uint8_t hram[128];   // FF80..FFFF, the interrupt enable register last
uint8_t io[128];     // FF00..FF7F
uint8_t eram[131072];

// --- the cartridge ---
int romSize = 0;
int romMask = 0;     // banks - 1
int mbc = 0;         // 0 none, 1 MBC1, 3 MBC3, 5 MBC5
int romOff = 0;      // rom[] index of 4000..7FFF minus 0x4000
int rom0Off = 0;     // rom[] index of 0000..3FFF (MBC1 in mode 1)
int ramOn = 0;
int ramOff = 0;
int ramMask = 0;     // the external RAM's size - 1, -1 none
int bankLo = 1;
int bankHi = 0;
int mbcMode = 0;
int rtcSel = 0;      // MBC3: 8..12 a clock register in A000..BFFF
uint8_t rtc[5];

// --- timer, picture ---
int divc = 0;
int timc = 0;
int tper = 1024;
int ly = 0;
int lineCyc = 0;
int mode = 2;
int nextEvt = 80;
int coinc = 0;
int winLine = 0;
int drawFrame = 1;   // 0: this frame is skipped (the emulation is behind)

// --- buttons: 1 = pressed. Cross: right 1, left 2, up 4, down 8; A 1, B 2, Select 4, Start 8
int joyD = 0;
int joyB = 0;

// serial port bytes (the test cartridges print through it)
uint8_t serBuf[1024];
int serN = 0;

uint8_t cycT[256] = {4,12,8,8,4,4,8,4,20,8,8,8,4,4,8,4,4,12,8,8,4,4,8,4,12,8,8,8,4,4,8,4,8,12,8,8,4,4,8,4,8,8,8,8,4,4,8,4,8,12,8,8,12,12,12,4,8,8,8,8,4,4,8,4,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,8,8,8,8,8,8,4,8,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,8,12,12,16,12,16,8,16,8,16,12,0,12,24,8,16,8,12,12,0,12,16,8,16,8,16,12,0,12,0,8,16,12,12,8,0,0,16,8,16,16,4,16,0,0,0,8,16,12,12,8,4,0,16,8,16,12,8,16,4,0,0,8,16};

// the four greys of the first Game Boy, green, RGB565 with its two bytes swapped
uint16_t shade[4] = {57757,24973,1587,49417};
uint8_t lineIdx[160];   // the background's colour numbers on this line
uint8_t sprDone[160];   // a sprite already took this pixel
int sprI[10];
int sprN = 0;

// ===== the memory map =====
int joyRead() {
  int v;
  v = 15;
  if ((io[0] & 16) == 0) v = v & (15 - joyD);
  if ((io[0] & 32) == 0) v = v & (15 - joyB);
  return 192 + (io[0] & 48) + v;
}

int rdio(int i) {
  if (i == 0) return joyRead();
  if (i == 4) return (divc >> 8) & 255;
  if (i == 15) return io[15] | 224;
  if (i == 65) return 128 + (io[65] & 120) + coinc + mode;
  if (i == 68) return ly;
  return io[i];
}

int rd(int a) {
  if (a < 16384) return rom[rom0Off + a];
  if (a < 32768) return rom[romOff + a];
  if (a < 40960) return vram[a - 32768];
  if (a < 49152) {
    if (ramOn == 0) return 255;
    if (rtcSel != 0) return rtc[rtcSel - 8];
    if (ramMask < 0) return 255;
    return eram[(ramOff + a - 40960) & ramMask];
  }
  if (a < 57344) return wram[a - 49152];
  if (a < 65024) return wram[a - 57344];
  if (a < 65184) return oam[a - 65024];
  if (a < 65280) return 255;
  if (a < 65408) return rdio(a - 65280);
  return hram[a - 65408];
}

void mapRom() {
  int b;
  if (mbc == 1) {
    b = bankHi * 32 + bankLo;
    romOff = (b & romMask) * 16384 - 16384;
    if (mbcMode == 1) {
      rom0Off = ((bankHi * 32) & romMask) * 16384;
      ramOff = bankHi * 8192;
    } else {
      rom0Off = 0;
      ramOff = 0;
    }
    return;
  }
  if (mbc == 3) {
    romOff = (bankLo & romMask) * 16384 - 16384;
    return;
  }
  if (mbc == 5) {
    b = bankHi * 256 + bankLo;
    romOff = (b & romMask) * 16384 - 16384;
  }
}

void mbcWrite(int a, int v) {
  if (a < 8192) {
    if ((v & 15) == 10) {
      ramOn = 1;
    } else {
      ramOn = 0;
    }
    return;
  }
  if (mbc == 1) {
    if (a < 16384) {
      bankLo = v & 31;
      if (bankLo == 0) bankLo = 1;
    } else if (a < 24576) {
      bankHi = v & 3;
    } else {
      mbcMode = v & 1;
    }
    mapRom();
    return;
  }
  if (mbc == 3) {
    if (a < 16384) {
      bankLo = v & 127;
      if (bankLo == 0) bankLo = 1;
      mapRom();
    } else if (a < 24576) {
      if (v >= 8 && v <= 12) {
        rtcSel = v;
      } else {
        rtcSel = 0;
        ramOff = (v & 3) * 8192;
      }
    }
    return;
  }
  if (mbc == 5) {
    if (a < 12288) {
      bankLo = v;
      mapRom();
    } else if (a < 16384) {
      bankHi = v & 1;
      mapRom();
    } else if (a < 24576) {
      ramOff = (v & 15) * 8192;
    }
  }
}

void wrio(int i, int v) {
  int s;
  if (i == 0) {
    io[0] = v & 48;
    return;
  }
  if (i == 2) {
    if ((v & 129) == 129) {
      if (serN < 1024) {
        serBuf[serN] = io[1];
        serN++;
      }
      io[1] = 255;
      io[2] = v & 127;
      io[15] = io[15] | 8;
    } else {
      io[2] = v;
    }
    return;
  }
  if (i == 4) {
    divc = 0;
    return;
  }
  if (i == 7) {
    io[7] = v;
    s = v & 3;
    if (s == 0) tper = 1024;
    if (s == 1) tper = 16;
    if (s == 2) tper = 64;
    if (s == 3) tper = 256;
    return;
  }
  if (i == 15) {
    io[15] = v & 31;
    return;
  }
  if (i == 64) {
    if ((v & 128) == 0) {
      if ((io[64] & 128) != 0) {
        ly = 0;
        lineCyc = 0;
        mode = 0;
      }
    } else if ((io[64] & 128) == 0) {
      ly = 0;
      lineCyc = 0;
      mode = 2;
      nextEvt = 80;
      winLine = 0;
    }
    io[64] = v;
    return;
  }
  if (i == 65) {
    io[65] = v & 120;
    return;
  }
  if (i == 68) return;
  if (i == 70) {
    // DMA: 160 bytes to the sprite table, at once
    s = v * 256;
    for (int k = 0; k < 160; k++) oam[k] = rd(s + k);
    io[70] = v;
    return;
  }
  io[i] = v;
}

void wr(int a, int v) {
  if (a < 32768) {
    mbcWrite(a, v);
    return;
  }
  if (a < 40960) {
    vram[a - 32768] = v;
    return;
  }
  if (a < 49152) {
    if (ramOn == 0) return;
    if (rtcSel != 0) {
      rtc[rtcSel - 8] = v;
      return;
    }
    if (ramMask < 0) return;
    eram[(ramOff + a - 40960) & ramMask] = v;
    return;
  }
  if (a < 57344) {
    wram[a - 49152] = v;
    return;
  }
  if (a < 65024) {
    wram[a - 57344] = v;
    return;
  }
  if (a < 65184) {
    oam[a - 65024] = v;
    return;
  }
  if (a < 65280) return;
  if (a < 65408) {
    wrio(a - 65280, v);
    return;
  }
  hram[a - 65408] = v;
}

int fetch() {
  int v;
  if (PC < 16384) {
    v = rom[rom0Off + PC];
  } else if (PC < 32768) {
    v = rom[romOff + PC];
  } else {
    v = rd(PC);
  }
  PC = (PC + 1) & 65535;
  return v;
}

int fetch16() {
  int lo;
  lo = fetch();
  return lo + fetch() * 256;
}

void push(int v) {
  SP = (SP - 1) & 65535;
  wr(SP, v >> 8);
  SP = (SP - 1) & 65535;
  wr(SP, v & 255);
}

int pop() {
  int lo;
  lo = rd(SP);
  SP = (SP + 1) & 65535;
  lo = lo + rd(SP) * 256;
  SP = (SP + 1) & 65535;
  return lo;
}

// ===== the CPU =====
int getR(int i) {
  if (i == 6) return rd(r[4] * 256 + r[5]);
  return r[i];
}

void setR(int i, int v) {
  if (i == 6) {
    wr(r[4] * 256 + r[5], v);
    return;
  }
  r[i] = v;
}

// BC DE HL SP
int getRR(int p) {
  if (p == 3) return SP;
  return r[p * 2] * 256 + r[p * 2 + 1];
}

void setRR(int p, int v) {
  if (p == 3) {
    SP = v & 65535;
    return;
  }
  r[p * 2] = (v >> 8) & 255;
  r[p * 2 + 1] = v & 255;
}

// NZ Z NC C
int cond(int k) {
  if (k == 0) return (F & 128) == 0 ? 1 : 0;
  if (k == 1) return (F & 128) != 0 ? 1 : 0;
  if (k == 2) return (F & 16) == 0 ? 1 : 0;
  return (F & 16) != 0 ? 1 : 0;
}

// ADD ADC SUB SBC AND XOR OR CP
void alu(int k, int v) {
  int a;
  int res;
  int c;
  a = r[7];
  if (k == 0 || k == 1) {
    c = 0;
    if (k == 1) {
      if ((F & 16) != 0) c = 1;
    }
    res = a + v + c;
    F = 0;
    if ((res & 255) == 0) F = 128;
    if ((a & 15) + (v & 15) + c > 15) F = F + 32;
    if (res > 255) F = F + 16;
    r[7] = res & 255;
    return;
  }
  if (k == 2 || k == 3 || k == 7) {
    c = 0;
    if (k == 3) {
      if ((F & 16) != 0) c = 1;
    }
    res = a - v - c;
    F = 64;
    if ((res & 255) == 0) F = F + 128;
    if ((a & 15) - (v & 15) - c < 0) F = F + 32;
    if (res < 0) F = F + 16;
    if (k != 7) r[7] = res & 255;
    return;
  }
  if (k == 4) {
    res = a & v;
    F = 32;
  } else if (k == 5) {
    res = (a | v) - (a & v);
    F = 0;
  } else {
    res = a | v;
    F = 0;
  }
  if (res == 0) F = F + 128;
  r[7] = res;
}

int inc8(int v) {
  v = (v + 1) & 255;
  F = F & 16;
  if (v == 0) F = F + 128;
  if ((v & 15) == 0) F = F + 32;
  return v;
}

int dec8(int v) {
  v = (v - 1) & 255;
  F = (F & 16) + 64;
  if (v == 0) F = F + 128;
  if ((v & 15) == 15) F = F + 32;
  return v;
}

// SP + a signed byte, with the flags of ADD SP,e and LD HL,SP+e
int spPlus() {
  int e;
  int res;
  e = fetch();
  F = 0;
  if ((SP & 15) + (e & 15) > 15) F = 32;
  if ((SP & 255) + e > 255) F = F + 16;
  if (e > 127) e = e - 256;
  res = (SP + e) & 65535;
  return res;
}

// the CB instructions: rotations and shifts, BIT, RES, SET
void cbOp() {
  int op;
  int z;
  int y;
  int v;
  int c;
  op = fetch();
  z = op & 7;
  y = (op >> 3) & 7;
  if (z == 6) {
    cyc = cyc + 16;
  } else {
    cyc = cyc + 8;
  }
  v = getR(z);
  if (op >= 64) {
    if (op < 128) {
      F = (F & 16) + 32;
      if ((v & (1 << y)) == 0) F = F + 128;
      if (z == 6) cyc = cyc - 4;
      return;
    }
    if (op < 192) {
      setR(z, v & (255 - (1 << y)));
    } else {
      setR(z, v | (1 << y));
    }
    return;
  }
  c = 0;
  if (y == 0) {
    c = v >> 7;
    v = ((v << 1) | c) & 255;
  } else if (y == 1) {
    c = v & 1;
    v = (v >> 1) | (c << 7);
  } else if (y == 2) {
    c = v >> 7;
    v = ((v << 1) | ((F >> 4) & 1)) & 255;
  } else if (y == 3) {
    c = v & 1;
    v = (v >> 1) | ((F & 16) << 3);
  } else if (y == 4) {
    c = v >> 7;
    v = (v << 1) & 255;
  } else if (y == 5) {
    c = v & 1;
    v = (v >> 1) | (v & 128);
  } else if (y == 6) {
    v = ((v & 15) << 4) | (v >> 4);
  } else {
    c = v & 1;
    v = v >> 1;
  }
  F = 0;
  if (v == 0) F = 128;
  if (c != 0) F = F + 16;
  setR(z, v);
}

// 00..3F
void block0(int op) {
  int z;
  int y;
  int p;
  int v;
  int a;
  int hl;
  z = op & 7;
  y = (op >> 3) & 7;
  p = (op >> 4) & 3;
  if (z == 4) {
    setR(y, inc8(getR(y)));
    return;
  }
  if (z == 5) {
    setR(y, dec8(getR(y)));
    return;
  }
  if (z == 6) {
    setR(y, fetch());
    return;
  }
  if (z == 0) {
    if (y == 0) return;
    if (y == 1) {
      v = fetch16();
      wr(v, SP & 255);
      wr((v + 1) & 65535, SP >> 8);
      return;
    }
    if (y == 2) {
      fetch();
      return;
    }
    v = fetch();
    if (v > 127) v = v - 256;
    if (y == 3 || cond(y - 4) == 1) {
      PC = (PC + v) & 65535;
      if (y != 3) cyc = cyc + 4;
    }
    return;
  }
  if (z == 1) {
    if ((op & 8) == 0) {
      setRR(p, fetch16());
      return;
    }
    // ADD HL,rr
    hl = getRR(2);
    v = getRR(p);
    F = F & 128;
    if ((hl & 4095) + (v & 4095) > 4095) F = F + 32;
    if (hl + v > 65535) F = F + 16;
    setRR(2, hl + v);
    return;
  }
  if (z == 2) {
    if (p == 0) {
      a = getRR(0);
    } else if (p == 1) {
      a = getRR(1);
    } else {
      a = getRR(2);
      if (p == 2) {
        setRR(2, a + 1);
      } else {
        setRR(2, a - 1);
      }
    }
    if ((op & 8) == 0) {
      wr(a, r[7]);
    } else {
      r[7] = rd(a);
    }
    return;
  }
  if (z == 3) {
    if ((op & 8) == 0) {
      setRR(p, getRR(p) + 1);
    } else {
      setRR(p, getRR(p) - 1);
    }
    return;
  }
  // z == 7: RLCA RRCA RLA RRA DAA CPL SCF CCF
  a = r[7];
  if (y == 0) {
    v = a >> 7;
    r[7] = ((a << 1) | v) & 255;
    F = v * 16;
  } else if (y == 1) {
    v = a & 1;
    r[7] = (a >> 1) | (v << 7);
    F = v * 16;
  } else if (y == 2) {
    v = a >> 7;
    r[7] = ((a << 1) | ((F >> 4) & 1)) & 255;
    F = v * 16;
  } else if (y == 3) {
    v = a & 1;
    r[7] = (a >> 1) | ((F & 16) << 3);
    F = v * 16;
  } else if (y == 4) {
    v = F & 16;
    if ((F & 64) == 0) {
      if (v != 0 || a > 153) {
        a = a + 96;
        v = 16;
      }
      if ((F & 32) != 0 || (a & 15) > 9) a = a + 6;
    } else {
      if (v != 0) a = a - 96;
      if ((F & 32) != 0) a = a - 6;
    }
    a = a & 255;
    F = (F & 64) + v;
    if (a == 0) F = F + 128;
    r[7] = a;
  } else if (y == 5) {
    r[7] = 255 - a;
    F = F | 96;
  } else if (y == 6) {
    F = (F & 128) + 16;
  } else {
    F = (F & 128) + 16 - (F & 16);
  }
}

// C0..FF
void block3(int op) {
  int z;
  int y;
  int p;
  int v;
  z = op & 7;
  y = (op >> 3) & 7;
  p = (op >> 4) & 3;
  if (z == 6) {
    alu(y, fetch());
    return;
  }
  if (z == 7) {
    push(PC);
    PC = y * 8;
    return;
  }
  if (z == 0) {
    if (y < 4) {
      if (cond(y) == 1) {
        PC = pop();
        cyc = cyc + 12;
      }
      return;
    }
    if (y == 4) {
      wr(65280 + fetch(), r[7]);
    } else if (y == 5) {
      SP = spPlus();
    } else if (y == 6) {
      r[7] = rd(65280 + fetch());
    } else {
      setRR(2, spPlus());
    }
    return;
  }
  if (z == 1) {
    if ((op & 8) == 0) {
      v = pop();
      if (p == 3) {
        r[7] = v >> 8;
        F = v & 240;
      } else {
        setRR(p, v);
      }
      return;
    }
    if (p == 0) {
      PC = pop();
    } else if (p == 1) {
      PC = pop();
      IME = 1;
    } else if (p == 2) {
      PC = getRR(2);
    } else {
      SP = getRR(2);
    }
    return;
  }
  if (z == 2) {
    if (y < 4) {
      v = fetch16();
      if (cond(y) == 1) {
        PC = v;
        cyc = cyc + 4;
      }
      return;
    }
    if (y == 4) {
      wr(65280 + r[1], r[7]);
    } else if (y == 5) {
      wr(fetch16(), r[7]);
    } else if (y == 6) {
      r[7] = rd(65280 + r[1]);
    } else {
      r[7] = rd(fetch16());
    }
    return;
  }
  if (z == 3) {
    if (y == 0) {
      PC = fetch16();
    } else if (y == 1) {
      cbOp();
    } else if (y == 6) {
      IME = 0;
      imeNext = 0;
    } else if (y == 7) {
      imeNext = 1;
    }
    return;
  }
  if (z == 4) {
    v = fetch16();
    if (y < 4) {
      if (cond(y) == 1) {
        push(PC);
        PC = v;
        cyc = cyc + 12;
      }
    }
    return;
  }
  // z == 5
  if ((op & 8) == 0) {
    if (p == 3) {
      push(r[7] * 256 + F);
    } else {
      push(getRR(p));
    }
    return;
  }
  if (p == 0) {
    v = fetch16();
    push(PC);
    PC = v;
  }
}

// one instruction (or an interrupt), its clock cycles in cyc
void step() {
  int op;
  int f;
  int en;
  f = io[15] & hram[127] & 31;
  if (f != 0) {
    halted = 0;
    if (IME == 1) {
      IME = 0;
      imeNext = 0;
      push(PC);
      if ((f & 1) != 0) {
        PC = 64;
        io[15] = io[15] - 1;
      } else if ((f & 2) != 0) {
        PC = 72;
        io[15] = io[15] - 2;
      } else if ((f & 4) != 0) {
        PC = 80;
        io[15] = io[15] - 4;
      } else if ((f & 8) != 0) {
        PC = 88;
        io[15] = io[15] - 8;
      } else {
        PC = 96;
        io[15] = io[15] - 16;
      }
      cyc = 20;
      return;
    }
  }
  if (halted == 1) {
    // asleep until an interrupt: on to the next thing that can raise one
    cyc = nextEvt - lineCyc;
    if ((io[7] & 4) != 0) {
      if (tper - timc < cyc) cyc = tper - timc;
    }
    if (cyc < 4) cyc = 4;
    return;
  }
  en = imeNext;
  imeNext = 0;
  op = fetch();
  cyc = cycT[op];
  if (op < 64) {
    block0(op);
  } else if (op < 128) {
    if (op == 118) {
      halted = 1;
    } else {
      setR((op >> 3) & 7, getR(op & 7));
    }
  } else if (op < 192) {
    alu((op >> 3) & 7, getR(op & 7));
  } else {
    block3(op);
  }
  if (en == 1) IME = 1;
  if (cyc == 0) cyc = 4;
}

// ===== the picture =====
// one line of background, window and sprites into the display
void renderLine() {
  int lc;
  int o;
  int bgp;
  int x;
  int px;
  int sy;
  int map;
  int t;
  int ad;
  int lo;
  int hi;
  int b;
  int ci;
  int xEnd;
  int wx;
  int h;
  int i;
  int y;
  int fl;
  int pal;
  int sx;
  lc = io[64];
  o = picOff + ly * STRIDE;
  bgp = io[71];
  wx = io[75] - 7;
  xEnd = 160;
  if ((lc & 32) != 0 && ly >= io[74] && wx < 160) {
    xEnd = wx;
    if (xEnd < 0) xEnd = 0;
  }
  if ((lc & 1) == 0) {
    for (x = 0; x < 160; x++) {
      lineIdx[x] = 0;
      display[o + x] = shade[bgp & 3];
    }
  } else {
    sy = (ly + io[66]) & 255;
    map = 6144;
    if ((lc & 8) != 0) map = 7168;
    map = map + (sy >> 3) * 32;
    sy = (sy & 7) * 2;
    x = 0;
    while (x < xEnd) {
      px = (io[67] + x) & 255;
      t = vram[map + (px >> 3)];
      if ((lc & 16) != 0) {
        ad = t * 16 + sy;
      } else {
        if (t > 127) t = t - 256;
        ad = 4096 + t * 16 + sy;
      }
      lo = vram[ad];
      hi = vram[ad + 1];
      for (b = 7 - (px & 7); b >= 0; b--) {
        if (x < xEnd) {
          ci = ((lo >> b) & 1) + ((hi >> b) & 1) * 2;
          lineIdx[x] = ci;
          display[o + x] = shade[(bgp >> (ci * 2)) & 3];
          x++;
        }
      }
    }
    if (xEnd < 160) {
      map = 6144;
      if ((lc & 64) != 0) map = 7168;
      map = map + (winLine >> 3) * 32;
      sy = (winLine & 7) * 2;
      x = xEnd;
      while (x < 160) {
        px = x - wx;
        t = vram[map + (px >> 3)];
        if ((lc & 16) != 0) {
          ad = t * 16 + sy;
        } else {
          if (t > 127) t = t - 256;
          ad = 4096 + t * 16 + sy;
        }
        lo = vram[ad];
        hi = vram[ad + 1];
        for (b = 7 - (px & 7); b >= 0; b--) {
          if (x < 160) {
            ci = ((lo >> b) & 1) + ((hi >> b) & 1) * 2;
            lineIdx[x] = ci;
            display[o + x] = shade[(bgp >> (ci * 2)) & 3];
            x++;
          }
        }
      }
      winLine++;
    }
  }
  if ((lc & 2) == 0) return;
  // the sprites on this line: at most 10, in OAM order
  h = 8;
  if ((lc & 4) != 0) h = 16;
  sprN = 0;
  for (i = 0; i < 160; i = i + 4) {
    y = ly - oam[i] + 16;
    if (y >= 0 && y < h && sprN < 10) {
      sprI[sprN] = i;
      sprN++;
    }
  }
  if (sprN == 0) return;
  // the leftmost first, then the first in OAM: insertion sort
  for (i = 1; i < sprN; i++) {
    t = sprI[i];
    b = i - 1;
    while (b >= 0 && oam[sprI[b] + 1] > oam[t + 1]) {
      sprI[b + 1] = sprI[b];
      b--;
    }
    sprI[b + 1] = t;
  }
  for (x = 0; x < 160; x++) sprDone[x] = 0;
  for (i = 0; i < sprN; i++) {
    t = sprI[i];
    y = ly - oam[t] + 16;
    fl = oam[t + 3];
    if ((fl & 64) != 0) y = h - 1 - y;
    ad = oam[t + 2];
    if (h == 16) ad = ad & 254;
    ad = ad * 16 + y * 2;
    lo = vram[ad];
    hi = vram[ad + 1];
    pal = io[72];
    if ((fl & 16) != 0) pal = io[73];
    sx = oam[t + 1] - 8;
    for (px = 0; px < 8; px++) {
      x = sx + px;
      if (x >= 0 && x < 160) {
        b = 7 - px;
        if ((fl & 32) != 0) b = px;
        ci = ((lo >> b) & 1) + ((hi >> b) & 1) * 2;
        if (ci != 0 && sprDone[x] == 0) {
          sprDone[x] = 1;
          if ((fl & 128) == 0 || lineIdx[x] == 0) display[o + x] = shade[(pal >> (ci * 2)) & 3];
        }
      }
    }
  }
}

void lycCheck() {
  if (ly == io[69]) {
    coinc = 4;
    if ((io[65] & 64) != 0) io[15] = io[15] | 2;
  } else {
    coinc = 0;
  }
}

// the next step of the line: mode 2 (sprite search), 3 (drawing), 0 (hblank), 1 (vblank)
void ppuEvent() {
  if (mode == 2) {
    mode = 3;
    nextEvt = 252;
    return;
  }
  if (mode == 3) {
    mode = 0;
    nextEvt = 456;
    if (drawFrame == 1) renderLine();
    if ((io[65] & 8) != 0) io[15] = io[15] | 2;
    return;
  }
  lineCyc = lineCyc - 456;
  ly++;
  if (mode == 0) {
    if (ly == 144) {
      mode = 1;
      io[15] = io[15] | 1;
      if ((io[65] & 16) != 0) io[15] = io[15] | 2;
    } else {
      mode = 2;
      nextEvt = 80;
      if ((io[65] & 32) != 0) io[15] = io[15] | 2;
    }
  } else if (ly == 154) {
    ly = 0;
    winLine = 0;
    mode = 2;
    nextEvt = 80;
    if ((io[65] & 32) != 0) io[15] = io[15] | 2;
  }
  lycCheck();
}

// one frame of the Game Boy: 70224 clock cycles
void runFrame() {
  int done;
  done = 0;
  while (done < FRAME_CYC) {
    step();
    done = done + cyc;
    divc = (divc + cyc) & 65535;
    if ((io[7] & 4) != 0) {
      timc = timc + cyc;
      while (timc >= tper) {
        timc = timc - tper;
        if (io[5] == 255) {
          io[5] = io[6];
          io[15] = io[15] | 4;
        } else {
          io[5] = io[5] + 1;
        }
      }
    }
    if ((io[64] & 128) != 0) {
      lineCyc = lineCyc + cyc;
      while (lineCyc >= nextEvt) ppuEvent();
    }
  }
}

// ===== the cartridge =====
// the machine as the boot program leaves it, with cartridge i in rom[]
int startCart(int size) {
  int t;
  int k;
  romSize = size;
  romMask = size / 16384 - 1;
  if (romMask < 1) romMask = 1;
  t = rom[327];
  mbc = 0;
  if (t >= 1 && t <= 3) mbc = 1;
  if (t >= 15 && t <= 19) mbc = 3;
  if (t >= 25 && t <= 30) mbc = 5;
  k = rom[329];
  ramMask = -1;
  if (k == 1) ramMask = 2047;
  if (k == 2) ramMask = 8191;
  if (k == 3) ramMask = 32767;
  if (k == 4) ramMask = 131071;
  if (k == 5) ramMask = 65535;
  if (mbc == 3 && ramMask < 0) ramMask = 32767;
  bankLo = 1;
  bankHi = 0;
  mbcMode = 0;
  ramOn = 0;
  ramOff = 0;
  rtcSel = 0;
  rom0Off = 0;
  romOff = 0;
  if (mbc != 0) mapRom();
  for (k = 0; k < 8192; k++) {
    vram[k] = 0;
    wram[k] = 0;
  }
  for (k = 0; k < 160; k++) oam[k] = 0;
  for (k = 0; k < 128; k++) {
    hram[k] = 0;
    io[k] = 0;
  }
  r[0] = 0;
  r[1] = 19;
  r[2] = 0;
  r[3] = 216;
  r[4] = 1;
  r[5] = 77;
  r[7] = 1;
  F = 176;
  SP = 65534;
  PC = 256;
  IME = 0;
  imeNext = 0;
  halted = 0;
  io[0] = 48;
  io[16] = 128;
  io[64] = 145;
  io[71] = 252;
  io[72] = 255;
  io[73] = 255;
  divc = 44032;
  timc = 0;
  tper = 1024;
  ly = 0;
  lineCyc = 0;
  mode = 2;
  nextEvt = 80;
  winLine = 0;
  serN = 0;
  return mbc;
}

// ===== the buttons =====
void readButtons() {
  int d;
  int b;
  d = 0;
  b = 0;
  if (keyHeld(100) != 0) d = d + 1;
  if (keyHeld(113) != 0) d = d + 2;
  if (keyHeld(122) != 0) d = d + 4;
  if (keyHeld(115) != 0) d = d + 8;
  if (keyHeld(KEY_A1) != 0 || keyHeld(KEY_A2) != 0) b = b + 1;
  if (keyHeld(KEY_B1) != 0 || keyHeld(KEY_B2) != 0) b = b + 2;
  if (keyHeld(KEY_SEL1) != 0 || keyHeld(KEY_SEL2) != 0) b = b + 4;
  if (keyHeld(KEY_START) != 0) b = b + 8;
  // a button newly pressed: the joypad interrupt
  if ((d & (15 - joyD)) != 0 || (b & (15 - joyB)) != 0) io[15] = io[15] | 16;
  joyD = d;
  joyB = b;
}

// ===== the screen around the picture, and text =====
// 5x7 letters 32..95, 7 rows each, bit 4 the leftmost column (tools/make_tables.py)
uint8_t font[448] = {0,0,0,0,0,0,0,4,4,4,4,4,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,15,20,14,5,30,4,0,0,0,0,0,0,0,12,18,20,8,21,18,13,4,4,8,0,0,0,0,2,4,8,8,8,4,2,8,4,2,2,2,4,8,0,0,0,0,0,0,0,0,4,4,31,4,4,0,0,0,0,0,12,4,8,0,0,0,31,0,0,0,0,0,0,0,0,12,12,1,1,2,4,8,16,16,14,17,19,21,25,17,14,4,12,4,4,4,4,14,14,17,1,2,4,8,31,31,2,4,2,1,17,14,2,6,10,18,31,2,2,31,16,30,1,1,17,14,6,8,16,30,17,17,14,31,1,2,4,8,8,8,14,17,17,14,17,17,14,14,17,17,15,1,2,12,0,12,12,0,12,12,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,31,0,31,0,0,0,0,0,0,0,0,0,14,17,1,2,4,0,4,0,0,0,0,0,0,0,14,17,17,31,17,17,17,30,17,17,30,17,17,30,14,17,16,16,16,17,14,30,17,17,17,17,17,30,31,16,16,30,16,16,31,31,16,16,30,16,16,16,14,17,16,23,17,17,15,17,17,17,31,17,17,17,14,4,4,4,4,4,14,7,2,2,2,2,18,12,17,18,20,24,20,18,17,16,16,16,16,16,16,31,17,27,21,21,17,17,17,17,17,25,21,19,17,17,14,17,17,17,17,17,14,30,17,17,30,16,16,16,14,17,17,17,21,18,13,30,17,17,30,20,18,17,15,16,16,14,1,1,30,31,4,4,4,4,4,4,17,17,17,17,17,17,14,17,17,17,17,17,10,4,17,17,17,21,21,21,10,17,17,10,4,10,17,17,17,17,10,4,4,4,4,31,1,2,4,8,16,31,14,8,8,8,8,8,14,0,0,0,0,0,0,0,14,2,2,2,2,2,14,0,0,0,0,0,0,0,0,0,0,0,0,0,31};

void clearScreen(int c) {
  for (int k = 0; k < scrH * STRIDE; k++) display[k] = c;
}

// a letter at (x, y) of the 266x160 screen
void letter(int x, int y, int ch, int c) {
  int col;
  if (ch >= 97 && ch <= 122) ch = ch - 32;
  if (ch < 32 || ch > 95) ch = 63;
  for (int j = 0; j < 7; j++) {
    col = font[(ch - 32) * 7 + j];
    for (int i = 0; i < 5; i++) {
      if (((col >> (4 - i)) & 1) != 0) display[(y + j) * STRIDE + x + i] = c;
    }
  }
}

// cartridge i's name at (x, y), at most n letters, without its .gb
void romText(int i, int x, int y, int n, int c) {
  int ch;
  int k;
  k = 0;
  ch = romNameChar(i, 0);
  while (ch != 0 && k < n) {
    if (ch == 46) {
      if (romNameChar(i, k + 1) == 103 || romNameChar(i, k + 1) == 71) return;
    }
    letter(x + k * 6, y, ch, c);
    k++;
    ch = romNameChar(i, k);
  }
}

uint8_t txTitle[9] = {71,65,77,69,32,66,79,89,0};
uint8_t txNone[25] = {78,79,32,46,71,66,32,70,73,76,69,32,79,78,32,70,76,65,83,72,47,83,68,0,0};
uint8_t txFail[17] = {67,65,78,78,79,84,32,76,79,65,68,32,73,84,0,0,0};

void text(uint8_t *s, int x, int y, int c) {
  int k;
  k = 0;
  while (s[k] != 0) {
    letter(x + k * 6, y, s[k], c);
    k++;
  }
}

// the frame around the 160x144 picture, in both buffers
void drawBorder() {
  for (int pass = 0; pass < 2; pass++) {
    clearScreen(0);
    for (int y = picY - 4; y < picY + 148; y++) {
      for (int x = picX - 4; x < picX + 164; x++) display[y * STRIDE + x] = C_FRAME;
    }
    for (int y = picY; y < picY + 144; y++) {
      for (int x = picX; x < picX + 160; x++) display[y * STRIDE + x] = shade[0];
    }
    updateDisplay();
  }
}

// ===== the list of cartridges =====
// returns the one chosen, -1 when there are none
int chooseRom() {
  int n;
  int sel;
  int first;
  int k;
  int c;
  int lastK;
  int seenE;
  int rows;
  rows = (scrH - 20) / 8;
  n = romCount();
  sel = 0;
  lastK = 0;
  seenE = 0;
  while (1) {
    clearScreen(C_LIST);
    text(txTitle, (scrW - 48) / 2, 4, C_SEL);
    if (n == 0) {
      text(txNone, (scrW - 144) / 2, scrH / 2 - 10, C_TEXT);
    } else {
      first = sel - rows / 2;
      if (first < 0) first = 0;
      for (int i = first; i < n && i < first + rows; i++) {
        c = C_TEXT;
        if (i == sel) {
          c = C_SEL;
          for (int y = 18 + (i - first) * 8 - 1; y < 18 + (i - first) * 8 + 8; y++) {
            for (int x = 6; x < scrW - 6; x++) display[y * STRIDE + x] = C_BAR;
          }
        }
        romText(i, 10, 18 + (i - first) * 8, (scrW - 20) / 6, c);
      }
    }
    updateDisplay();
    if (n == 0) {
      delay(200);
    } else {
      k = 0;
      if (keyHeld(122) != 0) k = 1;
      if (keyHeld(115) != 0) k = 2;
      if (k != lastK) {
        if (k == 1 && sel > 0) sel--;
        if (k == 2 && sel < n - 1) sel++;
      }
      lastK = k;
      if (keyHeld(KEY_START) != 0 || keyHeld(KEY_A1) != 0) {
        seenE = 1;
      } else if (seenE == 1) {
        return sel;
      }
      delay(30);
    }
  }
  return -1;
}

// ===== main =====
int main() {
  int sel;
  int size;
  int t0;
  int late;
  int frac;
  int now;
  int quit;
  enableGraphicMode();
  setPixelsize(pixelsize);
  scrW = 800 / pixelsize;
  scrH = 480 / pixelsize;
  picX = (scrW - 160) / 2;
  picY = (scrH - 144) / 2;
  picOff = picY * STRIDE + picX;
  while (1) {
    sel = chooseRom();
    size = romLoad(sel);
    if (size < 32768) {
      clearScreen(C_LIST);
      text(txFail, (scrW - 84) / 2, scrH / 2 - 4, C_TEXT);
      updateDisplay();
      delay(2000);
    } else {
      startCart(size);
      drawBorder();
      t0 = millis();
      late = 0;
      frac = 0;
      quit = 0;
      while (quit == 0) {
        readButtons();
        // behind by more than a frame: emulate this one without drawing it
        drawFrame = 1;
        if (late > FRAME_MS + 1) drawFrame = 0;
        runFrame();
        if (drawFrame == 1) updateDisplay();
        t0 = t0 + FRAME_MS;
        frac = frac + FRAME_FRAC;
        if (frac >= 1000) {
          frac = frac - 1000;
          t0++;
        }
        now = millis();
        late = now - t0;
        if (late < 0) {
          delay(0 - late);
        } else if (late > 200) {
          t0 = now;
        }
        if (keyHeld(27) != 0) quit = 1;
      }
      while (keyHeld(27) != 0) delay(20);
    }
  }
  return 0;
}
