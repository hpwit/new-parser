// ============================================================
// CITY SANDBOX  -  milestone (b): on foot and driving, collisions, views
// Style C "clear day downtown". 400x240 RGB565, upscaled 2x by present().
//
// Language subset used on purpose (so it survives a small compiler):
//   int / uint8_t / uint16_t globals and arrays, define with one numeric token,
//   if / else / for / while, functions defined before use, at most 6 parameters.
//   No struct, switch, ternary, macro, 64-bit, float, or local arrays.
//   Signed values are divided ( / ), never shifted, so the result does not
//   depend on whether >> is arithmetic.  && is never relied on to short-circuit.
//
// Fixed point: world and camera space are 24.8 (256 = 1 unit, about 0.5 m),
// sin/cos are 2.12 (4096 = 1.0), angles are 0..1023 for a full turn.
// ============================================================

// ===== BINDINGS - placeholder names, adapt to your firmware =====
external uint16_t fb[96000];                      // 400x240 render target, row-major
external void present();                          // upscale fb 2x and show it
external int millis();                            // milliseconds since boot
external int keyDown(int key);                    // 1 while the key is held
external int wadLoad(uint8_t *dst, int maxlen);   // reads city.wad, returns byte count

// ===== 1. CONSTANTS =====
// --- screen and camera ---
#define SCR_W 400
#define SCR_H 240
#define HALF_W 200
#define HORIZON 96
#define FOCAL 250
#define FOCAL16 16384000
#define NEAR_Z 768
// --- city layout (24.8) ---
#define CITY_N 16
#define PITCH 13824
#define HALF_PITCH 6912
#define ROAD_HALF 1792
#define PAD 10240
#define CITY_MIN 2048
#define CITY_MAX 219136
// --- draw distance, fog, level of detail ---
#define VIEW_R 5
#define DRAW_DIST 62208
#define DRAW_U 243
#define GROW_U 32
#define FOG_START 24000
#define FOG_SPAN 34000
#define BLOCK_RAD 9728
#define CULL_MARGIN 63232
#define D2_WIN_NEAR 4900
#define D2_WIN_BAND 28900
#define D2_PROPS_FULL 14112
#define D2_PROPS_SIMPLE 33700
#define D2_ZEBRA 5715
#define D2_LOT 35721
// --- polygon budget per frame (quads; one quad = two triangles) ---
#define BAND_BUDGET 330
#define WIN_BUDGET 540
#define PROP_BUDGET 580
#define MAX_POLY 600
// --- controls and timing ---
#define KEY_LEFT 1
#define KEY_RIGHT 2
#define KEY_UP 3
#define KEY_DOWN 4
#define KEY_QUIT 5
#define KEY_ENTER 6
#define KEY_VIEW 7
#define KEY_BRAKE 8
#define TURN_STEP 10
#define MOVE_STEP 307
#define TICK_MS 33
#define WAD_MAX 8192

// ===== 2. MATHS TABLES AND ASSETS (filled from the WAD) =====
uint8_t wad[8192];
int sinT[1024];
uint16_t pal[960];
uint16_t skyC[8];
uint8_t patT[64];
uint16_t rowCol[240];

int camX = 111462;
int camZ = 82944;
int camA = 0;
int camH = 1792;   // camera height, 24.8: set by the view
int sinA = 0;
int cosA = 4096;
int tX = 0;
int tZ = 0;
int polyCount = 0;
int seed = 20261127;

// Pseudo random 0..n-1 from the fixed seed.
int rnd(int n) {
  int r;
  seed = seed * 1103515245 + 12345;
  r = (seed >> 16) & 32767;
  return r % n;
}

// Stateless hash 0..32767 of two small integers.
int hash2(int a, int b) {
  int v;
  v = a * 7919 + b * 104729 + 12345;
  v = v * 1103515245 + 12345;
  return (v >> 16) & 32767;
}

// Unsigned 16-bit little-endian read from the WAD buffer.
int rd16(int o) {
  int v;
  v = wad[o];
  v = v + wad[o + 1] * 256;
  return v;
}

// 32-bit little-endian read from the WAD buffer.
int rd32(int o) {
  return rd16(o) + rd16(o + 2) * 65536;
}

// Offset of the lump whose name starts with the four given characters, 0 if absent.
int findLump(int a, int b, int c, int d) {
  int n;
  int o;
  n = rd16(4);
  for (int i = 0; i < n; i++) {
    o = 8 + i * 16;
    if (wad[o] == a) {
      if (wad[o + 1] == b) {
        if (wad[o + 2] == c) {
          if (wad[o + 3] == d) {
            return rd32(o + 8);
          }
        }
      }
    }
  }
  return 0;
}

// --- meshes: boxes of 7 values (x0 x1 z0 z1 y0 y1 colour), up to 12 ---
int carBox[84];
int carN = 0;
int pedBox[84];
int pedN = 0;

// Signed 16-bit read from the WAD buffer.
int rds16(int o) {
  int v;
  v = rd16(o);
  if (v >= 32768) v = v - 65536;
  return v;
}

// Reads a mesh lump into dst; returns its box count.
int loadMesh(int o, int *dst) {
  int n;
  n = rds16(o);
  if (n > 12) n = 12;
  for (int i = 0; i < n * 7; i++) dst[i] = rds16(o + 2 + i * 2);
  return n;
}

// Loads palette, sky colours, sine table, window patterns and meshes. Returns 1 on success.
int loadAssets() {
  int n;
  int o;
  int v;
  n = wadLoad(wad, WAD_MAX);
  if (n < 24) return 0;
  if (wad[0] != 67) return 0;
  if (wad[1] != 87) return 0;
  o = findLump(80, 65, 76, 69);
  if (o == 0) return 0;
  for (int i = 0; i < 960; i++) pal[i] = rd16(o + i * 2);
  o = findLump(83, 75, 89, 67);
  if (o == 0) return 0;
  for (int i = 0; i < 8; i++) skyC[i] = rd16(o + i * 2);
  o = findLump(83, 73, 78, 84);
  if (o == 0) return 0;
  for (int i = 0; i < 1024; i++) {
    v = rd16(o + i * 2);
    if (v >= 32768) v = v - 65536;
    sinT[i] = v;
  }
  o = findLump(80, 65, 84, 84);
  if (o == 0) return 0;
  for (int i = 0; i < 64; i++) patT[i] = wad[o + i];
  o = findLump(67, 65, 82, 77);
  if (o == 0) return 0;
  carN = loadMesh(o, carBox);
  o = findLump(80, 69, 68, 77);
  if (o == 0) return 0;
  pedN = loadMesh(o, pedBox);
  return 1;
}

// ===== 3. MESH DATA =====
// Milestone (a) only needs boxes; car and character meshes arrive in (b) as a WAD lump.

// ===== 4. WORLD GENERATION =====
uint8_t blkType[256];   // 0 buildings, 1 park, 2 water
uint8_t lotX0[1024];    // building footprint inside its block, in units
uint8_t lotX1[1024];
uint8_t lotZ0[1024];
uint8_t lotZ1[1024];
uint8_t lotH[1024];     // height in units, 0 = empty lot
uint8_t lotC[1024];     // palette colour
uint8_t lotP[1024];     // window pattern style

// Builds the 16x16 city from the fixed seed: west column is the waterfront,
// about one block in nine is a park, towers get taller toward the centre.
void genCity() {
  int b;
  int i;
  int t;
  int lx;
  int lz;
  int dx;
  int dz;
  int cf;
  int r;
  int h;
  for (int iz = 0; iz < CITY_N; iz++) {
    for (int ix = 0; ix < CITY_N; ix++) {
      b = iz * 16 + ix;
      t = 0;
      if (ix == 0) {
        t = 2;
      } else {
        if (rnd(100) < 11) t = 1;
      }
      blkType[b] = t;
      dx = ix - 8;
      if (dx < 0) dx = 0 - dx;
      dz = iz - 8;
      if (dz < 0) dz = 0 - dz;
      cf = dx;
      if (dz > cf) cf = dz;
      cf = 8 - cf;
      for (int l = 0; l < 4; l++) {
        i = b * 4 + l;
        lx = l & 1;
        lz = l / 2;
        r = 4 + lx * 16 + rnd(3);
        lotX0[i] = r;
        lotX1[i] = r + 10 + rnd(4);
        r = 4 + lz * 16 + rnd(3);
        lotZ0[i] = r;
        lotZ1[i] = r + 10 + rnd(4);
        r = rnd(100);
        h = 9 + (r * r * (14 + cf * 7)) / 10000;
        if (rnd(100) < 8) h = 0;
        lotH[i] = h;
        lotC[i] = 12 + rnd(6);
        lotP[i] = rnd(8);
      }
    }
  }
}

// ===== 5. RENDERER =====
int spanL[240];
int spanR[240];
int px[8];
int py[8];
int ax[4];
int az[4];
int ox[8];
int oz[8];

// World point to camera space: tX is right, tZ is forward.
void toCam(int wx, int wz) {
  int dx;
  int dz;
  dx = wx - camX;
  dz = wz - camZ;
  tX = (dx * cosA - dz * sinA) / 4096;
  tZ = (dx * sinA + dz * cosA) / 4096;
}

// Fog level 0..7 for a camera-space depth.
int fogOf(int z) {
  int f;
  if (z <= FOG_START) return 0;
  f = ((z - FOG_START) * 8) / FOG_SPAN;
  if (f > 7) f = 7;
  return f;
}

// Fills the convex polygon in px/py (n vertices, any order around the outline).
// Edges are always walked top to bottom, so two polygons sharing an edge meet
// without gaps. This is the hot loop: a native span fill would speed it up most.
void fillPoly(int n, int c) {
  int ymin;
  int ymax;
  int xmin;
  int xmax;
  int j;
  int x0;
  int y0;
  int x1;
  int y1;
  int slope;
  int ys;
  int ye;
  int xf;
  int x;
  int o;
  ymin = py[0];
  ymax = py[0];
  xmin = px[0];
  xmax = px[0];
  for (int i = 1; i < n; i++) {
    if (py[i] < ymin) ymin = py[i];
    if (py[i] > ymax) ymax = py[i];
    if (px[i] < xmin) xmin = px[i];
    if (px[i] > xmax) xmax = px[i];
  }
  if (ymax <= 0) return;
  if (ymin >= SCR_H) return;
  if (xmax <= 0) return;
  if (xmin >= SCR_W) return;
  if (ymin < 0) ymin = 0;
  if (ymax > SCR_H) ymax = SCR_H;
  polyCount++;
  for (int y = ymin; y < ymax; y++) {
    spanL[y] = 100000;
    spanR[y] = 0 - 100000;
  }
  for (int i = 0; i < n; i++) {
    j = i + 1;
    if (j == n) j = 0;
    x0 = px[i];
    y0 = py[i];
    x1 = px[j];
    y1 = py[j];
    if (y0 != y1) {
      if (y0 > y1) {
        x0 = px[j];
        y0 = py[j];
        x1 = px[i];
        y1 = py[i];
      }
      slope = ((x1 - x0) * 256) / (y1 - y0);
      ys = y0;
      if (ys < ymin) ys = ymin;
      ye = y1;
      if (ye > ymax) ye = ymax;
      xf = x0 * 256 + slope * (ys - y0) + 16777216;
      for (int y = ys; y < ye; y++) {
        x = (xf >> 8) - 65536;
        if (x < spanL[y]) spanL[y] = x;
        if (x > spanR[y]) spanR[y] = x;
        xf = xf + slope;
      }
    }
  }
  for (int y = ymin; y < ymax; y++) {
    x0 = spanL[y];
    x1 = spanR[y];
    if (x0 < 0) x0 = 0;
    if (x1 > SCR_W) x1 = SCR_W;
    o = y * SCR_W;
    for (int k = x0; k < x1; k++) fb[o + k] = c;
  }
}

// Sky bands above the horizon, fogged road colour below it.
void drawBackground() {
  int c;
  int o;
  for (int y = 0; y < SCR_H; y++) {
    if (y < HORIZON) {
      c = skyC[(y * 6) / HORIZON];
    } else {
      c = rowCol[y];
    }
    o = y * SCR_W;
    for (int x = 0; x < SCR_W; x++) fb[o + x] = c;
  }
}

// Precomputes the ground colour of every screen row (depth only depends on the row).
void initRows() {
  int z;
  for (int y = 0; y < SCR_H; y++) {
    if (y <= HORIZON) {
      rowCol[y] = skyC[6];
    } else {
      z = (camH * FOCAL) / (y - HORIZON);
      rowCol[y] = pal[fogOf(z)];
    }
  }
}

// Draws the horizontal quad held in ax/az (camera space) at height y,
// clipped against the near plane.
void flatCam(int y, int ci, int sh) {
  int m;
  int j;
  int ina;
  int inb;
  int t;
  int s;
  int zs;
  m = 0;
  zs = 0;
  for (int i = 0; i < 4; i++) {
    j = i + 1;
    if (j == 4) j = 0;
    ina = 0;
    if (az[i] >= NEAR_Z) ina = 1;
    inb = 0;
    if (az[j] >= NEAR_Z) inb = 1;
    if (ina == 1) {
      ox[m] = ax[i];
      oz[m] = az[i];
      m++;
    }
    if (ina != inb) {
      t = ((NEAR_Z - az[i]) * 1024) / (az[j] - az[i]);
      ox[m] = ax[i] + ((ax[j] - ax[i]) * t) / 1024;
      oz[m] = NEAR_Z;
      m++;
    }
  }
  if (m < 3) return;
  for (int k = 0; k < m; k++) {
    s = FOCAL16 / oz[k];
    px[k] = HALF_W + (ox[k] * FOCAL) / oz[k];
    py[k] = HORIZON - ((y - camH) * s) / 65536;
    zs = zs + oz[k];
  }
  fillPoly(m, pal[(ci * 5 + sh) * 8 + fogOf(zs / m)]);
}

// Loads the axis-aligned world rectangle into ax/az as camera-space corners.
void setQuad(int x0, int z0, int x1, int z1) {
  toCam(x0, z0);
  ax[0] = tX;
  az[0] = tZ;
  toCam(x1, z0);
  ax[1] = tX;
  az[1] = tZ;
  toCam(x1, z1);
  ax[2] = tX;
  az[2] = tZ;
  toCam(x0, z1);
  ax[3] = tX;
  az[3] = tZ;
}

// --- vertical walls; the current box is described by these globals ---
int curY0 = 0;      // bottom, 24.8
int curY1 = 0;      // top, 24.8
int curCi = 0;      // palette colour
int curStyle = 0;   // window pattern, -1 for none
int curId = 0;      // used to vary glass tint
int curLod = 2;     // 0 single windows, 1 bands per floor, 2 none
int wxa = 0;
int wza = 0;
int wxb = 0;
int wzb = 0;

// Clips a wall (two camera-space columns) to the near plane into wxa..wzb. 0 if hidden.
int clipWall(int xa, int za, int xb, int zb) {
  int t;
  wxa = xa;
  wza = za;
  wxb = xb;
  wzb = zb;
  if (za < NEAR_Z) {
    if (zb < NEAR_Z) return 0;
    t = ((NEAR_Z - za) * 1024) / (zb - za);
    wxa = xa + ((xb - xa) * t) / 1024;
    wza = NEAR_Z;
  } else {
    if (zb < NEAR_Z) {
      t = ((NEAR_Z - zb) * 1024) / (za - zb);
      wxb = xb + ((xa - xb) * t) / 1024;
      wzb = NEAR_Z;
    }
  }
  return 1;
}

// Draws one wall of the current box between two camera-space columns.
void drawWall(int xa, int za, int xb, int zb, int sh) {
  int sa;
  int sb;
  if (clipWall(xa, za, xb, zb) == 0) return;
  sa = FOCAL16 / wza;
  sb = FOCAL16 / wzb;
  px[0] = HALF_W + (wxa * FOCAL) / wza;
  px[1] = HALF_W + (wxb * FOCAL) / wzb;
  px[2] = px[1];
  px[3] = px[0];
  py[0] = HORIZON - ((curY0 - camH) * sa) / 65536;
  py[1] = HORIZON - ((curY0 - camH) * sb) / 65536;
  py[2] = HORIZON - ((curY1 - camH) * sb) / 65536;
  py[3] = HORIZON - ((curY1 - camH) * sa) / 65536;
  fillPoly(4, pal[(curCi * 5 + sh) * 8 + fogOf((wza + wzb) / 2)]);
}

// Windows on the wall just drawn. len10 is the wall length in tenths of a unit.
// Near buildings get single windows chosen by the pattern bits, mid-distance
// ones get one band per floor, far ones get nothing. Bands stop first when the
// frame is busy, so the budget is kept for the windows closest to the camera.
void drawWindows(int xa, int za, int xb, int zb, int len10, int sh) {
  int floors;
  int f;
  int row;
  int ya;
  int yb;
  int sa;
  int sb;
  int x0;
  int x1;
  int u0;
  int u1;
  int cx0;
  int cz0;
  int cx1;
  int cz1;
  int g;
  int k;
  if (polyCount > WIN_BUDGET) return;
  floors = (curY1 - curY0 - 512) / 1280;
  if (floors < 1) return;
  f = fogOf((za + zb) / 2);
  if (curLod == 1) {
    if (polyCount > BAND_BUDGET) return;
    if (clipWall(xa, za, xb, zb) == 0) return;
    sa = FOCAL16 / wza;
    sb = FOCAL16 / wzb;
    x0 = HALF_W + (wxa * FOCAL) / wza;
    x1 = HALF_W + (wxb * FOCAL) / wzb;
    for (int fl = 0; fl < floors; fl++) {
      row = patT[curStyle * 8 + (fl & 7)];
      if (row != 0) {
        ya = curY0 + 640 + fl * 1280 - camH;
        yb = ya + 666;
        px[0] = x0;
        px[1] = x1;
        px[2] = x1;
        px[3] = x0;
        py[0] = HORIZON - (ya * sa) / 65536;
        py[1] = HORIZON - (ya * sb) / 65536;
        py[2] = HORIZON - (yb * sb) / 65536;
        py[3] = HORIZON - (yb * sa) / 65536;
        fillPoly(4, pal[(10 * 5 + sh) * 8 + f]);
      }
    }
    return;
  }
  k = 0;
  u0 = 12;
  u1 = 36;
  while (u1 <= len10 - 10) {
    cx0 = xa + ((xb - xa) * u0) / len10;
    cz0 = za + ((zb - za) * u0) / len10;
    cx1 = xa + ((xb - xa) * u1) / len10;
    cz1 = za + ((zb - za) * u1) / len10;
    if (cz0 >= NEAR_Z) {
      if (cz1 >= NEAR_Z) {
        sa = FOCAL16 / cz0;
        sb = FOCAL16 / cz1;
        x0 = HALF_W + (cx0 * FOCAL) / cz0;
        x1 = HALF_W + (cx1 * FOCAL) / cz1;
        for (int fl = 0; fl < floors; fl++) {
          row = patT[curStyle * 8 + (fl & 7)];
          if (((row >> (k & 7)) & 1) == 1) {
            if (polyCount <= WIN_BUDGET) {
              ya = curY0 + 640 + fl * 1280 - camH;
              yb = ya + 666;
              g = 10;
              if (((curId + fl * 7 + k * 13) & 3) == 0) g = 11;
              px[0] = x0;
              px[1] = x1;
              px[2] = x1;
              px[3] = x0;
              py[0] = HORIZON - (ya * sa) / 65536;
              py[1] = HORIZON - (ya * sb) / 65536;
              py[2] = HORIZON - (yb * sb) / 65536;
              py[3] = HORIZON - (yb * sa) / 65536;
              fillPoly(4, pal[(g * 5 + sh) * 8 + f]);
            }
          }
        }
      }
    }
    k++;
    u0 = u0 + 40;
    u1 = u1 + 40;
  }
}

int bcx[4];
int bcz[4];

// One side of the current box, from corner i to corner j, plus its windows.
void boxFace(int i, int j, int sh, int lenFP) {
  drawWall(bcx[i], bcz[i], bcx[j], bcz[j], sh);
  if (curStyle >= 0) {
    if (curLod < 2) {
      drawWindows(bcx[i], bcz[i], bcx[j], bcz[j], (lenFP * 10) / 256, sh);
    }
  }
}

// Draws the axis-aligned world box described by the cur* globals. A box is
// convex, so drawing only the faces turned toward the camera needs no sorting.
void drawBox(int x0, int z0, int x1, int z1) {
  int vis;
  toCam(x0, z0);
  bcx[0] = tX;
  bcz[0] = tZ;
  toCam(x1, z0);
  bcx[1] = tX;
  bcz[1] = tZ;
  toCam(x1, z1);
  bcx[2] = tX;
  bcz[2] = tZ;
  toCam(x0, z1);
  bcx[3] = tX;
  bcz[3] = tZ;
  vis = 0;
  for (int i = 0; i < 4; i++) {
    if (bcz[i] >= NEAR_Z) vis = 1;
  }
  if (vis == 0) return;
  if (camZ > z1) boxFace(2, 3, 2, x1 - x0);
  if (camX < x0) boxFace(3, 0, 4, z1 - z0);
  if (camX > x1) boxFace(1, 2, 1, z1 - z0);
  if (camZ < z0) boxFace(0, 1, 3, x1 - x0);
  if (camH > curY1) {
    for (int i = 0; i < 4; i++) {
      ax[i] = bcx[i];
      az[i] = bcz[i];
    }
    flatCam(curY1, curCi, 0);
  }
}

// ===== 6. ENTITIES (static props in this milestone) =====
// Street tree: trunk, canopy and a small crown. lod 1 keeps only the canopy.
void drawTree(int x, int z, int lod) {
  curStyle = 0 - 1;
  if (lod == 0) {
    curY0 = 0;
    curY1 = 922;
    curCi = 6;
    drawBox(x - 102, z - 102, x + 102, z + 102);
  }
  curY0 = 870;
  curY1 = 2048;
  curCi = 7;
  drawBox(x - 538, z - 538, x + 538, z + 538);
  if (lod == 0) {
    curY0 = 2048;
    curY1 = 2560;
    curCi = 18;
    drawBox(x - 333, z - 333, x + 333, z + 333);
  }
}

// Street lamp: pole and a head reaching over the road. dir 0 faces -z, 1 faces +z.
void drawLamp(int x, int z, int dir) {
  curStyle = 0 - 1;
  curY0 = 0;
  curY1 = 2304;
  curCi = 8;
  drawBox(x - 46, z - 46, x + 46, z + 46);
  curY0 = 2202;
  curY1 = 2355;
  curCi = 9;
  if (dir == 0) {
    drawBox(x - 77, z - 512, x + 77, z);
  } else {
    drawBox(x - 77, z, x + 77, z + 512);
  }
}

// Squared distance in whole units from the camera to a world point.
int dist2(int wx, int wz) {
  int dx;
  int dz;
  dx = (wx - camX) / 256;
  dz = (wz - camZ) / 256;
  return dx * dx + dz * dz;
}

int nearX = 0;
int nearZ = 0;

// Point of lot i's footprint closest to the camera, into nearX/nearZ.
void nearestOnLot(int i, int wx, int wz) {
  int a;
  int b;
  a = wx + lotX0[i] * 256;
  b = wx + lotX1[i] * 256;
  nearX = camX;
  if (nearX < a) nearX = a;
  if (nearX > b) nearX = b;
  a = wz + lotZ0[i] * 256;
  b = wz + lotZ1[i] * 256;
  nearZ = camZ;
  if (nearZ < a) nearZ = a;
  if (nearZ > b) nearZ = b;
}

// Building on lot i. Height eases in over the last GROW_U units of draw
// distance, so towers rise out of the fog and do not pop in.
void drawBuilding(int i, int wx, int wz) {
  int dx;
  int dz;
  int du;
  int d2;
  int g;
  int h;
  nearestOnLot(i, wx, wz);
  dx = (nearX - camX) / 256;
  if (dx < 0) dx = 0 - dx;
  dz = (nearZ - camZ) / 256;
  if (dz < 0) dz = 0 - dz;
  d2 = dx * dx + dz * dz;
  du = dx + dz / 2;
  if (dz > dx) du = dz + dx / 2;
  g = DRAW_U - du;
  if (g <= 0) return;
  h = lotH[i] * 256;
  if (g < GROW_U) h = (h * g) / GROW_U;
  curY0 = 0;
  curY1 = h;
  curCi = lotC[i];
  curStyle = lotP[i];
  curId = i;
  curLod = 2;
  if (d2 < D2_WIN_BAND) curLod = 1;
  if (d2 < D2_WIN_NEAR) curLod = 0;
  drawBox(wx + lotX0[i] * 256, wz + lotZ0[i] * 256, wx + lotX1[i] * 256, wz + lotZ1[i] * 256);
}

// --- visible block list, far to near ---
int visI[128];
int visD[128];
int visN = 0;

// Collects the blocks inside the view frustum and draw distance, sorted far to near.
void collectBlocks() {
  int cbx;
  int cbz;
  int d;
  int k;
  int go;
  int ok;
  cbx = camX / PITCH;
  cbz = camZ / PITCH;
  visN = 0;
  for (int iz = cbz - VIEW_R; iz <= cbz + VIEW_R; iz++) {
    for (int ix = cbx - VIEW_R; ix <= cbx + VIEW_R; ix++) {
      ok = 1;
      if (ix < 0) ok = 0;
      if (ix >= CITY_N) ok = 0;
      if (iz < 0) ok = 0;
      if (iz >= CITY_N) ok = 0;
      if (ok == 1) {
        toCam(ix * PITCH + HALF_PITCH, iz * PITCH + HALF_PITCH);
        if (tZ < 0 - BLOCK_RAD) ok = 0;
        if (tZ > DRAW_DIST + BLOCK_RAD) ok = 0;
        if (tX * 5 - tZ * 4 > CULL_MARGIN) ok = 0;
        if (0 - tX * 5 - tZ * 4 > CULL_MARGIN) ok = 0;
      }
      if (ok == 1) {
        d = dist2(ix * PITCH + HALF_PITCH, iz * PITCH + HALF_PITCH);
        k = visN;
        go = 1;
        while (go == 1) {
          go = 0;
          if (k > 0) {
            if (visD[k - 1] < d) {
              visD[k] = visD[k - 1];
              visI[k] = visI[k - 1];
              k--;
              go = 1;
            }
          }
        }
        visD[k] = d;
        visI[k] = iz * 16 + ix;
        visN++;
      }
    }
  }
}

// Ground of one block: sidewalk pad, inner lot or grass, then the lane dashes
// and zebra crossings of the two roads on its west and south sides.
void drawGround(int b, int d) {
  int ix;
  int iz;
  int wx;
  int wz;
  int t;
  int o;
  ix = b % 16;
  iz = b / 16;
  wx = ix * PITCH;
  wz = iz * PITCH;
  t = blkType[b];
  if (t == 2) {
    setQuad(wx - 55296, wz, wx + PAD + ROAD_HALF, wz + PITCH);
    flatCam(0, 5, 0);
    return;
  }
  setQuad(wx + ROAD_HALF, wz + ROAD_HALF, wx + ROAD_HALF + PAD, wz + ROAD_HALF + PAD);
  flatCam(0, 1, 0);
  if (d < D2_LOT) {
    setQuad(wx + ROAD_HALF + 768, wz + ROAD_HALF + 768, wx + ROAD_HALF + PAD - 768, wz + ROAD_HALF + PAD - 768);
    if (t == 1) {
      flatCam(0, 3, 0);
    } else {
      flatCam(0, 2, 0);
    }
  }
  if (d < D2_PROPS_FULL) {
    for (int j = 0; j < 4; j++) {
      o = ROAD_HALF + 1536 + j * 2304;
      if (ix > 1) {
        setQuad(wx - 77, wz + o, wx + 77, wz + o + 1280);
        flatCam(0, 4, 0);
      }
      if (iz > 0) {
        setQuad(wx + o, wz - 77, wx + o + 1280, wz + 77);
        flatCam(0, 4, 0);
      }
    }
  }
  if (d < D2_ZEBRA) {
    for (int j = 0; j < 5; j++) {
      o = j * 666 - 1536;
      if (ix > 1) {
        setQuad(wx + o, wz + ROAD_HALF + 128, wx + o + 384, wz + ROAD_HALF + 768);
        flatCam(0, 4, 0);
      }
      if (iz > 0) {
        setQuad(wx + ROAD_HALF + 128, wz + o, wx + ROAD_HALF + 768, wz + o + 384);
        flatCam(0, 4, 0);
      }
    }
  }
}

// --- per-block object list, far to near ---
int itT[16];   // 0 building, 1 tree, 2 lamp facing -z, 3 lamp facing +z
int itA[16];
int itB[16];
int itD[16];
int itN = 0;

// Inserts an object into the block list, keeping it sorted far to near.
void addItem(int t, int a, int b, int d) {
  int k;
  int go;
  if (itN >= 16) return;
  k = itN;
  go = 1;
  while (go == 1) {
    go = 0;
    if (k > 0) {
      if (itD[k - 1] < d) {
        itD[k] = itD[k - 1];
        itT[k] = itT[k - 1];
        itA[k] = itA[k - 1];
        itB[k] = itB[k - 1];
        k--;
        go = 1;
      }
    }
  }
  itD[k] = d;
  itT[k] = t;
  itA[k] = a;
  itB[k] = b;
  itN++;
}

// Buildings, trees and lamps of one block, sorted by distance and drawn far to near.
void drawBlockObjects(int b, int d) {
  int ix;
  int iz;
  int wx;
  int wz;
  int t;
  int i;
  int x;
  int z;
  int lod;
  t = blkType[b];
  if (t == 2) return;
  ix = b % 16;
  iz = b / 16;
  wx = ix * PITCH + ROAD_HALF;
  wz = iz * PITCH + ROAD_HALF;
  itN = 0;
  lod = 2;
  if (d < D2_PROPS_SIMPLE) lod = 1;
  if (d < D2_PROPS_FULL) lod = 0;
  if (t == 1) {
    if (lod < 2) {
      for (int j = 0; j < 6; j++) {
        x = wx + 1280 + (hash2(b, j) % 7680);
        z = wz + 1280 + (hash2(b, j + 8) % 7680);
        addItem(1, x, z, dist2(x, z));
      }
    }
  } else {
    for (int l = 0; l < 4; l++) {
      i = b * 4 + l;
      if (lotH[i] > 0) {
        nearestOnLot(i, wx, wz);
        addItem(0, i, 0, dist2(nearX, nearZ));
      }
    }
    if (lod < 2) {
      for (int j = 0; j < 4; j++) {
        x = wx + 384;
        if (j > 1) x = wx + PAD - 384;
        z = wz + 3072;
        if ((j & 1) == 1) z = wz + 7168;
        addItem(1, x, z, dist2(x, z));
      }
    }
    if (lod == 0) {
      addItem(2, wx + 5120, wz + 307, dist2(wx + 5120, wz + 307));
      addItem(3, wx + 5120, wz + PAD - 307, dist2(wx + 5120, wz + PAD - 307));
    }
  }
  for (int k = 0; k < itN; k++) {
    t = itT[k];
    if (t == 0) {
      if (polyCount < MAX_POLY) drawBuilding(itA[k], wx, wz);
    } else {
      if (polyCount < PROP_BUDGET) {
        if (t == 1) drawTree(itA[k], itB[k], lod);
        if (t == 2) drawLamp(itA[k], itB[k], 0);
        if (t == 3) drawLamp(itA[k], itB[k], 1);
      }
    }
  }
}


// ===== 7. PLAYER, CAR AND COLLISIONS =====
// The player walks, or drives the car. Positions are 24.8, headings 0..1023
// (forward is (sin, cos), right is (cos, -sin)), speeds 24.8 per 30 Hz step.
// the car's collision radius
#define CAR_R 640
// the person's
#define PED_R 160
#define CAR_ACC 22
#define CAR_BRAKE 48
#define CAR_DRAG 6
// top speed: 2.2 units a step
#define CAR_MAX 560
#define CAR_REV 200
// heading change per step at full speed
#define CAR_TURN 18
#define WALK_STEP 96
#define WALK_TURN 14
// squared units: how close to the car to get in
#define ENTER_D2 64

int driving = 1;
int carX = 111462;
int carZ = 82944;
int carA = 0;
int carSpd = 0;
int pedX = 0;
int pedZ = 0;
int pedA = 0;
int view = 0;              // 0 chase, 1 far chase, 2 hood
int lastEnter = 0;
int lastView = 0;

// 1 when a circle of radius r at (x, z) is in the water, outside the city
// or inside a building.
int blocked(int x, int z, int r) {
  int bx;
  int bz;
  int b;
  int i;
  int wx;
  int wz;
  if (x - r < CITY_MIN) return 1;
  if (x + r > CITY_MAX) return 1;
  if (z - r < CITY_MIN) return 1;
  if (z + r > CITY_MAX) return 1;
  if (x - r < PITCH - ROAD_HALF) return 1;   // the waterfront column
  for (int k = 0; k < 4; k++) {
    bx = x - r;
    if ((k & 1) == 1) bx = x + r;
    bz = z - r;
    if (k > 1) bz = z + r;
    bx = bx / PITCH;
    bz = bz / PITCH;
    if (bx >= 0) {
      if (bx < CITY_N) {
        if (bz >= 0) {
          if (bz < CITY_N) {
            b = bz * 16 + bx;
            if (blkType[b] == 0) {
              wx = bx * PITCH + ROAD_HALF;
              wz = bz * PITCH + ROAD_HALF;
              for (int l = 0; l < 4; l++) {
                i = b * 4 + l;
                if (lotH[i] > 0) {
                  if (x + r > wx + lotX0[i] * 256) {
                    if (x - r < wx + lotX1[i] * 256) {
                      if (z + r > wz + lotZ0[i] * 256) {
                        if (z - r < wz + lotZ1[i] * 256) return 1;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return 0;
}

// Heading wrapped to 0..1023.
int wrapA(int a) {
  return a & 1023;
}

// One 30 Hz step of the car from the held keys.
void driveStep() {
  int s;
  int c;
  int nx;
  int nz;
  int turn;
  if (keyDown(KEY_UP) != 0) {
    carSpd = carSpd + CAR_ACC;
  } else {
    if (keyDown(KEY_DOWN) != 0) {
      if (carSpd > 0) {
        carSpd = carSpd - CAR_BRAKE;
        if (carSpd < 0) carSpd = 0;
      } else {
        carSpd = carSpd - CAR_ACC;
      }
    } else {
      if (carSpd > 0) {
        carSpd = carSpd - CAR_DRAG;
        if (carSpd < 0) carSpd = 0;
      }
      if (carSpd < 0) {
        carSpd = carSpd + CAR_DRAG;
        if (carSpd > 0) carSpd = 0;
      }
    }
  }
  if (keyDown(KEY_BRAKE) != 0) {
    if (carSpd > 0) {
      carSpd = carSpd - CAR_BRAKE * 2;
      if (carSpd < 0) carSpd = 0;
    }
    if (carSpd < 0) {
      carSpd = carSpd + CAR_BRAKE * 2;
      if (carSpd > 0) carSpd = 0;
    }
  }
  if (carSpd > CAR_MAX) carSpd = CAR_MAX;
  if (carSpd < 0 - CAR_REV) carSpd = 0 - CAR_REV;
  // steering turns faster with speed, and the other way when reversing
  turn = (CAR_TURN * carSpd) / CAR_MAX;
  if (carSpd > 0) {
    if (turn < 3) turn = 3;
  }
  if (carSpd == 0) turn = 0;
  if (keyDown(KEY_LEFT) != 0) carA = wrapA(carA - turn);
  if (keyDown(KEY_RIGHT) != 0) carA = wrapA(carA + turn);
  s = sinT[carA];
  c = sinT[(carA + 256) & 1023];
  nx = carX + (s * carSpd) / 4096;
  nz = carZ + (c * carSpd) / 4096;
  if (blocked(nx, nz, CAR_R) == 0) {
    carX = nx;
    carZ = nz;
  } else {
    // slide along the wall when one axis is free, else bounce back
    if (blocked(nx, carZ, CAR_R) == 0) {
      carX = nx;
      carSpd = (carSpd * 3) / 4;
    } else {
      if (blocked(carX, nz, CAR_R) == 0) {
        carZ = nz;
        carSpd = (carSpd * 3) / 4;
      } else {
        carSpd = 0 - carSpd / 4;
      }
    }
  }
}

// One 30 Hz step on foot.
void walkStep() {
  int s;
  int c;
  int nx;
  int nz;
  int step;
  if (keyDown(KEY_LEFT) != 0) pedA = wrapA(pedA - WALK_TURN);
  if (keyDown(KEY_RIGHT) != 0) pedA = wrapA(pedA + WALK_TURN);
  step = 0;
  if (keyDown(KEY_UP) != 0) step = WALK_STEP;
  if (keyDown(KEY_DOWN) != 0) step = 0 - WALK_STEP / 2;
  if (step != 0) {
    s = sinT[pedA];
    c = sinT[(pedA + 256) & 1023];
    nx = pedX + (s * step) / 4096;
    nz = pedZ + (c * step) / 4096;
    if (blocked(nx, pedZ, PED_R) == 0) pedX = nx;
    if (blocked(pedX, nz, PED_R) == 0) pedZ = nz;
  }
}

// Getting out puts the person beside the driver's door; getting in needs
// the car within a few steps and stopped.
void enterOrExit() {
  int s;
  int c;
  int dx;
  int dz;
  if (driving == 1) {
    if (carSpd > 40) return;
    if (carSpd < 0 - 40) return;
    s = sinT[carA];
    c = sinT[(carA + 256) & 1023];
    // the left side: minus the right vector (cos, -sin)
    pedX = carX - (c * 896) / 4096;
    pedZ = carZ + (s * 896) / 4096;
    pedA = carA;
    if (blocked(pedX, pedZ, PED_R) == 1) {
      pedX = carX + (c * 896) / 4096;
      pedZ = carZ - (s * 896) / 4096;
    }
    carSpd = 0;
    driving = 0;
  } else {
    dx = (pedX - carX) / 256;
    dz = (pedZ - carZ) / 256;
    if (dx * dx + dz * dz <= ENTER_D2) driving = 1;
  }
}

// The camera from the view: behind the car (near or far, higher) or at its
// bonnet; behind the person on foot. The heading follows with a little lag.
void placeCamera() {
  int tx;
  int tz;
  int ta;
  int d;
  int diff;
  int s;
  int c;
  if (driving == 1) {
    tx = carX;
    tz = carZ;
    ta = carA;
  } else {
    tx = pedX;
    tz = pedZ;
    ta = pedA;
  }
  diff = ((ta - camA + 1536) & 1023) - 512;
  camA = wrapA(camA + diff / 3);
  if (diff > 0) {
    if (diff < 3) camA = ta;
  }
  if (diff < 0) {
    if (diff > 0 - 3) camA = ta;
  }
  d = 3584;
  camH = 1792;
  if (view == 1) {
    d = 6400;
    camH = 3072;
  }
  if (view == 2) {
    d = 0 - 1280;
    camH = 1100;
  }
  if (driving == 0) {
    // on foot: over the shoulder, farther back, or at eye height
    d = 1792;
    camH = 1280;
    if (view == 1) {
      d = 3584;
      camH = 2304;
    }
    if (view == 2) {
      d = 0 - 128;
      camH = 860;
    }
  }
  s = sinT[camA];
  c = sinT[(camA + 256) & 1023];
  camX = tx - (s * d) / 4096;
  camZ = tz - (c * d) / 4096;
}

// One 30 Hz step: the keys pressed once (get in or out, the view), then
// the car or the person, then the camera.
void update() {
  int k;
  k = keyDown(KEY_ENTER);
  if (k != 0) {
    if (lastEnter == 0) enterOrExit();
  }
  lastEnter = k;
  k = keyDown(KEY_VIEW);
  if (k != 0) {
    if (lastView == 0) {
      view = view + 1;
      if (view == 3) view = 0;
      initRows();
    }
  }
  lastView = k;
  if (driving == 1) {
    driveStep();
  } else {
    walkStep();
  }
  placeCamera();
}

// ===== 8. DRAWING THE CAR AND THE PERSON =====
int owx[4];
int owz[4];

// One box of a mesh at (cx, cz) turned by heading a: the faces turned to
// the camera, then the top when the camera is above it.
void drawOBox(int cx, int cz, int a, int x0, int x1, int z0, int z1) {
  int s;
  int c;
  int lx;
  int lz;
  s = sinT[a];
  c = sinT[(a + 256) & 1023];
  for (int i = 0; i < 4; i++) {
    lx = x0;
    if (i == 1) lx = x1;
    if (i == 2) lx = x1;
    lz = z0;
    if (i >= 2) lz = z1;
    owx[i] = cx + (lx * c + lz * s) / 4096;
    owz[i] = cz + (lz * c - lx * s) / 4096;
    toCam(owx[i], owz[i]);
    bcx[i] = tX;
    bcz[i] = tZ;
  }
  // back (normal -forward), right (+right), front (+forward), left (-right)
  if ((camX - owx[0]) * (0 - s) + (camZ - owz[0]) * (0 - c) > 0) drawWall(bcx[0], bcz[0], bcx[1], bcz[1], 3);
  if ((camX - owx[1]) * c + (camZ - owz[1]) * (0 - s) > 0) drawWall(bcx[1], bcz[1], bcx[2], bcz[2], 1);
  if ((camX - owx[2]) * s + (camZ - owz[2]) * c > 0) drawWall(bcx[2], bcz[2], bcx[3], bcz[3], 2);
  if ((camX - owx[3]) * (0 - c) + (camZ - owz[3]) * s > 0) drawWall(bcx[3], bcz[3], bcx[0], bcz[0], 4);
  if (camH > curY1) {
    for (int i = 0; i < 4; i++) {
      ax[i] = bcx[i];
      az[i] = bcz[i];
    }
    flatCam(curY1, curCi, 0);
  }
}

int boxOrd[12];
int boxD[12];

// A mesh (n boxes of 7 values) at (cx, cz) with heading a, its boxes drawn
// far to near.
void drawMesh(int *m, int n, int cx, int cz, int a) {
  int s;
  int c;
  int mx;
  int mz;
  int k;
  int go;
  int d;
  s = sinT[a];
  c = sinT[(a + 256) & 1023];
  for (int i = 0; i < n; i++) {
    mx = (m[i * 7] + m[i * 7 + 1]) / 2;
    mz = (m[i * 7 + 2] + m[i * 7 + 3]) / 2;
    d = dist2(cx + (mx * c + mz * s) / 4096, cz + (mz * c - mx * s) / 4096);
    k = i;
    go = 1;
    while (go == 1) {
      go = 0;
      if (k > 0) {
        if (boxD[k - 1] < d) {
          boxD[k] = boxD[k - 1];
          boxOrd[k] = boxOrd[k - 1];
          k--;
          go = 1;
        }
      }
    }
    boxD[k] = d;
    boxOrd[k] = i;
  }
  curStyle = 0 - 1;
  for (int k2 = 0; k2 < n; k2++) {
    k = boxOrd[k2] * 7;
    curY0 = m[k + 4];
    curY1 = m[k + 5];
    curCi = m[k + 6];
    drawOBox(cx, cz, a, m[k], m[k + 1], m[k + 2], m[k + 3]);
  }
}

// After the city: the person and the car, the farther one first.
void drawActors() {
  if (view == 2) {
    if (driving == 1) return;   // from the bonnet the car is not seen
  }
  if (driving == 1) {
    drawMesh(carBox, carN, carX, carZ, carA);
    return;
  }
  if (dist2(carX, carZ) > dist2(pedX, pedZ)) {
    drawMesh(carBox, carN, carX, carZ, carA);
    drawMesh(pedBox, pedN, pedX, pedZ, pedA);
  } else {
    drawMesh(pedBox, pedN, pedX, pedZ, pedA);
    drawMesh(carBox, carN, carX, carZ, carA);
  }
}

// One full frame: background, all ground, then all objects far to near, then the car and the person.
void render() {
  polyCount = 0;
  sinA = sinT[camA];
  cosA = sinT[(camA + 256) & 1023];
  drawBackground();
  collectBlocks();
  for (int k = 0; k < visN; k++) drawGround(visI[k], visD[k]);
  for (int k = 0; k < visN; k++) drawBlockObjects(visI[k], visD[k]);
  drawActors();
}

// ===== 11. MAIN LOOP =====
// Fixed 30 Hz update decoupled from rendering, at most 3 steps per frame.
int main() {
  int last;
  int now;
  int acc;
  int run;
  if (loadAssets() == 0) return 1;
  genCity();
  placeCamera();
  initRows();
  last = millis();
  acc = 0;
  run = 1;
  while (run == 1) {
    now = millis();
    acc = acc + now - last;
    last = now;
    if (acc > 99) acc = 99;
    while (acc >= TICK_MS) {
      update();
      acc = acc - TICK_MS;
    }
    render();
    present();
    if (keyDown(KEY_QUIT) != 0) run = 0;
  }
  return 0;
}
