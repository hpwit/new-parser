// 60 bouncing balls on a 128x96 CRGB matrix, drawn four ways, each frame
// timed with micros():
//   - native float:  the balls in C++, compiled by the Arduino IDE;
//   - native fixed:  the same in C++ with 16.16 fixed-point integers;
//   - script float:  the BouncingBalls script (a struct with a
//                    constructor and two methods, an array of them);
//   - script ffloat: the same script with ffloat (BouncingBallsFfloat).
// Each frame clears the matrix, moves every ball and draws it, calling
// hsv() -- FastLED's CHSV unless USE_FASTLED_HSV is 0 -- for every pixel
// lit. The balls start from the same rand() seed in every version; the
// sketch prints the average and best time per frame over 100 frames, the
// number of pixels lit and a checksum of the last frame -- the native and
// script versions of a type give the same checksum, i.e. exactly the same
// pictures.
//
// The shade is computed as (distance / r^2)^2 rather than distance^2 / r^4,
// which would pass ffloat's +-32768 on the bigger balls.
#include "script_executable.h"
#include "binding.h"
#include <string.h>

// Must match the script's own #define width / #define height.
#define LED_WIDTH 128
#define LED_HEIGHT 96

// hsv(), for the scripts and the native code alike: FastLED's CHSV ->
// CRGB conversion (hsv2rgb_rainbow) -- what a sketch driving a real panel
// uses -- or, with USE_FASTLED_HSV set to 0 (FastLED not installed), a
// small integer conversion. The timings in README.md use FastLED's.
#define USE_FASTLED_HSV 1
#if USE_FASTLED_HSV
#include <FastLED.h>
#define HSV_KIND "FastLED CHSV"
CRGB scriptHsv(int h, int s, int v)
{
   return CHSV(h, s, v);
}
#else
#define HSV_KIND "integer hsv"
// 3 bytes, red/green/blue -- the compiler's built-in CRGB layout.
struct CRGB
{
   uint8_t red, green, blue;
};
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
#endif

// The script's `external CRGB leds[12288];` binds to this buffer.
static CRGB ledsBuffer[LED_HEIGHT][LED_WIDTH];

uint32_t scriptRand(uint32_t mod)
{
   return rand() % (mod + 1);
}

void scriptClear()
{
   memset(ledsBuffer, 0, sizeof(ledsBuffer));
}

void scriptShow() {}

static int countLit()
{
   int lit = 0;
   for (int y = 0; y < LED_HEIGHT; y++)
      for (int x = 0; x < LED_WIDTH; x++)
      {
         CRGB c = ledsBuffer[y][x];
         if (c.red || c.green || c.blue)
            lit++;
      }
   return lit;
}

static uint32_t checksum()
{
   uint32_t h = 2166136261u;
   uint8_t *p = (uint8_t *)ledsBuffer;
   for (size_t i = 0; i < sizeof(ledsBuffer); i++)
      h = (h ^ p[i]) * 16777619u;
   return h;
}

char scriptFloat[] = R"EOF(
#define max_nb_balls 200
#define NUM_BALLS 60
#define rmax 12
#define rmin 4
#define width 128
#define height 96
#define panel_width 128

external CRGB leds[12288];
external CRGB hsv(int h, int s, int v);
external uint32_t rand(uint32_t mod);
external void clear();
external void show();

struct ball
{
   float vx, vy, xc, yc, r;
   int color;
   void drawBall()
   {
      int startx = xc - r;
      int r2 = r * r;
      int starty = yc - r;
      int _xc = xc;
      int _yc = yc;
      for (int i = startx; i <= _xc; i++)
         for (int j = starty; j <= _yc; j++)
         {
            int distance = (i - xc) ^ 2 + (j - yc) ^ 2;
            if (distance <= r2)
            {
               // distance^2 / r^4 as (distance / r^2)^2: both would pass
               // float's +-32768 on the bigger balls (r up to ~16).
               float q = distance;
               q = q / r2;
               float shade = 255 * (1 - q * q);
               int v = shade;
               CRGB cc = hsv(color, 255, v);
               leds[j * panel_width + i] = cc;
               leds[j * panel_width + (int)(2 * xc - i)] = cc;
               leds[(int)(2 * xc - i) + (int)(2 * yc - j) * panel_width] = cc;
               leds[i + (int)(2 * yc - j) * panel_width] = cc;
            }
         }
   }
   void updateBall()
   {
      xc += vx;
      yc += vy;
      if (xc >= (width - r - 1))
      {
         xc = width - r - 1;
         vx = -vx;
      }
      if (xc < (r + 1))
      {
         xc = r + 1;
         vx = -vx;
      }
      if (yc >= height - r - 1)
      {
         yc = height - r - 1;
         vy = -vy;
      }
      if (yc < r + 1)
      {
         yc = r + 1;
         vy = -vy;
      }
      drawBall();
   }
   ball()
   {
      vx = rand(300) / 255 + 0.7;
      vy = rand(280) / 255 + 0.5;
      r = (rmax - rmin) * (rand(280) / 180) + rmin;
      xc = width / 2 * (rand(280) / 255 + 0.3) + 15;
      yc = height / 2 * (rand(280) / 255 + 0.3) + 15;
      color = rand(255);
   }
};

ball Balls[max_nb_balls];

void main()
{
   int num = NUM_BALLS;
   if (num > max_nb_balls)
      num = max_nb_balls;
   clear();
   for (int i = 0; i < num; i++)
      Balls[i].updateBall();
}
)EOF";
char scriptFfloat[] = R"EOF(
#define max_nb_balls 200
#define NUM_BALLS 60
#define rmax 12
#define rmin 4
#define width 128
#define height 96
#define panel_width 128

external CRGB leds[12288];
external CRGB hsv(int h, int s, int v);
external uint32_t rand(uint32_t mod);
external void clear();
external void show();

struct ball
{
   ffloat vx, vy, xc, yc, r;
   int color;
   void drawBall()
   {
      int startx = xc - r;
      int r2 = r * r;
      int starty = yc - r;
      int _xc = xc;
      int _yc = yc;
      for (int i = startx; i <= _xc; i++)
         for (int j = starty; j <= _yc; j++)
         {
            int distance = (i - xc) ^ 2 + (j - yc) ^ 2;
            if (distance <= r2)
            {
               // distance^2 / r^4 as (distance / r^2)^2: both would pass
               // ffloat's +-32768 on the bigger balls (r up to ~16).
               ffloat q = distance;
               q = q / r2;
               ffloat shade = 255 * (1 - q * q);
               int v = shade;
               CRGB cc = hsv(color, 255, v);
               leds[j * panel_width + i] = cc;
               leds[j * panel_width + (int)(2 * xc - i)] = cc;
               leds[(int)(2 * xc - i) + (int)(2 * yc - j) * panel_width] = cc;
               leds[i + (int)(2 * yc - j) * panel_width] = cc;
            }
         }
   }
   void updateBall()
   {
      xc += vx;
      yc += vy;
      if (xc >= (width - r - 1))
      {
         xc = width - r - 1;
         vx = -vx;
      }
      if (xc < (r + 1))
      {
         xc = r + 1;
         vx = -vx;
      }
      if (yc >= height - r - 1)
      {
         yc = height - r - 1;
         vy = -vy;
      }
      if (yc < r + 1)
      {
         yc = r + 1;
         vy = -vy;
      }
      drawBall();
   }
   ball()
   {
      vx = rand(300) / 255 + 0.7;
      vy = rand(280) / 255 + 0.5;
      r = (rmax - rmin) * (rand(280) / 180) + rmin;
      xc = width / 2 * (rand(280) / 255 + 0.3) + 15;
      yc = height / 2 * (rand(280) / 255 + 0.3) + 15;
      color = rand(255);
   }
};

ball Balls[max_nb_balls];

void main()
{
   int num = NUM_BALLS;
   if (num > max_nb_balls)
      num = max_nb_balls;
   clear();
   for (int i = 0; i < num; i++)
      Balls[i].updateBall();
}
)EOF";


// ---- the same frame compiled natively: float, and 16.16 fixed point ----
#define NB 60
struct NBallF { float vx, vy, xc, yc, r; int color; };
static NBallF nbf[NB];
static void initF()
{
   for (int k = 0; k < NB; k++)
   {
      NBallF &b = nbf[k];
      b.vx = scriptRand(300) / 255.0f + 0.7f;
      b.vy = scriptRand(280) / 255.0f + 0.5f;
      b.r = (12 - 4) * (scriptRand(280) / 180.0f) + 4;
      b.xc = 128 / 2 * (scriptRand(280) / 255.0f + 0.3f) + 15;
      b.yc = 96 / 2 * (scriptRand(280) / 255.0f + 0.3f) + 15;
      b.color = scriptRand(255);
   }
}
static void frameF()
{
   scriptClear();
   CRGB *leds = &ledsBuffer[0][0];
   for (int k = 0; k < NB; k++)
   {
      NBallF &b = nbf[k];
      b.xc += b.vx; b.yc += b.vy;
      if (b.xc >= 128 - b.r - 1) { b.xc = 128 - b.r - 1; b.vx = -b.vx; }
      if (b.xc < b.r + 1) { b.xc = b.r + 1; b.vx = -b.vx; }
      if (b.yc >= 96 - b.r - 1) { b.yc = 96 - b.r - 1; b.vy = -b.vy; }
      if (b.yc < b.r + 1) { b.yc = b.r + 1; b.vy = -b.vy; }
      int startx = b.xc - b.r, r2 = b.r * b.r, starty = b.yc - b.r, _xc = b.xc, _yc = b.yc;
      for (int i = startx; i <= _xc; i++)
         for (int j = starty; j <= _yc; j++)
         {
            float dx = i - b.xc, dy = j - b.yc;
            int distance = dx * dx + dy * dy;
            if (distance <= r2)
            {
               float q = (float)distance / r2;
               int v = 255 * (1 - q * q);
               CRGB cc = scriptHsv(b.color, 255, v);
               int mx = (int)(2 * b.xc - i), my = (int)(2 * b.yc - j);
               leds[j * 128 + i] = cc;
               leds[j * 128 + mx] = cc;
               leds[mx + my * 128] = cc;
               leds[i + my * 128] = cc;
            }
         }
   }
}
typedef int32_t fx; // 16.16
#define FX(v) ((fx)((v) * 65536.0))
#define fxmul(a, b) ((fx)(((int64_t)(a) * (b)) >> 16))
struct NBallX { fx vx, vy, xc, yc, r; int color; };
static NBallX nbx[NB];
static void initX()
{
   for (int k = 0; k < NB; k++)
   {
      NBallX &b = nbx[k];
      b.vx = ((int64_t)scriptRand(300) << 16) / 255 + FX(0.7);
      b.vy = ((int64_t)scriptRand(280) << 16) / 255 + FX(0.5);
      b.r = (12 - 4) * (((int64_t)scriptRand(280) << 16) / 180) + FX(4);
      b.xc = 64 * (((int64_t)scriptRand(280) << 16) / 255 + FX(0.3)) + FX(15);
      b.yc = 48 * (((int64_t)scriptRand(280) << 16) / 255 + FX(0.3)) + FX(15);
      b.color = scriptRand(255);
   }
}
static void frameX()
{
   scriptClear();
   CRGB *leds = &ledsBuffer[0][0];
   for (int k = 0; k < NB; k++)
   {
      NBallX &b = nbx[k];
      b.xc += b.vx; b.yc += b.vy;
      if (b.xc >= FX(128 - 1) - b.r) { b.xc = FX(128 - 1) - b.r; b.vx = -b.vx; }
      if (b.xc < b.r + FX(1)) { b.xc = b.r + FX(1); b.vx = -b.vx; }
      if (b.yc >= FX(96 - 1) - b.r) { b.yc = FX(96 - 1) - b.r; b.vy = -b.vy; }
      if (b.yc < b.r + FX(1)) { b.yc = b.r + FX(1); b.vy = -b.vy; }
      int startx = (b.xc - b.r) >> 16, r2 = fxmul(b.r, b.r) >> 16, starty = (b.yc - b.r) >> 16;
      int _xc = b.xc >> 16, _yc = b.yc >> 16;
      for (int i = startx; i <= _xc; i++)
         for (int j = starty; j <= _yc; j++)
         {
            fx dx = (i << 16) - b.xc, dy = (j << 16) - b.yc;
            int distance = (fxmul(dx, dx) + fxmul(dy, dy)) >> 16;
            if (distance <= r2)
            {
               fx q = ((int64_t)distance << 16) / r2;
               int v = (255 * (FX(1) - fxmul(q, q))) >> 16;
               CRGB cc = scriptHsv(b.color, 255, v);
               int mx = (2 * b.xc - (i << 16)) >> 16, my = (2 * b.yc - (j << 16)) >> 16;
               leds[j * 128 + i] = cc;
               leds[j * 128 + mx] = cc;
               leds[mx + my * 128] = cc;
               leds[i + my * 128] = cc;
            }
         }
   }
}
static void benchNative(const char *name, void (*init)(), void (*frame)())
{
   srand(42);
   init();
   frame();
   const int N = 100;
   uint32_t best = 0xffffffff, total = 0;
   long litSum = 0;
   for (int i = 0; i < N; i++)
   {
      uint32_t t0 = micros();
      frame();
      uint32_t dt = micros() - t0;
      total += dt;
      if (dt < best) best = dt;
      litSum += countLit();
   }
   printf("BENCH %s: %d frames, avg %lu us, best %lu us, avg lit %ld, last checksum %08lx\n", name, N,
          (unsigned long)(total / N), (unsigned long)best, litSum / N, (unsigned long)checksum());
}

static void bench(const char *name, char *s)
{
   ScriptExecutable exec = parseScript(s);
   if (!exec.isExeExists())
   {
      printf("%s: failed to compile/load\n", name);
      return;
   }
   srand(42);
   exec.execute("main"); // constructors + first frame, not timed
   const int N = 100;
   uint32_t best = 0xffffffff, worst = 0, total = 0;
   long litSum = 0;
   for (int i = 0; i < N; i++)
   {
      uint32_t t0 = micros();
      exec.executeOnly("main");
      uint32_t dt = micros() - t0;
      total += dt;
      if (dt < best) best = dt;
      if (dt > worst) worst = dt;
      litSum += countLit();
      if ((i & 15) == 15) yield();
   }
   printf("BENCH %s: %d frames, avg %lu us, best %lu us, worst %lu us, avg lit %ld, last checksum %08lx\n", name, N,
          (unsigned long)(total / N), (unsigned long)best, (unsigned long)worst, litSum / N, (unsigned long)checksum());
   exec.free();
}

void setup()
{
   Serial.begin(115200);
   delay(1000);
   bindVariable((char *)"CRGB", (char *)"leds", (char *)"[]", (void *)ledsBuffer);
   bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", (void *)scriptHsv);
   bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
   bindFunction((char *)"void", (char *)"clear", NULL, (void *)scriptClear);
   bindFunction((char *)"void", (char *)"show", NULL, (void *)scriptShow);
   printf("CPU %lu MHz, %s\n", (unsigned long)getCpuFrequencyMhz(), HSV_KIND);
   benchNative("native float", initF, frameF);
   benchNative("native fixed", initX, frameX);
   bench("script float ", scriptFloat);
   bench("script ffloat", scriptFfloat);
   benchNative("native float", initF, frameF);
   benchNative("native fixed", initX, frameX);
   bench("script float ", scriptFloat);
   bench("script ffloat", scriptFfloat);
   printf("BENCH DONE\n");
}

void loop() {}
