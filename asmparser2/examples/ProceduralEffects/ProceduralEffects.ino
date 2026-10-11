// Five procedural effects for a 128x96 CRGB matrix, each a separate
// script compiled on the device: a plasma, a fire, a Voronoi pattern, a
// mandala and spinning squares.
// No stored graphics -- every pixel is computed.
//
//  - plasmaScript: four sine waves summed per pixel (two axis-aligned, one
//    diagonal, one radial around a wandering centre) and looked up in a
//    256-entry rainbow palette. sin() and hsv() are only called while
//    building the two tables; the per-pixel loop is integer-only.
//  - fireScript: a heat field two rows taller than the panel. The hidden
//    bottom row holds embers that cool every frame and are re-lit in short
//    random bursts; every visible cell becomes a slightly cooled average of
//    the three cells below it and the one two rows below, then goes through
//    a black -> red -> yellow -> white palette.
//  - voronoiScript: nine seeds wander on sine paths. Each pixel finds its
//    nearest and second-nearest seed; the difference of the two squared
//    distances, divided by twice the distance between those two seeds, is
//    the pixel's distance to the cell border, which gives borders of even
//    width that fade from near-white to the cell's colour. Inside a cell
//    only the seed and a ring pulsing around it are lit. The per-pixel
//    loop is integer-only; hsv() is called once per lit pixel.
//  - mandalaScript: a six-fold kaleidoscope. Each pixel's angle (folded
//    into one mirrored wedge) and radius come from two tables built once
//    with atan2() and hypot(); they only cover the top half of the panel,
//    since the pattern is point-symmetric about the centre. Rings warped
//    into petal shapes drift outward and swell with a slow "breath", a
//    fainter spiral layer weaves through them, and the hue follows radius
//    and time. Integer-only per pixel apart from one hsv() call.
//  - squaresScript: six squares orbit the centre while each spins on
//    itself, alternately clockwise and anticlockwise, and slowly changes
//    size. A pixel is rotated into each square's own axes with a sine
//    table used as fixed-point sin/cos; the larger of the two absolute
//    coordinates says whether it is inside and how far from the centre,
//    and the signed coordinate along one axis gives a colour gradient
//    that turns with the square. Behind them, the same test applied with
//    a rotation that grows with distance from the centre draws a dim
//    swirl of twisted concentric squares.
//
// Each script's main() draws for 20 seconds and returns, and loop()
// cycles through the five compiled executables.
//
// Host functions the scripts use, all defined below and bound in setup():
// leds[] (the frame buffer), hsv(), sin(), atan2(), hypot(), rand(),
// millis() and show().
// show() is the one place to connect a real panel driver -- see
// scriptShow().
//
// Every host-bound function is also declared `external` in the script
// text (see BouncingBalls.ino's header comment for why).
//
// Memory: the frame buffer is 128*96*3 = 36864 bytes; fireScript's heat
// field and mandalaScript's two tables are about 12.5 KB each, allocated
// when the scripts are compiled in setup(). This runs without PSRAM.
#include "script_executable.h"
#include "binding.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

// Must match the scripts' own #define width / #define height.
#define LED_WIDTH 128
#define LED_HEIGHT 96

// 3 bytes, red/green/blue, no padding -- matches this compiler's built-in
// CRGB layout.
struct CRGB
{
   uint8_t red, green, blue;
};

// Every script's `external CRGB leds[height, width];` binds to this buffer:
// row 0 is the top of the panel, column 0 the left edge.
static CRGB ledsBuffer[LED_HEIGHT][LED_WIDTH];

// Integer HSV (0-255 per channel) -> RGB, same conversion as the other
// examples.
CRGB scriptHsv(int h, int s, int v)
{
   CRGB c;
   if (s <= 0)
   {
      c.red = c.green = c.blue = (uint8_t)v;
      return c;
   }
   uint8_t region = (uint8_t)h / 43;
   uint8_t remainder = ((uint8_t)h - (region * 43)) * 6;
   uint8_t p = (uint8_t)((v * (255 - s)) >> 8);
   uint8_t q = (uint8_t)((v * (255 - ((s * remainder) >> 8))) >> 8);
   uint8_t t = (uint8_t)((v * (255 - ((s * (255 - remainder)) >> 8))) >> 8);
   switch (region)
   {
   case 0:  c.red = v; c.green = t; c.blue = p; break;
   case 1:  c.red = q; c.green = v; c.blue = p; break;
   case 2:  c.red = p; c.green = v; c.blue = t; break;
   case 3:  c.red = p; c.green = q; c.blue = v; break;
   case 4:  c.red = t; c.green = p; c.blue = v; break;
   default: c.red = v; c.green = p; c.blue = q; break;
   }
   return c;
}

float scriptSin(float rad)
{
   return sinf(rad);
}

float scriptAtan2(float y, float x)
{
   return atan2f(y, x);
}

float scriptHypot(float x, float y)
{
   return hypotf(x, y);
}

// Inclusive of mod, same convention as BouncingBalls.ino's scriptRand.
uint32_t scriptRand(uint32_t mod)
{
   return rand() % (mod + 1);
}

uint32_t scriptMillis()
{
   return millis();
}

// Called by the scripts once per finished frame. Push ledsBuffer to the
// panel here: it is LED_HEIGHT rows of LED_WIDTH pixels, 3 bytes each in
// red, green, blue order, so a driver that takes a raw RGB buffer can be
// given (uint8_t *)ledsBuffer directly. Without a driver this only reports
// the frame rate and one pixel once per second, which is enough to see
// that the scripts are running and writing into the buffer.
static uint32_t g_frames = 0;
static uint32_t g_lastReport = 0;
void scriptShow()
{
   // driver.showPixels((uint8_t *)ledsBuffer);

   g_frames++;
   uint32_t now = millis();
   if (now - g_lastReport >= 1000)
   {
      CRGB c = ledsBuffer[LED_HEIGHT - 1][LED_WIDTH / 2];
      printf("show(): %lu fps, bottom-centre pixel R%u G%u B%u\n",
             (unsigned long)g_frames, c.red, c.green, c.blue);
      g_frames = 0;
      g_lastReport = now;
   }
   // Let the idle task run so the task watchdog stays quiet while a
   // script's main() holds loop() for 20 seconds.
   yield();
}

char plasmaScript[] = R"EOF(
#define width 128
#define height 96

external CRGB leds[height, width];
external CRGB hsv(int h, int s, int v);
external float sin(float a);
external uint32_t millis();
external void show();

uint8_t wave[256];
CRGB palette[256];

void buildTables()
{
   for (int i = 0; i < 256; i++)
   {
      float a = i * 0.02454369;
      wave[i] = (int)(sin(a) * 127.0 + 128.0);
      palette[i] = hsv(i, 255, 255);
   }
}

void main()
{
   buildTables();
   uint32_t start = millis();
   int t = 0;
   while (millis() - start < 20000)
   {
      int cx = wave[t % 256] / 2;
      int cy = wave[(t * 2 + 64) % 256] * 3 / 8;
      for (int y = 0; y < height; y++)
      {
         int wy = wave[(y * 3 + t * 2) % 256];
         int dy = (y - cy) * (y - cy);
         for (int x = 0; x < width; x++)
         {
            int d = ((x - cx) * (x - cx) + dy) / 32;
            int v = wave[(x * 2 + t) % 256] + wy;
            v = v + wave[(x + y + t * 3) % 256];
            v = v + wave[(d + t * 4) % 256];
            leds[y, x] = palette[(v / 2 + t) % 256];
         }
      }
      show();
      t++;
   }
}
)EOF";

char fireScript[] = R"EOF(
#define width 128
#define height 96
#define rows 98

external CRGB leds[height, width];
external uint32_t rand(uint32_t mod);
external uint32_t millis();
external void show();

uint8_t heat[rows, width];
CRGB palette[256];

void buildPalette()
{
   for (int i = 0; i < 256; i++)
   {
      if (i < 64)
         palette[i] = CRGB(i * 4, 0, 0);
      else if (i < 160)
         palette[i] = CRGB(255, (i - 64) * 8 / 3, 0);
      else
         palette[i] = CRGB(255, 255, (i - 160) * 8 / 3);
   }
}

void feedFire()
{
   for (int x = 0; x < width; x++)
   {
      heat[height + 1, x] = heat[height + 1, x] * 13 / 16;
   }
   for (int b = 0; b < 4; b++)
   {
      int x0 = rand(127);
      int w = 2 + rand(8);
      for (int k = 0; k < w; k++)
      {
         heat[height + 1, (x0 + k) % width] = 255;
      }
   }
   for (int x = 0; x < width; x++)
   {
      heat[height, x] = heat[height + 1, x];
   }
}

void main()
{
   buildPalette();
   for (int y = 0; y < rows; y++)
   {
      for (int x = 0; x < width; x++)
      {
         heat[y, x] = 0;
      }
   }

   uint32_t start = millis();
   while (millis() - start < 20000)
   {
      feedFire();
      for (int y = 0; y < height; y++)
      {
         for (int x = 0; x < width; x++)
         {
            int xl = (x + width - 1) % width;
            int xr = (x + 1) % width;
            int s = heat[y + 1, xl] + heat[y + 1, x] + heat[y + 1, xr] + heat[y + 2, x];
            int v = s * 32 / 130;
            heat[y, x] = v;
            leds[y, x] = palette[v];
         }
      }
      show();
   }
}
)EOF";

char voronoiScript[] = R"EOF(
#define width 128
#define height 96
#define NSEEDS 9

external CRGB leds[height, width];
external CRGB hsv(int h, int s, int v);
external float sin(float a);
external uint32_t millis();
external void show();

uint8_t wave[256];
int sx[NSEEDS];
int sy[NSEEDS];
int ringIn[NSEEDS];
int ringOut[NSEEDS];
int gap[NSEEDS, NSEEDS];

int isqrt(int v)
{
   int r = 0;
   while ((r + 1) * (r + 1) <= v)
      r++;
   return r;
}

void moveSeeds(int t)
{
   for (int i = 0; i < NSEEDS; i++)
   {
      sx[i] = wave[(t * (i % 3 + 1) / 2 + i * 29) % 256] / 2;
      sy[i] = wave[(t * (i % 2 + 1) / 2 + i * 53 + 64) % 256] * 3 / 8;
      int r = 6 + wave[(t * 3 + i * 40) % 256] / 20;
      ringIn[i] = (r - 1) * (r - 1);
      ringOut[i] = (r + 1) * (r + 1);
   }
   for (int i = 0; i < NSEEDS; i++)
   {
      for (int j = 0; j < NSEEDS; j++)
      {
         int dx = sx[i] - sx[j];
         int dy = sy[i] - sy[j];
         gap[i, j] = 2 * isqrt(dx * dx + dy * dy) + 1;
      }
   }
}

void main()
{
   for (int i = 0; i < 256; i++)
   {
      float a = i * 0.02454369;
      wave[i] = (int)(sin(a) * 127.0 + 128.0);
   }
   uint32_t start = millis();
   int t = 0;
   while (millis() - start < 20000)
   {
      moveSeeds(t);
      for (int y = 0; y < height; y++)
      {
         for (int x = 0; x < width; x++)
         {
            int best = 1000000;
            int second = 1000000;
            int bi = 0;
            int si = 0;
            for (int i = 0; i < NSEEDS; i++)
            {
               int dx = x - sx[i];
               int dy = y - sy[i];
               int d = dx * dx + dy * dy;
               if (d < best)
               {
                  second = best;
                  si = bi;
                  best = d;
                  bi = i;
               }
               else
               {
                  if (d < second)
                  {
                     second = d;
                     si = i;
                  }
               }
            }
            int edge = (second - best) / gap[bi, si];
            int v = 0;
            int s = 255;
            if (edge < 5)
            {
               v = 255 - edge * 40;
               s = 110 + edge * 29;
            }
            else
            {
               if (best < 6)
               {
                  v = 255;
               }
               else
               {
                  if (best >= ringIn[bi] && best <= ringOut[bi])
                     v = 170;
               }
            }
            if (v > 0)
               leds[y, x] = hsv((bi * 28 + t) % 256, s, v);
            else
               leds[y, x] = CRGB(0, 0, 0);
         }
      }
      show();
      t++;
   }
}
)EOF";

char mandalaScript[] = R"EOF(
#define width 128
#define height 96
#define half 48

external CRGB leds[height, width];
external CRGB hsv(int h, int s, int v);
external float sin(float a);
external float atan2(float y, float x);
external float hypot(float x, float y);
external uint32_t millis();
external void show();

uint8_t wave[256];
uint8_t ang[half, width];
uint8_t rad[half, width];

void buildTables()
{
   for (int i = 0; i < 256; i++)
   {
      float a = i * 0.02454369;
      wave[i] = (int)(sin(a) * 127.0 + 128.0);
   }
   for (int y = 0; y < half; y++)
   {
      for (int x = 0; x < width; x++)
      {
         float dx = x - 63.5;
         float dy = y - 47.5;
         int turn = (int)(atan2(dy, dx) * 244.46199 + 768.0);
         ang[y, x] = turn % 256;
         rad[y, x] = (int)(hypot(dx, dy));
      }
   }
}

void main()
{
   buildTables();
   uint32_t start = millis();
   int t = 0;
   while (millis() - start < 20000)
   {
      int breath = wave[(t / 2) % 256];
      int swell = 40 + breath / 3;
      int drift = 256 - t % 256;
      for (int y = 0; y < height; y++)
      {
         for (int x = 0; x < width; x++)
         {
            int ty = y;
            int tx = x;
            if (y >= half)
            {
               ty = height - 1 - y;
               tx = width - 1 - x;
            }
            int r = rad[ty, tx];
            int p = (ang[ty, tx] + t / 2) % 256;
            if (p > 127)
               p = 255 - p;
            int petal = wave[(p * 2 + 192) % 256];
            int soft = wave[(r * 5 + petal * swell / 256 + drift) % 256];
            int b = soft * soft / 255;
            b = b * b / 255;
            int b2 = wave[(p * 2 + r * 2 + t) % 256];
            b2 = b2 * b2 / 255;
            b2 = b2 * b2 / 400;
            int hue = r * 2 + petal / 4 + t;
            int v = soft / 6 + 16;
            if (b > v)
               v = b;
            if (b2 > v)
            {
               v = b2;
               hue = hue + 128;
            }
            if (r < 3)
               v = 255;
            leds[y, x] = hsv(hue % 256, 255 - v / 3, v);
         }
      }
      show();
      t++;
   }
}
)EOF";

char squaresScript[] = R"EOF(
#define width 128
#define height 96
#define NSQ 6

external CRGB leds[height, width];
external CRGB hsv(int h, int s, int v);
external float sin(float a);
external uint32_t millis();
external void show();

uint8_t wave[256];
int qx[NSQ];
int qy[NSQ];
int qc[NSQ];
int qs[NSQ];
int qsize[NSQ];

void moveSquares(int t)
{
   for (int i = 0; i < NSQ; i++)
   {
      int orbit = (t + i * 43) % 256;
      int reach = 24 + (i % 2) * 16;
      int oc = wave[(orbit + 64) % 256] - 128;
      int os = wave[orbit] - 128;
      qx[i] = 64 + reach * oc / 127;
      qy[i] = 48 + reach * os / 160;
      int spin = (t * 3 + i * 40) % 256;
      if (i % 2 == 1)
         spin = 255 - spin;
      qc[i] = wave[(spin + 64) % 256] - 128;
      qs[i] = wave[spin] - 128;
      qsize[i] = (9 + wave[(t * 2 + i * 60) % 256] / 40) * 127;
   }
}

void main()
{
   for (int i = 0; i < 256; i++)
   {
      float a = i * 0.02454369;
      wave[i] = (int)(sin(a) * 127.0 + 128.0);
   }
   uint32_t start = millis();
   int t = 0;
   while (millis() - start < 20000)
   {
      moveSquares(t);
      int flow = 256 - (t * 2) % 256;
      for (int y = 0; y < height; y++)
      {
         for (int x = 0; x < width; x++)
         {
            int dx = x - 64;
            int dy = y - 48;
            int twist = (t + (dx * dx + dy * dy) / 48) % 256;
            int c = wave[(twist + 64) % 256] - 128;
            int s = wave[twist] - 128;
            int u = dx * c + dy * s;
            int w = dy * c - dx * s;
            if (u < 0)
               u = 0 - u;
            if (w < 0)
               w = 0 - w;
            int m = u;
            if (w > m)
               m = w;
            m = m / 127;
            int band = wave[(m * 10 + flow) % 256];
            int v = band * band / 520;
            int hue = m * 3 + t;
            int sat = 255;
            for (int i = 0; i < NSQ; i++)
            {
               dx = x - qx[i];
               dy = y - qy[i];
               u = dx * qc[i] + dy * qs[i];
               w = dy * qc[i] - dx * qs[i];
               int across = u;
               if (u < 0)
                  u = 0 - u;
               if (w < 0)
                  w = 0 - w;
               m = u;
               if (w > m)
                  m = w;
               if (m < qsize[i])
               {
                  int g = m * 255 / qsize[i];
                  v = 110 + g * 145 / 255;
                  hue = i * 43 + 64 + across * 56 / qsize[i] + t * 2;
                  sat = 255 - g * g / 600;
               }
            }
            leds[y, x] = hsv((hue + 512) % 256, sat, v);
         }
      }
      show();
      t++;
   }
}
)EOF";

static ScriptExecutable *g_plasma = NULL;
static ScriptExecutable *g_fire = NULL;
static ScriptExecutable *g_voronoi = NULL;
static ScriptExecutable *g_mandala = NULL;
static ScriptExecutable *g_squares = NULL;

void setup()
{
   Serial.begin(115200);

   bindVariable((char *)"CRGB", (char *)"leds", (char *)"[]", (void *)ledsBuffer);
   bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", (void *)scriptHsv);
   bindFunction((char *)"float", (char *)"sin", (char *)"float", (void *)scriptSin);
   bindFunction((char *)"float", (char *)"atan2", (char *)"float,float", (void *)scriptAtan2);
   bindFunction((char *)"float", (char *)"hypot", (char *)"float,float", (void *)scriptHypot);
   bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
   bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
   bindFunction((char *)"void", (char *)"show", NULL, (void *)scriptShow);

   // ScriptExecutable can't be copied or moved, so all five are kept as
   // function-local statics and reached from loop() through pointers.
   static ScriptExecutable plasma = parseScript(plasmaScript);
   static ScriptExecutable fire = parseScript(fireScript);
   static ScriptExecutable voronoi = parseScript(voronoiScript);
   static ScriptExecutable mandala = parseScript(mandalaScript);
   static ScriptExecutable squares = parseScript(squaresScript);
   if (plasma.isExeExists())
      g_plasma = &plasma;
   else
      printf("plasma script failed to compile/load\n");
   if (fire.isExeExists())
      g_fire = &fire;
   else
      printf("fire script failed to compile/load\n");
   if (voronoi.isExeExists())
      g_voronoi = &voronoi;
   else
      printf("voronoi script failed to compile/load\n");
   if (mandala.isExeExists())
      g_mandala = &mandala;
   else
      printf("mandala script failed to compile/load\n");
   if (squares.isExeExists())
      g_squares = &squares;
   else
      printf("squares script failed to compile/load\n");
}

void loop()
{
   if (g_plasma != NULL)
   {
      printf("********** plasma **********\n");
      g_plasma->execute("main");
   }
   if (g_fire != NULL)
   {
      printf("********** fire **********\n");
      g_fire->execute("main");
   }
   if (g_voronoi != NULL)
   {
      printf("********** voronoi **********\n");
      g_voronoi->execute("main");
   }
   if (g_mandala != NULL)
   {
      printf("********** mandala **********\n");
      g_mandala->execute("main");
   }
   if (g_squares != NULL)
   {
      printf("********** squares **********\n");
      g_squares->execute("main");
   }
   if (g_plasma == NULL && g_fire == NULL && g_voronoi == NULL && g_mandala == NULL && g_squares == NULL)
      delay(1000);
}
