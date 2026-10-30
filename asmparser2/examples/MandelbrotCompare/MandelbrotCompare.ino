// The Mandelbrot set on a 128x96 CRGB buffer computed four ways, each
// frame timed with micros():
//   - native float:  the algorithm in C++, compiled by the Arduino IDE;
//   - native fixed:  the same in C++ with 16.16 fixed-point integers, the
//                    arithmetic the compiler generates for ffloat;
//   - script float:  the MandelbrotFfloat script with float;
//   - script ffloat: the MandelbrotFfloat script as it is.
// All four call the same hsv() and write the same buffer. Each is run 5
// times; the sketch prints the best and average time per frame, the number
// of pixels inside the set and a checksum of the buffer -- the native and
// script versions of a type should give the same checksum, i.e. exactly
// the same picture.
//
// Measured on an ESP32 at 240MHz:
//   native float  32.4ms   inside 3318  checksum e19ee723
//   native fixed  28.9ms   inside 3293  checksum d0dc310f
//   script float  66.9ms   inside 3318  checksum e19ee723
//   script ffloat 72.4ms   inside 3293  checksum d0dc310f
//
// The native float code uses float literals (2.7f): in C++ a plain 2.7 is
// a double, computed in software and much slower, while the script's
// float is single precision. Nothing is sent to leds here (show() does
// nothing), so the times are the computation only.
#include "script_executable.h"
#include "binding.h"
#include <string.h>

#define LED_WIDTH 128
#define LED_HEIGHT 96
#define MAX_ITERATIONS 50
#define RUNS 5

// 3 bytes, red/green/blue -- the compiler's built-in CRGB layout.
struct CRGB
{
   uint8_t red, green, blue;
};

// The scripts' `external CRGB leds[height, width];` binds to this buffer.
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

// ---- the scripts: one frame per call of main() ----

char scriptFloat[] = R"EOF(
#define maxIterations 50
#define width 128
#define height 96

external CRGB leds[height, width];
external CRGB hsv(int h, int s, int v);

void render2D(int x1, int y1)
{
   float x0 = x1 * 2.7 / width - 2.1;
   float y0 = y1 * 2.2 / height - 1.1;
   float x = 0;
   float y = 0;
   float x2 = 0;
   float y2 = 0;
   int iter = 0;
   while (iter < maxIterations && x2 + y2 <= 4)
   {
      float fX = x2 - y2 + x0;
      y = 2 * x * y + y0;
      x = fX;
      x2 = x * x;
      y2 = y * y;
      iter++;
   }
   if (iter < maxIterations)
   {
      leds[y1 * width + x1] = hsv((iter * 255 / maxIterations), 255, 255);
   }
   else
   {
      leds[y1 * width + x1] = CRGB(0, 0, 0);
   }
}

void main()
{
   for (int i = 0; i < width; i++)
   {
      for (int j = 0; j < height; j++)
      {
         render2D(i, j);
      }
   }
}
)EOF";

char scriptFfloat[] = R"EOF(
#define maxIterations 50
#define width 128
#define height 96

external CRGB leds[height, width];
external CRGB hsv(int h, int s, int v);

void render2D(int x1, int y1)
{
   ffloat x0 = x1 * 2.7 / width - 2.1;
   ffloat y0 = y1 * 2.2 / height - 1.1;
   ffloat x = 0;
   ffloat y = 0;
   ffloat x2 = 0;
   ffloat y2 = 0;
   int iter = 0;
   while (iter < maxIterations && x2 + y2 <= 4)
   {
      ffloat fX = x2 - y2 + x0;
      y = 2 * x * y + y0;
      x = fX;
      x2 = x * x;
      y2 = y * y;
      iter++;
   }
   if (iter < maxIterations)
   {
      leds[y1 * width + x1] = hsv((iter * 255 / maxIterations), 255, 255);
   }
   else
   {
      leds[y1 * width + x1] = CRGB(0, 0, 0);
   }
}

void main()
{
   for (int i = 0; i < width; i++)
   {
      for (int j = 0; j < height; j++)
      {
         render2D(i, j);
      }
   }
}
)EOF";

// ---- the same Mandelbrot compiled natively ----

static void nativeRenderFloat(int x1, int y1)
{
   float x0 = x1 * 2.7f / LED_WIDTH - 2.1f;
   float y0 = y1 * 2.2f / LED_HEIGHT - 1.1f;
   float x = 0, y = 0, x2 = 0, y2 = 0;
   int iter = 0;
   while (iter < MAX_ITERATIONS && x2 + y2 <= 4)
   {
      float fX = x2 - y2 + x0;
      y = 2 * x * y + y0;
      x = fX;
      x2 = x * x;
      y2 = y * y;
      iter++;
   }
   ledsBuffer[y1][x1] = iter < MAX_ITERATIONS ? scriptHsv(iter * 255 / MAX_ITERATIONS, 255, 255) : CRGB{0, 0, 0};
}

// 16.16 fixed point, as ffloat: constants rounded to nearest, a multiply
// is the 64-bit product shifted back by 16.
#define FIXED(v) ((int32_t)((v) * 65536.0 + ((v) < 0 ? -0.5 : 0.5)))
static inline int32_t fixedMul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }

static void nativeRenderFixed(int x1, int y1)
{
   int32_t x0 = x1 * FIXED(2.7) / LED_WIDTH - FIXED(2.1);
   int32_t y0 = y1 * FIXED(2.2) / LED_HEIGHT - FIXED(1.1);
   int32_t x = 0, y = 0, x2 = 0, y2 = 0;
   int iter = 0;
   while (iter < MAX_ITERATIONS && x2 + y2 <= FIXED(4.0))
   {
      int32_t fX = x2 - y2 + x0;
      y = fixedMul(2 * x, y) + y0;
      x = fX;
      x2 = fixedMul(x, x);
      y2 = fixedMul(y, y);
      iter++;
   }
   ledsBuffer[y1][x1] = iter < MAX_ITERATIONS ? scriptHsv(iter * 255 / MAX_ITERATIONS, 255, 255) : CRGB{0, 0, 0};
}

// ---- measuring ----

static int countInside()
{
   int inside = 0;
   for (int y = 0; y < LED_HEIGHT; y++)
      for (int x = 0; x < LED_WIDTH; x++)
      {
         CRGB c = ledsBuffer[y][x];
         if (!c.red && !c.green && !c.blue)
            inside++;
      }
   return inside;
}

// FNV-1a over the whole buffer: the same picture gives the same value.
static uint32_t checksum()
{
   uint32_t h = 2166136261u;
   uint8_t *p = (uint8_t *)ledsBuffer;
   for (size_t i = 0; i < sizeof(ledsBuffer); i++)
      h = (h ^ p[i]) * 16777619u;
   return h;
}

static void report(const char *name, uint32_t best, uint32_t total)
{
   printf("%-14s best %6lu us  avg %6lu us  inside %4d  checksum %08lx\n", name, (unsigned long)best,
          (unsigned long)(total / RUNS), countInside(), (unsigned long)checksum());
}

static void benchNative(const char *name, void (*render)(int, int))
{
   uint32_t best = 0xffffffff, total = 0;
   for (int r = 0; r < RUNS; r++)
   {
      memset(ledsBuffer, 0x55, sizeof(ledsBuffer)); // so a missed pixel shows in the checksum
      uint32_t t0 = micros();
      for (int x = 0; x < LED_WIDTH; x++)
         for (int y = 0; y < LED_HEIGHT; y++)
            render(x, y);
      uint32_t dt = micros() - t0;
      total += dt;
      if (dt < best)
         best = dt;
      yield();
   }
   report(name, best, total);
}

static void benchScript(const char *name, char *script)
{
   ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      printf("%s: script failed to compile/load\n", name);
      return;
   }
   uint32_t best = 0xffffffff, total = 0;
   for (int r = 0; r < RUNS; r++)
   {
      memset(ledsBuffer, 0x55, sizeof(ledsBuffer));
      uint32_t t0 = micros();
      exec.execute("main");
      uint32_t dt = micros() - t0;
      total += dt;
      if (dt < best)
         best = dt;
      yield();
   }
   report(name, best, total);
   exec.free();
}

void setup()
{
   Serial.begin(115200);
   delay(1000);

   bindVariable((char *)"CRGB", (char *)"leds", (char *)"[]", (void *)ledsBuffer);
   bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", (void *)scriptHsv);

   printf("Mandelbrot %dx%d, %d iterations, CPU %lu MHz\n", LED_WIDTH, LED_HEIGHT, MAX_ITERATIONS,
          (unsigned long)getCpuFrequencyMhz());
   benchNative("native float", nativeRenderFloat);
   benchNative("native fixed", nativeRenderFixed);
   benchScript("script float", scriptFloat);
   benchScript("script ffloat", scriptFfloat);
}

void loop()
{
}
