// The Mandelbrot set on a 128x96 CRGB matrix, computed with integers only.
//
// This is the classic float script (v1's sc_examples/Mandel.sc) rewritten
// in fixed-point arithmetic: every fractional value is stored as an int
// equal to the real value times ONE (4096, i.e. 12 fractional bits).
//
//  - Constants are pre-multiplied by 4096: 2.7 -> 11059, 2.1 -> 8602,
//    2.2 -> 9011, 1.1 -> 4506.
//  - Adding or subtracting two fixed-point values needs nothing special.
//  - Multiplying two of them carries the scale twice, so each product is
//    followed by `/ ONE` (`2 * x * y / ONE`, `x * x / ONE`).
//  - Multiplying by a plain integer (`x1 * 11059`) needs no correction.
//  - The escape test `x2 + y2 <= 4` becomes `<= 4 * ONE`.
//
// Why 4096 and not a larger scale: the loop only continues while
// x*x + y*y <= 4, but the next x can reach about 4 + 2.1 = 6.1 before the
// test stops it. At scale 4096 that is about 25000, whose square
// (6.2e8) still fits in a signed 32-bit int. At scale 8192 it would not.
//
// Compared with the float script, a few percent of the pixels differ:
// scattered points on the set's boundary, and the thin spike along the
// real axis. In float the middle row has y0 exactly 0, so the whole spike
// stays inside the set; here y0 comes out as -1/4096 for that row and
// those points escape.
//
// The script's main() never returns (while (true)), so execute() below
// does not either; show() is where a real driver would push the buffer to
// a panel. Every host-bound function is also declared `external` in the
// script text (see BouncingBalls.ino's header comment for why).
#include "script_executable.h"
#include "binding.h"
#include <string.h>

// Must match the script's own #define width / #define height.
#define LED_WIDTH 128
#define LED_HEIGHT 96

// 3 bytes, red/green/blue, no padding -- matches this compiler's built-in
// CRGB layout.
struct CRGB
{
   uint8_t red, green, blue;
};

// The script's `external CRGB leds[height, width];` binds to this buffer.
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

// Called by the script once per finished frame. Push ledsBuffer to the
// panel here. Without a driver this reports how long each frame took and
// how many pixels fell inside the set (drawn black), which is enough to
// see that the script is running and writing into the buffer.
static uint32_t g_lastShow = 0;
void scriptShow()
{
   // driver.showPixels((uint8_t *)ledsBuffer);

   int inside = 0;
   for (int y = 0; y < LED_HEIGHT; y++)
      for (int x = 0; x < LED_WIDTH; x++)
      {
         CRGB c = ledsBuffer[y][x];
         if (!c.red && !c.green && !c.blue)
            inside++;
      }
   uint32_t now = millis();
   printf("show(): frame took %lu ms, %d pixels inside the set\n",
          (unsigned long)(now - g_lastShow), inside);
   g_lastShow = now;
   // Let the idle task run so the task watchdog stays quiet while the
   // script's main() holds setup() forever.
   yield();
}

char script[] = R"EOF(
#define maxIterations 50
#define width 128
#define height 96
#define ONE 4096

external CRGB leds[height, width];
external CRGB hsv(int h, int s, int v);
external void show();

void render2D(int x1, int y1)
{
   int x0 = x1 * 11059 / width - 8602;
   int y0 = y1 * 9011 / height - 4506;
   int x = 0;
   int y = 0;
   int x2 = 0;
   int y2 = 0;
   int iter = 0;
   while (iter < maxIterations && x2 + y2 <= 4 * ONE)
   {
      int fX = x2 - y2 + x0;
      y = 2 * x * y / ONE + y0;
      x = fX;
      x2 = x * x / ONE;
      y2 = y * y / ONE;
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
   while (true)
   {
      for (int i = 0; i < width; i++)
      {
         for (int j = 0; j < height; j++)
         {
            render2D(i, j);
         }
      }
      show();
   }
}
)EOF";

void setup()
{
   Serial.begin(115200);

   bindVariable((char *)"CRGB", (char *)"leds", (char *)"[]", (void *)ledsBuffer);
   bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", (void *)scriptHsv);
   bindFunction((char *)"void", (char *)"show", NULL, (void *)scriptShow);

   ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      printf("script failed to compile/load\n");
      return;
   }

   g_lastShow = millis();
   exec.execute("main");
}

void loop()
{
}
