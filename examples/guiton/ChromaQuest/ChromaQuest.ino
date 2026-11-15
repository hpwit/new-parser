// ChromaQuest on the Guition ESP32-4848S040, a 4-inch 480x480 touch screen:
// the script of examples/matouch/ChromaQuest, unchanged but for the frame
// buffer's width, shown 1.2 times larger across the screen's width (480x288),
// with a touch pad under it (TouchPad.h):
//   the arrows move Lumo while held (Z Q S D, and their release); it fires by
//   itself. START plays (Space), QUIT gives the game up.
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with
// show(), _frame_buffer, pixel_size, offset) and FastLED (CHSV).
// Guition4848S040.h is the panel configuration.
//
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB,
// Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)".
#include "Guition4848S040.h"
#include "FastLED.h"
#include "script_executable.h"
#include "binding.h"
#include "chroma_assets.h"

LGFX lcd;
uint16_t *frame_buffer;
// where the script draws: its own 400x240 picture (480 wide), in PSRAM;
// show16() scales it into the frame buffer
uint16_t *gameBuffer;
static uint32_t _sizex = 400;
static uint32_t _sizey = 240;
// the art, in flash, as the script's pointers
static uint16_t *gfxP = (uint16_t *)chromaGfx, *colorP = (uint16_t *)worldColor;
static uint8_t *lumP = (uint8_t *)worldLum;

// the game 1.2 times larger: 480x288 on the screen, the pad under it
#define PAD_Y 288
#define PAD_SIDES 0
#include "TouchPad.h"

// the buttons, under the game (480x192, see TouchPad.h)
const PadButton padButtons[] = {
  {85, 4, 80, 58, 'z', PAD_HOLD, "^"},
  {4, 64, 80, 64, 'q', PAD_HOLD, "<"},
  {166, 64, 80, 64, 'd', PAD_HOLD, ">"},
  {85, 130, 80, 58, 's', PAD_HOLD, "v"},
  {290, 64, 186, 124, ' ', PAD_TAP, "START"},
  {376, 4, 100, 52, 27, PAD_TAP, "QUIT"},
};
const int padButtonCount = sizeof(padButtons) / sizeof(padButtons[0]);

// HSV -> RGB565, byte swapped like LovyanGFX's frame buffer
static uint16_t hsv16(uint8_t h, uint8_t s, uint8_t v) {
  CRGB d = CHSV(h, s, v);
  uint16_t j = lcd.color565(d.red, d.green, d.blue);
  return ((j & 0xff) << 8) | ((j & 0xFF00) >> 8);
}

// the script's 400x240 picture, 1.2 times larger, into the frame buffer's top
// 480x288: each five pixels become six (the first twice), each five rows six
static void scaleGame(uint16_t *fb) {
  for (int dy = 0; dy < PAD_Y; dy++) {
    uint16_t *dst = fb + dy * 480;
    if (dy % 6 == 1) {
      memcpy(dst, dst - 480, 480 * 2);
      continue;
    }
    const uint16_t *src = gameBuffer + (dy * 5 / 6) * 480;
    for (int sx = 0; sx < 400; sx += 5) {
      dst[0] = src[sx];
      dst[1] = src[sx];
      dst[2] = src[sx + 1];
      dst[3] = src[sx + 2];
      dst[4] = src[sx + 3];
      dst[5] = src[sx + 4];
      dst += 6;
    }
  }
}

void show16() {
  scaleGame(frame_buffer);
  padShow(frame_buffer);
  lcd._bus_instance.show();
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  lcd._panel_instance.resetWriteBuffer();
}

void clearscreen() {
  memset(frame_buffer, 0, 480 * 480 * 2);
  memset(gameBuffer, 0, 480 * 240 * 2);
  padDrawn[0] = padDrawn[1] = 0xFFFFFFFF;
}

// the games ask for pixel size 2 (400x240 on 800x480); here the screen is
// 480x480 and show16() scales the game, so the size stays 1
void setPixelsize(int h) {
  clearscreen();
  show16();
  clearscreen();
  lcd._bus_instance.offset = 0;
  lcd._bus_instance.pixel_size = 1;
}

uint32_t scriptMillis() {
  return millis();
}

uint32_t scriptRand(uint32_t n) {
  return esp_random() % n;
}

// the backlight, 0 (off) to 255: a script can dim the screen
void scriptSetBrightness(int level) {
  lcd.setBrightness(level < 0 ? 0 : level > 255 ? 255 : level);
}

char chromaQuest[] = R"EOF(
// the frame buffer's width, whatever the pixel size
#define STRIDE 480
#define SW 400
#define SH 240
#define WORLD 96000
// the transparent colour of the art (magenta, byte swapped)
#define KEY 8184
// positions are in sixteenths of a pixel
#define FP 16
#define TOP 16
#define GROUND 222
#define SHOTS 16
#define ENEMIES 10
#define BOLTS 20
#define DROPS 8
#define BURSTS 10
#define STEP_MS 16

// where each sprite is in chromaGfx[]: offset, width, height (chroma_assets.py)
#define LUMO0 0
#define LUMO0_W 22
#define LUMO0_H 24
#define LUMO1 528
#define LUMO1_W 22
#define LUMO1_H 24
#define DROPR0 1056
#define DROPR0_W 12
#define DROPR0_H 16
#define DROPR1 1248
#define DROPR1_W 12
#define DROPR1_H 16
#define DROPG0 1440
#define DROPG0_W 12
#define DROPG0_H 16
#define DROPG1 1632
#define DROPG1_W 12
#define DROPG1_H 16
#define DROPB0 1824
#define DROPB0_W 12
#define DROPB0_H 16
#define DROPB1 2016
#define DROPB1_W 12
#define DROPB1_H 16
#define MOTH0 2208
#define MOTH0_W 24
#define MOTH0_H 18
#define MOTH1 2640
#define MOTH1_W 24
#define MOTH1_H 18
#define DEVIL0 3072
#define DEVIL0_W 20
#define DEVIL0_H 26
#define DEVIL1 3592
#define DEVIL1_W 20
#define DEVIL1_H 26
#define SHARD0 4112
#define SHARD0_W 22
#define SHARD0_H 22
#define SHARD1 4596
#define SHARD1_W 22
#define SHARD1_H 22
#define SPARK 5080
#define SPARK_W 10
#define SPARK_H 6
#define BOLT 5140
#define BOLT_W 8
#define BOLT_H 8
#define BURST0 5204
#define BURST0_W 30
#define BURST0_H 30
#define BURST1 6104
#define BURST1_W 30
#define BURST1_H 30
#define BURST2 7004
#define BURST2_W 30
#define BURST2_H 30
#define BURST3 7904
#define BURST3_W 30
#define BURST3_H 30
#define BURST4 8804
#define BURST4_W 30
#define BURST4_H 30
#define BURST5 9704
#define BURST5_W 30
#define BURST5_H 30


int sinT[64] = {0, 6, 12, 19, 24, 30, 36, 41, 45, 49, 53, 56, 59, 61, 63, 64, 64, 64, 63, 61, 59, 56, 53, 49, 45, 41, 36, 30, 24, 19, 12, 6, 0, -6, -12, -19, -24, -30, -36, -41, -45, -49, -53, -56, -59, -61, -63, -64, -64, -64, -63, -61, -59, -56, -53, -49, -45, -41, -36, -30, -24, -19, -12, -6};

// an 8x8 font of its own, bold and rounded: 0-9 then A-Z, 8 rows a
// character, the leftmost pixel in the highest bit
uint8_t font8[288] = {126, 199, 207, 219, 243, 227, 126, 0, 24, 56, 120, 24, 24, 24, 126, 0, 126, 195, 6, 28, 112, 192, 255, 0, 126, 195, 3, 62, 3, 195, 126, 0, 14, 30, 54, 102, 255, 6, 6, 0, 255, 192, 254, 3, 3, 195, 126, 0, 62, 96, 192, 254, 195, 195, 126, 0, 255, 195, 6, 12, 24, 24, 24, 0, 126, 195, 195, 126, 195, 195, 126, 0, 126, 195, 195, 127, 3, 6, 124, 0, 60, 102, 195, 195, 255, 195, 195, 0, 254, 195, 195, 254, 195, 195, 254, 0, 126, 195, 192, 192, 192, 195, 126, 0, 252, 198, 195, 195, 195, 198, 252, 0, 255, 192, 192, 252, 192, 192, 255, 0, 255, 192, 192, 252, 192, 192, 192, 0, 126, 195, 192, 207, 195, 195, 126, 0, 195, 195, 195, 255, 195, 195, 195, 0, 126, 24, 24, 24, 24, 24, 126, 0, 63, 6, 6, 6, 198, 198, 124, 0, 198, 204, 216, 240, 216, 204, 198, 0, 192, 192, 192, 192, 192, 192, 255, 0, 195, 231, 255, 219, 195, 195, 195, 0, 195, 227, 243, 219, 207, 199, 195, 0, 126, 195, 195, 195, 195, 195, 126, 0, 254, 195, 195, 254, 192, 192, 192, 0, 126, 195, 195, 195, 203, 198, 123, 0, 254, 195, 195, 254, 204, 198, 195, 0, 126, 195, 192, 126, 3, 195, 126, 0, 255, 24, 24, 24, 24, 24, 24, 0, 195, 195, 195, 195, 195, 195, 126, 0, 195, 195, 195, 195, 102, 60, 24, 0, 195, 195, 195, 219, 255, 231, 195, 0, 195, 102, 60, 24, 60, 102, 195, 0, 195, 195, 102, 60, 24, 24, 24, 0, 255, 6, 12, 24, 48, 96, 255, 0};
// its colours, a row each: chrome, gold, fire, ice
uint16_t textGrad[32];
uint16_t textShadow;

uint16_t greyLut[256];     // brightness -> a grey, as the frame buffer wants it

// the hero
int px;
int py;
int pvx;
int pvy;
int facing;
int lives;
int invincible;
int dead;
int fireTimer;

// shots
int sx[16];
int sy[16];
int svx[16];
int son[16];

// the colour thieves: kind by level, colour 0 red, 1 green, 2 blue
int ex[10];
int ey[10];
int eon[10];
int ecol[10];
int ehp[10];
int eage[10];
int ebase[10];
int edx[10];
int edy[10];
int efire[10];
int eflash[10];

// their grey bolts
int bx[20];
int by[20];
int bvx[20];
int bvy[20];
int bon[20];

// the drops of colour
int qx[8];
int qy[8];
int qvy[8];
int qon[8];
int qcol[8];
int qage[8];

// splashes
int ox[10];
int oy[10];
int oage[10];

// the world's colour: collected, needed, and shown (0..16 each, easing in)
int got[3];
int need;
int shown[3];

int score;
int high;
int level;
int phase;                 // 0 level title, 1 playing, 2 caught, 3 restored, 4 game over
int phaseTimer;
int spawnTimer;
int demo;
int frame;

uint16_t white;
uint16_t grey;
uint16_t dark;
uint16_t yellow;
uint16_t barBack;
uint16_t channel[3];
uint16_t channelDim[3];

int sine(int a)
{
   return sinT[a & 63];
}

// r, g, b (0-255) as the frame buffer wants them: RGB565, byte swapped
uint16_t pack(int r, int g, int b)
{
   int v = ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3);
   return ((v & 255) << 8) | (v >> 8);
}

void makeColors()
{
   for (int i = 0; i < 256; i++)
      greyLut[i] = pack(i, i, i);
   white = pack(255, 255, 255);
   grey = pack(150, 150, 165);
   dark = pack(14, 12, 24);
   yellow = pack(255, 225, 90);
   barBack = pack(50, 48, 64);
   channel[0] = pack(240, 50, 60);
   channel[1] = pack(50, 220, 80);
   channel[2] = pack(60, 120, 255);
   channelDim[0] = pack(90, 30, 35);
   channelDim[1] = pack(25, 80, 35);
   channelDim[2] = pack(30, 45, 100);
   // chrome: white, steel blue, a dark band, then bright again
   textShadow = pack(10, 8, 20);
   for (int r = 0; r < 8; r++)
   {
      textGrad[r] = pack(255 - r * 20, 255 - r * 12, 255);
      textGrad[8 + r] = pack(255, 250 - r * 18, 120 - r * 14);
      textGrad[16 + r] = pack(255, 60 + r * 24, 40);
      textGrad[24 + r] = pack(150 + r * 10, 230, 255);
   }
   textGrad[3] = pack(90, 100, 170);
   textGrad[4] = pack(200, 160, 255);
   textGrad[11] = pack(150, 80, 20);
   textGrad[12] = pack(255, 200, 80);
}

void fillRect(int x, int y, int w, int h, uint16_t c)
{
   if (x < 0)
   {
      w = w + x;
      x = 0;
   }
   if (x + w > SW)
      w = SW - x;
   if (y < 0)
   {
      h = h + y;
      y = 0;
   }
   if (y + h > SH)
      h = SH - y;
   for (int j = 0; j < h; j++)
   {
      int row = (y + j) * STRIDE + x;
      for (int i = 0; i < w; i++)
         led16[row + i] = c;
   }
}

// a sprite of chromaGfx[] at (x, y), its transparent pixels left out
void drawSprite(int base, int w, int h, int x, int y)
{
   int i0 = 0;
   int i1 = w;
   if (x < 0)
      i0 = -x;
   if (x + w > SW)
      i1 = SW - x;
   if (i0 >= i1)
      return;
   for (int j = 0; j < h; j++)
   {
      int yy = y + j;
      if (yy >= 0 && yy < SH)
      {
         int row = yy * STRIDE + x;
         int src = base + j * w;
         for (int i = i0; i < i1; i++)
         {
            uint16_t c = chromaGfx[src + i];
            if (c != KEY)
               led16[row + i] = c;
         }
      }
   }
}

// the same sprite all in one colour: a hit, or a thief's colour showing
void drawTinted(int base, int w, int h, int x, int y, uint16_t tint)
{
   for (int j = 0; j < h; j++)
   {
      int yy = y + j;
      if (yy >= 0 && yy < SH)
         for (int i = 0; i < w; i++)
         {
            int xx = x + i;
            if (xx >= 0 && xx < SW && chromaGfx[base + j * w + i] != KEY)
               led16[yy * STRIDE + xx] = tint;
         }
   }
}

// a character of font8 at (x, y), sc pixels a dot: a dark shadow down and
// to the right, then each row in its colour of the style's gradient
void drawGlyph(int x, int y, int g, int sc, int style)
{
   for (int pass = 0; pass < 2; pass++)
   {
      int off = sc;
      if (pass == 1)
         off = 0;
      for (int r = 0; r < 8; r++)
      {
         int bits = font8[g * 8 + r];
         uint16_t c = textShadow;
         if (pass == 1)
            c = textGrad[style * 8 + r];
         for (int b = 0; b < 8; b++)
            if ((bits >> (7 - b)) & 1)
               fillRect(x + b * sc + off, y + r * sc + off, sc, sc, c);
      }
   }
}

// capitals, digits and spaces, 9 dots apart
void drawText(int x, int y, char *s, int sc, int style)
{
   int i = 0;
   while (s[i] != 0)
   {
      int ch = s[i];
      if (ch >= 48 && ch <= 57)
         drawGlyph(x, y, ch - 48, sc, style);
      if (ch >= 65 && ch <= 90)
         drawGlyph(x, y, ch - 55, sc, style);
      x = x + 9 * sc;
      i++;
   }
}

int digits[8];
void drawNumber(int x, int y, int v, int sc, int style)
{
   int n = 0;
   while (v > 0 || n == 0)
   {
      digits[n] = v % 10;
      v = v / 10;
      n++;
   }
   for (int k = n - 1; k >= 0; k--)
   {
      drawGlyph(x, y, digits[k], sc, style);
      x = x + 9 * sc;
   }
}

// --- the world: grey, coloured, or on the way ---

void drawWorld()
{
   int base = ((level - 1) % 3) * WORLD;
   int fr = shown[0];
   int fg = shown[1];
   int fb = shown[2];
   if (fr == 16 && fg == 16 && fb == 16)
   {
      for (int y = 0; y < SH; y++)
      {
         int row = y * STRIDE;
         int src = base + y * SW;
         for (int x = 0; x < SW; x++)
            led16[row + x] = worldColor[src + x];
      }
      return;
   }
   if (fr == 0 && fg == 0 && fb == 0)
   {
      for (int y = 0; y < SH; y++)
      {
         int row = y * STRIDE;
         int src = base + y * SW;
         for (int x = 0; x < SW; x++)
            led16[row + x] = greyLut[worldLum[src + x]];
      }
      return;
   }
   // each channel: the grey, plus its share of the colour
   for (int y = 0; y < SH; y++)
   {
      int row = y * STRIDE;
      int src = base + y * SW;
      for (int x = 0; x < SW; x++)
      {
         int c = worldColor[src + x];
         int l = worldLum[src + x];
         int v = ((c & 255) << 8) | (c >> 8);
         int r = (v >> 8) & 248;
         int g = (v >> 3) & 252;
         int b = (v << 3) & 248;
         r = l + (((r - l) * fr) >> 4);
         g = l + (((g - l) * fg) >> 4);
         b = l + (((b - l) * fb) >> 4);
         int o = ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3);
         led16[row + x] = ((o & 255) << 8) | (o >> 8);
      }
   }
}

// --- the game ---

void splash(int x, int y)
{
   for (int k = 0; k < BURSTS; k++)
      if (oage[k] < 0)
      {
         ox[k] = x;
         oy[k] = y;
         oage[k] = 0;
         return;
      }
}

void addScore(int n)
{
   score = score + n;
   if (score > high)
      high = score;
}

// a colour the world still needs, if any
int neededColor()
{
   int c = rand(3);
   for (int k = 0; k < 3; k++)
   {
      int t = (c + k) % 3;
      if (got[t] < need)
         return t;
   }
   return c;
}

void spawnThief()
{
   for (int e = 0; e < ENEMIES; e++)
      if (!eon[e])
      {
         eon[e] = 1;
         ecol[e] = neededColor();
         eage[e] = rand(64);
         eflash[e] = 0;
         ehp[e] = 1 + level / 2;
         efire[e] = 90 + rand(120);
         int fromLeft = rand(2);
         ey[e] = (TOP + 20 + rand(GROUND - TOP - 60)) * FP;
         ebase[e] = ey[e];
         if (fromLeft)
         {
            ex[e] = -24 * FP;
            edx[e] = 1;
         }
         else
         {
            ex[e] = (SW + 4) * FP;
            edx[e] = -1;
         }
         edy[e] = 1;
         if (rand(2) == 0)
            edy[e] = -1;
         return;
      }
}

void dropColor(int x, int y, int col)
{
   for (int k = 0; k < DROPS; k++)
      if (!qon[k])
      {
         qon[k] = 1;
         qx[k] = x;
         qy[k] = y;
         qvy[k] = -12;
         qcol[k] = col;
         qage[k] = 0;
         return;
      }
}

void bolt(int x, int y)
{
   int ddx = px + 11 * FP - x;
   int ddy = py + 12 * FP - y;
   int ax = ddx;
   if (ax < 0)
      ax = -ax;
   int ay = ddy;
   if (ay < 0)
      ay = -ay;
   int len = ax + ay / 2;
   if (ay > ax)
      len = ay + ax / 2;
   if (len < 1)
      len = 1;
   int speed = 22 + level * 3;
   for (int b = 0; b < BOLTS; b++)
      if (!bon[b])
      {
         bon[b] = 1;
         bx[b] = x;
         by[b] = y;
         bvx[b] = ddx * speed / len;
         bvy[b] = ddy * speed / len;
         return;
      }
}

void startLevel()
{
   for (int k = 0; k < 3; k++)
   {
      got[k] = 0;
      shown[k] = 0;
   }
   need = 2 + (level - 1) % 3 + (level - 1) / 3;
   for (int e = 0; e < ENEMIES; e++)
      eon[e] = 0;
   for (int b = 0; b < BOLTS; b++)
      bon[b] = 0;
   for (int k = 0; k < DROPS; k++)
      qon[k] = 0;
   for (int k = 0; k < SHOTS; k++)
      son[k] = 0;
   px = 40 * FP;
   py = 110 * FP;
   pvx = 0;
   pvy = 0;
   facing = 1;
   invincible = 120;
   dead = 0;
   spawnTimer = 60;
   phase = 0;
   phaseTimer = 150;
}

void newGame(int asDemo)
{
   demo = asDemo;
   score = 0;
   lives = 3;
   level = 1;
   for (int k = 0; k < BURSTS; k++)
      oage[k] = -1;
   startLevel();
}

void hitLumo()
{
   if (invincible > 0 || dead)
      return;
   splash(px / FP - 4, py / FP - 3);
   dead = 100;
   // the demo shows the game: it goes on whatever happens
   if (!demo)
      lives--;
}

// the demo pilot: to the drops, else in line with a thief, away from bolts
void autopilot()
{
   int tx = 60 * FP;
   int ty = 110 * FP;
   int best = 1000000;
   bool aDrop = false;
   for (int k = 0; k < DROPS; k++)
      if (qon[k])
      {
         int d = qx[k] - px;
         if (d < 0)
            d = -d;
         if (d < best)
         {
            best = d;
            tx = qx[k] - 5 * FP;
            ty = qy[k] - 4 * FP;
            aDrop = true;
         }
      }
   if (!aDrop)
      for (int e = 0; e < ENEMIES; e++)
         if (eon[e])
         {
            int d = ex[e] - px;
            if (d < 0)
               d = -d;
            if (d < best)
            {
               best = d;
               ty = ey[e];
               tx = ex[e] - 110 * FP;
               if (ex[e] < px)
                  tx = ex[e] + 110 * FP;
            }
         }
   for (int b = 0; b < BOLTS; b++)
      if (bon[b])
      {
         int ddx = bx[b] - px - 11 * FP;
         int ddy = by[b] - py - 12 * FP;
         if (ddx > -40 * FP && ddx < 40 * FP && ddy > -26 * FP && ddy < 26 * FP)
         {
            if (ddy > 0)
               ty = py - 40 * FP;
            else
               ty = py + 40 * FP;
         }
      }
   pvx = 0;
   if (tx < px - 3 * FP)
      pvx = -1;
   if (tx > px + 3 * FP)
      pvx = 1;
   pvy = 0;
   if (ty < py - 3 * FP)
      pvy = -1;
   if (ty > py + 3 * FP)
      pvy = 1;
   if (!aDrop)
      for (int e = 0; e < ENEMIES; e++)
         if (eon[e] && ey[e] > py - 20 * FP && ey[e] < py + 20 * FP)
         {
            if (ex[e] > px)
               facing = 1;
            else
               facing = -1;
         }
}

// one step of 1/60 s
void step()
{
   for (int k = 0; k < BURSTS; k++)
      if (oage[k] >= 0)
      {
         oage[k] = oage[k] + 1;
         if (oage[k] >= 24)
            oage[k] = -1;
      }
   // the colour washes in, a little each few steps
   if (frame % 2 == 0)
      for (int k = 0; k < 3; k++)
      {
         int target = got[k] * 16 / need;
         if (target > 16)
            target = 16;
         if (shown[k] < target)
            shown[k] = shown[k] + 1;
      }
   if (phase == 4)
   {
      phaseTimer--;
      if (phaseTimer <= 0)
         newGame(1);
      return;
   }
   if (phase == 0 || phase == 3)
   {
      phaseTimer--;
      if (phaseTimer <= 0)
      {
         if (phase == 3)
         {
            level++;
            startLevel();
            return;
         }
         phase = 1;
      }
      if (phase == 3)
         return;
   }
   if (demo)
      autopilot();
   // Lumo
   if (dead > 0)
   {
      dead--;
      if (dead == 0)
      {
         if (lives <= 0)
         {
            phase = 4;
            phaseTimer = 300;
            return;
         }
         px = 40 * FP;
         py = 110 * FP;
         invincible = 150;
      }
   }
   else
   {
      px = px + pvx * 30;
      py = py + pvy * 30;
      if (pvx != 0)
         facing = pvx;
      if (px < 0)
         px = 0;
      if (px > (SW - 22) * FP)
         px = (SW - 22) * FP;
      if (py < TOP * FP)
         py = TOP * FP;
      if (py > (GROUND - 24) * FP)
         py = (GROUND - 24) * FP;
      if (invincible > 0)
         invincible--;
      fireTimer--;
      if (fireTimer <= 0 && phase == 1)
      {
         for (int k = 0; k < SHOTS; k++)
            if (!son[k])
            {
               son[k] = 1;
               sx[k] = px + 6 * FP + facing * 12 * FP;
               sy[k] = py + 11 * FP;
               svx[k] = facing * 80;
               break;
            }
         fireTimer = 10;
      }
   }
   for (int k = 0; k < SHOTS; k++)
      if (son[k])
      {
         sx[k] = sx[k] + svx[k];
         if (sx[k] < -12 * FP || sx[k] > SW * FP)
            son[k] = 0;
      }
   // new thieves
   if (phase == 1)
   {
      spawnTimer--;
      if (spawnTimer <= 0)
      {
         spawnThief();
         spawnTimer = 110 - level * 10;
         if (spawnTimer < 40)
            spawnTimer = 40;
      }
   }
   // the thieves: moths flutter, dust devils sway, shards bounce
   int kind = (level - 1) % 3;
   for (int e = 0; e < ENEMIES; e++)
      if (eon[e])
      {
         eage[e] = eage[e] + 1;
         if (eflash[e] > 0)
            eflash[e] = eflash[e] - 1;
         if (kind == 0)
         {
            ex[e] = ex[e] + edx[e] * (14 + level);
            ey[e] = ebase[e] + sine(eage[e]) * 30 * FP / 64;
         }
         if (kind == 1)
         {
            ex[e] = ex[e] + edx[e] * (9 + level);
            ey[e] = ebase[e] + sine(eage[e] / 2) * 50 * FP / 64;
         }
         if (kind == 2)
         {
            ex[e] = ex[e] + edx[e] * (16 + level);
            ey[e] = ey[e] + edy[e] * (12 + level);
            if (ey[e] < TOP * FP || ey[e] > (GROUND - 24) * FP)
               edy[e] = -edy[e];
         }
         efire[e] = efire[e] - 1;
         if (efire[e] <= 0 && phase == 1 && ex[e] > 0 && ex[e] < (SW - 20) * FP)
         {
            bolt(ex[e] + 10 * FP, ey[e] + 10 * FP);
            efire[e] = 150 + rand(120) - level * 10;
         }
         if (ex[e] < -40 * FP || ex[e] > (SW + 40) * FP)
            eon[e] = 0;
      }
   // bolts
   for (int b = 0; b < BOLTS; b++)
      if (bon[b])
      {
         bx[b] = bx[b] + bvx[b];
         by[b] = by[b] + bvy[b];
         if (bx[b] < -8 * FP || bx[b] > SW * FP || by[b] < 0 || by[b] > SH * FP)
            bon[b] = 0;
         int ddx = bx[b] + 4 * FP - px - 11 * FP;
         int ddy = by[b] + 4 * FP - py - 13 * FP;
         if (bon[b] && !dead && ddx > -7 * FP && ddx < 7 * FP && ddy > -7 * FP && ddy < 7 * FP)
         {
            bon[b] = 0;
            hitLumo();
         }
      }
   // shots against the thieves
   for (int k = 0; k < SHOTS; k++)
      if (son[k])
         for (int e = 0; e < ENEMIES; e++)
            if (eon[e] && son[k] && sx[k] + 10 * FP > ex[e] && sx[k] < ex[e] + 22 * FP && sy[k] + 3 * FP > ey[e] &&
                sy[k] < ey[e] + 22 * FP)
            {
               son[k] = 0;
               ehp[e] = ehp[e] - 1;
               eflash[e] = 4;
               if (ehp[e] <= 0)
               {
                  eon[e] = 0;
                  splash(ex[e] / FP - 4, ey[e] / FP - 4);
                  dropColor(ex[e] + 5 * FP, ey[e] + 4 * FP, ecol[e]);
                  addScore(100);
               }
            }
   // touching a thief
   if (!dead && invincible == 0)
      for (int e = 0; e < ENEMIES; e++)
         if (eon[e])
         {
            int ddx = ex[e] - px;
            int ddy = ey[e] - py;
            if (ddx > -18 * FP && ddx < 18 * FP && ddy > -18 * FP && ddy < 18 * FP)
            {
               eon[e] = 0;
               splash(ex[e] / FP, ey[e] / FP);
               hitLumo();
            }
         }
   // drops: a hop, then they fall gently to the ground; caught, they paint
   for (int k = 0; k < DROPS; k++)
      if (qon[k])
      {
         qage[k] = qage[k] + 1;
         qvy[k] = qvy[k] + 1;
         if (qvy[k] > 10)
            qvy[k] = 10;
         qy[k] = qy[k] + qvy[k];
         if (qy[k] > (GROUND - 16) * FP)
         {
            qy[k] = (GROUND - 16) * FP;
            qvy[k] = 0;
         }
         if (qage[k] > 480)
            qon[k] = 0;
         int ddx = qx[k] - px - 5 * FP;
         int ddy = qy[k] - py - 4 * FP;
         if (!dead && ddx > -14 * FP && ddx < 18 * FP && ddy > -18 * FP && ddy < 22 * FP)
         {
            qon[k] = 0;
            int c = qcol[k];
            if (got[c] < need)
            {
               got[c] = got[c] + 1;
               addScore(250);
            }
            else
               addScore(500);
            splash(qx[k] / FP - 9, qy[k] / FP - 7);
            if (got[0] >= need && got[1] >= need && got[2] >= need && phase == 1)
            {
               phase = 3;
               phaseTimer = 240;
               addScore(2000 * level);
               for (int e = 0; e < ENEMIES; e++)
                  if (eon[e])
                  {
                     eon[e] = 0;
                     splash(ex[e] / FP, ey[e] / FP);
                  }
               for (int b = 0; b < BOLTS; b++)
                  bon[b] = 0;
            }
         }
      }
}

void handleKey(int k)
{
   if (k == 27)
   {
      // Escape: the game is given up, back to the demo
      if (!demo)
         newGame(1);
      return;
   }
   if (k == 32 && (demo || phase == 4))
   {
      newGame(0);
      return;
   }
   if (demo)
      return;
   // a direction moves Lumo while it is held: its release comes as its
   // letter + 128 and stops that way
   if (k >= 128)
   {
      int r = k - 128;
      if (r == 122 && pvy == -1)
         pvy = 0;
      if (r == 115 && pvy == 1)
         pvy = 0;
      if (r == 113 && pvx == -1)
         pvx = 0;
      if (r == 100 && pvx == 1)
         pvx = 0;
      return;
   }
   if (k == 122 || k == 90)
      pvy = -1;
   if (k == 115 || k == 83)
      pvy = 1;
   if (k == 113 || k == 81)
      pvx = -1;
   if (k == 100 || k == 68)
      pvx = 1;
}

void drawThings()
{
   int f = (frame / 8) % 2;
   for (int k = 0; k < DROPS; k++)
      if (qon[k] && (qage[k] < 390 || (frame / 4) % 2 == 0))
      {
         int x = qx[k] / FP;
         int y = qy[k] / FP;
         int c = qcol[k];
         if (c == 0)
            drawSprite(DROPR0 + f * DROPR0_W * DROPR0_H, DROPR0_W, DROPR0_H, x, y);
         if (c == 1)
            drawSprite(DROPG0 + f * DROPG0_W * DROPG0_H, DROPG0_W, DROPG0_H, x, y);
         if (c == 2)
            drawSprite(DROPB0 + f * DROPB0_W * DROPB0_H, DROPB0_W, DROPB0_H, x, y);
      }
   int kind = (level - 1) % 3;
   for (int e = 0; e < ENEMIES; e++)
      if (eon[e])
      {
         int x = ex[e] / FP;
         int y = ey[e] / FP;
         int base = MOTH0 + f * MOTH0_W * MOTH0_H;
         int w = MOTH0_W;
         int h = MOTH0_H;
         if (kind == 1)
         {
            base = DEVIL0 + f * DEVIL0_W * DEVIL0_H;
            w = DEVIL0_W;
            h = DEVIL0_H;
         }
         if (kind == 2)
         {
            base = SHARD0 + f * SHARD0_W * SHARD0_H;
            w = SHARD0_W;
            h = SHARD0_H;
         }
         if (eflash[e] > 0)
            drawTinted(base, w, h, x, y, white);
         else
            drawSprite(base, w, h, x, y);
         // the colour it stole, glowing in its heart
         int cx = x + w / 2 - 2;
         int cy = y + h / 2 - 2;
         fillRect(cx, cy, 4, 4, channel[ecol[e]]);
         fillRect(cx + 1, cy - 1, 2, 6, channel[ecol[e]]);
         fillRect(cx + 1, cy + 1, 1, 1, white);
      }
   for (int b = 0; b < BOLTS; b++)
      if (bon[b])
         drawSprite(BOLT, BOLT_W, BOLT_H, bx[b] / FP, by[b] / FP);
   for (int k = 0; k < SHOTS; k++)
      if (son[k])
         drawSprite(SPARK, SPARK_W, SPARK_H, sx[k] / FP, sy[k] / FP);
   if (!dead && phase != 4 && (invincible == 0 || (frame / 3) % 2 == 0))
      drawSprite(LUMO0 + ((frame / 10) % 2) * LUMO0_W * LUMO0_H, LUMO0_W, LUMO0_H, px / FP, py / FP);
   for (int k = 0; k < BURSTS; k++)
      if (oage[k] >= 0)
         drawSprite(BURST0 + (oage[k] / 4) * BURST0_W * BURST0_H, BURST0_W, BURST0_H, ox[k], oy[k]);
}

void drawHud()
{
   fillRect(0, 0, SW, 13, dark);
   drawNumber(4, 2, score, 1, 0);
   drawText(72, 2, "HI", 1, 1);
   drawNumber(92, 2, high, 1, 1);
   // the three gauges: a block per drop needed
   for (int c = 0; c < 3; c++)
   {
      int x0 = 176 + c * 50;
      int w = 44 / need;
      for (int k = 0; k < need; k++)
      {
         uint16_t col = channelDim[c];
         if (k < got[c])
            col = channel[c];
         fillRect(x0 + k * w, 3, w - 2, 7, col);
      }
   }
   for (int i = 0; i < lives - 1 && i < 4; i++)
   {
      fillRect(334 + i * 12, 3, 8, 8, yellow);
      fillRect(335 + i * 12, 2, 6, 10, yellow);
      fillRect(336 + i * 12, 4, 3, 3, white);
   }
   drawNumber(386, 2, level, 1, 3);
   if (phase == 0)
   {
      int kind = (level - 1) % 3;
      if (kind == 0)
         drawText(56, 70, "THE MOON", 4, 0);
      if (kind == 1)
         drawText(128, 70, "MARS", 4, 2);
      if (kind == 2)
         drawText(92, 70, "SATURN", 4, 1);
      drawText(101, 120, "BRING BACK THE COLOURS", 1, 3);
   }
   if (phase == 3)
   {
      drawText(11, 86, "WORLD RESTORED", 3, 0);
      int bonus = 2000 * level;
      int w = 18;
      if (bonus >= 10000)
         w = 22;
      drawNumber(200 - w * 2, 124, bonus, 2, 1);
   }
   if (phase == 4)
      drawText(78, 104, "GAME OVER", 3, 2);
   if (demo && phase != 0 && (frame / 20) % 2 == 0)
      drawText(150, 226, "PRESS SPACE", 1, 0);
}

int main()
{
   setPixelsize(2);           // 400x240, each 2x2 pixels
   makeColors();
   high = 0;
   newGame(1);
   uint32_t last = millis();
   while (true)
   {
      int k = readKey();
      while (k != 0)
      {
         handleKey(k);
         k = readKey();
      }
      uint32_t now = millis();
      int n = 0;
      while (now - last >= STEP_MS && n < 4)
      {
         step();
         last = last + STEP_MS;
         n++;
      }
      if (now - last > 100)
         last = now;
      drawWorld();
      drawThings();
      drawHud();
      show16();
      frame++;
   }
}
)EOF";
// lcd.init() on core 0 so Bus_RGB's interrupts stay off core 1, where the script draws
static void lcdInitTask(void *done) {
  lcd.init();
  lcd.setBrightness(255);
  xTaskNotifyGive((TaskHandle_t)done);
  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);
  xTaskCreatePinnedToCore(lcdInitTask, "lcdInit", 4096, xTaskGetCurrentTaskHandle(), 5, NULL, 0);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  gameBuffer = (uint16_t *)heap_caps_calloc(480 * 240, 2, MALLOC_CAP_SPIRAM);

  bindVariable((char *)"uint32_t", (char *)"width", NULL, (void *)&_sizex);
  bindVariable((char *)"uint32_t", (char *)"height", NULL, (void *)&_sizey);
  // the script's own picture; show16() scales it into the frame buffer
  bindVariable((char *)"uint16_t *", (char *)"led16", NULL, (void *)&gameBuffer);
  bindFunction((char *)"uint16_t", (char *)"hsv2", (char *)"int,int,int", (void *)hsv16);
  bindFunction((char *)"void", (char *)"show16", NULL, (void *)show16);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
  bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
  bindFunction((char *)"int", (char *)"readKey", NULL, (void *)readKey);
  bindFunction((char *)"void", (char *)"setBrightness", (char *)"int", (void *)scriptSetBrightness);

  // the sprites, and the three worlds in colour and in brightness
  bindVariable((char *)"uint16_t *", (char *)"chromaGfx", NULL, (void *)&gfxP);
  bindVariable((char *)"uint16_t *", (char *)"worldColor", NULL, (void *)&colorP);
  bindVariable((char *)"uint8_t *", (char *)"worldLum", NULL, (void *)&lumP);

  uint32_t t0 = millis();
  static ScriptExecutable exec = parseScript(chromaQuest);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("chromaquest: %u bytes of code, compiled in %u ms\n", (unsigned)exec.exe.binary_size, (unsigned)(millis() - t0));

  // the touch screen, read on core 0
  xTaskCreatePinnedToCore(padTouchTask, "touch", 4096, NULL, 3, NULL, 0);

  exec.execute("main");
}

void loop() {
  delay(1000);
}
