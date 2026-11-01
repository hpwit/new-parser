// A scrolling rainbow on a 128x96 matrix, drawn by a script and by the
// same code compiled natively, timed side by side over 100 frames:
//
//    for (int i = 0; i < width; i++)
//       for (int j = 0; j < height; j++)
//          leds[j * width + i] = hsv(i + h + j, 255, 180);
//
// with h, the offset that makes it scroll, going up by one each frame.
// Every pixel is one call to the host's hsv() -- FastLED's CHSV unless
// USE_FASTLED_HSV is 0 -- so this measures a tight double loop around a
// call to a bound C function and a CRGB store. The hue wraps around:
// i + h + j goes past 255.
//
// show() is where a real driver would push the frame to the panel. Here
// it hashes the frame instead (FNV-1a), to check that the script draws
// exactly what the native code draws, and the time it takes is left out:
// only the drawing between two show() calls is counted.
#include "script_executable.h"
#include "binding.h"
#include <string.h>

#define LED_WIDTH 128
#define LED_HEIGHT 96
#define FRAMES 100

// hsv(), for the scripts and the native code alike: FastLED's CHSV ->
// CRGB conversion (hsv2rgb_rainbow) -- what a sketch driving a real panel
// uses -- or, with USE_FASTLED_HSV set to 0 (FastLED not installed), a
// small integer conversion. The timings in README.md use FastLED's.
#define USE_FASTLED_HSV 1
#if USE_FASTLED_HSV
#include <FastLED.h>
#define HSV_KIND "FastLED CHSV"
CRGB hsv(int h, int s, int v)
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
CRGB hsv(int h, int s, int v)
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

static CRGB leds[LED_WIDTH * LED_HEIGHT];

// Time spent drawing (between show() calls), frames shown, and a hash of
// every frame in order.
static unsigned long drawUs, lastShow;
static int framesShown;
static uint32_t frameHash;

void show()
{
   drawUs += micros() - lastShow;
   framesShown++;
   const uint8_t *p = (const uint8_t *)leds;
   for (size_t i = 0; i < sizeof(leds); i++)
      frameHash = (frameHash ^ p[i]) * 16777619u;
   lastShow = micros();
}

static void startRun()
{
   memset(leds, 0, sizeof(leds));
   drawUs = 0;
   framesShown = 0;
   frameHash = 2166136261u;
   lastShow = micros();
}

char script[] = R"EOF(
#define width 128
#define height 96

external CRGB leds[12288];
external CRGB hsv(int h, int s, int v);
external void show();

void main()
{
   for (int h = 0; h < 100; h++)
   {
      for (int i = 0; i < width; i++)
         for (int j = 0; j < height; j++)
            leds[j * width + i] = hsv(i + h + j, 255, 180);
      show();
   }
}
)EOF";

// The same loop, compiled natively.
static void __attribute__((noinline)) nativeRainbow()
{
   for (int h = 0; h < FRAMES; h++)
   {
      for (int i = 0; i < LED_WIDTH; i++)
         for (int j = 0; j < LED_HEIGHT; j++)
            leds[j * LED_WIDTH + i] = hsv(i + h + j, 255, 180);
      show();
   }
}

static void report(const char *name)
{
   printf("%s: %d frames, %lu us drawing, %.2f ms/frame (%.1f fps), hash %08lx\n", name, framesShown, drawUs,
          drawUs / 1000.0 / framesShown, framesShown * 1e6 / drawUs, (unsigned long)frameHash);
}

void setup()
{
   Serial.begin(115200);
   delay(500);
   bindVariable((char *)"CRGB", (char *)"leds", (char *)"[]", (void *)leds);
   bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", (void *)hsv);
   bindFunction((char *)"void", (char *)"show", NULL, (void *)show);

   unsigned long t0 = micros();
   ScriptExecutable exec = parseScript(script);
   unsigned long compileUs = micros() - t0;
   if (!exec.isExeExists())
   {
      printf("script failed to compile/load\n");
      return;
   }
   printf("CPU %lu MHz, %s, script compiled in %lu ms\n", (unsigned long)getCpuFrequencyMhz(), HSV_KIND,
          compileUs / 1000);

   for (int run = 0; run < 2; run++)
   {
      startRun();
      exec.execute("main");
      unsigned long scriptUs = drawUs;
      uint32_t scriptHash = frameHash;
      report("script");

      startRun();
      nativeRainbow();
      report("native");

      printf("frames %s, script/native = %.2f\n\n", scriptHash == frameHash ? "identical" : "DIFFERENT",
             (double)scriptUs / drawUs);
   }
   exec.free();
}

void loop()
{
}
