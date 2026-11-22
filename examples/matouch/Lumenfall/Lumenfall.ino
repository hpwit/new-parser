// Lumenfall, on the ESP32-S3-Touch-LCD-7: a side-scrolling platform game
// at 400x240 (shown 2x on the 800x480 panel) in RGB565, played with a USB
// keyboard. Wren, the last lamplighter, runs and jumps through four
// darkened lands, two levels each, with a guardian at the end of every
// land, and lights their beacons again. Three parallax layers behind every
// level. See README_GAMES.md, "Lumenfall".
//
// Every picture (the layers, the 16x16 tiles, the sprites), the eight
// levels and the story come from a WAD file on the FFat partition,
// /lumenfall.wad (data/lumenfall.wad, made by tools/make_wad.py). The
// sketch reads it into PSRAM and binds it for the script (wad, wadLoaded,
// wadFind). The script is tools/lumenfall.sc, put in here by
// tools/make_sketch.py; SolarOS runs the same script (lumenfall.bin).
//
// Keys
//   left / right (or Q D, A)   run
//   Space, up (or Z W K)       jump; hold for height
//   F (or J X)                 shoot; hold to keep firing
//   down (S)                   crouch; down and jump drops through a ledge
//   Enter                      pause; Escape leaves
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with
// show(), _frame_buffer, pixel_size, offset) and the ESP32_USB_Host_HID
// library with the key hooks (see the README's "A USB keyboard driving a
// script"). Matouch7.h is the panel configuration.
//
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB, Partition
// Scheme "16M Flash (3MB APP/9.9MB FATFS)".
#include <FS.h>
#include <FFat.h>
#include "Matouch7.h"
#include "keyboard.h"
#include "script_executable.h"
#include "asm_execute.h"
#include "binding.h"

LGFX lcd;
// where the script draws: a pointer, not an array, since updateDisplay()
// swaps the frame buffers
uint16_t *frame_buffer;
static uint32_t stride = 800;

void updateDisplay() {
  lcd._bus_instance.show();
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  lcd._panel_instance.resetWriteBuffer();
}

void enableGraphicMode() {
}

void setPixelsize(int h) {
  memset(frame_buffer, 0, 800 * 480 * 2);
  updateDisplay();
  memset(frame_buffer, 0, 800 * 480 * 2);
  lcd._bus_instance.offset = 0;
  lcd._bus_instance.pixel_size = h;
}

uint32_t scriptMillis() {
  return millis();
}

// as SolarOS's delay(): the task sleeps, at least one tick, so the other
// tasks (the idle task, the keyboard) run
void scriptDelay(uint32_t ms) {
  TickType_t t = pdMS_TO_TICKS(ms);
  vTaskDelay(t > 0 ? t : 1);
}

uint32_t scriptRand(uint32_t n) {
  return n ? esp_random() % n : 0;
}

// --- the WAD: read from the FFat partition into PSRAM ---
static uint8_t *wadData = NULL;
static int wadLoaded = 0;

static void loadWad(const char *path) {
  if (!FFat.begin(false)) {
    printf("FFat does not mount: put data/lumenfall.wad on it (see the README)\n");
    return;
  }
  File f = FFat.open(path, FILE_READ);
  if (!f) {
    printf("no %s on the FFat partition\n", path);
    return;
  }
  size_t size = f.size();
  wadData = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (wadData && f.read(wadData, size) == size && memcmp(wadData + 1, "WAD", 3) == 0) {
    wadLoaded = 1;
    printf("%s: %u bytes\n", path, (unsigned)size);
  } else {
    printf("%s does not read\n", path);
    free(wadData);
    wadData = NULL;
  }
  f.close();
}

// where the entry of that name starts in the WAD, -1 when it is not there
static int wadFind(char *name) {
  if (!wadData)
    return -1;
  int32_t n, dir;
  memcpy(&n, wadData + 4, 4);
  memcpy(&dir, wadData + 8, 4);
  for (int i = 0; i < n; i++) {
    const uint8_t *e = wadData + dir + 16 * i;
    if (strncmp((const char *)e + 8, name, 8) == 0) {
      int32_t pos;
      memcpy(&pos, e, 4);
      return pos;
    }
  }
  return -1;
}

// --- the keys: readKey() gives each press (the arrows as Z Q S D, a
// released arrow as its letter + 128), keyHeld(c) whether one is down ---
#define KEY_QUEUE 16
static volatile uint8_t keyQueue[KEY_QUEUE];
static volatile uint8_t keyHead = 0, keyTail = 0;
static volatile uint8_t keyDown[128];

static void pushKey(uint8_t c) {
  uint8_t next = (keyHead + 1) % KEY_QUEUE;
  if (next != keyTail) {
    keyQueue[keyHead] = c;
    keyHead = next;
  }
}

int readKey() {
  if (keyTail == keyHead)
    return 0;
  int c = keyQueue[keyTail];
  keyTail = (keyTail + 1) % KEY_QUEUE;
  return c;
}

int keyHeld(int c) {
  return keyDown[c & 127];
}

static uint8_t keyLetter(key_return k) {
  if (k.key_code == HID_KEY_UP)
    return 'z';
  if (k.key_code == HID_KEY_LEFT)
    return 'q';
  if (k.key_code == HID_KEY_DOWN)
    return 's';
  if (k.key_code == HID_KEY_RIGHT)
    return 'd';
  if (k.key_code == HID_KEY_ESC)
    return 27;
  if (k.key_code == HID_KEY_ENTER)
    return 13;
  return tolower(k.key_char) & 127;
}

void print_char(key_return k) {
  uint8_t c = keyLetter(k);
  if (c != 0) {
    keyDown[c] = 1;
    pushKey(c);
  }
}

void release_char(key_return k) {
  uint8_t c = keyLetter(k);
  keyDown[c] = 0;
  if (c == 'z' || c == 'q' || c == 's' || c == 'd')
    pushKey(c | 0x80);
}

// --- the compile's progress, on the Serial Monitor: the script is compiled
// here, on the board, and its 80 KB take a while. setCompileProgress()
// calls showProgress() as the compiler works (at most once a percent);
// it prints a line every 10% and whenever the stage changes. ---
static void showProgress(int percent, const char *stage, void *ctx) {
  static int shownTens = -1;
  static const char *shownStage = NULL;
  (void)ctx;
  if (percent / 10 == shownTens && stage == shownStage)
    return;
  shownTens = percent / 10;
  shownStage = stage;
  char bar[21];
  for (int i = 0; i < 20; i++)
    bar[i] = i < percent / 5 ? '#' : '.';
  bar[20] = 0;
  printf("compiling lumenfall [%s] %3d%%  %s\n", bar, percent, stage);
}

char lumenfall[] = R"EOF(// LUMENFALL - a lantern-bearer's run through four darkened lands.
//
// A side-scrolling platform game for the ESP32-S3-Touch-LCD-7 at 400x240
// (setPixelsize(2) shows it on the whole 800x480 panel), in RGB565:
// three parallax layers behind the level, 16x16 tiles, Wren and the
// creatures of each land, a guardian at the end of every second level.
//
// Runs as examples/matouch/Lumenfall, in the matouch GameLauncher
// (lumenfall.bin) and on SolarOS (asm run lumenfall.bin, the WAD next to
// it). lumenfall.wad holds every picture, the eight levels and the story:
// tools/make_wad.py builds it from tools/art.py, tiles.py, sprites.py and
// levels.py, and writes the generated part below.
//
// It always runs at 400x240, shown 2x on the whole panel (pixel size 2),
// and takes no parameters: the same lumenfall.bin runs everywhere.
//
// Keys
//   left / right (or Q / D, A)   run
//   Space, up (or Z, W, K)       jump; hold it to jump higher
//   F (or J, X)                  shoot the lantern-caster; hold to keep firing
//   down (or S)                  crouch; down and jump drops through a ledge
//   Enter                        pause
//   Escape                       leave (asks first)
//
// The rules
//   Stomp a creature or shoot it. Gather glints (100 = a life), break open
//   lumen boxes from below. A prism makes Wren's shots stronger until she
//   is hurt. Touch the lantern posts: you come back there if you fall.
//   Light the beacon at the end of each level. Every second level ends
//   with the land's guardian; light its beacon to free the land.

// --- generated by tools/make_wad.py: do not edit by hand ---
#define T_EMPTY 0
#define T_TOP 1
#define T_TOP_L 2
#define T_TOP_R 3
#define T_FILL 4
#define T_FILL_L 5
#define T_FILL_R 6
#define T_FILL2 7
#define T_PLAT_L 8
#define T_PLAT_M 9
#define T_PLAT_R 10
#define T_BLOCK 11
#define T_BOX_A 12
#define T_BOX_B 13
#define T_BOX_USED 14
#define T_SPIKES 15
#define T_LIQTOP_A 16
#define T_LIQTOP_B 17
#define T_LIQ_A 18
#define T_LIQ_B 19
#define T_CRATE 20
#define T_DECO1 21
#define T_DECO2 22
#define T_DECO3 23
#define T_DECO4 24
#define T_DECO5 25
#define T_DECO6 26
#define T_DECO7 27
#define T_DECO8 28
#define T_COLUMN 29
#define T_COLUMN_TOP 30
#define T_GLINT_A 31
#define T_GLINT_B 32
#define T_GLINT_C 33
#define T_GLINT_D 34
#define T_UNDER 35
#define K_NONE 0
#define K_WALKER 1
#define K_FLYER 2
#define K_SPECIAL 3
#define K_BOSS 4
#define K_CHECK 5
#define K_BEACON 6
#define K_PLATH 7
#define K_PLATV 8
#define K_HEART 9
#define K_PRISM 10
#define K_LIFE 11
#define K_BOXITEM 12
#define W_STAND 0
#define W_BLINK 1
#define W_RUN0 2
#define W_RUN1 3
#define W_RUN2 4
#define W_RUN3 5
#define W_RUN4 6
#define W_RUN5 7
#define W_JUMP 8
#define W_FALL 9
#define W_CROUCH 10
#define W_HURT 11
#define W_AIM 12
#define W_ARUN0 13
#define W_ARUN1 14
#define W_ARUN2 15
#define W_ARUN3 16
#define W_ARUN4 17
#define W_ARUN5 18
#define W_AJUMP 19
#define W_AFALL 20
#define TX_INTRO 0
#define TX_END 404
#define KEY 0x1FF8
// --- end of the generated part ---

// ------------------------------------------------------------------ sizes

#define SW 400
#define SH 240
#define ROWS 15
#define MAXW 400
#define LW 512

// what the keyboard gives (readKey() and keyHeld() characters)
#define KEY_UP 122
#define KEY_LEFT 113
#define KEY_DOWN 115
#define KEY_RIGHT 100
#define KEY_A 97
#define KEY_W 119
#define KEY_K 107
#define KEY_F 102
#define KEY_J 106
#define KEY_X 120
#define KEY_Y 121
#define KEY_N 110
#define KEY_C 99
#define KEY_SPACE 32
#define KEY_ENTER 13
#define KEY_ESC 27

// the physics, in sixteenths of a pixel per step (30 steps a second)
#define SUB 16
#define GRAV 6
#define JUMPV 118
#define JUMPCUT 44
#define MAXFALL 120
#define RUNMAX 40
#define RUNACC 4
#define AIRACC 3
#define FRICTION 5
#define STEP_MS 33

// Wren's box: 10 wide, 20 tall, x her middle, y her feet
#define HW 5
#define HH 20

// sprite sheets (sprite numbers)
#define S_WREN 0
#define S_CRIT 1
#define S_BOSS 2
#define S_PLAT 3
#define S_BOLT 4
#define S_BIGBOLT 5
#define S_ORB 6
#define S_SPORE 7
#define S_SHARD 8
#define S_FIRE 9
#define S_HEART 10
#define S_PRISM 11
#define S_LIFE 12
#define S_PUFF 13
#define S_SPARK 14
#define S_CHECK 15
#define S_BEACON 16
#define S_HUD 17
#define S_LOGO 18
#define NSPR 19

// what moves in a level
#define E_NONE 0
#define E_WALKER 1
#define E_FLYER 2
#define E_HOPPER 3
#define E_TURRET 4
#define E_BOSS 5
#define E_CHECK 6
#define E_BEACON 7
#define E_PLATH 8
#define E_PLATV 9
#define E_HEART 10
#define E_PRISM 11
#define E_LIFE 12
#define E_GLINT 13
#define MAXE 72

// shots
#define P_BOLT 0
#define P_BIG 1
#define P_ORB 2
#define P_SPORE 3
#define P_SHARD 4
#define P_FIRE 5
#define MAXP 32

// sparks and things that fade
#define F_PUFF 0
#define F_SPARK 1
#define F_SCORE 2
#define F_DUST 3
#define MAXF 48

// ------------------------------------------------------------------ memory

// one lump unpacked at a time
uint8_t ubuf[80000];
uint16_t pal[256];

// the three parallax layers of the land, 512 wide
uint16_t lay0[77824];
uint16_t lay1[65536];
uint16_t lay2[61440];
int layY[3];
int layH[3];
int layFirst[3];
int layFull[3];

// the land's 36 tiles, 256 pixels each; 1 where a tile has no transparent pixel
uint16_t tilePix[9216];
uint8_t tileOpaque[40];

// every sprite, one after the other
uint16_t sprPix[90000];
int sprOff[NSPR];
int sprW[NSPR];
int sprH[NSPR];
int sprN[NSPR];
int sprFill;
int landSprFrom;

// the level
uint8_t map[6000];
int mapW;
int land;
int levelNo;
int bossLevel;

// the screen
int pixelSize;
int orgX;
int orgY;
int orgOff;
int camX;
int camMin;
int camMax;
int shake;

// the font: 95 widths, then 8 row bytes a character
int fontOff;
int textOff;

// ------------------------------------------------------------------ small helpers

int iabs(int v)
{
   if (v < 0)
      return -v;
   return v;
}

int imin(int a, int b)
{
   if (a < b)
      return a;
   return b;
}

int imax(int a, int b)
{
   if (a > b)
      return a;
   return b;
}

int clampi(int v, int lo, int hi)
{
   if (v < lo)
      return lo;
   if (v > hi)
      return hi;
   return v;
}

int sgn(int v)
{
   if (v < 0)
      return -1;
   if (v > 0)
      return 1;
   return 0;
}

// a colour as the panel wants it: RGB565, its two bytes swapped
uint16_t col(int r, int g, int b)
{
   int v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
   return ((v & 255) << 8) | (v >> 8);
}

// a number from 0 to n-1
int rnd(int n)
{
   if (n <= 1)
      return 0;
   return rand(n);
}

// a small sine table: isin(a) for a in 0..255 (a full turn), -64..64
int sinTab[64];

void makeSines()
{
   // a quarter wave, built with integer steps: 64 * sin(i * pi / 128)
   int s = 0;
   int c = 64 * 256;
   for (int i = 0; i < 64; i++)
   {
      sinTab[i] = s >> 8;
      // rotate (c, s) by pi/128: s += c * 0.02454, c -= s * 0.02454
      s = s + ((c * 201) >> 13);
      c = c - ((s * 201) >> 13);
   }
}

int isin(int a)
{
   a = a & 255;
   if (a < 64)
      return sinTab[a];
   if (a < 128)
      return sinTab[127 - a];
   if (a < 192)
      return -sinTab[a - 128];
   return -sinTab[255 - a];
}

int icos(int a)
{
   return isin(a + 64);
}

// ------------------------------------------------------------------ the WAD

int rdw16(int o)
{
   return wad[o] | (wad[o + 1] << 8);
}

int rd16(int o)
{
   return ubuf[o] | (ubuf[o + 1] << 8);
}

// unpacks a compressed lump into ubuf; returns its size
int unpack(char *name)
{
   int off = wadFind(name);
   if (off < 0)
      return 0;
   int n = wad[off] | (wad[off + 1] << 8) | (wad[off + 2] << 16) | (wad[off + 3] << 24);
   int i = off + 4;
   int o = 0;
   while (o < n)
   {
      int t = wad[i];
      i++;
      if (t < 128)
      {
         int cnt = t + 1;
         for (int k = 0; k < cnt; k++)
            ubuf[o + k] = wad[i + k];
         i = i + cnt;
         o = o + cnt;
      }
      else
      {
         int len = (t & 127) + 3;
         int d = wad[i] | (wad[i + 1] << 8);
         i = i + 2;
         for (int k = 0; k < len; k++)
            ubuf[o + k] = ubuf[o + k - d];
         o = o + len;
      }
   }
   return n;
}

// the picture in ubuf: its header
int imgW;
int imgH;
int imgN;
int imgY;
int imgFirst;
int imgFull;

// reads an image lump's header and its colours; returns where its pixels start
int imageHeader(char *name)
{
   unpack(name);
   imgW = rd16(0);
   imgH = rd16(2);
   imgN = rd16(4);
   int ncol = rd16(6);
   imgY = rd16(8);
   imgFirst = rd16(10);
   imgFull = rd16(12);
   for (int i = 0; i < ncol; i++)
      pal[i] = rd16(16 + i * 2);
   return 16 + ncol * 2;
}

// a background layer of the land into lay0, lay1 or lay2
void loadLayer(char *name, int which)
{
   int p = imageHeader(name);
   int n = imgW * imgH;
   uint16_t *dst = lay0;
   if (which == 1)
      dst = lay1;
   if (which == 2)
      dst = lay2;
   for (int i = 0; i < n; i++)
      dst[i] = pal[ubuf[p + i]];
   layY[which] = imgY;
   layH[which] = imgH;
   layFirst[which] = imgFirst;
   layFull[which] = imgFull;
}

void loadTiles(char *name)
{
   int p = imageHeader(name);
   int n = imgW * imgH * imgN;
   for (int i = 0; i < n; i++)
      tilePix[i] = pal[ubuf[p + i]];
   for (int t = 0; t < imgN && t < 40; t++)
   {
      int full = 1;
      for (int i = 0; i < 256; i++)
         if (tilePix[t * 256 + i] == KEY)
         {
            full = 0;
            break;
         }
      tileOpaque[t] = full;
   }
}

// a sprite sheet into sprPix, at the end of what is there
void loadSprite(char *name, int id)
{
   int p = imageHeader(name);
   int n = imgW * imgH * imgN;
   sprOff[id] = sprFill;
   sprW[id] = imgW;
   sprH[id] = imgH;
   sprN[id] = imgN;
   for (int i = 0; i < n; i++)
      sprPix[sprFill + i] = pal[ubuf[p + i]];
   sprFill = sprFill + n;
}

// a lump name with a digit at its end: "BG" + land + "_" + n and so on
char nameBuf[12];

char *lumpName2(char *a, int d1, int sep, int d2)
{
   int i = 0;
   while (a[i] != 0)
   {
      nameBuf[i] = a[i];
      i++;
   }
   nameBuf[i] = 48 + d1;
   i++;
   if (sep != 0)
   {
      nameBuf[i] = sep;
      i++;
      nameBuf[i] = 48 + d2;
      i++;
   }
   nameBuf[i] = 0;
   return nameBuf;
}

// ------------------------------------------------------------------ drawing

// the parallax: rows y0..y1-1 of layer `which`, scrolled by `scroll` pixels
void drawLayer(int which, int scroll, int y0, int y1)
{
   uint16_t *src = lay0;
   if (which == 1)
      src = lay1;
   if (which == 2)
      src = lay2;
   int ly = layY[which];
   int full = ly + layFull[which];
   int sx = scroll & (LW - 1);
   int n1 = LW - sx;
   if (n1 > SW)
      n1 = SW;
   y0 = imax(y0, ly + layFirst[which]);
   y1 = imin(y1, ly + layH[which]);
   for (int y = y0; y < y1; y++)
   {
      int o = (y - ly) * LW;
      int dst = orgOff + y * stride;
      if (y >= full)
      {
         int s = o + sx;
         for (int x = 0; x < n1; x++)
            display[dst + x] = src[s + x];
         int rest = SW - n1;
         int dd = dst + n1;
         for (int x = 0; x < rest; x++)
            display[dd + x] = src[o + x];
      }
      else
      {
         int s = o + sx;
         for (int x = 0; x < n1; x++)
         {
            uint16_t c = src[s + x];
            if (c != KEY)
               display[dst + x] = c;
         }
         int rest = SW - n1;
         int dd = dst + n1;
         for (int x = 0; x < rest; x++)
         {
            uint16_t c = src[o + x];
            if (c != KEY)
               display[dd + x] = c;
         }
      }
   }
}

// the three layers, each drawn only where the next one doesn't cover it
void drawBackground(int cx)
{
   int y1 = imin(layY[0] + layH[0], layY[1] + layFull[1]);
   drawLayer(0, cx >> 3, 0, y1);
   int y2 = imin(layY[1] + layH[1], layY[2] + layFull[2]);
   drawLayer(1, cx >> 2, 0, y2);
   drawLayer(2, cx >> 1, 0, SH);
}

void fillRect(int x, int y, int w, int h, uint16_t c)
{
   int x0 = imax(0, x);
   int y0 = imax(0, y);
   int x1 = imin(SW, x + w);
   int y1 = imin(SH, y + h);
   for (int yy = y0; yy < y1; yy++)
   {
      int o = orgOff + yy * stride;
      for (int xx = x0; xx < x1; xx++)
         display[o + xx] = c;
   }
}

// darkens a rectangle: every other pixel (a 50% screen)
void dimRect(int x, int y, int w, int h, uint16_t c)
{
   int x0 = imax(0, x);
   int y0 = imax(0, y);
   int x1 = imin(SW, x + w);
   int y1 = imin(SH, y + h);
   for (int yy = y0; yy < y1; yy++)
   {
      int o = orgOff + yy * stride;
      for (int xx = x0 + ((x0 + yy) & 1); xx < x1; xx = xx + 2)
         display[o + xx] = c;
   }
}

// a 16x16 tile at screen (x, y)
void drawTile(int t, int x, int y)
{
   int src = t * 256;
   int i0 = 0;
   int i1 = 16;
   if (x < 0)
      i0 = -x;
   if (x + 16 > SW)
      i1 = SW - x;
   if (i0 >= i1)
      return;
   int j0 = 0;
   int j1 = 16;
   if (y < 0)
      j0 = -y;
   if (y + 16 > SH)
      j1 = SH - y;
   if (tileOpaque[t] == 1)
   {
      for (int j = j0; j < j1; j++)
      {
         int o = orgOff + (y + j) * stride + x;
         int s = src + j * 16;
         for (int i = i0; i < i1; i++)
            display[o + i] = tilePix[s + i];
      }
   }
   else
   {
      for (int j = j0; j < j1; j++)
      {
         int o = orgOff + (y + j) * stride + x;
         int s = src + j * 16;
         for (int i = i0; i < i1; i++)
         {
            uint16_t c = tilePix[s + i];
            if (c != KEY)
               display[o + i] = c;
         }
      }
   }
}

// a sprite's frame at screen (x, y), mirrored when flip is 1; flash 1 draws it white
void drawSprite(int id, int frame, int x, int y, int flip, int flash)
{
   int w = sprW[id];
   int h = sprH[id];
   if (x >= SW || y >= SH || x + w <= 0 || y + h <= 0)
      return;
   int src = sprOff[id] + frame * w * h;
   int i0 = imax(0, -x);
   int i1 = imin(w, SW - x);
   int j0 = imax(0, -y);
   int j1 = imin(h, SH - y);
   uint16_t white = col(255, 255, 255);
   for (int j = j0; j < j1; j++)
   {
      int o = orgOff + (y + j) * stride + x;
      int s = src + j * w;
      if (flip == 0)
      {
         for (int i = i0; i < i1; i++)
         {
            uint16_t c = sprPix[s + i];
            if (c != KEY)
            {
               if (flash == 1)
                  c = white;
               display[o + i] = c;
            }
         }
      }
      else
      {
         int sr = s + w - 1;
         for (int i = i0; i < i1; i++)
         {
            uint16_t c = sprPix[sr - i];
            if (c != KEY)
            {
               if (flash == 1)
                  c = white;
               display[o + i] = c;
            }
         }
      }
   }
}

// ------------------------------------------------------------------ text

uint16_t inkCol;
uint16_t shadowCol;
int useShadow;
int textScale;

int glyphW(int c)
{
   if (c < 32 || c > 126)
      return 0;
   return wad[fontOff + c - 32] * textScale;
}

int textWidth(char *s)
{
   int w = 0;
   int i = 0;
   while (s[i] != 0)
   {
      int c = s[i];
      w = w + glyphW(c);
      i++;
   }
   return w;
}

void plotGlyph(int c, int x, int y, uint16_t ink)
{
   int g = fontOff + 95 + (c - 32) * 8;
   int sc = textScale;
   for (int r = 0; r < 8; r++)
   {
      int bits = wad[g + r];
      if (bits == 0)
         continue;
      for (int i = 0; i < 8; i++)
      {
         if (((bits << i) & 128) == 0)
            continue;
         int px = x + i * sc;
         int py = y + r * sc;
         for (int sy = 0; sy < sc; sy++)
         {
            int yy = py + sy;
            if (yy < 0 || yy >= SH)
               continue;
            int o = orgOff + yy * stride;
            for (int sx = 0; sx < sc; sx++)
            {
               int xx = px + sx;
               if (xx >= 0 && xx < SW)
                  display[o + xx] = ink;
            }
         }
      }
   }
}

int drawChar(int c, int x, int y)
{
   if (c < 32 || c > 126)
      return x;
   if (useShadow == 1)
      plotGlyph(c, x + textScale, y + textScale, shadowCol);
   plotGlyph(c, x, y, inkCol);
   return x + glyphW(c);
}

int drawText(char *s, int x, int y)
{
   int i = 0;
   while (s[i] != 0)
   {
      int c = s[i];
      x = drawChar(c, x, y);
      i++;
   }
   return x;
}

void centreText(char *s, int y)
{
   drawText(s, (SW - textWidth(s)) / 2, y);
}

void centreAt(char *s, int x, int y)
{
   drawText(s, x - textWidth(s) / 2, y);
}

void setInk(uint16_t ink, uint16_t shadow, int scale)
{
   inkCol = ink;
   shadowCol = shadow;
   useShadow = 1;
   textScale = scale;
}

char numBuf[16];
char numTmp[12];

// a number as text, with at least `digits` digits
char *numText(int v, int digits)
{
   int neg = 0;
   if (v < 0)
   {
      neg = 1;
      v = -v;
   }
   int n = 0;
   while (v > 0 || n < digits || n == 0)
   {
      numTmp[n] = 48 + v % 10;
      v = v / 10;
      n++;
      if (n >= 11)
         break;
   }
   int i = 0;
   if (neg == 1)
   {
      numBuf[0] = 45;
      i = 1;
   }
   for (int k = n - 1; k >= 0; k--)
   {
      numBuf[i] = numTmp[k];
      i++;
   }
   numBuf[i] = 0;
   return numBuf;
}

int drawNum(int v, int digits, int x, int y)
{
   return drawText(numText(v, digits), x, y);
}

// the TEXT lump: the story's pages, read from the WAD as they are drawn.
// A page ends at '|' or at the end of its string; lines wrap at `maxw`.
// Returns where the next page starts (-1: that was the last).
int drawStoryPage(int off, int x, int y, int maxw, int lineH)
{
   int base = textOff;
   int cx = x;
   int cy = y;
   int i = off;
   while (true)
   {
      int c = wad[base + i];
      if (c == 0)
         return -1;
      if (c == 124)
         return i + 1;
      if (c == 32)
      {
         // the next word: does it fit on this line?
         int w = glyphW(32);
         int k = i + 1;
         int c2 = wad[base + k];
         while (c2 != 0 && c2 != 32 && c2 != 124)
         {
            w = w + glyphW(c2);
            k++;
            c2 = wad[base + k];
         }
         if (cx + w > x + maxw)
         {
            cx = x;
            cy = cy + lineH;
         }
         else
            cx = drawChar(32, cx, cy);
         i++;
         continue;
      }
      cx = drawChar(c, cx, cy);
      i++;
   }
   return -1;
}

// ------------------------------------------------------------------ the game's state

int score;
int hiScore;
int lives;
int glints;
int health;
int maxHealth;
int power;          // 1 with a prism: stronger, faster shots
int frameNo;        // steps since the level began
int animTick;       // the tiles' and sprites' slow animation
int quit;           // 1: back to the title
int levelDone;      // 1: the beacon is lit
int bossOn;         // 1: the guardian is awake (the camera stops)
int bossIdx;        // its entity
int arenaX;         // the arena's left edge, in pixels
int paused;
int showName;       // steps the level's name stays on screen
int levelTime;      // seconds spent in the level
int startLevel = 0; // the first level of a game (0; tests start later)
int godMode = 0;    // 1: nothing hurts Wren (tests)

// Wren
int hx;             // her middle, in sixteenths of a pixel
int hy;             // her feet
int hvx;
int hvy;
int hFace;          // 1 right, -1 left
int hGround;        // 1 when standing
int hCoyote;        // steps she may still jump after leaving an edge
int hJumpBuf;       // steps a jump press waits to land
int hJumping;       // 1 while the jump button holds the jump up
int hHurt;          // steps she can't be hurt again (blinks)
int hShootCool;
int hAim;           // steps she keeps her arm up after a shot
int hCrouch;
int hDead;          // >0: steps of her fall off the screen
int hPlat;          // the moving platform she stands on, -1 none
int hDropThrough;   // steps she falls through one-way ledges
int hRunPhase;
int hBlink;
int checkX;         // where she comes back
int checkY;

// the keys this step
int kLeft;
int kRight;
int kDown;
int kJump;
int kFire;
int pressJump;
int pressFire;

// creatures and things
int eType[MAXE];
int eX[MAXE];       // middle, sixteenths
int eY[MAXE];       // feet, sixteenths
int eVX[MAXE];
int eVY[MAXE];
int eW[MAXE];       // half width, pixels
int eH[MAXE];       // height, pixels
int eHP[MAXE];
int eState[MAXE];
int eTimer[MAXE];
int eDir[MAXE];
int eFlash[MAXE];
int eA[MAXE];
int eB[MAXE];
int eC[MAXE];
int eAwake[MAXE];
int eCount;

// shots
int pAlive[MAXP];
int pKind[MAXP];
int pX[MAXP];
int pY[MAXP];
int pVX[MAXP];
int pVY[MAXP];
int pLife[MAXP];
int pHero[MAXP];

// effects
int fAlive[MAXF];
int fKind[MAXF];
int fX[MAXF];
int fY[MAXF];
int fVX[MAXF];
int fVY[MAXF];
int fLife[MAXF];
int fVal[MAXF];

// lumen boxes with something in them: tile position and what
int boxX[48];
int boxY[48];
int boxWhat[48];
int boxCount;

// the names of the levels (as the WAD's levels.py)
char *levelName(int n)
{
   if (n == 0)
      return "Dawnmere Fields";
   if (n == 1)
      return "The Bramble Gate";
   if (n == 2)
      return "Mirewood";
   if (n == 3)
      return "The Moth Hollow";
   if (n == 4)
      return "Glimmerdeep";
   if (n == 5)
      return "The Geode Throne";
   if (n == 6)
      return "The Hollow Spire";
   return "The Gloam Crown";
}

char *bossName(int l)
{
   if (l == 0)
      return "THE BRAMBLE BOAR";
   if (l == 1)
      return "THE MOTH MATRIARCH";
   if (l == 2)
      return "THE GEODE GOLEM";
   return "THE GLOAM KING";
}

// ------------------------------------------------------------------ tiles

int tileAt(int tx, int ty)
{
   if (ty < 0 || ty >= ROWS)
      return T_EMPTY;
   if (tx < 0 || tx >= mapW)
      return T_BLOCK;
   return map[ty * mapW + tx];
}

void setTile(int tx, int ty, int t)
{
   if (tx >= 0 && tx < mapW && ty >= 0 && ty < ROWS)
      map[ty * mapW + tx] = t;
}

// 1: nothing passes through it
int isSolid(int t)
{
   if (t >= T_TOP && t <= T_FILL2)
      return 1;
   if (t == T_BLOCK || t == T_BOX_A || t == T_BOX_B || t == T_BOX_USED || t == T_CRATE)
      return 1;
   if (t == T_COLUMN || t == T_COLUMN_TOP || t == T_UNDER)
      return 1;
   return 0;
}

// 1: a ledge you land on from above and jump up through
int isLedge(int t)
{
   return t >= T_PLAT_L && t <= T_PLAT_R;
}

int isLiquid(int t)
{
   return t >= T_LIQTOP_A && t <= T_LIQ_B;
}

int isGlint(int t)
{
   return t >= T_GLINT_A && t <= T_GLINT_D;
}

// solid at a pixel of the level
int solidPx(int x, int y)
{
   if (x < 0)
      return 1;
   return isSolid(tileAt(x >> 4, y >> 4));
}

// ------------------------------------------------------------------ the level's things

int spawn(int type, int x, int y)
{
   int i = -1;
   for (int k = 0; k < MAXE; k++)
      if (eType[k] == E_NONE)
      {
         i = k;
         break;
      }
   if (i < 0)
      return -1;
   if (i >= eCount)
      eCount = i + 1;
   eType[i] = type;
   eX[i] = x * SUB;
   eY[i] = y * SUB;
   eVX[i] = 0;
   eVY[i] = 0;
   eW[i] = 6;
   eH[i] = 14;
   eHP[i] = 1;
   eState[i] = 0;
   eTimer[i] = 0;
   eDir[i] = -1;
   eFlash[i] = 0;
   eA[i] = 0;
   eB[i] = 0;
   eC[i] = 0;
   eAwake[i] = 0;
   return i;
}

void removeThing(int i)
{
   eType[i] = E_NONE;
}

// a thing of the level file at tile (tx, ty): ty is the row it stands on
void placeThing(int kind, int tx, int ty, int v)
{
   int px = tx * 16 + 8;
   int py = ty * 16 + 16;
   int i = -1;
   if (kind == K_WALKER)
   {
      i = spawn(E_WALKER, px, py);
      if (i >= 0 && land == 3)
         eHP[i] = 2;
   }
   else if (kind == K_FLYER)
   {
      i = spawn(E_FLYER, px, py);
      if (i >= 0)
      {
         eA[i] = py;          // the height it hovers about
         eB[i] = rnd(256);    // its phase
         eH[i] = 12;
      }
   }
   else if (kind == K_SPECIAL)
   {
      if (land == 0 || land == 2)
      {
         i = spawn(E_HOPPER, px, py);
         if (i >= 0)
            eHP[i] = 2;
      }
      else
      {
         i = spawn(E_TURRET, px, py);
         if (i >= 0)
         {
            eHP[i] = 3;
            eTimer[i] = 40 + rnd(40);
         }
      }
   }
   else if (kind == K_BOSS)
   {
      i = spawn(E_BOSS, px, py);
      if (i >= 0)
      {
         bossIdx = i;
         eW[i] = 18;
         eH[i] = 40;
         eHP[i] = 20 + land * 6;
         eC[i] = eHP[i];
         eA[i] = px;
         arenaX = (tx - 18) * 16;
      }
   }
   else if (kind == K_CHECK)
   {
      i = spawn(E_CHECK, px, py);
      if (i >= 0)
         eH[i] = 24;
   }
   else if (kind == K_BEACON)
   {
      i = spawn(E_BEACON, px, py);
      if (i >= 0)
      {
         eW[i] = 12;
         eH[i] = 46;
         eA[i] = v;          // 1: hidden until the guardian falls
      }
   }
   else if (kind == K_PLATH || kind == K_PLATV)
   {
      i = spawn(kind == K_PLATH ? E_PLATH : E_PLATV, px + 16, py);
      if (i >= 0)
      {
         eW[i] = 24;
         eH[i] = 8;
         eA[i] = eX[i];
         eB[i] = eY[i];
         eC[i] = v * 16;      // how far it travels
         eDir[i] = 1;
         if (kind == K_PLATV)
            eY[i] = eY[i] - 64 * SUB / 16;
      }
   }
   else if (kind == K_HEART || kind == K_PRISM || kind == K_LIFE)
   {
      i = spawn(E_HEART + kind - K_HEART, px, py);
   }
   else if (kind == K_BOXITEM)
   {
      if (boxCount < 48)
      {
         boxX[boxCount] = tx;
         boxY[boxCount] = ty;
         boxWhat[boxCount] = v;
         boxCount++;
      }
   }
}

// the land's pictures: its layers, tiles, creatures, guardian, platform
int loadedLand = -1;

void loadLand(int l)
{
   if (l == loadedLand)
      return;
   for (int n = 0; n < 3; n++)
      loadLayer(lumpName2("BG", l, 95, n), n);
   loadTiles(lumpName2("TILES", l, 0, 0));
   sprFill = landSprFrom;
   loadSprite(lumpName2("CRIT", l, 0, 0), S_CRIT);
   loadSprite(lumpName2("BOSS", l, 0, 0), S_BOSS);
   loadSprite(lumpName2("PLAT", l, 0, 0), S_PLAT);
   loadedLand = l;
}

void clearAll()
{
   for (int i = 0; i < MAXE; i++)
      eType[i] = E_NONE;
   eCount = 0;
   for (int i = 0; i < MAXP; i++)
      pAlive[i] = 0;
   for (int i = 0; i < MAXF; i++)
      fAlive[i] = 0;
   boxCount = 0;
}

int thingBuf[600];

// the level n (0..7): its map and its things
void loadLevel(int n)
{
   levelNo = n;
   unpack(lumpName2("LEVEL", n + 1, 0, 0));
   mapW = rd16(0);
   land = ubuf[2];
   bossLevel = ubuf[3];
   int sx = ubuf[4];
   int sy = ubuf[5];
   int cnt = rd16(6);
   int p = 8;
   for (int i = 0; i < ROWS * mapW; i++)
      map[i] = ubuf[p + i];
   p = p + ROWS * mapW;
   // the things after the map: copied out first, as placing them may unpack
   clearAll();
   bossIdx = -1;
   arenaX = 0;
   int nt = imin(cnt, 120);
   for (int i = 0; i < nt; i++)
   {
      thingBuf[i * 5] = ubuf[p];
      thingBuf[i * 5 + 1] = ubuf[p + 1] | (ubuf[p + 2] << 8);
      thingBuf[i * 5 + 2] = ubuf[p + 3];
      thingBuf[i * 5 + 3] = ubuf[p + 4];
      p = p + 5;
   }
   for (int i = 0; i < nt; i++)
      placeThing(thingBuf[i * 5], thingBuf[i * 5 + 1], thingBuf[i * 5 + 2], thingBuf[i * 5 + 3]);
   checkX = sx * 16 + 8;
   checkY = (sy + 1) * 16;
   camMin = 0;
   camMax = mapW * 16 - SW;
}

// ------------------------------------------------------------------ effects

void effect(int kind, int x, int y, int vx, int vy, int life, int val)
{
   for (int i = 0; i < MAXF; i++)
      if (fAlive[i] == 0)
      {
         fAlive[i] = 1;
         fKind[i] = kind;
         fX[i] = x;
         fY[i] = y;
         fVX[i] = vx;
         fVY[i] = vy;
         fLife[i] = life;
         fVal[i] = val;
         return;
      }
}

// a burst of light at pixel (x, y)
void puffAt(int x, int y)
{
   effect(F_PUFF, x * SUB, y * SUB, 0, 0, 16, 0);
   for (int k = 0; k < 4; k++)
      effect(F_SPARK, x * SUB, y * SUB, isin(k * 64 + 32) * 3, icos(k * 64 + 32) * 3, 9, 0);
}

void sparkAt(int x, int y)
{
   effect(F_SPARK, x * SUB, y * SUB, 0, -8, 9, 0);
}

void addScore(int v, int x, int y)
{
   score = score + v;
   if (score > hiScore)
      hiScore = score;
   if (x >= 0)
      effect(F_SCORE, x * SUB, y * SUB, 0, -10, 30, v);
}

void dustAt(int x, int y)
{
   effect(F_DUST, (x - 4) * SUB, y * SUB, -6, -4, 10, 0);
   effect(F_DUST, (x + 4) * SUB, y * SUB, 6, -4, 10, 0);
}

void updateEffects()
{
   for (int i = 0; i < MAXF; i++)
   {
      if (fAlive[i] == 0)
         continue;
      fX[i] = fX[i] + fVX[i];
      fY[i] = fY[i] + fVY[i];
      if (fKind[i] == F_SPARK)
         fVY[i] = fVY[i] + 1;
      fLife[i] = fLife[i] - 1;
      if (fLife[i] <= 0)
         fAlive[i] = 0;
   }
}

uint16_t dustCol;

void drawEffects()
{
   for (int i = 0; i < MAXF; i++)
   {
      if (fAlive[i] == 0)
         continue;
      int x = (fX[i] >> 4) - camX;
      int y = fY[i] >> 4;
      int k = fKind[i];
      if (k == F_PUFF)
         drawSprite(S_PUFF, imin(3, (16 - fLife[i]) >> 2), x - 8, y - 8, 0, 0);
      else if (k == F_SPARK)
         drawSprite(S_SPARK, imin(2, (9 - fLife[i]) / 3), x - 4, y - 4, 0, 0);
      else if (k == F_DUST)
         fillRect(x - 1, y - 1, 2 + (fLife[i] >> 2), 2, dustCol);
      else if (k == F_SCORE)
      {
         setInk(col(255, 240, 160), col(40, 20, 30), 1);
         centreAt(numText(fVal[i], 1), x, y - 4);
      }
   }
}

// ------------------------------------------------------------------ shots

int shoot(int kind, int x, int y, int vx, int vy, int hero)
{
   for (int i = 0; i < MAXP; i++)
      if (pAlive[i] == 0)
      {
         pAlive[i] = 1;
         pKind[i] = kind;
         pX[i] = x;
         pY[i] = y;
         pVX[i] = vx;
         pVY[i] = vy;
         pLife[i] = 90;
         pHero[i] = hero;
         return i;
      }
   return -1;
}

int heroShots()
{
   int n = 0;
   for (int i = 0; i < MAXP; i++)
      if (pAlive[i] == 1 && pHero[i] == 1)
         n++;
   return n;
}

// a shot aimed from (x, y) at Wren, at `speed` sixteenths a step
void aimAtHero(int kind, int x, int y, int speed)
{
   int dx = hx - x;
   int dy = (hy - 10 * SUB) - y;
   int ax = iabs(dx);
   int ay = iabs(dy);
   int len = imax(ax, ay) + imin(ax, ay) * 3 / 8 + 1;
   shoot(kind, x, y, dx * speed / len, dy * speed / len, 0);
}

// ------------------------------------------------------------------ Wren

int heroPx()
{
   return hx >> 4;
}

int heroPy()
{
   return hy >> 4;
}

// 1 when her box overlaps the box (cx +- w, y from top to bottom), in pixels
int touchHero(int cx, int w, int top, int bottom)
{
   int x = hx >> 4;
   int y = hy >> 4;
   int hTop = y - HH + (hCrouch == 1 ? 8 : 0);
   if (x + HW <= cx - w || x - HW >= cx + w)
      return 0;
   if (y <= top || hTop >= bottom)
      return 0;
   return 1;
}

void heroDie()
{
   if (hDead > 0 || godMode == 1)
      return;
   hDead = 1;
   hvx = 0;
   hvy = -96;
   power = 0;
   health = 0;
   puffAt(hx >> 4, (hy >> 4) - 10);
}

void hurtHero(int srcX)
{
   if (hHurt > 0 || hDead > 0 || levelDone > 0 || godMode == 1)
      return;
   if (power == 1)
      power = 0;
   else
      health = health - 1;
   hHurt = 70;
   hvx = (hx >= srcX ? 1 : -1) * 30;
   hvy = -56;
   hGround = 0;
   hPlat = -1;
   if (health <= 0)
      heroDie();
}

// a lumen box hit from below at tile (tx, ty)
void hitBox(int tx, int ty)
{
   int t = tileAt(tx, ty);
   if (t != T_BOX_A && t != T_BOX_B)
   {
      if (t == T_CRATE && power == 1)
      {
         setTile(tx, ty, T_EMPTY);
         puffAt(tx * 16 + 8, ty * 16 + 8);
         addScore(50, tx * 16 + 8, ty * 16);
      }
      return;
   }
   setTile(tx, ty, T_BOX_USED);
   int what = 0;
   for (int i = 0; i < boxCount; i++)
      if (boxX[i] == tx && boxY[i] == ty)
         what = boxWhat[i];
   int x = tx * 16 + 8;
   int y = ty * 16;
   if (what == K_HEART || what == K_PRISM || what == K_LIFE)
   {
      int e = spawn(E_HEART + what - K_HEART, x, y);
      if (e >= 0)
      {
         eVY[e] = -70;
         eVX[e] = 12 * hFace;
         eState[e] = 1;
      }
   }
   else
   {
      int e = spawn(E_GLINT, x, y);
      if (e >= 0)
         eVY[e] = -80;
   }
   sparkAt(x, y);
   // a creature standing on the box is thrown off
   for (int i = 0; i < eCount; i++)
      if (eType[i] == E_WALKER && eState[i] == 0 && iabs((eX[i] >> 4) - x) < 12 && iabs((eY[i] >> 4) - y) < 4)
      {
         eState[i] = 2;
         eVY[i] = -60;
         addScore(200, x, y - 16);
      }
}

void gainGlint(int x, int y)
{
   glints++;
   addScore(10, -1, 0);
   sparkAt(x, y);
   if (glints >= 100)
   {
      glints = glints - 100;
      lives++;
      effect(F_SCORE, x * SUB, (y - 10) * SUB, 0, -8, 40, 1000);
   }
}

void moveHeroX()
{
   int nx = hx + hvx;
   int px = nx >> 4;
   int top = (hy >> 4) - HH + (hCrouch == 1 ? 8 : 0);
   int bot = (hy >> 4) - 1;
   int mid = (top + bot) >> 1;
   if (hvx > 0)
   {
      int edge = px + HW - 1;
      if (solidPx(edge, top) == 1 || solidPx(edge, mid) == 1 || solidPx(edge, bot) == 1)
      {
         px = (edge >> 4) * 16 - HW;
         nx = px * SUB + SUB - 1;
         hvx = 0;
      }
   }
   else if (hvx < 0)
   {
      int edge = px - HW;
      if (solidPx(edge, top) == 1 || solidPx(edge, mid) == 1 || solidPx(edge, bot) == 1)
      {
         px = ((edge >> 4) + 1) * 16 + HW;
         nx = px * SUB;
         hvx = 0;
      }
   }
   // the screen's edges: the start of the level, the guardian's arena
   int minX = imax(HW + 2, (bossOn == 1 ? camX : 0) + HW + 2);
   int maxX = mapW * 16 - HW - 2;
   if ((nx >> 4) < minX)
   {
      nx = minX * SUB;
      hvx = 0;
   }
   if ((nx >> 4) > maxX)
   {
      nx = maxX * SUB;
      hvx = 0;
   }
   hx = nx;
}

// a moving platform under (x, feetY), -1 none; the feet must have come from above
int platformUnder(int x, int oldFeet, int newFeet)
{
   for (int i = 0; i < eCount; i++)
   {
      int t = eType[i];
      if (t != E_PLATH && t != E_PLATV)
         continue;
      int top = eY[i] >> 4;
      int cx = eX[i] >> 4;
      if (x + HW - 1 < cx - eW[i] || x - HW + 1 > cx + eW[i])
         continue;
      if (oldFeet <= top + 1 && newFeet >= top)
         return i;
   }
   return -1;
}

void moveHeroY()
{
   int ny = hy + hvy;
   int x = hx >> 4;
   int l = x - HW + 1;
   int r = x + HW - 2;
   if (hvy > 0)
   {
      int oldFeet = hy >> 4;
      int feet = ny >> 4;
      int ty = feet >> 4;
      int landed = 0;
      for (int k = 0; k < 2; k++)
      {
         int px = k == 0 ? l : r;
         int t = tileAt(px >> 4, ty);
         if (isSolid(t) == 1)
            landed = 1;
         if (isLedge(t) == 1 && hDropThrough == 0 && (oldFeet >> 4) < ty)
            landed = 1;
      }
      if (landed == 1 && (feet & 15) < 12)
      {
         ny = ty * 16 * SUB;
         if (hGround == 0 && hvy > 60)
            dustAt(x, ty * 16);
         hvy = 0;
         hGround = 1;
         hPlat = -1;
      }
      else
      {
         int p = platformUnder(x, oldFeet, feet);
         if (p >= 0 && hDropThrough == 0)
         {
            ny = eY[p];
            hvy = 0;
            hGround = 1;
            hPlat = p;
         }
      }
   }
   else if (hvy < 0)
   {
      int head = (ny >> 4) - HH + (hCrouch == 1 ? 8 : 0);
      int ty = head >> 4;
      int hit = 0;
      int hitX = -1;
      for (int k = 0; k < 2; k++)
      {
         int px = k == 0 ? l : r;
         int t = tileAt(px >> 4, ty);
         if (head >= 0 && isSolid(t) == 1)
         {
            hit = 1;
            // the box nearest her middle is the one she hits
            if (hitX < 0 || iabs((px >> 4) * 16 + 8 - x) < iabs(hitX * 16 + 8 - x))
               hitX = px >> 4;
         }
      }
      if (hit == 1)
      {
         ny = ((ty + 1) * 16 + HH - (hCrouch == 1 ? 8 : 0)) * SUB;
         hvy = 0;
         hJumping = 0;
         hitBox(hitX, ty);
      }
   }
   hy = ny;
}

// 1 when something holds her feet up at her position
int supported()
{
   int x = hx >> 4;
   int feet = hy >> 4;
   if ((feet & 15) != 0)
      return 0;
   int ty = feet >> 4;
   int a = tileAt((x - HW + 1) >> 4, ty);
   int b = tileAt((x + HW - 2) >> 4, ty);
   if (isSolid(a) == 1 || isSolid(b) == 1)
      return 1;
   if (hDropThrough == 0 && (isLedge(a) == 1 || isLedge(b) == 1))
      return 1;
   return 0;
}

// what her box touches: glints, spikes, liquid, the bottom of the screen
void heroTiles()
{
   int x = hx >> 4;
   int y = hy >> 4;
   int top = y - HH + (hCrouch == 1 ? 8 : 0);
   for (int ty = top >> 4; ty <= (y - 1) >> 4; ty++)
      for (int tx = (x - HW) >> 4; tx <= (x + HW - 1) >> 4; tx++)
      {
         int t = tileAt(tx, ty);
         if (isGlint(t) == 1)
         {
            setTile(tx, ty, T_EMPTY);
            gainGlint(tx * 16 + 8, ty * 16 + 8);
         }
         else if (t == T_SPIKES && y > ty * 16 + 8)
            hurtHero(tx * 16 + 8);
         else if (isLiquid(t) == 1 && y > ty * 16 + 6)
            heroDie();
      }
   if (y > SH + 24)
   {
      if (godMode == 1)
      {
         hy = 0;
         hvy = 0;
      }
      heroDie();
   }
}

void updateHero()
{
   if (hDead > 0)
   {
      hDead++;
      hvy = imin(hvy + GRAV, MAXFALL);
      hy = hy + hvy;
      return;
   }
   if (hHurt > 0)
      hHurt--;
   if (hShootCool > 0)
      hShootCool--;
   if (hAim > 0)
      hAim--;
   if (hDropThrough > 0)
      hDropThrough--;
   if (levelDone > 0)
   {
      // the beacon is lit: she stands and watches
      kLeft = 0;
      kRight = 0;
      pressJump = 0;
      pressFire = 0;
      kFire = 0;
      kDown = 0;
   }

   // running (not while knocked back)
   int want = 0;
   if (kLeft == 1)
      want = -1;
   if (kRight == 1)
      want = 1;
   hCrouch = (kDown == 1 && hGround == 1 && want == 0) ? 1 : 0;
   if (hHurt > 52)
      want = 0;
   if (want != 0 && hCrouch == 0)
   {
      hFace = want;
      int acc = hGround == 1 ? RUNACC : AIRACC;
      hvx = hvx + want * acc;
      if (hvx > RUNMAX)
         hvx = RUNMAX;
      if (hvx < -RUNMAX)
         hvx = -RUNMAX;
   }
   else if (hGround == 1)
   {
      if (hvx > 0)
         hvx = imax(0, hvx - FRICTION);
      else if (hvx < 0)
         hvx = imin(0, hvx + FRICTION);
   }

   // jumping: a press waits a few steps; she may jump just after leaving an edge
   if (pressJump == 1)
      hJumpBuf = 6;
   else if (hJumpBuf > 0)
      hJumpBuf--;
   if (hGround == 1)
      hCoyote = 5;
   else if (hCoyote > 0)
      hCoyote--;
   if (hJumpBuf > 0 && hCoyote > 0)
   {
      if (kDown == 1 && hGround == 1 && hPlat < 0)
      {
         // down and jump: through the ledge she stands on
         int ty = (hy >> 4) >> 4;
         if (isLedge(tileAt((hx >> 4) >> 4, ty)) == 1)
         {
            hDropThrough = 10;
            hGround = 0;
            hy = hy + SUB;
         }
      }
      else
      {
         hvy = -JUMPV - iabs(hvx) / 6;
         hGround = 0;
         hCoyote = 0;
         hJumping = 1;
         hPlat = -1;
         dustAt(hx >> 4, hy >> 4);
      }
      hJumpBuf = 0;
   }
   if (hJumping == 1 && kJump == 0 && hvy < -JUMPCUT)
   {
      hvy = -JUMPCUT;
      hJumping = 0;
   }
   if (hvy >= 0)
      hJumping = 0;

   // shooting
   if ((pressFire == 1 || (kFire == 1 && hShootCool == 0)) && hShootCool == 0)
   {
      int limit = power == 1 ? 5 : 3;
      if (heroShots() < limit)
      {
         int sy = (hy >> 4) - (hCrouch == 1 ? 7 : 13);
         int sx = (hx >> 4) + hFace * 9;
         shoot(power == 1 ? P_BIG : P_BOLT, sx * SUB, sy * SUB, hFace * (power == 1 ? 120 : 100), 0, 1);
         hShootCool = power == 1 ? 6 : 9;
         hAim = 12;
      }
   }

   // gravity
   if (hGround == 0)
   {
      hvy = hvy + GRAV;
      if (hvy > MAXFALL)
         hvy = MAXFALL;
   }

   // riding a platform
   if (hPlat >= 0)
   {
      if (eType[hPlat] == E_PLATH || eType[hPlat] == E_PLATV)
      {
         int cx = eX[hPlat] >> 4;
         if ((hx >> 4) + HW < cx - eW[hPlat] || (hx >> 4) - HW > cx + eW[hPlat])
         {
            hPlat = -1;
            hGround = 0;
         }
         else
         {
            hx = hx + eVX[hPlat];
            hy = eY[hPlat];
         }
      }
      else
         hPlat = -1;
   }

   moveHeroX();
   moveHeroY();
   if (hGround == 1 && hPlat < 0 && supported() == 0)
   {
      hGround = 0;
      hCoyote = 5;
   }
   heroTiles();

   if (hGround == 1 && hvx != 0)
      hRunPhase = hRunPhase + iabs(hvx);
   hBlink++;
}

int heroFrame()
{
   if (hDead > 0 || hHurt > 56)
      return W_HURT;
   int aim = hAim > 0 ? 1 : 0;
   if (hGround == 0)
   {
      if (hvy < 0)
         return aim == 1 ? W_AJUMP : W_JUMP;
      return aim == 1 ? W_AFALL : W_FALL;
   }
   if (hCrouch == 1)
      return W_CROUCH;
   if (iabs(hvx) > 6)
   {
      int f = (hRunPhase / 200) % 6;
      return (aim == 1 ? W_ARUN0 : W_RUN0) + f;
   }
   if (aim == 1)
      return W_AIM;
   if ((hBlink % 120) < 6)
      return W_BLINK;
   return W_STAND;
}

void drawHero()
{
   if (hHurt > 0 && hDead == 0 && (hHurt & 4) != 0)
      return;
   int x = (hx >> 4) - camX - 8;
   int y = (hy >> 4) - 24;
   drawSprite(S_WREN, heroFrame(), x, y, hFace < 0 ? 1 : 0, 0);
}

// ------------------------------------------------------------------ creatures

// 1 when creature i's box overlaps a shot at pixel (x, y)
int hitsCreature(int i, int x, int y)
{
   int cx = eX[i] >> 4;
   int fy = eY[i] >> 4;
   return x >= cx - eW[i] - 2 && x <= cx + eW[i] + 2 && y >= fy - eH[i] && y <= fy;
}

int isFoe(int t)
{
   return t == E_WALKER || t == E_FLYER || t == E_HOPPER || t == E_TURRET || t == E_BOSS;
}

void defeat(int i, int stomped)
{
   int x = eX[i] >> 4;
   int y = (eY[i] >> 4) - eH[i] / 2;
   int pts = 100;
   if (eType[i] == E_HOPPER || eType[i] == E_TURRET)
      pts = 200;
   if (eType[i] == E_WALKER && stomped == 1)
   {
      // flattened: it stays a moment
      eState[i] = 1;
      eTimer[i] = 24;
      addScore(pts, x, y - 8);
      return;
   }
   puffAt(x, y);
   addScore(pts, x, y - 8);
   // now and then a creature leaves a glint behind
   if (rnd(4) == 0)
   {
      int g = spawn(E_GLINT, x, y);
      if (g >= 0)
         eVY[g] = -60;
   }
   removeThing(i);
}

// a creature that walks: gravity, walls and edges turn it around
void walkCreature(int i, int speed, int edges)
{
   eVX[i] = eDir[i] * speed;
   eVY[i] = imin(eVY[i] + GRAV, MAXFALL);
   int x = eX[i] >> 4;
   int nx = (eX[i] + eVX[i]) >> 4;
   int y = eY[i] >> 4;
   int front = nx + eDir[i] * eW[i];
   if (solidPx(front, y - 4) == 1 || solidPx(front, y - eH[i] + 2) == 1)
   {
      eDir[i] = -eDir[i];
      nx = x;
   }
   else if (edges == 1 && eVY[i] <= GRAV)
   {
      int below = tileAt(front >> 4, y >> 4);
      if (isSolid(below) == 0 && isLedge(below) == 0)
      {
         eDir[i] = -eDir[i];
         nx = x;
      }
   }
   eX[i] = nx * SUB + (eX[i] & 15);
   int ny = eY[i] + eVY[i];
   int feet = ny >> 4;
   int t1 = tileAt((nx - eW[i] + 1) >> 4, feet >> 4);
   int t2 = tileAt((nx + eW[i] - 1) >> 4, feet >> 4);
   int oldRow = (eY[i] >> 4) >> 4;
   if (eVY[i] > 0 && (isSolid(t1) == 1 || isSolid(t2) == 1 ||
                      ((isLedge(t1) == 1 || isLedge(t2) == 1) && oldRow < (feet >> 4) + 1 && (feet & 15) < 10)))
   {
      ny = (feet >> 4) * 16 * SUB;
      eVY[i] = 0;
   }
   eY[i] = ny;
   if ((eY[i] >> 4) > SH + 32)
      removeThing(i);
}

void updateWalker(int i)
{
   if (eState[i] == 1)
   {
      eTimer[i] = eTimer[i] - 1;
      if (eTimer[i] <= 0)
      {
         puffAt(eX[i] >> 4, (eY[i] >> 4) - 4);
         removeThing(i);
      }
      return;
   }
   if (eState[i] == 2)
   {
      // thrown off a box: falls off the screen
      eVY[i] = eVY[i] + GRAV;
      eY[i] = eY[i] + eVY[i];
      if ((eY[i] >> 4) > SH + 32)
         removeThing(i);
      return;
   }
   int speed = 7 + land * 2;
   walkCreature(i, speed, land == 0 ? 0 : 1);
}

void updateFlyer(int i)
{
   eB[i] = eB[i] + 3;
   int x = eX[i] >> 4;
   int hxp = hx >> 4;
   int dx = hxp - x;
   if (land == 2 && eState[i] == 0 && iabs(dx) < 70 && (hy >> 4) > (eA[i] >> 4) - 40)
   {
      // the cave bat swoops at her
      eState[i] = 1;
      eTimer[i] = 40;
      eVX[i] = sgn(dx) * 30;
      eVY[i] = 40;
   }
   if (eState[i] == 1)
   {
      eX[i] = eX[i] + eVX[i];
      eY[i] = eY[i] + eVY[i];
      eVY[i] = eVY[i] - 2;
      eTimer[i] = eTimer[i] - 1;
      if (eTimer[i] <= 0)
         eState[i] = 0;
      return;
   }
   eDir[i] = sgn(dx);
   eX[i] = eX[i] + sgn(dx) * (land == 0 ? 6 : 10);
   int target = eA[i] - 24 * SUB + isin(eB[i]) * 5;
   eY[i] = eY[i] + (target - eY[i]) / 8;
   if (land == 3)
   {
      // the wisp spits cold fire now and then
      eTimer[i] = eTimer[i] + 1;
      if (eTimer[i] > 90 && iabs(dx) < 180)
      {
         eTimer[i] = 0;
         aimAtHero(P_FIRE, eX[i], eY[i] - 8 * SUB, 48);
      }
   }
}

void updateHopper(int i)
{
   int onGround = eVY[i] == 0 ? 1 : 0;
   if (onGround == 1)
   {
      eTimer[i] = eTimer[i] + 1;
      eDir[i] = sgn((hx >> 4) - (eX[i] >> 4));
      if (eDir[i] == 0)
         eDir[i] = 1;
      if (eTimer[i] > 50 + (i & 15) && iabs((hx >> 4) - (eX[i] >> 4)) < 200)
      {
         eTimer[i] = 0;
         eVY[i] = -96;
         eA[i] = eDir[i] * 18;
         eY[i] = eY[i] - SUB;
      }
      eVX[i] = 0;
   }
   int speed = onGround == 1 ? 0 : iabs(eA[i]);
   if (onGround == 0)
      eDir[i] = sgn(eA[i]);
   walkCreature(i, speed, 0);
}

void updateTurret(int i)
{
   eDir[i] = (hx >> 4) < (eX[i] >> 4) ? -1 : 1;
   eTimer[i] = eTimer[i] - 1;
   if (eTimer[i] <= 0)
   {
      eTimer[i] = 70 + rnd(40);
      int dx = (hx >> 4) - (eX[i] >> 4);
      if (iabs(dx) < 220)
      {
         eState[i] = 10;
         if (land == 1)
         {
            // spores lobbed in an arc
            int vx = clampi(dx * 16 / 40, -48, 48);
            shoot(P_SPORE, eX[i], eY[i] - 12 * SUB, vx, -70, 0);
         }
         else
            aimAtHero(P_FIRE, eX[i] + eDir[i] * 6 * SUB, eY[i] - 8 * SUB, 52);
      }
   }
   if (eState[i] > 0)
      eState[i] = eState[i] - 1;
   eVY[i] = imin(eVY[i] + GRAV, MAXFALL);
   walkCreature(i, 0, 0);
}

// a pickup: pops out, falls, sits; touched, it is Wren's
void updateItem(int i)
{
   if (eState[i] == 1)
   {
      eVY[i] = imin(eVY[i] + GRAV, MAXFALL);
      eDir[i] = sgn(eVX[i]);
      if (eDir[i] == 0)
         eDir[i] = 1;
      walkCreature(i, iabs(eVX[i]), 0);
      if (eVY[i] == 0)
      {
         eVX[i] = 0;
         eState[i] = 2;
      }
   }
   eTimer[i] = eTimer[i] + 1;
   int x = eX[i] >> 4;
   int y = eY[i] >> 4;
   if (eTimer[i] > 10 && touchHero(x, 7, y - 12, y) == 1)
   {
      int t = eType[i];
      if (t == E_HEART)
      {
         if (health < maxHealth)
            health++;
         else
            addScore(500, x, y - 16);
      }
      else if (t == E_PRISM)
         power = 1;
      else if (t == E_LIFE)
      {
         lives++;
         effect(F_SCORE, x * SUB, (y - 16) * SUB, 0, -8, 40, 1000);
      }
      addScore(100, -1, 0);
      puffAt(x, y - 6);
      removeThing(i);
   }
}

// a glint thrown from a box: it rises, turns, and is Wren's
void updateGlintThing(int i)
{
   eY[i] = eY[i] + eVY[i];
   eVY[i] = eVY[i] + 8;
   eTimer[i] = eTimer[i] + 1;
   if (eTimer[i] > 16)
   {
      gainGlint(eX[i] >> 4, (eY[i] >> 4) - 8);
      removeThing(i);
   }
}

void updatePlatform(int i)
{
   int ox = eX[i];
   int oy = eY[i];
   if (eType[i] == E_PLATH)
   {
      eX[i] = eX[i] + eDir[i] * 16;
      if (eX[i] > eA[i] + eC[i] * SUB)
         eDir[i] = -1;
      if (eX[i] < eA[i])
         eDir[i] = 1;
   }
   else
   {
      eY[i] = eY[i] + eDir[i] * 12;
      if (eY[i] > eB[i])
         eDir[i] = -1;
      if (eY[i] < eB[i] - eC[i] * SUB)
         eDir[i] = 1;
   }
   eVX[i] = eX[i] - ox;
   eVY[i] = eY[i] - oy;
}

void updateCheck(int i)
{
   if (eState[i] == 1)
      return;
   int x = eX[i] >> 4;
   int y = eY[i] >> 4;
   if (touchHero(x, 6, y - 24, y) == 1)
   {
      eState[i] = 1;
      checkX = x;
      checkY = y;
      for (int k = 0; k < 6; k++)
         effect(F_SPARK, x * SUB, (y - 18) * SUB, isin(k * 43) * 2, icos(k * 43) * 2 - 10, 9, 0);
      addScore(100, x, y - 30);
   }
}

void updateBeacon(int i)
{
   if (eA[i] == 1 || eState[i] == 1)
      return;
   int x = eX[i] >> 4;
   int y = eY[i] >> 4;
   if (touchHero(x, 14, y - 46, y) == 1)
   {
      eState[i] = 1;
      levelDone = 1;
      for (int k = 0; k < 12; k++)
         effect(F_SPARK, x * SUB, (y - 44) * SUB, isin(k * 21) * 3, icos(k * 21) * 3 - 16, 9, 0);
   }
}

// ------------------------------------------------------------------ the guardians

// states: 0 asleep, then each guardian its own; 9 beaten (falling apart)
void bossShake(int n)
{
   shake = imax(shake, n);
}

void updateBoar(int i)
{
   int x = eX[i] >> 4;
   int hxp = hx >> 4;
   int st = eState[i];
   eTimer[i] = eTimer[i] + 1;
   int angry = eHP[i] < eC[i] / 2 ? 1 : 0;
   if (st == 1)
   {
      // pacing, facing her
      eDir[i] = hxp < x ? -1 : 1;
      walkCreature(i, 10, 0);
      if (eTimer[i] > (angry == 1 ? 50 : 80))
      {
         eState[i] = 2;
         eTimer[i] = 0;
      }
   }
   else if (st == 2)
   {
      // pawing the ground before the charge
      if ((eTimer[i] & 3) == 0)
         dustAt(x - eDir[i] * 10, eY[i] >> 4);
      walkCreature(i, 0, 0);
      if (eTimer[i] > 24)
      {
         eState[i] = 3;
         eTimer[i] = 0;
      }
   }
   else if (st == 3)
   {
      // the charge: until it hits the wall
      int before = eX[i];
      walkCreature(i, angry == 1 ? 70 : 56, 0);
      if (eX[i] == before || x < arenaX + 24 || x > arenaX + SW - 24)
      {
         eState[i] = 4;
         eTimer[i] = 0;
         bossShake(10);
         eVY[i] = -50;
         for (int k = 0; k < 3; k++)
            shoot(P_SHARD, eX[i], eY[i] - 30 * SUB, -eDir[i] * (16 + k * 12), -80 - k * 10, 0);
      }
   }
   else if (st == 4)
   {
      // dazed: the time to strike
      walkCreature(i, 0, 0);
      if (eTimer[i] > 60)
      {
         eDir[i] = -eDir[i];
         eState[i] = angry == 1 && rnd(2) == 0 ? 5 : 1;
         eTimer[i] = 0;
      }
   }
   else if (st == 5)
   {
      // a great leap at her
      if (eTimer[i] == 1)
      {
         eVY[i] = -110;
         eA[i] = sgn(hxp - x) * 34;
      }
      eDir[i] = sgn(eA[i]);
      walkCreature(i, iabs(eA[i]), 0);
      if (eTimer[i] > 6 && eVY[i] == 0)
      {
         bossShake(8);
         eState[i] = 1;
         eTimer[i] = 0;
      }
   }
}

void updateMoth(int i)
{
   int st = eState[i];
   eTimer[i] = eTimer[i] + 1;
   int angry = eHP[i] < eC[i] / 2 ? 1 : 0;
   int cx = arenaX + SW / 2;
   if (st == 1)
   {
      // a wide figure of eight over the arena, spores falling
      int a = eTimer[i] * 2;
      int tx = (cx + isin(a) * 150 / 64) * SUB;
      int ty = (100 + isin(a * 2) * 20 / 64) * SUB;
      eX[i] = eX[i] + (tx - eX[i]) / 10;
      eY[i] = eY[i] + (ty - eY[i]) / 10;
      eDir[i] = (hx >> 4) < (eX[i] >> 4) ? -1 : 1;
      if (eTimer[i] % (angry == 1 ? 24 : 36) == 0)
         shoot(P_SPORE, eX[i], eY[i], (rnd(3) - 1) * 14, 10, 0);
      if (eTimer[i] > 160)
      {
         eState[i] = 2;
         eTimer[i] = 0;
         eA[i] = hx;
      }
   }
   else if (st == 2)
   {
      // a dive at where she stood
      int ty = (SH - 40) * SUB;
      eX[i] = eX[i] + (eA[i] - eX[i]) / 8;
      eY[i] = eY[i] + (ty + 24 * SUB - eY[i]) / 7;
      if (eTimer[i] > 30)
      {
         eState[i] = 3;
         eTimer[i] = 0;
         for (int k = 0; k < (angry == 1 ? 8 : 5); k++)
            shoot(P_SPORE, eX[i], eY[i] - 20 * SUB, isin(k * 256 / 8) * 1, -40 - icos(k * 32) / 2, 0);
      }
   }
   else if (st == 3)
   {
      // rising again
      eY[i] = eY[i] - 28;
      if (eTimer[i] > 40)
      {
         eState[i] = 1;
         eTimer[i] = 0;
      }
   }
}

void updateGolem(int i)
{
   int x = eX[i] >> 4;
   int hxp = hx >> 4;
   int st = eState[i];
   eTimer[i] = eTimer[i] + 1;
   int angry = eHP[i] < eC[i] / 2 ? 1 : 0;
   if (st == 1)
   {
      eDir[i] = hxp < x ? -1 : 1;
      walkCreature(i, 6, 0);
      if (eTimer[i] > (angry == 1 ? 70 : 100))
      {
         eState[i] = rnd(2) == 0 ? 2 : 3;
         eTimer[i] = 0;
      }
   }
   else if (st == 2)
   {
      // arms up, then the stamp: crystals fall from the roof
      walkCreature(i, 0, 0);
      if (eTimer[i] == 30)
      {
         bossShake(14);
         int n = angry == 1 ? 7 : 5;
         for (int k = 0; k < n; k++)
            shoot(P_SHARD, (arenaX + 20 + rnd(SW - 40)) * SUB, (-8 - rnd(60)) * SUB, 0, 30, 0);
      }
      if (eTimer[i] > 50)
      {
         eState[i] = 1;
         eTimer[i] = 0;
      }
   }
   else if (st == 3)
   {
      // shards thrown in a fan
      walkCreature(i, 0, 0);
      if (eTimer[i] == 20 || (angry == 1 && eTimer[i] == 40))
         for (int k = -2; k <= 2; k++)
            shoot(P_SHARD, eX[i], eY[i] - 36 * SUB, eDir[i] * (30 + k * 6), -60 + k * 14, 0);
      if (eTimer[i] > 60)
      {
         eState[i] = 1;
         eTimer[i] = 0;
      }
   }
}

int kingSpot(int k)
{
   if (k == 0)
      return arenaX + 70;
   if (k == 1)
      return arenaX + SW / 2;
   return arenaX + SW - 70;
}

void updateKing(int i)
{
   int st = eState[i];
   eTimer[i] = eTimer[i] + 1;
   int angry = eHP[i] < eC[i] / 2 ? 1 : 0;
   eDir[i] = (hx >> 4) < (eX[i] >> 4) ? -1 : 1;
   int floatY = (SH - 34 - 30 + isin(eTimer[i] * 4) * 6 / 64) * SUB;
   eY[i] = eY[i] + (floatY - eY[i]) / 6;
   if (st == 1)
   {
      // orbs at her: three, then five when angry
      if (eTimer[i] % 40 == 20)
      {
         int n = angry == 1 ? 2 : 1;
         for (int k = -n; k <= n; k++)
         {
            int dx = (hx - eX[i]) >> 4;
            int dy = ((hy >> 4) - 12) - ((eY[i] >> 4) - 26);
            int ax = iabs(dx);
            int ay = iabs(dy);
            int len = imax(ax, ay) + imin(ax, ay) * 3 / 8 + 1;
            int vx = dx * 44 / len;
            int vy = dy * 44 / len;
            shoot(P_ORB, eX[i], eY[i] - 26 * SUB, vx - vy * k / 5, vy + vx * k / 5, 0);
         }
      }
      if (eTimer[i] > 130)
      {
         eState[i] = 2;
         eTimer[i] = 0;
      }
   }
   else if (st == 2)
   {
      // gone into shadow; back somewhere else
      if (eTimer[i] == 14)
      {
         int k = rnd(3);
         eX[i] = kingSpot(k) * SUB;
         puffAt(eX[i] >> 4, (eY[i] >> 4) - 20);
      }
      if (eTimer[i] > 28)
      {
         eState[i] = 1;
         eTimer[i] = 0;
         if (angry == 1)
         {
            int count = 0;
            for (int k = 0; k < eCount; k++)
               if (eType[k] == E_FLYER)
                  count++;
            if (count < 2)
            {
               int w = spawn(E_FLYER, (eX[i] >> 4) + (rnd(2) == 0 ? -40 : 40), 110);
               if (w >= 0)
               {
                  eA[w] = 110 * SUB;
                  eAwake[w] = 1;
                  eH[w] = 12;
               }
            }
         }
      }
   }
}

// the guardian is beaten: it falls apart; its beacon comes out of the dark
int bossDefeated()
{
   int i = bossIdx;
   eState[i] = 9;
   eTimer[i] = 0;
   addScore(5000, eX[i] >> 4, (eY[i] >> 4) - 50);
   for (int k = 0; k < MAXP; k++)
      if (pHero[k] == 0)
         pAlive[k] = 0;
   for (int k = 0; k < eCount; k++)
      if (eType[k] == E_FLYER)
         defeat(k, 0);
   return 0;
}

void damage(int i, int n)
{
   if (eType[i] == E_BOSS)
   {
      if (eAwake[i] == 0 || eState[i] == 9)
         return;
      eHP[i] = eHP[i] - n;
      eFlash[i] = 6;
      if (eHP[i] <= 0)
         bossDefeated();
      return;
   }
   eHP[i] = eHP[i] - n;
   eFlash[i] = 5;
   if (eHP[i] <= 0)
      defeat(i, 0);
}

void updateBoss(int i)
{
   if (eAwake[i] == 0)
      return;
   if (eState[i] == 0)
   {
      eState[i] = 1;
      eTimer[i] = 0;
   }
   if (eFlash[i] > 0)
      eFlash[i] = eFlash[i] - 1;
   if (eState[i] == 9)
   {
      eTimer[i] = eTimer[i] + 1;
      if (eTimer[i] % 6 == 0)
         puffAt((eX[i] >> 4) - 20 + rnd(40), (eY[i] >> 4) - 8 - rnd(40));
      if (eTimer[i] % 12 == 0)
         bossShake(4);
      if (eTimer[i] > 90)
      {
         for (int k = 0; k < eCount; k++)
            if (eType[k] == E_BEACON)
               eA[k] = 0;
         bossOn = 0;
         camMax = mapW * 16 - SW;
         removeThing(i);
         bossIdx = -1;
      }
      return;
   }
   if (land == 0)
      updateBoar(i);
   else if (land == 1)
      updateMoth(i);
   else if (land == 2)
      updateGolem(i);
   else
      updateKing(i);
}

// ------------------------------------------------------------------ everything that moves

void updateThings()
{
   int left = camX - 48;
   int right = camX + SW + 48;
   for (int i = 0; i < eCount; i++)
   {
      int t = eType[i];
      if (t == E_NONE)
         continue;
      int x = eX[i] >> 4;
      if (t != E_BOSS && t != E_PLATH && t != E_PLATV && t != E_BEACON)
      {
         if (x < left || x > right)
         {
            // far off the screen: asleep (and gone if it was left behind awake)
            if (eAwake[i] == 1 && isFoe(t) == 1 && x < camX - 240)
               removeThing(i);
            continue;
         }
         eAwake[i] = 1;
      }
      if (eFlash[i] > 0 && t != E_BOSS)
         eFlash[i] = eFlash[i] - 1;
      if (t == E_WALKER)
         updateWalker(i);
      else if (t == E_FLYER)
         updateFlyer(i);
      else if (t == E_HOPPER)
         updateHopper(i);
      else if (t == E_TURRET)
         updateTurret(i);
      else if (t == E_BOSS)
         updateBoss(i);
      else if (t == E_PLATH || t == E_PLATV)
         updatePlatform(i);
      else if (t == E_CHECK)
         updateCheck(i);
      else if (t == E_BEACON)
         updateBeacon(i);
      else if (t == E_GLINT)
         updateGlintThing(i);
      else
         updateItem(i);
   }
}

// Wren against the creatures: a stomp from above, else she is hurt
void heroVsCreatures()
{
   if (hDead > 0)
      return;
   int x = hx >> 4;
   int y = hy >> 4;
   for (int i = 0; i < eCount; i++)
   {
      int t = eType[i];
      if (isFoe(t) == 0)
         continue;
      if (t == E_WALKER && eState[i] != 0)
         continue;
      if (t == E_BOSS && (eAwake[i] == 0 || eState[i] == 9))
         continue;
      int cx = eX[i] >> 4;
      int fy = eY[i] >> 4;
      int top = fy - eH[i];
      if (touchHero(cx, eW[i], top, fy) == 0)
         continue;
      if (hvy > 0 && y - top < 10 && hHurt < 50)
      {
         // a stomp
         hy = top * SUB;
         hvy = kJump == 1 ? -104 : -72;
         hJumping = kJump;
         hGround = 0;
         if (t == E_BOSS)
         {
            damage(i, 2);
            bossShake(3);
         }
         else if (t == E_WALKER && eHP[i] > 1)
         {
            eHP[i] = eHP[i] - 1;
            eFlash[i] = 6;
         }
         else
            defeat(i, 1);
         sparkAt(x, y);
      }
      else
         hurtHero(cx);
   }
}

void updateShots()
{
   for (int i = 0; i < MAXP; i++)
   {
      if (pAlive[i] == 0)
         continue;
      int k = pKind[i];
      pX[i] = pX[i] + pVX[i];
      pY[i] = pY[i] + pVY[i];
      if (k == P_SPORE || k == P_SHARD)
         pVY[i] = imin(pVY[i] + 3, 90);
      pLife[i] = pLife[i] - 1;
      int x = pX[i] >> 4;
      int y = pY[i] >> 4;
      if (pLife[i] <= 0 || x < camX - 16 || x > camX + SW + 16 || y > SH + 16)
      {
         pAlive[i] = 0;
         continue;
      }
      if (y >= 0 && solidPx(x, y) == 1 && k != P_ORB)
      {
         pAlive[i] = 0;
         sparkAt(x, y);
         if (pHero[i] == 1 && power == 1)
         {
            // the prism's shots break crates
            int t = tileAt(x >> 4, y >> 4);
            if (t == T_CRATE)
            {
               setTile(x >> 4, y >> 4, T_EMPTY);
               puffAt((x >> 4) * 16 + 8, (y >> 4) * 16 + 8);
            }
         }
         continue;
      }
      if (pHero[i] == 1)
      {
         for (int e = 0; e < eCount; e++)
         {
            if (isFoe(eType[e]) == 0)
               continue;
            if (eType[e] == E_WALKER && eState[e] != 0)
               continue;
            if (hitsCreature(e, x, y) == 1)
            {
               damage(e, k == P_BIG ? 2 : 1);
               sparkAt(x, y);
               pAlive[i] = 0;
               break;
            }
         }
      }
      else if (touchHero(x, 3, y - 3, y + 3) == 1)
      {
         hurtHero(x);
         pAlive[i] = 0;
      }
   }
}

// ------------------------------------------------------------------ the camera

void updateCamera()
{
   int target;
   if (bossOn == 1)
      target = arenaX;
   else
      target = (hx >> 4) - 160 + hFace * 30;
   target = clampi(target, camMin, imax(camMin, camMax));
   int d = target - camX;
   if (bossOn == 1)
      camX = camX + sgn(d) * imin(iabs(d), 4);
   else
      camX = camX + d / 6;
   camX = clampi(camX, 0, imax(0, mapW * 16 - SW));
   if (shake > 0)
      shake--;
}

// the guardian wakes when Wren walks into its arena
void checkArena()
{
   if (bossLevel == 0 || bossOn == 1 || bossIdx < 0)
      return;
   if ((hx >> 4) > arenaX + 60)
   {
      bossOn = 1;
      eAwake[bossIdx] = 1;
      camMin = arenaX;
      camMax = arenaX;
      showName = 0;
   }
}

// ------------------------------------------------------------------ drawing the world

int liqFrame;
int boxFrame;
int glintFrame;

void drawTiles(int cx, int oy)
{
   int tx0 = cx >> 4;
   int tx1 = (cx + SW - 1) >> 4;
   for (int ty = 0; ty < ROWS; ty++)
   {
      int y = ty * 16 + oy;
      int row = ty * mapW;
      for (int tx = tx0; tx <= tx1; tx++)
      {
         if (tx < 0 || tx >= mapW)
            continue;
         int t = map[row + tx];
         if (t == T_EMPTY)
            continue;
         if (t == T_LIQTOP_A || t == T_LIQ_A)
            t = t + liqFrame;
         else if (t == T_BOX_A)
            t = t + boxFrame;
         else if (t == T_GLINT_A)
            t = t + glintFrame;
         drawTile(t, tx * 16 - cx, y);
      }
   }
}

void drawThings(int cx, int oy)
{
   for (int i = 0; i < eCount; i++)
   {
      int t = eType[i];
      if (t == E_NONE)
         continue;
      int x = (eX[i] >> 4) - cx;
      int y = (eY[i] >> 4) + oy;
      if (x < -64 || x > SW + 64)
         continue;
      int flip = eDir[i] < 0 ? 1 : 0;
      int fl = eFlash[i] > 0 && (eFlash[i] & 1) == 1 ? 1 : 0;
      int a = (animTick >> 1) & 1;
      if (t == E_WALKER)
      {
         int f = a;
         if (eState[i] == 1)
            f = 2;
         drawSprite(S_CRIT, f, x - 8, y - 16, flip, fl);
      }
      else if (t == E_FLYER)
         drawSprite(S_CRIT, 3 + a, x - 8, y - 14, flip, fl);
      else if (t == E_HOPPER)
         drawSprite(S_CRIT, eVY[i] == 0 ? 5 : 6, x - 8, y - 16, flip, fl);
      else if (t == E_TURRET)
         drawSprite(S_CRIT, eState[i] > 0 ? 6 : 5, x - 8, y - 16, flip, fl);
      else if (t == E_BOSS)
      {
         if (eAwake[i] == 0 && x > SW)
            continue;
         int f = 0;
         int st = eState[i];
         if (land == 0 && (st == 3 || st == 5))
            f = 1;
         if (land == 1)
            f = (animTick & 1);
         if (land == 2 && st == 2 && eTimer[i] < 30)
            f = 1;
         if (land == 3 && st == 1 && eTimer[i] % 40 > 14 && eTimer[i] % 40 < 24)
            f = 1;
         if (eFlash[i] > 0 || st == 9)
            f = 2;
         if (land == 3 && st == 2 && eTimer[i] > 6 && eTimer[i] < 22)
            continue;
         drawSprite(S_BOSS, f, x - 24, y - 48, flip, 0);
      }
      else if (t == E_PLATH || t == E_PLATV)
         drawSprite(S_PLAT, 0, x - 24, y, 0, 0);
      else if (t == E_CHECK)
         drawSprite(S_CHECK, eState[i] == 0 ? 0 : 1 + a, x - 8, y - 24, 0, 0);
      else if (t == E_BEACON)
      {
         if (eA[i] == 1)
            continue;
         drawSprite(S_BEACON, eState[i] == 0 ? 0 : 1 + a, x - 16, y - 48, 0, 0);
      }
      else if (t == E_HEART)
         drawSprite(S_HEART, a, x - 6, y - 12, 0, 0);
      else if (t == E_PRISM)
         drawSprite(S_PRISM, a, x - 6, y - 12, 0, 0);
      else if (t == E_LIFE)
         drawSprite(S_LIFE, a, x - 6, y - 12, 0, 0);
      else if (t == E_GLINT)
         drawTile(T_GLINT_A + glintFrame, x - 8, y - 16);
   }
}

void drawShots(int cx)
{
   for (int i = 0; i < MAXP; i++)
   {
      if (pAlive[i] == 0)
         continue;
      int x = (pX[i] >> 4) - cx;
      int y = pY[i] >> 4;
      int k = pKind[i];
      int f = (animTick + i) & 1;
      int id = S_BOLT + k;
      drawSprite(id, f, x - 4, y - 3, pVX[i] < 0 ? 1 : 0, 0);
   }
}

// the bar at the top: hearts, glints, lives, the score
void drawHud()
{
   dimRect(0, 0, SW, 13, col(8, 6, 16));
   for (int k = 0; k < maxHealth; k++)
      drawSprite(S_HUD, k < health ? 0 : 1, 4 + k * 10, 3, 0, 0);
   int x = 8 + maxHealth * 10;
   if (power == 1)
   {
      drawSprite(S_PRISM, (animTick >> 1) & 1, x, 1, 0, 0);
      x = x + 14;
   }
   drawSprite(S_HUD, 2, x + 4, 3, 0, 0);
   setInk(col(255, 236, 150), col(20, 12, 24), 1);
   x = drawNum(glints, 2, x + 14, 3);
   drawSprite(S_HUD, 3, x + 10, 3, 0, 0);
   setInk(col(255, 255, 255), col(20, 12, 24), 1);
   drawText("x", x + 20, 3);
   drawNum(imax(0, lives), 1, x + 27, 3);
   setInk(col(180, 220, 255), col(20, 12, 24), 1);
   char *lv = numText(levelNo / 2 + 1, 1);
   int w = drawText("LAND ", 160, 3);
   w = drawText(lv, w, 3);
   w = drawText("-", w, 3);
   drawNum(levelNo % 2 + 1, 1, w, 3);
   setInk(col(255, 255, 255), col(20, 12, 24), 1);
   char *sc = numText(score, 7);
   drawText(sc, SW - 4 - textWidth(sc), 3);
   if (bossOn == 1 && bossIdx >= 0)
   {
      // the guardian's strength
      int bw = 200;
      int fillw = bw * imax(0, eHP[bossIdx]) / imax(1, eC[bossIdx]);
      fillRect(100, SH - 12, bw + 2, 7, col(10, 8, 20));
      fillRect(101, SH - 11, fillw, 5, col(230, 70, 90));
      fillRect(101, SH - 11, fillw, 1, col(255, 170, 170));
      setInk(col(255, 220, 220), col(10, 8, 20), 1);
      centreText(bossName(land), SH - 22);
   }
}

void drawWorld()
{
   int cx = camX;
   int oy = 0;
   if (shake > 0)
   {
      cx = cx + ((shake & 2) == 0 ? -1 : 1) * imin(3, shake);
      oy = ((shake & 1) == 0 ? 1 : -1) * imin(2, shake >> 1);
   }
   cx = clampi(cx, 0, imax(0, mapW * 16 - SW));
   drawBackground(cx);
   drawTiles(cx, oy);
   drawThings(cx, oy);
   if (hDead == 0 || hDead < 60)
   {
      int keep = camX;
      camX = cx;
      drawHero();
      camX = keep;
   }
   drawShots(cx);
   int keep2 = camX;
   camX = cx;
   drawEffects();
   camX = keep2;
   drawHud();
   if (showName > 0)
   {
      setInk(col(255, 240, 190), col(30, 14, 30), 2);
      centreText(levelName(levelNo), 40);
   }
   if (paused == 1)
   {
      dimRect(0, 0, SW, SH, col(0, 0, 0));
      setInk(col(255, 255, 255), col(0, 0, 0), 3);
      centreText("PAUSED", 92);
      setInk(col(200, 210, 240), col(0, 0, 0), 1);
      centreText("Enter: go on    Escape: leave", 132);
   }
}

// ------------------------------------------------------------------ the keys

int getKey()
{
   int k = readKey();
   if (k == 10)
      k = 13;
   return k;
}

int held(int a, int b, int c)
{
   if (keyHeld(a) == 1)
      return 1;
   if (b != 0 && keyHeld(b) == 1)
      return 1;
   if (c != 0 && keyHeld(c) == 1)
      return 1;
   return 0;
}

int menuKey;

// the keys of this step; Enter and Escape are kept for the loop
void readKeys()
{
   pressJump = 0;
   pressFire = 0;
   menuKey = 0;
   int k = getKey();
   while (k != 0)
   {
      if (k == KEY_SPACE || k == KEY_UP || k == KEY_W || k == KEY_K)
         pressJump = 1;
      else if (k == KEY_F || k == KEY_J || k == KEY_X)
         pressFire = 1;
      else if (k == KEY_ENTER || k == KEY_ESC)
         menuKey = k;
      k = getKey();
   }
   kLeft = held(KEY_LEFT, KEY_A, 0);
   kRight = held(KEY_RIGHT, 0, 0);
   kDown = held(KEY_DOWN, 0, 0);
   kJump = held(KEY_SPACE, KEY_UP, KEY_W) | held(KEY_K, 0, 0);
   kFire = held(KEY_F, KEY_J, KEY_X);
}

// waits for Enter or Space (1), or Escape (0)
int waitKey()
{
   while (true)
   {
      int k = getKey();
      if (k == KEY_ENTER || k == KEY_SPACE)
         return 1;
      if (k == KEY_ESC)
         return 0;
      delay(15);
   }
   return 0;
}

// ------------------------------------------------------------------ fades and screens

// darkens what is on the screen in 4 steps (a dither), each one shown
void fadeOut()
{
   uint16_t black = col(0, 0, 0);
   for (int step = 0; step < 4; step++)
   {
         for (int y = 0; y < SH; y++)
      {
         int o = orgOff + y * stride;
         int phase = ((y & 1) << 1) + step;
         for (int x = (phase & 3); x < SW; x = x + 4)
            display[o + x] = black;
      }
      // the other buffer gets the same pixels after the swap: draw it twice
      updateDisplay();
      for (int y = 0; y < SH; y++)
      {
         int o = orgOff + y * stride;
         for (int s2 = 0; s2 <= step; s2++)
         {
            int phase = ((y & 1) << 1) + s2;
            for (int x = (phase & 3); x < SW; x = x + 4)
               display[o + x] = black;
         }
      }
      delay(40);
   }
}

void clearScreen(uint16_t c)
{
   fillRect(0, 0, SW, SH, c);
}

// a still with text, drawn into both buffers so it stays put
void present()
{
   updateDisplay();
}

void titleBackdrop(int scroll)
{
   drawBackground(scroll);
   int y = SH - 32;
   // a strip of meadow along the bottom
   for (int tx = 0; tx < 26; tx++)
   {
      int x = tx * 16 - (scroll & 15);
      drawTile(T_TOP, x, y);
      drawTile(T_FILL, x, y + 16);
   }
}

void controlsScreen()
{
   clearScreen(col(10, 8, 24));
   setInk(col(255, 220, 140), col(40, 16, 40), 2);
   centreText("THE KEYS", 16);
   setInk(col(230, 236, 255), col(0, 0, 0), 1);
   int y = 50;
   drawText("Left, right (Q D, A)", 40, y);
   drawText("run", 250, y);
   y = y + 14;
   drawText("Space, up (Z W K)", 40, y);
   drawText("jump - hold it for height", 250, y);
   y = y + 14;
   drawText("F (J X)", 40, y);
   drawText("shoot - hold to keep firing", 250, y);
   y = y + 14;
   drawText("Down (S)", 40, y);
   drawText("crouch", 250, y);
   y = y + 14;
   drawText("Down and jump", 40, y);
   drawText("drop through a ledge", 250, y);
   y = y + 14;
   drawText("Enter", 40, y);
   drawText("pause", 250, y);
   y = y + 14;
   drawText("Escape", 40, y);
   drawText("leave", 250, y);
   y = y + 24;
   setInk(col(255, 236, 150), col(0, 0, 0), 1);
   drawText("Stomp creatures or shoot them. 100 glints make a life.", 40, y);
   y = y + 12;
   drawText("Hit lumen boxes from below. A prism powers your shots", 40, y);
   y = y + 12;
   drawText("until you are hurt. Lantern posts keep your place.", 40, y);
   y = y + 12;
   drawText("Light the beacon at the end of every level.", 40, y);
   setInk(col(180, 190, 220), col(0, 0, 0), 1);
   centreText("press Enter", SH - 20);
   present();
   waitKey();
}

int titleAnim;

// the title; returns 0 when Escape leaves the game
int title()
{
   loadLand(0);
   uint32_t t0 = millis();
   int scroll = 0;
   while (true)
   {
      uint32_t now = millis();
      if (pixelSize == 2)
         scroll = (now - t0) / 20;
      titleBackdrop(scroll);
      int lw = sprW[S_LOGO];
      int bob = pixelSize == 2 ? isin((now - t0) / 12) / 21 : 0;
      drawSprite(S_LOGO, 0, (SW - lw) / 2, 30 + bob, 0, 0);
      setInk(col(255, 232, 190), col(40, 16, 40), 1);
      centreText("a lantern-bearer's run through four darkened lands", 94);
      if (((now - t0) / 500) % 2 == 0)
      {
         setInk(col(255, 255, 255), col(30, 10, 30), 2);
         centreText("PRESS ENTER", 140);
      }
      setInk(col(210, 225, 255), col(20, 10, 30), 1);
      centreText("C: the keys    Escape: leave", 170);
      if (hiScore > 0)
      {
         setInk(col(255, 220, 140), col(20, 10, 30), 1);
         int w = drawText("BEST ", 150, 186);
         drawNum(hiScore, 7, w, 186);
      }
      int dx = 60 + ((now - t0) / 30) % 300;
      drawSprite(S_WREN, W_RUN0 + ((now - t0) / 90) % 6, dx, SH - 56, 0, 0);
      present();
      int k = getKey();
      if (k == KEY_ENTER || k == KEY_SPACE)
         return 1;
      if (k == KEY_ESC)
         return 0;
      if (k == KEY_C)
         controlsScreen();
      if (pixelSize == 1)
         delay(60);
      else
         delay(10);
   }
   return 1;
}

// the story: pages, Enter for the next, Escape skips
void story(int off)
{
   int page = off;
   while (page >= 0)
   {
      clearScreen(col(8, 6, 20));
      fillRect(30, 30, SW - 60, 1, col(120, 90, 60));
      fillRect(30, SH - 40, SW - 60, 1, col(120, 90, 60));
      setInk(col(250, 240, 220), col(30, 20, 40), 1);
      int next = drawStoryPage(page, 44, 56, SW - 88, 14);
      setInk(col(170, 160, 200), col(0, 0, 0), 1);
      if (next >= 0)
         centreText("Enter", SH - 28);
      else
         centreText("Enter: begin", SH - 28);
      present();
      if (waitKey() == 0)
         return;
      page = next;
   }
}

// "LAND 2 - 1  Mirewood", the lives left
void levelCard()
{
   clearScreen(col(6, 4, 14));
   setInk(col(255, 220, 140), col(40, 16, 40), 2);
   int w = textWidth("LAND 1 - 1");
   int x = (SW - w) / 2;
   x = drawText("LAND ", x, 70);
   x = drawNum(levelNo / 2 + 1, 1, x, 70);
   x = drawText(" - ", x, 70);
   drawNum(levelNo % 2 + 1, 1, x, 70);
   setInk(col(240, 240, 255), col(20, 10, 40), 2);
   centreText(levelName(levelNo), 100);
   drawSprite(S_WREN, W_STAND, 170, 140, 0, 0);
   setInk(col(255, 255, 255), col(0, 0, 0), 2);
   drawText("x", 196, 148);
   drawNum(lives, 1, 214, 148);
   if (bossLevel == 1)
   {
      setInk(col(255, 140, 150), col(30, 0, 10), 1);
      centreText("its guardian waits at the end", 190);
   }
   present();
   delay(1800);
}

// ------------------------------------------------------------------ a level

void respawn()
{
   hx = checkX * SUB;
   hy = checkY * SUB;
   hvx = 0;
   hvy = 0;
   hFace = 1;
   hGround = 0;
   hCoyote = 0;
   hJumpBuf = 0;
   hJumping = 0;
   hHurt = 60;
   hDead = 0;
   hPlat = -1;
   hDropThrough = 0;
   hCrouch = 0;
   health = maxHealth;
   camX = clampi((hx >> 4) - 160, 0, imax(0, mapW * 16 - SW));
   for (int i = 0; i < MAXP; i++)
      pAlive[i] = 0;
   if (bossOn == 1 && bossIdx >= 0)
   {
      // the guardian heals and sleeps again
      bossOn = 0;
      camMin = 0;
      camMax = mapW * 16 - SW;
      eAwake[bossIdx] = 0;
      eState[bossIdx] = 0;
      eHP[bossIdx] = eC[bossIdx];
      eX[bossIdx] = eA[bossIdx] * SUB;
      eY[bossIdx] = (ROWS - 3) * 16 * SUB;
   }
}

// the tally when the beacon is lit
void tally()
{
   int bonus = glints * 20 + imax(0, 300 - levelTime) * 10;
   for (int k = 0; k < 30; k++)
   {
      drawWorld();
      dimRect(80, 70, 240, 100, col(0, 0, 0));
      setInk(col(255, 230, 150), col(40, 16, 30), 2);
      if (bossLevel == 1)
         centreText("THE LAND IS LIT", 84);
      else
         centreText("BEACON LIT", 84);
      setInk(col(240, 240, 255), col(0, 0, 0), 1);
      int x = drawText("time ", 120, 116);
      x = drawNum(levelTime, 1, x, 116);
      drawText("s", x, 116);
      x = drawText("bonus ", 210, 116);
      drawNum(bonus * k / 29, 1, x, 116);
      present();
      delay(40);
   }
   addScore(bonus, -1, 0);
   delay(900);
}

// plays level n; returns 1 cleared, 0 out of lives, -1 left
int playLevel(int n)
{
   loadLevel(n);
   loadLand(land);
   levelCard();
   bossOn = 0;
   levelDone = 0;
   paused = 0;
   shake = 0;
   frameNo = 0;
   levelTime = 0;
   showName = 90;
   respawn();
   hHurt = 0;
   dustCol = col(200, 190, 170);
   uint32_t last = millis();
   uint32_t secStart = last;
   int doneSteps = 0;
   while (true)
   {
      readKeys();
      if (menuKey == KEY_ENTER)
         paused = 1 - paused;
      if (menuKey == KEY_ESC)
      {
         paused = 1;
         drawWorld();
         dimRect(60, 80, 280, 60, col(0, 0, 0));
         setInk(col(255, 255, 255), col(0, 0, 0), 1);
         centreText("Leave this run? Y / N", 104);
         present();
         while (true)
         {
            int k = getKey();
            if (k == KEY_Y)
               return -1;
            if (k == KEY_N || k == KEY_ESC || k == KEY_ENTER)
               break;
            delay(15);
         }
         paused = 0;
         last = millis();
      }
      uint32_t now = millis();
      if (paused == 1)
      {
         drawWorld();
         present();
         delay(30);
         last = now;
         continue;
      }
      // fixed steps of 33 ms: one or two a frame, never more than three
      int steps = (now - last) / STEP_MS;
      if (steps < 1)
      {
         delay(STEP_MS - (now - last));
         continue;
      }
      if (steps > 3)
      {
         steps = 3;
         last = now;
      }
      else
         last = last + steps * STEP_MS;
      for (int s = 0; s < steps; s++)
      {
         frameNo++;
         if ((frameNo & 3) == 0)
            animTick++;
         liqFrame = (frameNo >> 4) & 1;
         boxFrame = (frameNo >> 3) & 1;
         glintFrame = (frameNo >> 2) & 3;
         if (showName > 0)
            showName--;
         updateHero();
         checkArena();
         updateThings();
         heroVsCreatures();
         updateShots();
         updateEffects();
         updateCamera();
         // one press is one press, whatever the steps
         pressJump = 0;
         pressFire = 0;
         if (levelDone > 0)
            doneSteps++;
         if (hDead > 0 && hDead > 70)
            break;
      }
      if (now - secStart >= 1000)
      {
         secStart = secStart + 1000;
         if (levelDone == 0)
            levelTime++;
      }
      drawWorld();
      present();
      if (hDead > 70)
      {
         lives--;
         fadeOut();
         if (lives < 0)
            return 0;
         levelCard();
         respawn();
         last = millis();
      }
      if (levelDone > 0 && doneSteps > 45)
      {
         tally();
         fadeOut();
         return 1;
      }
   }
   return -1;
}

// ------------------------------------------------------------------ the end, and losing

void gameOver()
{
   clearScreen(col(4, 2, 10));
   setInk(col(200, 120, 140), col(40, 0, 20), 3);
   centreText("THE LIGHT FADES", 80);
   setInk(col(220, 220, 240), col(0, 0, 0), 1);
   int w = drawText("score ", 150, 130);
   drawNum(score, 7, w, 130);
   centreText("Enter", 180);
   present();
   waitKey();
}

void ending()
{
   story(TX_END);
   // the four beacons, burning
   clearScreen(col(6, 6, 20));
   for (int k = 0; k < 4; k++)
      drawSprite(S_BEACON, 1, 40 + k * 90, 90, 0, 0);
   setInk(col(255, 230, 150), col(40, 16, 30), 2);
   centreText("LUMENFALL", 30);
   setInk(col(240, 240, 255), col(0, 0, 0), 1);
   int w = drawText("final score ", 130, 170);
   drawNum(score, 7, w, 170);
   centreText("made with ESPLiveScript on the ESP32-S3", 200);
   present();
   waitKey();
}

// ------------------------------------------------------------------ starting

void loadCommon()
{
   fontOff = wadFind("FONT");
   textOff = wadFind("TEXT");
   sprFill = 0;
   loadSprite("WREN", S_WREN);
   loadSprite("BOLT", S_BOLT);
   loadSprite("BIGBOLT", S_BIGBOLT);
   loadSprite("ORB", S_ORB);
   loadSprite("SPORE", S_SPORE);
   loadSprite("SHARD", S_SHARD);
   loadSprite("FIRE", S_FIRE);
   loadSprite("HEART", S_HEART);
   loadSprite("PRISM", S_PRISM);
   loadSprite("LIFE", S_LIFE);
   loadSprite("PUFF", S_PUFF);
   loadSprite("SPARK", S_SPARK);
   loadSprite("CHECK", S_CHECK);
   loadSprite("BEACON", S_BEACON);
   loadSprite("HUD", S_HUD);
   loadSprite("LOGO", S_LOGO);
   landSprFrom = sprFill;
}

void newGame()
{
   score = 0;
   lives = 3;
   glints = 0;
   maxHealth = 3;
   health = 3;
   power = 0;
}

int main()
{
   if (wadLoaded == 0)
   {
      printfln("lumenfall.wad must be next to the script");
      return 1;
   }
   pixelSize = 2;
   enableGraphicMode();
   setPixelsize(pixelSize);
   orgX = 0;
   orgY = 0;
   if (pixelSize == 1)
   {
      orgX = 200;
      orgY = 120;
   }
   orgOff = orgY * stride + orgX;
   clearScreen(col(0, 0, 0));
   setInk(col(255, 220, 140), col(0, 0, 0), 1);
   fontOff = wadFind("FONT");
   centreText("lighting the lanterns...", 116);
   present();
   makeSines();
   loadCommon();
   hiScore = 0;
   while (true)
   {
      if (title() == 0)
         return 0;
      fadeOut();
      story(TX_INTRO);
      newGame();
      int n = startLevel;
      startLevel = 0;
      while (n < 8)
      {
         int r = playLevel(n);
         if (r == 1)
            n++;
         else
         {
            if (r == 0)
               gameOver();
            break;
         }
      }
      if (n == 8)
         ending();
   }
   return 0;
}
)EOF";

// lcd.init() on core 0 so Bus_RGB's line copy interrupts stay off core 1, where the script draws
static void lcdInitTask(void *done) {
  lcd.init();
  xTaskNotifyGive((TaskHandle_t)done);
  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);
  xTaskCreatePinnedToCore(lcdInitTask, "lcdInit", 4096, xTaskGetCurrentTaskHandle(), 5, NULL, 0);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;

  // the names of SolarOS's script runtime, so that one lumenfall.bin runs
  // here, in the GameLauncher and on SolarOS
  bindVariable((char *)"uint16_t *", (char *)"display", NULL, (void *)&frame_buffer);
  bindVariable((char *)"uint32_t", (char *)"stride", NULL, (void *)&stride);
  bindFunction((char *)"void", (char *)"updateDisplay", NULL, (void *)updateDisplay);
  bindFunction((char *)"void", (char *)"enableGraphicMode", NULL, (void *)enableGraphicMode);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
  bindFunction((char *)"void", (char *)"delay", (char *)"uint32_t", (void *)scriptDelay);
  bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
  bindFunction((char *)"int", (char *)"readKey", NULL, (void *)readKey);
  bindFunction((char *)"int", (char *)"keyHeld", (char *)"int", (void *)keyHeld);
  loadWad("/lumenfall.wad");
  bindVariable((char *)"uint8_t *", (char *)"wad", NULL, (void *)&wadData);
  bindVariable((char *)"int", (char *)"wadLoaded", NULL, (void *)&wadLoaded);
  bindFunction((char *)"int", (char *)"wadFind", (char *)"char*", (void *)wadFind);

  // about 45 KB of code: in internal RAM when a block that big is free, else PSRAM
  setScriptCodeMemory(SCRIPT_CODE_AUTO);
  uint32_t t0 = millis();
  setCompileProgress(showProgress, NULL);
  static ScriptExecutable exec = parseScript(lumenfall);
  setCompileProgress(NULL, NULL);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("lumenfall: %u bytes of code, compiled in %u ms\n", (unsigned)exec.exe.binary_size, (unsigned)(millis() - t0));

  hid_keyboard_print_char = print_char;
  hid_keyboard_release_char = release_char;
  app_main_keyboard();

  exec.execute("main");
  printf("lumenfall: the game has ended\n");
}

void loop() {
  delay(1000);
}
