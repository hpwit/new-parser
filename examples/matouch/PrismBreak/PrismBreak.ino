// PrismBreak: a brick breaker written as an ESPLiveScript script, drawing
// straight into the frame buffer (led16) of the 800x480 RGB panel at 400x240
// (setPixelsize(2)), played with a USB keyboard.
//
// Bricks with a pattern each (stripes, checks, bands, dots, chevrons, cross
// hatching), bevelled; silver ones take two hits, gold ones none. A scrolling
// diamond background in the colours of the level, a field framed by metal
// pipes, a capsule paddle with glowing ends, the ball's trail, sparks from
// the broken bricks. Capsules fall from some bricks: W wider paddle, M three
// balls, S slower ball, + a life. Five layouts, then they come back faster.
//
// Until Space is pressed, the game plays itself (demo); then:
//   Q left, D right: the paddle keeps moving that way (the keyboard sends a
//   key when it goes down, not while it is held); S stops it. Space serves.
//   Escape gives the game up (back to the demo). The arrows work too.
// After the game is over, Space starts a new one.
//
// The keys: the keyboard library calls print_char() on its USB task for
// every key; it only stores the character, and the script takes them with
// readKey() once per frame, so all the script's code runs on one task.
//
// Needs the modified LovyanGFX Bus_RGB driver (double frame buffer with show(),
// _frame_buffer, pixel_size, offset), FastLED (CHSV) and the ESP32_USB_Host_HID
// library with the key hook (see the README's "A USB keyboard driving a
// script"). Matouch7.h is the panel configuration (Waveshare ESP32-S3-Touch-LCD
// 800x480 pinout).
//
// For the ESP32-S3-Touch-LCD-7: a 7-inch 800x480 RGB touch screen with an
// ESP32-S3 (16MB flash, 8MB OPI PSRAM). The keyboard goes on its USB host port.
// Board: ESP32S3 Dev Module, PSRAM "OPI PSRAM", Flash Size 16MB.
#include "Matouch7.h"
#include "FastLED.h"
#include "keyboard.h"
#include "script_executable.h"
#include "binding.h"

LGFX lcd;
uint16_t *frame_buffer;
static uint32_t _sizex = 800;
static uint32_t _sizey = 480;

// HSV -> RGB565, byte swapped like LovyanGFX's frame buffer
static uint16_t hsv16(uint8_t h, uint8_t s, uint8_t v) {
  CRGB d = CHSV(h, s, v);
  uint16_t j = lcd.color565(d.red, d.green, d.blue);
  return ((j & 0xff) << 8) | ((j & 0xFF00) >> 8);
}

void show16() {
  lcd._bus_instance.show();
  frame_buffer = (uint16_t *)lcd._bus_instance._frame_buffer;
  lcd._panel_instance.resetWriteBuffer();
}

void clearscreen() {
  memset(frame_buffer, 0, 800 * 480 * 2);
}

// pixel_size h: each cell is shown as h x h pixels, width x height cells
void setPixelsize(int h) {
  clearscreen();
  show16();
  clearscreen();
  lcd._bus_instance.offset = 0;
  lcd._bus_instance.pixel_size = h;
  _sizex = 800 / h;
  _sizey = 480 / h;
}

uint32_t scriptMillis() {
  return millis();
}

uint32_t scriptRand(uint32_t n) {
  return esp_random() % n;
}

// the keys, from the keyboard's task to the script's
#define KEY_QUEUE 16
static volatile uint8_t keyQueue[KEY_QUEUE];
static volatile uint8_t keyHead = 0, keyTail = 0;

void print_char(key_return k) {
  // Escape (no character of its own) quits the game; the arrows are Z Q S D
  uint8_t c = k.key_char;
  if (k.key_code == HID_KEY_ESC)
    c = 27;
  else if (k.key_code == HID_KEY_UP)
    c = 'z';
  else if (k.key_code == HID_KEY_LEFT)
    c = 'q';
  else if (k.key_code == HID_KEY_DOWN)
    c = 's';
  else if (k.key_code == HID_KEY_RIGHT)
    c = 'd';
  if (c == 0)
    return;
  uint8_t next = (keyHead + 1) % KEY_QUEUE;
  if (next != keyTail) {  // full: the key is dropped
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

char prismBreak[] = R"EOF(
// the frame buffer's width, whatever the pixel size
#define STRIDE 800
#define SW 400
#define SH 240
// the field: 13 columns of 16 x 8 bricks, 10 rows
#define FX 96
#define FY 20
#define FW 208
#define FB 238
#define COLS 13
#define ROWS 10
#define BRICK_Y 40
#define PADDLE_Y 222
#define LEVELS 5
#define BALLS 3
#define CAPS 4
#define SPARKS 48
// a step is 1/60 s
#define STEP_MS 16

// the layouts: 0 empty, 1-7 colours, 8 silver (2 hits), 9 gold (unbreakable)
uint8_t layouts[650] = {
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
   3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
   5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 7, 8, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 8, 7, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 8, 7, 6, 5, 6, 7, 8, 0, 0, 0,
   0, 0, 8, 7, 6, 5, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 8, 7, 6, 5, 6, 7, 8, 0, 0, 0,
   0, 0, 0, 0, 8, 7, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 7, 8, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 1,
   2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 1, 1, 2, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9,
   3, 3, 4, 4, 5, 5, 6, 6, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 1, 1, 2, 2, 3, 3, 4,
   9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 5, 5, 6, 6, 1, 1, 2, 2, 3, 3, 4, 4, 5,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 9, 9, 9, 0, 0, 0, 0, 0, 9, 9, 9, 9,
   9, 7, 7, 7, 7, 0, 0, 0, 6, 6, 6, 6, 9, 9, 7, 8, 8, 7, 0, 0, 0, 6, 8, 8, 6, 9,
   9, 7, 7, 7, 7, 0, 0, 0, 6, 6, 6, 6, 9, 9, 9, 9, 9, 0, 0, 0, 0, 0, 9, 9, 9, 9,
   0, 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 3, 2, 1, 0, 0, 0, 0, 0, 8, 0, 8, 0,
   5, 4, 3, 2, 1, 0, 0, 0, 8, 0, 8, 0, 8, 6, 5, 4, 3, 2, 1, 0, 0, 0, 8, 0, 8, 0,
   7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 5, 0, 5, 0, 5, 0, 5, 0, 5, 0, 5, 0, 5,
   0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
uint8_t bricks[130];
uint8_t hits[130];
uint8_t flash[130];
int bricksLeft;

uint16_t tex[1280];          // a 16 x 8 texture per brick type (0 unused)
uint16_t bgTile[1024];       // the 32 x 32 background tile of the level
uint16_t panelRow[240];

// the balls
float bx[3];
float by[3];
float bvx[3];
float bvy[3];
int bon[3];
float trailX[18];
float trailY[18];
float ballSpeed;
int stuck;                   // the ball waits on the paddle
int stuckTimer;
float stuckOffset;

// the paddle
float padX;
int padW;
int padDir;
int wideTimer;
int slowTimer;

// falling capsules: W wider, M three balls, S slower, + a life
int capX[4];
float capY[4];
int capType[4];
int capOn[4];

// sparks of a broken brick
float spX[48];
float spY[48];
float spVX[48];
float spVY[48];
int spLife[48];
uint16_t spColor[48];

int score;
int high;
int lives;
int level;
int phase;                   // 0 get ready, 1 playing, 2 ball lost, 3 level cleared, 4 game over
int phaseTimer;
int demo;
int frame;
int popup;
int popupTimer;
uint32_t seed;

// an 8x8 font, bold and rounded: 0-9, A-Z, then . : / and +; 8 rows a
// character, the leftmost pixel in the highest bit (ChromaQuest/tools/font8.py)
uint8_t font8[320] = {126, 199, 207, 219, 243, 227, 126, 0, 24, 56, 120, 24, 24, 24, 126, 0, 126, 195, 6, 28, 112, 192, 255, 0, 126, 195, 3, 62, 3, 195, 126, 0, 14, 30, 54, 102, 255, 6, 6, 0, 255, 192, 254, 3, 3, 195, 126, 0, 62, 96, 192, 254, 195, 195, 126, 0, 255, 195, 6, 12, 24, 24, 24, 0, 126, 195, 195, 126, 195, 195, 126, 0, 126, 195, 195, 127, 3, 6, 124, 0, 60, 102, 195, 195, 255, 195, 195, 0, 254, 195, 195, 254, 195, 195, 254, 0, 126, 195, 192, 192, 192, 195, 126, 0, 252, 198, 195, 195, 195, 198, 252, 0, 255, 192, 192, 252, 192, 192, 255, 0, 255, 192, 192, 252, 192, 192, 192, 0, 126, 195, 192, 207, 195, 195, 126, 0, 195, 195, 195, 255, 195, 195, 195, 0, 126, 24, 24, 24, 24, 24, 126, 0, 63, 6, 6, 6, 198, 198, 124, 0, 198, 204, 216, 240, 216, 204, 198, 0, 192, 192, 192, 192, 192, 192, 255, 0, 195, 231, 255, 219, 195, 195, 195, 0, 195, 227, 243, 219, 207, 199, 195, 0, 126, 195, 195, 195, 195, 195, 126, 0, 254, 195, 195, 254, 192, 192, 192, 0, 126, 195, 195, 195, 203, 198, 123, 0, 254, 195, 195, 254, 204, 198, 195, 0, 126, 195, 192, 126, 3, 195, 126, 0, 255, 24, 24, 24, 24, 24, 24, 0, 195, 195, 195, 195, 195, 195, 126, 0, 195, 195, 195, 195, 102, 60, 24, 0, 195, 195, 195, 219, 255, 231, 195, 0, 195, 102, 60, 24, 60, 102, 195, 0, 195, 195, 102, 60, 24, 24, 24, 0, 255, 6, 12, 24, 48, 96, 255, 0, 0, 0, 0, 0, 0, 192, 192, 0, 0, 192, 192, 0, 0, 192, 192, 0, 3, 6, 12, 24, 48, 96, 192, 0, 0, 24, 24, 126, 24, 24, 0, 0};
// how each row shades the text's colour, in sixteenths: scaled by rowMul,
// then lifted toward white by rowLift; a dark band, then a bright edge
int rowMul[8] = {16, 16, 16, 7, 16, 15, 12, 9};
int rowLift[8] = {10, 6, 2, 0, 5, 0, 0, 0};
// and the small 3x5 font, 0-9, A-Z then +: 5 rows of 3 bits, the top row first
int tiny[37] = {31599, 11415, 29671, 29647, 23497, 31183, 31215, 29330, 31727, 31695,
                11245, 27566, 14627, 27502, 31143, 31140, 14699, 23533, 29847, 4714,
                23861, 18727, 24557, 27501, 11114, 27556, 11123, 27565, 14478, 29842,
                23407, 23402, 23549, 23213, 23186, 29351, 1488};

int brickHue[8] = {0, 0, 22, 50, 96, 130, 165, 210};
int capHue[4] = {165, 96, 40, 0};
uint16_t white;
uint16_t grey;
uint16_t black;
uint16_t yellow;
uint16_t pipeA;
uint16_t pipeB;
uint16_t pipeGlow;
uint16_t padBody;
uint16_t padLight;
uint16_t padDark;
uint16_t padEnd;
uint16_t ballColor;
uint16_t trailColor;
uint16_t title[10];
uint16_t capColor[4];
uint16_t capDark[4];
uint16_t brickSpark[10];

uint32_t nextRandom(uint32_t n)
{
   return rand(n);
}

float mySqrt(float a)
{
   if (a <= 0)
      return 0;
   float x = a;
   if (x < 1)
      x = 1;
   for (int i = 0; i < 10; i++)
      x = (x + a / x) * 0.5;
   return x;
}

// the brick textures: a pattern per colour, a bevel around
void makeTextures()
{
   for (int t = 1; t < 10; t++)
   {
      int h = brickHue[t % 8];
      for (int y = 0; y < 8; y++)
         for (int x = 0; x < 16; x++)
         {
            int v = 200;
            int s = 230;
            bool lit = false;
            if (t == 1 && (x + y) % 4 < 2)
               lit = true;
            if (t == 2 && (x / 2 + y / 2) % 2 == 0)
               lit = true;
            if (t == 3 && y % 3 == 1)
               lit = true;
            if (t == 4 && x % 4 == 1 && y % 3 == 1)
               lit = true;
            if (t == 5 && x % 3 == 0)
               lit = true;
            if (t == 6)
            {
               int d = y - 3;
               if (d < 0)
                  d = -d;
               if ((x + d) % 5 == 0)
                  lit = true;
            }
            if (t == 7 && ((x + y) % 4 == 0 || (x - y + 16) % 4 == 0))
               lit = true;
            if (lit)
            {
               v = 255;
               s = 150;
            }
            if (t == 8)
            {
               // silver: a metal gradient and a diagonal shine
               h = 160;
               s = 30;
               v = 230 - y * 14;
               if (x - y * 2 > 3 && x - y * 2 < 7)
                  v = 255;
            }
            if (t == 9)
            {
               // gold: a gradient and two rivets
               h = 38;
               s = 220;
               v = 255 - y * 12;
               if ((x == 3 || x == 12) && (y == 3 || y == 4))
                  v = 120;
            }
            if (y == 0 || x == 0)
            {
               s = s / 2;
               v = 255;
            }
            if (y == 6 || x == 14)
               v = v / 2;
            uint16_t c = hsv2(h, s, v);
            if (y == 7 || x == 15)
               c = black;
            tex[t * 128 + y * 16 + x] = c;
         }
      brickSpark[t] = hsv2(h, 120, 255);
   }
}

// the background: diamonds in rings, a little lit dot at their corners,
// in the colours of the level
void makeBackground()
{
   int hue = (150 + level * 37) % 256;
   for (int y = 0; y < 32; y++)
      for (int x = 0; x < 32; x++)
      {
         int dx = x - 16;
         if (dx < 0)
            dx = -dx;
         int dy = y - 16;
         if (dy < 0)
            dy = -dy;
         int d = dx + dy;
         int v = 70 + (d % 8) * 5;
         int s = 255;
         if (d % 8 == 0)
            v = 140;
         if (d == 16)
         {
            v = 190;
            s = 170;
         }
         if (dx == 16 && dy == 16)
            v = 255;
         bgTile[y * 32 + x] = hsv2(hue + d / 2, s, v);
      }
}

void makeColors()
{
   white = hsv2(0, 0, 255);
   grey = hsv2(170, 40, 160);
   black = hsv2(0, 0, 0);
   yellow = hsv2(50, 220, 255);
   pipeA = hsv2(170, 60, 210);
   pipeB = hsv2(170, 120, 90);
   pipeGlow = hsv2(128, 220, 255);
   padBody = hsv2(160, 40, 220);
   padLight = hsv2(0, 0, 255);
   padDark = hsv2(165, 120, 90);
   padEnd = hsv2(0, 230, 255);
   ballColor = hsv2(40, 60, 255);
   trailColor = hsv2(30, 200, 200);
   for (int i = 0; i < 10; i++)
      title[i] = hsv2(i * 25, 210, 255);
   for (int i = 0; i < 4; i++)
   {
      capColor[i] = hsv2(capHue[i], 200, 255);
      capDark[i] = hsv2(capHue[i], 255, 110);
   }
   for (int y = 0; y < 240; y++)
      panelRow[y] = hsv2(175, 230, 16 + y / 10);
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

// a character at (x, y), sc pixels a dot: a dark shadow down and to the
// right, then each row in its shade of c; a run of dots is one rectangle
void drawGlyph(int x, int y, int g, int sc, uint16_t c)
{
   int v = ((c & 255) << 8) | (c >> 8);
   int cr = (v >> 11) & 31;
   int cg = (v >> 5) & 63;
   int cb = v & 31;
   for (int pass = 0; pass < 2; pass++)
   {
      int off = sc;
      if (pass == 1)
         off = 0;
      for (int r = 0; r < 8; r++)
      {
         int bits = font8[g * 8 + r];
         uint16_t rc = 16904;       // the shadow: a dark blue
         if (pass == 1)
         {
            int rr = (cr * rowMul[r]) >> 4;
            int rg = (cg * rowMul[r]) >> 4;
            int rb = (cb * rowMul[r]) >> 4;
            rr = rr + (((31 - rr) * rowLift[r]) >> 4);
            rg = rg + (((63 - rg) * rowLift[r]) >> 4);
            rb = rb + (((31 - rb) * rowLift[r]) >> 4);
            int w = (rr << 11) | (rg << 5) | rb;
            rc = ((w & 255) << 8) | (w >> 8);
         }
         int b = 0;
         while (b < 8)
         {
            if (((bits >> (7 - b)) & 1) == 1)
            {
               int e = b + 1;
               while (e < 8 && ((bits >> (7 - e)) & 1) == 1)
                  e++;
               fillRect(x + b * sc + off, y + r * sc + off, (e - b) * sc, sc, rc);
               b = e;
            }
            else
               b++;
         }
      }
   }
}

// a character of the small font, flat
void drawTinyGlyph(int x, int y, int g, int sc, uint16_t c)
{
   int bits = tiny[g];
   for (int r = 0; r < 5; r++)
      for (int b = 0; b < 3; b++)
         if ((bits >> ((4 - r) * 3 + 2 - b)) & 1)
            fillRect(x + b * sc, y + r * sc, sc, sc, c);
}

// capitals, digits, + and /
void drawText(int x, int y, char *s, int sc, uint16_t c)
{
   int i = 0;
   while (s[i] != 0)
   {
      int ch = s[i];
      if (ch >= 48 && ch <= 57)
         drawGlyph(x, y, ch - 48, sc, c);
      if (ch >= 65 && ch <= 90)
         drawGlyph(x, y, ch - 55, sc, c);
      if (ch == 47)
         drawGlyph(x, y, 38, sc, c);
      if (ch == 43)
         drawGlyph(x, y, 39, sc, c);
      x = x + 9 * sc;
      i++;
   }
}

int digits[8];
void drawNumber(int x, int y, int v, int sc, uint16_t c)
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
      drawGlyph(x, y, digits[k], sc, c);
      x = x + 9 * sc;
   }
}

void loadLevel()
{
   int base = ((level - 1) % LEVELS) * 130;
   bricksLeft = 0;
   for (int i = 0; i < 130; i++)
   {
      bricks[i] = layouts[base + i];
      hits[i] = 1;
      if (bricks[i] == 8)
         hits[i] = 2;
      flash[i] = 0;
      if (bricks[i] != 0 && bricks[i] != 9)
         bricksLeft++;
   }
   makeBackground();
}

void serve()
{
   for (int b = 0; b < BALLS; b++)
      bon[b] = 0;
   bon[0] = 1;
   stuck = 1;
   stuckTimer = 120;
   stuckOffset = 4;
   padW = 36;
   wideTimer = 0;
   slowTimer = 0;
   padDir = 0;
   ballSpeed = 2.0 + (level - 1) * 0.25;
   for (int c = 0; c < CAPS; c++)
      capOn[c] = 0;
   for (int k = 0; k < 18; k++)
   {
      trailX[k] = padX;
      trailY[k] = PADDLE_Y - 3;
   }
   phase = 0;
   phaseTimer = 90;
}

void newGame(int asDemo)
{
   demo = asDemo;
   score = 0;
   lives = 3;
   level = 1;
   padX = FX + FW / 2;
   loadLevel();
   serve();
}

void addScore(int n)
{
   score = score + n;
   if (score > high)
      high = score;
}

void burst(int i)
{
   int c = i % COLS;
   int r = i / COLS;
   float x = FX + c * 16 + 8;
   float y = BRICK_Y + r * 8 + 4;
   int made = 0;
   for (int k = 0; k < SPARKS && made < 6; k++)
      if (spLife[k] <= 0)
      {
         spX[k] = x;
         spY[k] = y;
         int rx = nextRandom(200);
         int ry = nextRandom(200);
         spVX[k] = (rx - 100) * 0.02;
         spVY[k] = (ry - 130) * 0.02;
         spLife[k] = 20 + nextRandom(12);
         spColor[k] = brickSpark[bricks[i]];
         made++;
      }
}

void dropCapsule(int i)
{
   if (nextRandom(100) >= 18)
      return;
   for (int c = 0; c < CAPS; c++)
      if (!capOn[c])
      {
         capOn[c] = 1;
         // the row and column as ints first: in an expression stored in a
         // float, / and % between ints are done in float
         int col = i % COLS;
         int row = i / COLS;
         capX[c] = FX + col * 16 + 8;
         capY[c] = BRICK_Y + row * 8 + 4;
         capType[c] = nextRandom(4);
         return;
      }
}

// a ball touched brick i
void hitBrick(int i)
{
   int t = bricks[i];
   flash[i] = 6;
   if (t == 9)
      return;
   hits[i] = hits[i] - 1;
   if (hits[i] > 0)
   {
      addScore(50);
      return;
   }
   addScore(50 + 10 * (8 - t % 8));
   if (t == 8)
      addScore(50 * level);
   burst(i);
   dropCapsule(i);
   bricks[i] = 0;
   bricksLeft--;
   if (ballSpeed < 4.2)
      ballSpeed = ballSpeed + 0.015;
   if (bricksLeft == 0)
   {
      phase = 3;
      phaseTimer = 150;
   }
}

// the brick under the ball's box (2 pixels around x, y), or -1
int brickAt(float x, float y)
{
   int x0 = (int)(x - 2 - FX);
   int x1 = (int)(x + 1 - FX);
   int y0 = (int)(y - 2 - BRICK_Y);
   int y1 = (int)(y + 1 - BRICK_Y);
   if (x1 < 0 || y1 < 0 || x0 >= FW || y0 >= ROWS * 8)
      return -1;
   for (int yy = y0; yy <= y1; yy = yy + 3)
      for (int xx = x0; xx <= x1; xx = xx + 3)
         if (xx >= 0 && yy >= 0 && xx < FW && yy < ROWS * 8)
         {
            int i = (yy / 8) * COLS + xx / 16;
            if (bricks[i] != 0)
               return i;
         }
   return -1;
}

void setSpeed(int b, float speed)
{
   float s = mySqrt(bvx[b] * bvx[b] + bvy[b] * bvy[b]);
   if (s > 0.01)
   {
      bvx[b] = bvx[b] * speed / s;
      bvy[b] = bvy[b] * speed / s;
   }
}

void moveBall(int b)
{
   float speed = ballSpeed;
   if (slowTimer > 0)
      speed = speed * 0.65;
   setSpeed(b, speed);
   int n = (int)(speed) + 1;
   for (int k = 0; k < n; k++)
   {
      float nx = bx[b] + bvx[b] / n;
      int i = brickAt(nx, by[b]);
      if (i >= 0)
      {
         bvx[b] = -bvx[b];
         hitBrick(i);
      }
      else if (nx < FX + 2 || nx > FX + FW - 2)
         bvx[b] = -bvx[b];
      else
         bx[b] = nx;
      float ny = by[b] + bvy[b] / n;
      i = brickAt(bx[b], ny);
      if (i >= 0)
      {
         bvy[b] = -bvy[b];
         hitBrick(i);
      }
      else if (ny < FY + 2)
         bvy[b] = -bvy[b];
      else
         by[b] = ny;
      // the paddle: the farther from its middle, the wider the angle
      if (bvy[b] > 0 && by[b] >= PADDLE_Y - 2 && by[b] <= PADDLE_Y + 3)
      {
         int reach = padW / 2 + 2;
         float off = (bx[b] - padX) / reach;
         if (off >= -1.0 && off <= 1.0)
         {
            bvx[b] = speed * off * 0.85;
            bvy[b] = -mySqrt(speed * speed - bvx[b] * bvx[b]);
            by[b] = PADDLE_Y - 3;
         }
      }
      if (by[b] > FB)
      {
         bon[b] = 0;
         return;
      }
   }
}

// the demo player: under the ball that comes down first
void autopilot()
{
   float target = FX + FW / 2;
   float best = 1000;
   for (int b = 0; b < BALLS; b++)
      if (bon[b] && bvy[b] > 0)
      {
         float t = (PADDLE_Y - by[b]) / bvy[b];
         if (t < best)
         {
            best = t;
            // where it will be, with the walls folded back
            float x = bx[b] + bvx[b] * t - FX;
            float w = FW;
            while (x < 0 || x > w)
            {
               if (x < 0)
                  x = -x;
               if (x > w)
                  x = 2 * w - x;
            }
            int aim = (b * 7 - 7) + ((frame / 90) % 3 - 1) * 6;
            target = FX + x + aim;
         }
      }
   if (best > 999)
      for (int c = 0; c < CAPS; c++)
         if (capOn[c] && capType[c] != 2)
            target = capX[c];
   padDir = 0;
   if (target < padX - 3)
      padDir = -1;
   if (target > padX + 3)
      padDir = 1;
}

void catchCapsule(int t)
{
   addScore(100);
   if (t == 0)
   {
      padW = 52;
      wideTimer = 900;
   }
   if (t == 1)
      for (int b = 0; b < BALLS; b++)
         if (!bon[b])
            for (int a = 0; a < BALLS; a++)
               if (bon[a] && !bon[b])
               {
                  bon[b] = 1;
                  bx[b] = bx[a];
                  by[b] = by[a];
                  bvx[b] = -bvx[a] + (b - 1) * 0.8;
                  bvy[b] = bvy[a];
               }
   if (t == 2)
      slowTimer = 600;
   if (t == 3 && lives < 6)
      lives++;
   popup = t;
   popupTimer = 60;
}

// one step of 1/60 s
void step()
{
   for (int i = 0; i < 130; i++)
      if (flash[i] > 0)
         flash[i] = flash[i] - 1;
   for (int k = 0; k < SPARKS; k++)
      if (spLife[k] > 0)
      {
         spLife[k] = spLife[k] - 1;
         spX[k] = spX[k] + spVX[k];
         spY[k] = spY[k] + spVY[k];
         spVY[k] = spVY[k] + 0.08;
      }
   if (popupTimer > 0)
      popupTimer--;
   if (phase != 1)
   {
      phaseTimer--;
      if (phaseTimer > 0)
         return;
      if (phase == 0)
         phase = 1;
      else if (phase == 2)
      {
         lives--;
         if (lives == 0)
         {
            phase = 4;
            phaseTimer = 300;
            return;
         }
         serve();
      }
      else if (phase == 3)
      {
         level++;
         loadLevel();
         serve();
      }
      else if (phase == 4)
         newGame(1);
      return;
   }
   if (demo)
      autopilot();
   // the paddle keeps going the way it was sent, up to the walls
   int half = padW / 2;
   padX = padX + padDir * 3.2;
   if (padX < FX + half)
   {
      padX = FX + half;
      padDir = 0;
   }
   if (padX > FX + FW - half)
   {
      padX = FX + FW - half;
      padDir = 0;
   }
   if (wideTimer > 0)
   {
      wideTimer--;
      if (wideTimer == 0)
         padW = 36;
   }
   if (slowTimer > 0)
      slowTimer--;
   if (stuck)
   {
      bx[0] = padX + stuckOffset;
      by[0] = PADDLE_Y - 3;
      stuckTimer--;
      if (stuckTimer <= 0)
         stuck = 0;
      bvx[0] = 0.8;
      bvy[0] = -1.8;
   }
   else
   {
      int alive = 0;
      for (int b = 0; b < BALLS; b++)
         if (bon[b])
         {
            moveBall(b);
            if (bon[b])
               alive++;
         }
      if (phase == 1 && alive == 0)
      {
         phase = 2;
         phaseTimer = 70;
      }
   }
   for (int k = 17; k >= 3; k--)
   {
      trailX[k] = trailX[k - 3];
      trailY[k] = trailY[k - 3];
   }
   for (int b = 0; b < BALLS; b++)
   {
      trailX[b] = bx[b];
      trailY[b] = by[b];
   }
   for (int c = 0; c < CAPS; c++)
      if (capOn[c])
      {
         capY[c] = capY[c] + 0.9;
         if (capY[c] >= PADDLE_Y - 4 && capY[c] <= PADDLE_Y + 4 && capX[c] > padX - half - 6 && capX[c] < padX + half + 6)
         {
            capOn[c] = 0;
            catchCapsule(capType[c]);
         }
         if (capY[c] > FB)
            capOn[c] = 0;
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
   if (k == 113 || k == 81)
      padDir = -1;
   if (k == 100 || k == 68)
      padDir = 1;
   if (k == 115 || k == 83)
      padDir = 0;
   if (k == 32 && stuck)
      stuck = 0;
}

void drawBackground()
{
   int scroll = (frame / 3) % 32;
   for (int y = FY; y < FB; y++)
   {
      int row = y * STRIDE + FX;
      int ty = ((y + 32 - scroll) % 32) * 32;
      for (int x = 0; x < FW; x++)
         led16[row + x] = bgTile[ty + (x + 8) % 32];
   }
   // the side panels
   for (int y = 0; y < SH; y++)
   {
      uint16_t c = panelRow[y];
      int row = y * STRIDE;
      for (int x = 0; x < FX - 6; x++)
         led16[row + x] = c;
      for (int x = FX + FW + 6; x < SW; x++)
         led16[row + x] = c;
   }
   for (int y = 0; y < FY - 4; y++)
   {
      int row = y * STRIDE;
      for (int x = FX - 6; x < FX + FW + 6; x++)
         led16[row + x] = panelRow[y];
   }
}

// the frame: three metal pipes, a glowing line inside
void drawFrame()
{
   fillRect(FX - 6, FY - 4, 6, SH - FY + 4, pipeB);
   fillRect(FX - 5, FY - 4, 2, SH - FY + 4, pipeA);
   fillRect(FX - 1, FY, 1, SH - FY, pipeGlow);
   fillRect(FX + FW, FY - 4, 6, SH - FY + 4, pipeB);
   fillRect(FX + FW + 3, FY - 4, 2, SH - FY + 4, pipeA);
   fillRect(FX + FW, FY, 1, SH - FY, pipeGlow);
   fillRect(FX - 6, FY - 4, FW + 12, 4, pipeB);
   fillRect(FX - 6, FY - 3, FW + 12, 1, pipeA);
   fillRect(FX, FY - 1, FW, 1, pipeGlow);
   // joints on the pipes
   for (int y = FY + 20; y < SH; y = y + 40)
   {
      fillRect(FX - 7, y, 8, 4, pipeA);
      fillRect(FX + FW - 1, y, 8, 4, pipeA);
   }
}

void drawBricks()
{
   for (int i = 0; i < 130; i++)
   {
      int t = bricks[i];
      if (t != 0)
      {
         int x = FX + (i % COLS) * 16;
         int y = BRICK_Y + (i / COLS) * 8;
         int base = t * 128;
         if (flash[i] > 0)
            fillRect(x, y, 15, 7, white);
         else
            for (int j = 0; j < 8; j++)
            {
               int row = (y + j) * STRIDE + x;
               int tr = base + j * 16;
               for (int k = 0; k < 16; k++)
                  led16[row + k] = tex[tr + k];
            }
         // a cracked silver brick
         if (t == 8 && hits[i] == 1)
         {
            led16[(y + 2) * STRIDE + x + 6] = black;
            led16[(y + 3) * STRIDE + x + 7] = black;
            led16[(y + 4) * STRIDE + x + 7] = black;
            led16[(y + 5) * STRIDE + x + 8] = black;
         }
      }
   }
}

void drawPaddle()
{
   int w = padW;
   int x = (int)(padX) - w / 2;
   int y = PADDLE_Y;
   // a capsule: rounded ends that glow, a metal body with a shine
   fillRect(x + 2, y, w - 4, 6, padBody);
   fillRect(x + 2, y, w - 4, 1, padLight);
   fillRect(x + 2, y + 1, w - 4, 1, padLight);
   fillRect(x + 2, y + 5, w - 4, 1, padDark);
   fillRect(x + w / 2 - 6, y + 2, 12, 2, pipeGlow);
   uint16_t e = padEnd;
   if ((frame / 6) % 2 == 0)
      e = yellow;
   fillRect(x, y + 1, 6, 4, e);
   fillRect(x + 1, y, 4, 6, e);
   fillRect(x + w - 6, y + 1, 6, 4, e);
   fillRect(x + w - 5, y, 4, 6, e);
}

void drawBalls()
{
   for (int k = 3; k < 18; k++)
   {
      int b = k % 3;
      if (bon[b])
      {
         int r = 2 - k / 9;
         fillRect((int)(trailX[k]) - r / 2, (int)(trailY[k]) - r / 2, r + 1, r + 1, trailColor);
      }
   }
   for (int b = 0; b < BALLS; b++)
      if (bon[b])
      {
         int x = (int)(bx[b]);
         int y = (int)(by[b]);
         fillRect(x - 2, y - 1, 4, 2, ballColor);
         fillRect(x - 1, y - 2, 2, 4, ballColor);
         led16[(y - 1) * STRIDE + x - 1] = white;
      }
}

void drawCapsule(int x, int y, int t)
{
   fillRect(x - 6, y - 2, 12, 5, capColor[t]);
   fillRect(x - 5, y - 3, 10, 7, capColor[t]);
   fillRect(x - 5, y + 2, 10, 2, capDark[t]);
   fillRect(x - 4, y - 3, 8, 1, white);
   int g = 32;                // W
   if (t == 1)
      g = 22;                 // M
   if (t == 2)
      g = 28;                 // S
   if (t == 3)
      g = 36;                 // +
   drawTinyGlyph(x - 1, y - 2, g, 1, black);
}

void drawCapsules()
{
   for (int c = 0; c < CAPS; c++)
      if (capOn[c])
         drawCapsule(capX[c], (int)(capY[c]), capType[c]);
}

void drawSparks()
{
   for (int k = 0; k < SPARKS; k++)
      if (spLife[k] > 0)
      {
         int x = (int)(spX[k]);
         int y = (int)(spY[k]);
         if (x > FX && x < FX + FW - 1 && y > FY && y < FB)
            fillRect(x, y, 2, 2, spColor[k]);
      }
}

void drawPanels()
{
   // PRISM / BREAK, a colour per letter
   drawText(2, 6, "P", 2, title[0]);
   drawText(19, 6, "R", 2, title[1]);
   drawText(36, 6, "I", 2, title[2]);
   drawText(53, 6, "S", 2, title[3]);
   drawText(70, 6, "M", 2, title[4]);
   drawText(2, 28, "B", 2, title[5]);
   drawText(19, 28, "R", 2, title[6]);
   drawText(36, 28, "E", 2, title[7]);
   drawText(53, 28, "A", 2, title[8]);
   drawText(70, 28, "K", 2, title[9]);
   drawText(10, 60, "SCORE", 1, grey);
   drawNumber(10, 72, score, 1, white);
   drawText(10, 100, "HIGH", 1, grey);
   drawNumber(10, 112, high, 1, white);
   drawText(312, 10, "LIVES", 1, grey);
   for (int i = 0; i < lives - 1 && i < 5; i++)
   {
      fillRect(314 + i * 14, 28, 11, 4, padBody);
      fillRect(314 + i * 14, 28, 2, 4, padEnd);
      fillRect(323 + i * 14, 28, 2, 4, padEnd);
   }
   drawText(312, 44, "LEVEL", 1, grey);
   drawNumber(312, 56, level, 1, white);
   // what the capsules do
   drawCapsule(320, 92, 0);
   drawText(332, 88, "WIDE", 1, capColor[0]);
   drawCapsule(320, 108, 1);
   drawText(332, 104, "MULTI", 1, capColor[1]);
   drawCapsule(320, 124, 2);
   drawText(332, 120, "SLOW", 1, capColor[2]);
   drawCapsule(320, 140, 3);
   drawText(332, 136, "LIFE", 1, capColor[3]);
   if (demo)
   {
      if ((frame / 20) % 2 == 0)
      {
         drawText(10, 190, "PRESS", 1, yellow);
         drawText(10, 204, "SPACE", 1, yellow);
      }
   }
   else
   {
      drawText(10, 180, "Q D MOVE", 1, grey);
      drawText(10, 191, "S STOP", 1, grey);
      drawText(10, 202, "SPC SERVE", 1, grey);
      drawText(10, 213, "ESC QUIT", 1, grey);
   }
   if (popupTimer > 0)
   {
      if (popup == 0)
         drawText(312, 170, "WIDE", 1, capColor[0]);
      if (popup == 1)
         drawText(312, 170, "MULTI", 1, capColor[1]);
      if (popup == 2)
         drawText(312, 170, "SLOW", 1, capColor[2]);
      if (popup == 3)
         drawText(312, 170, "LIFE", 1, capColor[3]);
   }
   if (phase == 0)
   {
      drawText(FX + 73, 160, "LEVEL", 1, yellow);
      drawNumber(FX + 127, 160, level, 1, yellow);
   }
   if (phase == 3)
      drawText(FX + 73, 160, "CLEARED", 1, yellow);
   if (phase == 4)
   {
      fillRect(FX + 40, 150, FW - 80, 26, black);
      drawText(FX + 64, 159, "GAME OVER", 1, hsv2(0, 230, 255));
   }
}

int main()
{
   setPixelsize(2);           // 400x240, each 2x2 pixels
   makeColors();
   makeTextures();
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
      drawBackground();
      drawFrame();
      drawBricks();
      drawCapsules();
      drawSparks();
      drawPaddle();
      drawBalls();
      drawPanels();
      show16();
      frame++;
   }
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

  bindVariable((char *)"uint32_t", (char *)"width", NULL, (void *)&_sizex);
  bindVariable((char *)"uint32_t", (char *)"height", NULL, (void *)&_sizey);
  // a pointer, not an array: show16() swaps the frame buffers
  bindVariable((char *)"uint16_t *", (char *)"led16", NULL, (void *)&frame_buffer);
  bindFunction((char *)"uint16_t", (char *)"hsv2", (char *)"int,int,int", (void *)hsv16);
  bindFunction((char *)"void", (char *)"show16", NULL, (void *)show16);
  bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", (void *)setPixelsize);
  bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
  bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
  bindFunction((char *)"int", (char *)"readKey", NULL, (void *)readKey);

  static ScriptExecutable exec = parseScript(prismBreak);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("prism break: %u bytes of code\n", (unsigned)exec.exe.binary_size);

  // the USB keyboard: keys go to print_char()
  hid_keyboard_print_char = print_char;
  app_main_keyboard();

  exec.execute("main");
}

void loop() {
  delay(1000);
}
