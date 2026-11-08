// NeonRacer: an anti-gravity race written as an ESPLiveScript script, drawing
// straight into the frame buffer (led16) of the 800x480 RGB panel at 400x240
// (setPixelsize(2)), played with a USB keyboard.
//
// A hover ship races three rivals over 3 laps of a neon track at sunset. The
// track is a list of segments, each with its curve and its height, drawn in
// perspective from the ship (each segment projected, the road filled between
// two segments, the hills hiding what is behind them); the city and the sun
// slide with the curves. Yellow chevrons are boost pads; off the track the
// ship slows down in sparks; running into a rival from behind bounces off.
// Lap time, best lap, place, speed.
//
// Until Space is pressed, the race runs by itself (demo); then:
//   Q left, D right (each press steers a bit more), S brakes; the ship
//   accelerates by itself. After the finish, Space starts a new race.
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
  if (k.key_char == 0)
    return;
  uint8_t next = (keyHead + 1) % KEY_QUEUE;
  if (next != keyTail) {  // full: the key is dropped
    keyQueue[keyHead] = k.key_char;
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

char neonRacer[] = R"EOF(
// the frame buffer's width, whatever the pixel size
#define STRIDE 800
// the screen, in script pixels
#define SW 400
#define SH 240
// the track: segments of SEG_LEN, the road 2 x ROAD_W wide
#define NSEG 1000
#define SEG_LEN 200
#define ROAD_W 2000
#define CAM_H 1000
#define DRAW 70
#define LAPS 3
#define RIVALS 3
// a step is 1/60 s
#define STEP_MS 16

float camDepth = 0.84;
float playerZ = 840.0;         // the ship is this far in front of the camera
float trackLength = 200000.0;  // NSEG * SEG_LEN
float maxSpeed = 200.0;        // world units per step
float boostSpeed = 290.0;

// the track, built from sections: length in segments, curve, height at the end
int secLen[16] = {50, 85, 40, 75, 50, 100, 35, 65, 85, 50, 75, 60, 65, 50, 65, 50};
float secCurve[16] = {0, 2.2, 0, -3.0, 0, 1.6, -1.0, 3.2, 0, -2.4, 0, 2.6, -1.8, 0, 1.2, 0};
float secHill[16] = {0, 0, 3000, 3000, -1000, 0, 1600, 4000, 0, -800, 2400, 0, -1600, 0, 1200, 0};
uint8_t secBoost[16] = {0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0};

float curve[1000];
float worldY[1001];
uint8_t boost[1000];

// what the road drawing kept per segment, for the sprites
float segX[70];
float segY[70];
float segScale[70];
int segClip[70];
int segIndex[70];

// the player
float position;                // the camera's place along the track
float playerX;                 // -1..1 across the road
float speed;
float steerVel;
float bank;
int lap;
int lapTime;                   // steps
int bestLap;
int raceTime;
int boostTimer;
int bump;

// the rivals
float rz[3];
float rx[3];
float rspeed[3];
float rwave[3];
int rlap[3];
int rhue[3] = {0, 96, 40};

int phase;                     // 0 countdown, 1 racing, 2 finished
int phaseTimer;
int place;
int demo;
int frame;
int skyShift;

// 3x5 pixel font: 0-9, A-Z, then . : and /
int font[39] = {31599, 11415, 29671, 29647, 23497, 31183, 31215, 29330, 31727, 31695,
                11245, 27566, 14627, 27502, 31143, 31140, 14699, 23533, 29847, 4714,
                23861, 18727, 24557, 27501, 11114, 27556, 11123, 27565, 14478, 29842,
                23407, 23402, 23549, 23213, 23186, 29351, 2, 1040, 4772};

uint16_t skyRow[240];
uint16_t floorA;
uint16_t floorB;
uint16_t floorLine;
uint16_t roadA;
uint16_t roadB;
uint16_t railA;
uint16_t railB;
uint16_t laneColor;
uint16_t boostColor;
uint16_t sunTop;
uint16_t sunBottom;
uint16_t cityColor;
uint16_t cityLight;
uint16_t white;
uint16_t grey;
uint16_t black;
uint16_t yellow;
uint16_t cyan;
uint16_t magenta;
uint16_t flame1;
uint16_t flame2;
uint16_t shipBody[4];
uint16_t shipDark[4];
uint16_t shipLight[4];
uint16_t canopy;
uint16_t shipStripe[4];
uint16_t fieldGlow;
uint16_t fieldDark;
uint8_t skyline[64];

float easeInOut(float a, float b, float t)
{
   return a + (b - a) * (t * t * (3.0 - 2.0 * t));
}

void buildTrack()
{
   int n = 0;
   float y = 0;
   worldY[0] = 0;
   for (int s = 0; s < 16; s++)
   {
      float y0 = y;
      float y1 = secHill[s];
      int len = secLen[s];
      for (int k = 0; k < len; k++)
      {
         float t = (k + 1) * 1.0 / len;
         // curves ease in and out too
         float c = secCurve[s];
         if (k < len / 4)
            c = c * k * 4.0 / len;
         if (k > len - len / 4)
            c = c * (len - k) * 4.0 / len;
         curve[n] = c;
         boost[n] = 0;
         if (secBoost[s] && k % 12 < 4 && k > 4)
            boost[n] = 1;
         worldY[n + 1] = easeInOut(y0, y1, t);
         n++;
      }
      y = y1;
   }
   // the skyline: towers of a few heights
   uint32_t h = 7;
   for (int i = 0; i < 64; i++)
   {
      h = h * 1103515245 + 12345;
      skyline[i] = 6 + (h >> 16) % 22;
      if (i % 9 == 4)
         skyline[i] = skyline[i] + 14;
   }
}

void makeColors()
{
   for (int y = 0; y < 240; y++)
   {
      // night blue at the top, magenta, then orange at the horizon
      int t = y * 255 / 120;
      if (t > 255)
         t = 255;
      int hue = 175 + t * 70 / 255;
      if (hue > 255)
         hue = hue - 256;
      skyRow[y] = hsv2(hue, 230, 60 + t * 150 / 255);
   }
   floorA = hsv2(200, 255, 120);
   floorB = hsv2(205, 255, 100);
   floorLine = hsv2(220, 200, 210);
   roadA = hsv2(170, 110, 150);
   roadB = hsv2(170, 110, 125);
   railA = hsv2(224, 220, 255);
   railB = hsv2(128, 220, 255);
   laneColor = hsv2(140, 60, 200);
   boostColor = hsv2(45, 230, 255);
   sunTop = hsv2(40, 220, 255);
   sunBottom = hsv2(240, 230, 255);
   cityColor = hsv2(190, 230, 45);
   cityLight = hsv2(45, 160, 230);
   white = hsv2(0, 0, 255);
   grey = hsv2(170, 40, 170);
   black = hsv2(0, 0, 0);
   yellow = hsv2(50, 220, 255);
   cyan = hsv2(128, 220, 255);
   magenta = hsv2(220, 230, 255);
   flame1 = hsv2(30, 255, 255);
   flame2 = hsv2(140, 160, 255);
   canopy = hsv2(160, 255, 40);
   fieldGlow = hsv2(140, 200, 255);
   fieldDark = hsv2(170, 200, 50);
   shipStripe[0] = hsv2(45, 230, 255);
   shipStripe[1] = white;
   shipStripe[2] = hsv2(45, 230, 255);
   shipStripe[3] = hsv2(200, 230, 255);
   shipBody[0] = hsv2(128, 70, 255);
   shipDark[0] = hsv2(160, 255, 110);
   shipLight[0] = hsv2(0, 0, 255);
   for (int i = 0; i < 3; i++)
   {
      shipBody[i + 1] = hsv2(rhue[i], 230, 230);
      shipDark[i + 1] = hsv2(rhue[i], 255, 110);
      shipLight[i + 1] = hsv2(rhue[i], 100, 255);
   }
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

// a horizontal run of pixels, clipped to the screen
void span(int y, int x0, int x1, uint16_t c)
{
   if (x0 < 0)
      x0 = 0;
   if (x1 > SW)
      x1 = SW;
   int row = y * STRIDE;
   for (int x = x0; x < x1; x++)
      led16[row + x] = c;
}

void drawGlyph(int x, int y, int g, int sc, uint16_t c)
{
   int bits = font[g];
   for (int r = 0; r < 5; r++)
      for (int b = 0; b < 3; b++)
         if ((bits >> ((4 - r) * 3 + 2 - b)) & 1)
            fillRect(x + b * sc, y + r * sc, sc, sc, c);
}

// capitals, digits, . : and /
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
      if (ch == 46)
         drawGlyph(x, y, 36, sc, c);
      if (ch == 58)
         drawGlyph(x, y, 37, sc, c);
      if (ch == 47)
         drawGlyph(x, y, 38, sc, c);
      x = x + 4 * sc;
      i++;
   }
}

int digits[8];
// a number, at least `minDigits` digits; returns where the text ends
int drawNumber(int x, int y, int v, int minDigits, int sc, uint16_t c)
{
   int n = 0;
   while (v > 0 || n < minDigits)
   {
      digits[n] = v % 10;
      v = v / 10;
      n++;
   }
   for (int k = n - 1; k >= 0; k--)
   {
      drawGlyph(x, y, digits[k], sc, c);
      x = x + 4 * sc;
   }
   return x;
}

// a time in steps as M:SS.T
int drawTime(int x, int y, int steps, int sc, uint16_t c)
{
   int tenths = steps / 6;
   x = drawNumber(x, y, tenths / 600, 1, sc, c);
   drawGlyph(x, y, 37, sc, c);
   x = drawNumber(x + 4 * sc, y, (tenths / 10) % 60, 2, sc, c);
   drawGlyph(x, y, 36, sc, c);
   return drawNumber(x + 4 * sc, y, tenths % 10, 1, sc, c);
}

void drawSky()
{
   for (int y = 0; y < SH; y++)
   {
      int row = y * STRIDE;
      uint16_t c = skyRow[y];
      for (int x = 0; x < SW; x++)
         led16[row + x] = c;
   }
   // the sun: a disc cut by bands that widen towards the horizon
   int cx = 200 - skyShift / 8 % 400;
   if (cx < -60)
      cx = cx + 400;
   for (int j = -44; j <= 44; j++)
   {
      int y = 82 + j;
      if (j > 4 && (j % 8) < (j - 4) / 8 + 1)
         continue;
      int w = 0;
      while ((w + 1) * (w + 1) + j * j <= 44 * 44)
         w++;
      uint16_t c = sunTop;
      if (j > 0)
         c = sunBottom;
      span(y, cx - w, cx + w, c);
   }
   // the city, sliding with the curves
   for (int x = 0; x < SW; x++)
   {
      int i = ((x + skyShift / 3) / 7) % 64;
      if (i < 0)
         i = i + 64;
      int h = skyline[i];
      fillRect(x, 120 - h, 1, h, cityColor);
      if ((x + skyShift / 3) % 7 == 3 && h > 12)
         for (int k = 4; k < h - 2; k = k + 5)
            led16[(120 - h + k) * STRIDE + x] = cityLight;
   }
}

// the road between two projected lines of the track
void drawRoad()
{
   int base = (int)(position / SEG_LEN);
   float basePercent = (position - base * SEG_LEN) / SEG_LEN;
   int pseg = (int)((position + playerZ) / SEG_LEN) % NSEG;
   float pPercent = (position + playerZ - (int)((position + playerZ) / SEG_LEN) * SEG_LEN) / SEG_LEN;
   float playerY = worldY[pseg] + (worldY[pseg + 1] - worldY[pseg]) * pPercent;
   float camY = playerY + CAM_H;
   float camX = playerX * ROAD_W;
   float x = 0;
   float dx = -(curve[base % NSEG] * basePercent);
   int maxy = SH;
   skyShift = skyShift + (int)(curve[pseg] * speed / 40);
   float prevX = 0;
   float prevY = 0;
   float prevW = 0;
   bool havePrev = false;
   for (int n = 0; n < DRAW; n++)
   {
      int i = (base + n) % NSEG;
      float looped = 0;
      if (base + n >= NSEG)
         looped = trackLength;
      float z = (base + n) * SEG_LEN - (position - looped);
      if (base + n >= NSEG && position < trackLength)
         z = (base + n) * SEG_LEN - position;
      float scale = camDepth / z;
      float sx = SW / 2 + scale * (x - camX) * SW / 2;
      float sy = SH / 2 - scale * (worldY[i] - camY) * SH / 2;
      float sw = scale * ROAD_W * SW / 2;
      segX[n] = sx;
      segY[n] = sy;
      segScale[n] = scale;
      segClip[n] = maxy;
      segIndex[n] = i;
      x = x + dx;
      dx = dx + curve[i];
      if (havePrev && z > 1)
      {
         int top = (int)(sy);
         int bottom = (int)(prevY);
         if (bottom > maxy)
            bottom = maxy;
         if (top < bottom)
         {
            int pi = (i + NSEG - 1) % NSEG;
            bool dark = false;
            if ((pi / 3) % 2 == 1)
               dark = true;
            uint16_t floorC = floorA;
            uint16_t roadC = roadA;
            uint16_t railC = railA;
            if (dark)
            {
               floorC = floorB;
               roadC = roadB;
               railC = railB;
            }
            if (pi % 6 == 0)
               floorC = floorLine;
            bool lane = false;
            if ((pi / 4) % 2 == 0)
               lane = true;
            bool pad = false;
            if (boost[pi])
               pad = true;
            for (int y = top; y < bottom; y++)
            {
               if (y >= 0)
               {
                  float t = (y - sy) / (prevY - sy);
                  float cx = sx + (prevX - sx) * t;
                  float w = sw + (prevW - sw) * t;
                  int rail = (int)(w * 0.08) + 1;
                  int l = (int)(cx - w);
                  int r = (int)(cx + w);
                  span(y, 0, l - rail, floorC);
                  span(y, l - rail, l, railC);
                  span(y, l, r, roadC);
                  span(y, r, r + rail, railC);
                  span(y, r + rail, SW, floorC);
                  if (pad)
                  {
                     // chevrons: a band that moves up the pad
                     int k = (int)(w * 0.35);
                     int off = (int)((1.0 - t) * k);
                     span(y, (int)(cx) - k + off, (int)(cx) - k + off + k / 3 + 1, boostColor);
                     span(y, (int)(cx) + k - off - k / 3 - 1, (int)(cx) + k - off, boostColor);
                  }
                  else if (lane)
                  {
                     int lw = (int)(w * 0.025) + 1;
                     span(y, (int)(cx - w / 3) - lw, (int)(cx - w / 3) + lw, laneColor);
                     span(y, (int)(cx + w / 3) - lw, (int)(cx + w / 3) + lw, laneColor);
                  }
               }
            }
            maxy = top;
         }
      }
      prevX = sx;
      prevY = sy;
      prevW = sw;
      havePrev = true;
   }
}

// an anti-gravity racer seen from behind: a low wide hull, a tail fin,
// two engine pods with the team's stripe, glowing engines, and the glow
// of the field that keeps it above the track
void drawShip(int cx, int bottom, int w, int k, int clip, int tilt)
{
   if (w < 6)
      return;
   int h = w / 3;
   int top = bottom - h;
   uint16_t body = shipBody[k];
   uint16_t dark = shipDark[k];
   uint16_t light = shipLight[k];
   uint16_t stripe = shipStripe[k];
   // the field under the hull
   int gy = bottom + h / 5 + 1;
   if (gy < clip && gy < SH)
   {
      span(gy, cx - w * 2 / 5, cx + w * 2 / 5, fieldDark);
      if ((frame + k) % 3 != 0)
         span(gy, cx - w / 4, cx + w / 4, fieldGlow);
   }
   for (int j = 0; j < h; j++)
   {
      int y = top + j;
      if (y >= 0 && y < clip && y < SH)
      {
         int lean = tilt * (h / 2 - j) / h;
         int x = cx + lean;
         // the tail fin, on top in the middle
         if (j < h * 2 / 5)
         {
            int fw = w / 36 + 1;
            span(y, x - fw, x + fw, light);
            if (j < 2)
               span(y, x - fw, x + fw, stripe);
         }
         else
         {
            // the hull: a wedge, wider at the back
            int jj = j - h * 2 / 5;
            int hh = h - h * 2 / 5;
            int half = w / 6 + (w / 4) * jj / hh;
            span(y, x - half, x + half, body);
            span(y, x - half, x - half + w / 30 + 1, light);
            span(y, x + half - w / 30 - 1, x + half, dark);
            // the cockpit, a dark slit at the front of the deck
            if (jj < hh / 3)
               span(y, x - w / 12, x + w / 12, canopy);
            // the team's stripe across the deck
            if (jj == hh / 2)
               span(y, x - half, x + half, stripe);
         }
         // the pods, on both sides in the lower half
         if (j >= h / 3)
         {
            int pw = w / 7;
            int px = w * 2 / 5;
            uint16_t pc = dark;
            if (j == h / 3 || j == h / 3 + 1)
               pc = stripe;
            span(y, x - px - pw / 2, x - px + pw / 2, pc);
            span(y, x + px - pw / 2, x + px + pw / 2, pc);
            span(y, x - px - pw / 2, x - px - pw / 2 + 1, light);
            span(y, x + px + pw / 2 - 1, x + px + pw / 2, light);
         }
      }
   }
   // the engines: a glowing ring in each pod and a smaller one in the hull
   uint16_t f = flame1;
   uint16_t g = flame2;
   if ((frame + k) % 4 < 2)
   {
      f = flame2;
      g = flame1;
   }
   int er = w / 16 + 1;
   int ey = bottom - er - 1;
   for (int j = -er; j <= er; j++)
   {
      int y = ey + j;
      if (y >= 0 && y < clip && y < SH)
      {
         int ew = 0;
         while ((ew + 1) * (ew + 1) + j * j <= er * er)
            ew++;
         int x = cx - tilt * (j + er) / (h + 1);
         span(y, x - w * 2 / 5 - ew, x - w * 2 / 5 + ew + 1, f);
         span(y, x + w * 2 / 5 - ew, x + w * 2 / 5 + ew + 1, f);
         if (ew > 1)
         {
            span(y, x - w * 2 / 5 - ew / 2, x - w * 2 / 5 + ew / 2 + 1, white);
            span(y, x + w * 2 / 5 - ew / 2, x + w * 2 / 5 + ew / 2 + 1, white);
         }
         span(y, x - ew / 2, x + ew / 2 + 1, g);
      }
   }
}

// the rivals ahead, farthest first, hidden by the hills in front of them
void drawRivals()
{
   for (int n = DRAW - 1; n > 0; n--)
      for (int r = 0; r < RIVALS; r++)
      {
         float d = rz[r] - position;
         if (d < 0)
            d = d + trackLength;
         int sn = (int)(d / SEG_LEN);
         if (sn == n)
         {
            float scale = segScale[n];
            int x = (int)(segX[n] + scale * rx[r] * ROAD_W * SW / 2);
            int w = (int)(scale * 1100 * SW / 2);
            drawShip(x, (int)(segY[n]), w, r + 1, segClip[n], 0);
         }
      }
}

void drawHud()
{
   drawText(8, 6, "LAP", 2, grey);
   int shown = lap + 1;
   if (shown > LAPS)
      shown = LAPS;
   int x = drawNumber(36, 6, shown, 1, 2, white);
   drawText(x, 6, "/", 2, grey);
   drawNumber(x + 8, 6, LAPS, 1, 2, white);
   drawText(8, 20, "TIME", 2, grey);
   drawTime(44, 20, lapTime, 2, white);
   if (bestLap > 0)
   {
      drawText(8, 34, "BEST", 2, grey);
      drawTime(44, 34, bestLap, 2, yellow);
   }
   drawText(330, 6, "POS", 2, grey);
   x = drawNumber(358, 6, place, 1, 2, white);
   drawText(x, 6, "/", 2, grey);
   drawNumber(x + 8, 6, RIVALS + 1, 1, 2, white);
   // speed: a bar going from cyan to magenta, and the number
   int kmh = (int)(speed * 2.2);
   x = drawNumber(306, 222, kmh, 3, 2, white);
   drawText(x + 4, 222, "KMH", 2, grey);
   int bars = (int)(speed * 20 / boostSpeed);
   for (int i = 0; i < 20; i++)
   {
      uint16_t c = cyan;
      if (i > 9)
         c = magenta;
      if (i > 15)
         c = yellow;
      if (i < bars)
         fillRect(8 + i * 6, 226, 4, 8, c);
      else
         fillRect(8 + i * 6, 232, 4, 2, grey);
   }
   if (boostTimer > 0 && (frame / 4) % 2 == 0)
      drawText(180, 22, "BOOST", 2, yellow);
   if (demo && (frame / 20) % 2 == 0)
      drawText(156, 6, "PRESS SPACE", 2, white);
   if (phase == 0)
   {
      int left = phaseTimer / 60 + 1;
      if (phaseTimer > 0)
         drawNumber(190, 70, left, 1, 8, yellow);
   }
   if (phase == 1 && raceTime < 45)
      drawText(176, 70, "GO", 8, cyan);
   if (phase == 2)
   {
      drawText(140, 70, "FINISH", 5, magenta);
      drawText(150, 110, "POS", 3, white);
      drawNumber(190, 110, place, 1, 3, yellow);
      drawTime(150, 132, raceTime, 3, white);
   }
}

void drawPlayer()
{
   int tilt = (int)(bank * 18);
   int y = 214;
   if (bump > 0)
      y = y - (bump / 2) % 3;
   drawShip(200, y, 120, 0, SH, tilt);
   if (playerX < -1.0 || playerX > 1.0)
      if ((frame / 2) % 2 == 0)
      {
         // sparks off the track
         for (int k = 0; k < 6; k++)
            fillRect(160 + (frame * 37 + k * 23) % 80, 206 + (k * 7 + frame) % 12, 2, 2, yellow);
      }
}

void newRace(int asDemo)
{
   demo = asDemo;
   position = 0;
   playerX = 0;
   speed = 0;
   steerVel = 0;
   bank = 0;
   lap = 0;
   lapTime = 0;
   raceTime = 0;
   boostTimer = 0;
   bump = 0;
   for (int r = 0; r < RIVALS; r++)
   {
      rz[r] = playerZ + (r + 1) * 3 * SEG_LEN;
      rx[r] = -0.5 + r * 0.5;
      rspeed[r] = 0;
      rwave[r] = r * 1.7;
      rlap[r] = 0;
   }
   phase = 0;
   phaseTimer = 180;
   place = RIVALS + 1;
}

// the demo pilot: aims at a lane, away from the rival ahead, and steers
// against the pull of the curve
void autopilot(int seg)
{
   float target = 0;
   for (int r = 0; r < RIVALS; r++)
   {
      float d = rz[r] - position - playerZ;
      if (d < 0)
         d = d + trackLength;
      if (d < SEG_LEN * 10)
      {
         if (rx[r] > 0)
            target = rx[r] - 0.75;
         else
            target = rx[r] + 0.75;
      }
   }
   int ahead = (seg + 3) % NSEG;
   float pull = curve[ahead] * speed / maxSpeed * 0.0053;
   float err = target - playerX + pull * 25;
   steerVel = steerVel + err * 0.01;
   if (steerVel > 0.03)
      steerVel = 0.03;
   if (steerVel < -0.03)
      steerVel = -0.03;
}

void handleKey(int k)
{
   if (k == 32 && (demo || phase == 2))
   {
      newRace(0);
      return;
   }
   if (demo)
      return;
   if (k == 113 || k == 81)
      steerVel = steerVel - 0.012;
   if (k == 100 || k == 68)
      steerVel = steerVel + 0.012;
   if (k == 115 || k == 83)
      speed = speed * 0.8;
}

float progress(int laps, float z)
{
   return laps * trackLength + z;
}

// one step of 1/60 s
void step()
{
   if (bump > 0)
      bump--;
   if (phase == 0)
   {
      phaseTimer--;
      if (phaseTimer <= 0)
         phase = 1;
      return;
   }
   if (phase == 2)
   {
      phaseTimer--;
      speed = speed * 0.98;
      position = position + speed;
      if (position >= trackLength)
         position = position - trackLength;
      if (phaseTimer <= 0 && demo)
         newRace(1);
      if (phaseTimer <= 0 && !demo)
         phaseTimer = 0;
      return;
   }
   raceTime++;
   lapTime++;
   int seg = (int)((position + playerZ) / SEG_LEN) % NSEG;
   if (demo)
      autopilot(seg);
   // speed: the ship accelerates by itself, slower off the track
   float top = maxSpeed;
   if (boostTimer > 0)
   {
      top = boostSpeed;
      boostTimer--;
   }
   if (speed < top)
      speed = speed + 1.6;
   else
      speed = speed - 2.0;
   if (playerX < -1.0 || playerX > 1.0)
   {
      if (speed > maxSpeed / 3)
         speed = speed - 4.0;
   }
   if (boost[seg] && playerX > -0.4 && playerX < 0.4)
      boostTimer = 90;
   // steering and the pull of the curve
   float pct = speed / maxSpeed;
   playerX = playerX + steerVel * 3.0 * pct;
   playerX = playerX - curve[seg] * pct * pct * 0.0053;
   steerVel = steerVel * 0.9;
   bank = bank * 0.8 + steerVel * 20.0 * 0.2;
   if (playerX < -2.0)
      playerX = -2.0;
   if (playerX > 2.0)
      playerX = 2.0;
   position = position + speed;
   if (position >= trackLength)
   {
      position = position - trackLength;
      lap++;
      if (bestLap == 0 || lapTime < bestLap)
         bestLap = lapTime;
      lapTime = 0;
      if (lap >= LAPS)
      {
         phase = 2;
         phaseTimer = 360;
      }
   }
   // the rivals: each its top speed and its way of weaving
   int ahead = 0;
   for (int r = 0; r < RIVALS; r++)
   {
      float rtop = maxSpeed * (0.95 + r * 0.035);
      if (rspeed[r] < rtop)
         rspeed[r] = rspeed[r] + 1.2;
      rwave[r] = rwave[r] + 0.012 + r * 0.004;
      float w = rwave[r] - (int)(rwave[r] / 4.0) * 4.0;
      if (w > 2.0)
         w = 4.0 - w;
      rx[r] = -0.6 + w * 0.6;
      rz[r] = rz[r] + rspeed[r];
      if (rz[r] >= trackLength)
      {
         rz[r] = rz[r] - trackLength;
         rlap[r] = rlap[r] + 1;
      }
      // caught up: the player bounces off and slows down
      float d = rz[r] - position - playerZ;
      if (d < -trackLength / 2)
         d = d + trackLength;
      if (d > trackLength / 2)
         d = d - trackLength;
      float side = playerX - rx[r];
      if (d > 0 && d < SEG_LEN * 0.9 && side > -0.35 && side < 0.35 && speed > rspeed[r])
      {
         speed = rspeed[r] * 0.8;
         if (side < 0)
            steerVel = -0.02;
         else
            steerVel = 0.02;
         bump = 20;
      }
      if (progress(rlap[r], rz[r]) > progress(lap, position + playerZ))
         ahead++;
   }
   place = ahead + 1;
}

int main()
{
   setPixelsize(2);           // 400x240, each 2x2 pixels
   makeColors();
   buildTrack();
   bestLap = 0;
   newRace(1);
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
      drawSky();
      drawRoad();
      drawRivals();
      drawPlayer();
      drawHud();
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

  static ScriptExecutable exec = parseScript(neonRacer);
  if (!exec.isExeExists()) {
    showCompileError();
    return;
  }
  printf("neon racer: %u bytes of code\n", (unsigned)exec.exe.binary_size);

  // the USB keyboard: keys go to print_char()
  hid_keyboard_print_char = print_char;
  app_main_keyboard();

  exec.execute("main");
}

void loop() {
  delay(1000);
}
