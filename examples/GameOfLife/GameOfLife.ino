// Conway's Game of Life on a 128x128 CRGB matrix, driven entirely from a
// script. Shows the same CRGB/hsv()/leds[] host-binding pattern as
// MultiEffectController.ino, boiled down to one small self-contained
// automaton:
//
//  - `external CRGB leds[height, width];` -- a 2D external array bound
//    straight to a real host CRGB buffer (bindVariable with a "[]"
//    signature). The script writes colours into it with `leds[j, i] = ...`.
//  - `external CRGB hsv(int, int, int);` -- returns a CRGB by value; the
//    host implements a normal integer HSV->RGB conversion so each
//    generation is drawn in a single evolving hue.
//  - `CRGB(0, 0, 0)` -- the CRGB constructor is native to the language
//    (see README.md's "CRGB and the hue function"), no binding needed,
//    used here to clear dead cells.
//  - `clear()` / `show()` -- ordinary void host calls; show() is where a
//    real driver would push the buffer to a panel.
//
// The automaton itself: `copy[height, width]` is the cell grid (1 = alive).
// create_leds() walks it row by row, and to evaluate row j against its
// *previous* state after it has already overwritten earlier rows, it keeps
// the last two rows it saw in `line1[]` / `linec[]` (a rolling two-row
// history) and reads the row below straight from `copy`. Each surviving or
// newly born cell is written to both `leds[]` (as colour) and `copy[]` (as
// the next generation). main() seeds a single vertical line down the middle
// column and runs 50 generations.
//
// Every host-bound function is also declared `external` in the script text.
// bindFunction() alone is enough to parse, but the assembler's jump-table
// reservation for the call is only emitted for an explicitly declared
// external -- see BouncingBalls.ino's header comment for the full story.
// `leds` is likewise declared external (it already was in the script as
// given).
//
// Note on size: with width = height = 128 the host leds buffer is
// 128*128*3 = 48 KB and the script's own `copy` grid is 16 KB, so this
// wants PSRAM on a real ESP32. Shrink both #defines together to run it in
// internal RAM.
#include "script_executable.h"
#include "binding.h"
#include <string.h>

// Must match the script's own #define width / #define height below. 3
// bytes, red/green/blue, no padding -- matches this compiler's built-in
// CRGB layout (tokenize.cpp's type table: __CRGB__ .sizes = {1,1,1},
// members red/green/blue).
#define LED_WIDTH 128
#define LED_HEIGHT 128

struct CRGB
{
   uint8_t red, green, blue;
};

// The script's `external CRGB leds[height, width];` binds straight to this.
static CRGB ledsBuffer[LED_HEIGHT][LED_WIDTH];

// Standard integer HSV (0-255 per channel) -> RGB. Same spirit as
// MultiEffectController.ino's scriptHsv -- a real colour, not a stand-in.
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

void scriptClear()
{
   memset(ledsBuffer, 0, sizeof(ledsBuffer));
}

// No physical panel here -- instead prove, once per generation, that the
// script's writes are really landing in ledsBuffer, by counting the lit
// pixels along the centre column the automaton seeds.
static uint32_t g_generation = 0;
void scriptShow()
{
   int lit = 0;
   for (int y = 0; y < LED_HEIGHT; y++)
   {
      CRGB c = ledsBuffer[y][LED_WIDTH / 2];
      if (c.red || c.green || c.blue)
         lit++;
   }
   printf("show(): generation %lu, lit pixels in centre column: %d\n",
          (unsigned long)g_generation++, lit);
}

char script[] = R"EOF(
#define width 128
#define height 128
external CRGB leds[height, width];
external CRGB hsv(int h, int s, int v);
external void clear();
external void show();
uint8_t copy[height, width];
uint8_t line1[width];
uint8_t linec[width];

void create_leds(int o) {
  CRGB color = hsv(o, 255, 255);
  for (int j = 1; j < height - 1; j++) {
    for (int h = 0; h < width; h++) {
      line1[h] = linec[h];
      linec[h] = copy[j, h];
    }
    for (int i = 1; i < width - 1; i++) {

      int res = linec[i - 1] + linec[i + 1];
      res = res + line1[i - 1] + line1[i + 1] + line1[i];
      res = res + copy[j + 1, i - 1] + copy[j + 1, i + 1] + copy[j + 1, i];
      bool alive = false;
      int l = copy[j, i];

      if (res == 3)
        alive = true;
      else
        alive = (l == 1 && res == 2) ? true : false;
      if (alive) {
        leds[j, i] = color;
        copy[j, i] = 1;
      } else {
        leds[j, i] = CRGB(0, 0, 0);
        copy[j, i] = 0;
      }
    }
  }
}

int main() {
  for (int h = 0; h < width * height; h++) {
    copy[h] = 0;
  }
  for (int h = 0; h < height; h++) {
    copy[h, width / 2] = 1;
  }
  clear();
  show();
  for (int i = 0; i < 50; i++) {
    create_leds(i);
    show();
  }
}
)EOF";

void setup()
{
   Serial.begin(115200);

   bindVariable((char *)"CRGB", (char *)"leds", (char *)"[]", (void *)ledsBuffer);
   bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", (void *)scriptHsv);
   bindFunction((char *)"void", (char *)"clear", NULL, (void *)scriptClear);
   bindFunction((char *)"void", (char *)"show", NULL, (void *)scriptShow);

   ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      printf("script failed to compile/load\n");
      return;
   }

   exec.execute("main");
   printf("********** done **********\n");
}

void loop()
{
}
