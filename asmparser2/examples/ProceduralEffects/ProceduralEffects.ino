// Two procedural effects for a 128x96 CRGB matrix, each a separate script
// compiled on the device: a plasma and a fire. No stored graphics -- every
// pixel is computed.
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
//
// Each script's main() draws for 20 seconds and returns, and loop()
// alternates between the two compiled executables.
//
// Host functions the scripts use, all defined below and bound in setup():
// leds[] (the frame buffer), hsv(), sin(), rand(), millis() and show().
// show() is the one place to connect a real panel driver -- see
// scriptShow().
//
// Every host-bound function is also declared `external` in the script
// text (see BouncingBalls.ino's header comment for why).
//
// Memory: the frame buffer is 128*96*3 = 36864 bytes and fireScript's heat
// field is 98*128 = 12544 bytes, so this runs without PSRAM.
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

// Both scripts' `external CRGB leds[height, width];` bind to this buffer:
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

static ScriptExecutable *g_plasma = NULL;
static ScriptExecutable *g_fire = NULL;

void setup()
{
   Serial.begin(115200);

   bindVariable((char *)"CRGB", (char *)"leds", (char *)"[]", (void *)ledsBuffer);
   bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", (void *)scriptHsv);
   bindFunction((char *)"float", (char *)"sin", (char *)"float", (void *)scriptSin);
   bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
   bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
   bindFunction((char *)"void", (char *)"show", NULL, (void *)scriptShow);

   // ScriptExecutable can't be copied or moved, so both are kept as
   // function-local statics and reached from loop() through pointers.
   static ScriptExecutable plasma = parseScript(plasmaScript);
   static ScriptExecutable fire = parseScript(fireScript);
   if (plasma.isExeExists())
      g_plasma = &plasma;
   else
      printf("plasma script failed to compile/load\n");
   if (fire.isExeExists())
      g_fire = &fire;
   else
      printf("fire script failed to compile/load\n");
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
   if (g_plasma == NULL && g_fire == NULL)
      delay(1000);
}
