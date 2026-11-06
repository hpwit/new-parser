// Bouncing balls on a 128x96 CRGB matrix, the ball state in ffloat.
//
// The bouncing-balls script (a struct with a constructor and two methods,
// an array of 200 of them, ^ powers, casts in leds[] indexes) with the
// ball's float members -- velocity, centre, radius -- made ffloat:
// 32-bit fixed point (16 integer, 16 fraction bits) written like a float.
// The compiler turns the arithmetic into integer instructions: the
// literals (0.7, 0.3...) become fixed-point constants, xc += vx and the
// wall tests are integer adds and compares, 2 * xc - i a fixed-point
// multiply, and rand(300) / 255 a fixed-point division.
//
// Two things differ from a float script:
//   - ffloat's range is +-32768. The float version shades each pixel with
//     distance^2 / r^4, where r^4 and distance^2 reach ~72000 on the
//     bigger balls; here it is (distance / r^2)^2, which stays below 1.
//   - As everywhere in this language, an expression is computed in the
//     type it is assigned to: the shade goes into an ffloat first, since
//     assigning it straight to the int v would compute it all in int.
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

uint32_t scriptRand(uint32_t mod)
{
   return rand() % (mod + 1);
}

void scriptClear()
{
   memset(ledsBuffer, 0, sizeof(ledsBuffer));
}

// Called by the script once per finished frame. Push ledsBuffer to the
// panel here. Without a driver this reports how long each frame took and
// how many pixels are lit, which is enough to see that the script is
// running and drawing into the buffer.
static uint32_t g_lastShow = 0;
void scriptShow()
{
   // driver.showPixels((uint8_t *)ledsBuffer);

   int lit = 0;
   for (int y = 0; y < LED_HEIGHT; y++)
      for (int x = 0; x < LED_WIDTH; x++)
      {
         CRGB c = ledsBuffer[y][x];
         if (c.red || c.green || c.blue)
            lit++;
      }
   uint32_t now = millis();
   printf("show(): frame took %lu ms, %d pixels lit\n", (unsigned long)(now - g_lastShow), lit);
   g_lastShow = now;
   // Let the idle task run so the task watchdog stays quiet while the
   // script's main() holds setup() forever.
   yield();
}

char script[] = R"EOF(
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
   while (true)
   {
      clear();
      for (int i = 0; i < num; i++)
         Balls[i].updateBall();
      show();
   }
}
)EOF";

void setup()
{
   Serial.begin(115200);

   bindVariable((char *)"CRGB", (char *)"leds", (char *)"[]", (void *)ledsBuffer);
   bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", (void *)scriptHsv);
   bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
   bindFunction((char *)"void", (char *)"clear", NULL, (void *)scriptClear);
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
