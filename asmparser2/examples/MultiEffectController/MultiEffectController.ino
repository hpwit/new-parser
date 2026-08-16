// Compiles *and actually runs* test/host/fixtures/multi_effect_controller.sc
// -- the same several-hundred-line, five-effect LED matrix script
// test/host/test_large_script.cpp checks against an ESP32-without-PSRAM
// size budget, and test/qemu/gen_large_script.cpp pulls four pure-integer
// helper functions (gcd/fib/isPrime/clampInt) out of to run under real
// Xtensa emulation.
//
// Neither of those ever calls this script's own main(): test_large_script
// only parses/assembles/loads it (structural checks only, every host
// binding left NULL -- see its registerBindings()), and gen_large_script
// calls the four helpers directly, bypassing main() and its while(true)
// render loop entirely. test/qemu/README.md's "Not verified" section says
// this outright: running main() "would need ten different external/
// auto-declared host functions ... each bridged or jump-table-patched by
// hand in a bare-metal runner, which wasn't attempted." This sketch is
// that missing piece -- a real host binding (not NULL) for every external
// call the script makes, so main()'s infinite effect loop actually runs.
//
// Two adaptations vs. the fixture file on disk:
//
//  1. Every host-bound external here is a real function/array, not NULL --
//     hsv()/leds/show()/clear()/rand()/delay()/millis()/sin()/hypot()/
//     pinInterrupt() all do genuine work (see the definitions below).
//     printfln() is the one exception: it's *not* bound at all, since
//     it's now a built-in the compiler registers automatically the first
//     time anything is parsed (see README.md's "Built-in printf /
//     printfln" and ScriptPrintf.ino) -- binding it here to anything,
//     even a real function, would just be redundant with that.
//
//  2. The `external` declarations for hsv/show/clear/rand/delay/millis/
//     sin/hypot/pinInterrupt, commented out in the fixture on disk (it
//     deliberately exercises the bindFunction()-only auto-declare path
//     for coverage, since it never actually executes anything), are
//     uncommented below. BouncingBalls.ino's header comment documents why
//     that matters: auto-declare-only registration resolves the call site
//     during parsing, but the scratch AST node it's resolved through is
//     thrown away right after, so the assembler's jump-table reservation
//     for that call never happens -- a gap that can silently produce a
//     binary that assembles without error but is wrong at runtime. Every
//     example in this repo that actually *executes* its script (as
//     opposed to test_large_script.cpp's structural-only check) works
//     around it the same way: declare each host-bound function `external`
//     in the script text too. leds[] already has its `external`
//     declaration in the fixture as-is (uncommented there already), so
//     it's unchanged here.
//
// Everything else -- all five effects, the struct/array/recursion/sort
// logic, the button-driven mode switch -- is character-for-character the
// same script. Uses parseScript()/ScriptExecutable (script_executable.h),
// like every other example here except LanguageBasics.ino/SaveScriptBinary.ino.
//
// A real button on GPIO4 (the pin the script itself hardcodes in its
// pinInterrupt() call) cycles through the five effects; without one wired
// up, the script still runs and free-runs through whichever effect is
// currently selected (particles, by default). Progress prints to Serial:
// runDiagnostics()'s one-time output, a mode-indicator-pixel/frame-count
// line from show() every 250 frames, and the script's own periodic trail
// checksum lines every 500 frames.
#include "script_executable.h"
#include "binding.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

// Must match the script's own #define WIDTH/HEIGHT below exactly -- these
// size the real host-side leds[]/trail buffers the script's external
// array binds to.
#define LED_WIDTH 80
#define LED_HEIGHT 40

// 3 bytes, red/green/blue in that order, no padding -- matches this
// compiler's built-in CRGB layout exactly (tokenize.cpp's type table:
// __CRGB__'s .sizes = {1,1,1}, .membersNames = {"red","green","blue"}).
struct CRGB
{
   uint8_t red, green, blue;
};

// The script's `external CRGB leds[HEIGHT, WIDTH];` binds straight to
// this -- real backing storage, not a NULL placeholder like every other
// compile-only use of this fixture in this repo.
static CRGB ledsBuffer[LED_HEIGHT][LED_WIDTH];

// Standard integer HSV (0-255 per channel) -> RGB conversion -- there to
// make hsv()'s return value a genuine color rather than a stand-in, same
// spirit as BouncingBalls.ino's real (if simplified) physics.
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
   case 0:
      c.red = v;
      c.green = t;
      c.blue = p;
      break;
   case 1:
      c.red = q;
      c.green = v;
      c.blue = p;
      break;
   case 2:
      c.red = p;
      c.green = v;
      c.blue = t;
      break;
   case 3:
      c.red = p;
      c.green = q;
      c.blue = v;
      break;
   case 4:
      c.red = t;
      c.green = p;
      c.blue = v;
      break;
   default:
      c.red = v;
      c.green = p;
      c.blue = q;
      break;
   }
   return c;
}

void scriptClear()
{
   memset(ledsBuffer, 0, sizeof(ledsBuffer));
}

// A real matrix driver would push ledsBuffer to actual hardware here
// (e.g. FastLED.show() after copying into its own strip buffer). This
// sketch has no physical display, so it instead proves -- periodically,
// not every frame, to avoid flooding Serial at ~60fps -- that hsv()'s
// output is really landing in ledsBuffer via drawModeIndicator()'s
// always-on status pixel at row 0, column 0.
static uint32_t g_frameCount = 0;
void scriptShow()
{
   g_frameCount++;
   if (g_frameCount % 250 == 0)
   {
      CRGB c = ledsBuffer[0][0];
      printf("show(): frame %lu, indicator pixel R%u G%u B%u\n",
             (unsigned long)g_frameCount, c.red, c.green, c.blue);
   }
}

uint32_t scriptRand(uint32_t mod)
{
   // Same convention as BouncingBalls.ino's scriptRand: inclusive of mod.
   return rand() % (mod + 1);
}

void scriptDelay(uint32_t ms)
{
   delay(ms);
}

uint32_t scriptMillis()
{
   return millis();
}

float scriptSin(float rad)
{
   return sinf(rad);
}

float scriptHypot(float x, float y)
{
   return hypotf(x, y);
}

// The script itself owns g_exec once it's populated in setup() -- see the
// `static ScriptExecutable holder` pattern below, same one
// KeyboardCallback.ino uses so this pointer stays valid for as long as
// the interrupt can fire.
static ScriptExecutable *g_exec = NULL;
static char g_interruptFnName[32] = {0};

// Direct re-entry into the already-compiled script from interrupt
// context -- exactly what pinInterrupt() exists for (README.md's
// "Interrupts" section). Safe to call from here specifically because
// callFunction() (asm_execute.cpp) only does a linear scan over a small
// in-RAM function table plus a direct call into the compiler's own
// generated Xtensa code (IRAM-resident, like every script this compiler
// produces) -- no heap allocation, no blocking. IRAM_ATTR is required on
// real ESP32 hardware since attachInterrupt() ISRs must not be paged out
// of flash.
void IRAM_ATTR onScriptInterrupt()
{
   if (g_exec != NULL)
   {
      g_exec->execute(g_interruptFnName);
   }
}

// external void pinInterrupt(uint32_t handle, char *fname, int pin);
// _handle_ (the `handle` argument) isn't needed here -- g_exec already
// points at the one script this sketch ever loads, unlike a framework
// juggling several scripts at once that would need it to disambiguate.
void scriptPinInterrupt(uint32_t handle, char *fname, int pin)
{
   (void)handle;
   strncpy(g_interruptFnName, fname, sizeof(g_interruptFnName) - 1);
   pinMode(pin, INPUT_PULLUP);
   attachInterrupt(digitalPinToInterrupt(pin), onScriptInterrupt, FALLING);
}

char script[] = R"EOF(
#define WIDTH 80
#define HEIGHT 40
#define NUM_PARTICLES 160
#define NUM_STARS 220
#define NUM_RIPPLES 70
#define NUM_COMETS 50

#define MODE_PARTICLES 0
#define MODE_STARS 1
#define MODE_PLASMA 2
#define MODE_RIPPLES 3
#define MODE_COMETS 4
#define NUM_MODES 5

#define GRAVITY 0.06
#define FRICTION 0.985
#define TRAIL_FADE 4

// ==================== hardware / host bindings ====================

external CRGB leds[HEIGHT, WIDTH];
external CRGB hsv(int h, int s, int v);
external void show();
external void clear();
external uint32_t rand(uint32_t mod);
external void delay(uint32_t d);
external uint32_t millis();
external float sin(float h);
external float hypot(float x, float y);
external void pinInterrupt(uint32_t handle, char *fname, int pin);

// ==================== global state ====================

uint8_t trail[HEIGHT, WIDTH];
int mode = MODE_PARTICLES;
uint32_t frame = 0;

// ==================== particle fountain ====================

struct Particle
{
   float x, y, vx, vy;
   int life, maxLife;
   int hue;

   void reset()
   {
      x = WIDTH / 2;
      y = HEIGHT / 2;
      vx = (rand(200) / 100.0) - 1.0;
      vy = -(rand(150) / 100.0) - 0.4;
      maxLife = 40 + rand(60);
      life = maxLife;
      hue = rand(255);
   }

   void update()
   {
      vy = vy + GRAVITY;
      vx = vx * FRICTION;
      x = x + vx;
      y = y + vy;
      life--;
      if (life <= 0 || x < 0 || x >= WIDTH || y >= HEIGHT)
      {
         reset();
      }
   }

   void draw()
   {
      if (life <= 0)
         return;
      int px = (int)(x);
      int py = (int)(y);
      if (px < 0 || px >= WIDTH || py < 0 || py >= HEIGHT)
         return;
      int brightness = 255 * life / maxLife;
      leds[py, px] = hsv(hue, 255, brightness);
   }

   Particle()
   {
      reset();
   }
}

// ==================== twinkling stars ====================

struct Star
{
   int x, y;
   uint8_t brightness;
   bool rising;

   void reset()
   {
      x = rand(WIDTH);
      y = rand(HEIGHT / 2);
      brightness = rand(120);
      rising = true;
   }

   void update()
   {
      if (rising)
      {
         if (brightness >= 250)
         {
            rising = false;
         }
         else
         {
            brightness = brightness + 6;
         }
      }
      else
      {
         if (brightness <= 8)
         {
            reset();
         }
         else
         {
            brightness = brightness - 3;
         }
      }
   }

   void draw()
   {
      leds[y, x] = hsv(160, 40, brightness);
   }

   Star()
   {
      reset();
   }
}

// ==================== expanding ripples ====================

struct Ripple
{
   int cx, cy;
   int radius;
   int maxRadius;
   int hue;
   bool active;

   void reset()
   {
      cx = rand(WIDTH);
      cy = rand(HEIGHT);
      radius = 0;
      maxRadius = 6 + rand(14);
      hue = rand(255);
      active = true;
   }

   void update()
   {
      if (!active)
         return;
      radius++;
      if (radius >= maxRadius)
      {
         active = false;
      }
   }

   void draw()
   {
      if (!active)
         return;
      int brightness = 255 - (255 * radius / maxRadius);
      for (int dx = -radius; dx <= radius; dx++)
      {
         int adx = dx;
         if (adx < 0)
            adx = -adx;
         int dy = radius - adx;
         int px = cx + dx;
         if (px < 0 || px >= WIDTH)
            continue;
         int py1 = cy + dy;
         int py2 = cy - dy;
         if (py1 >= 0 && py1 < HEIGHT)
            leds[py1, px] = hsv(hue, 255, brightness);
         if (dy != 0 && py2 >= 0 && py2 < HEIGHT)
            leds[py2, px] = hsv(hue, 255, brightness);
      }
   }

   Ripple()
   {
      reset();
   }
}

// ==================== edge-to-edge comets ====================

struct Comet
{
   float x, y, vx, vy;
   int hue;

   void reset()
   {
      int edge = rand(4);
      float speed = 0.6 + (rand(80) / 100.0);
      if (edge == 0)
      {
         x = 0;
         y = rand(HEIGHT);
         vx = speed;
         vy = (rand(100) / 100.0) - 0.5;
      }
      else if (edge == 1)
      {
         x = WIDTH - 1;
         y = rand(HEIGHT);
         vx = -speed;
         vy = (rand(100) / 100.0) - 0.5;
      }
      else if (edge == 2)
      {
         x = rand(WIDTH);
         y = 0;
         vy = speed;
         vx = (rand(100) / 100.0) - 0.5;
      }
      else
      {
         x = rand(WIDTH);
         y = HEIGHT - 1;
         vy = -speed;
         vx = (rand(100) / 100.0) - 0.5;
      }
      hue = rand(255);
   }

   void update()
   {
      x = x + vx;
      y = y + vy;
      if (x < -2 || x >= WIDTH + 2 || y < -2 || y >= HEIGHT + 2)
      {
         reset();
      }
   }

   void draw()
   {
      int px = (int)(x);
      int py = (int)(y);
      if (px < 0 || px >= WIDTH || py < 0 || py >= HEIGHT)
         return;
      leds[py, px] = hsv(hue, 200, 255);
   }

   Comet()
   {
      reset();
   }
}

Particle particles[NUM_PARTICLES];
Star stars[NUM_STARS];
Ripple ripples[NUM_RIPPLES];
Comet comets[NUM_COMETS];
int sortIndex[NUM_PARTICLES];
int sortStarIndex[NUM_STARS];

// ==================== general-purpose math helpers ====================

int clampInt(int v, int lo, int hi)
{
   if (v < lo)
      return lo;
   if (v > hi)
      return hi;
   return v;
}

float lerp(float a, float b, float t)
{
   return a + (b - a) * t;
}

int gcd(int a, int b)
{
   if (b == 0)
      return a;
   return gcd(b, a % b);
}

int fib(int n)
{
   if (n < 2)
      return n;
   return fib(n - 1) + fib(n - 2);
}

bool isPrime(int n)
{
   if (n < 2)
      return false;
   for (int i = 2; i * i <= n; i++)
   {
      if (n % i == 0)
         return false;
   }
   return true;
}

int wrapInt(int v, int lo, int hi)
{
   int range = hi - lo;
   if (range <= 0)
      return lo;
   int r = (v - lo) % range;
   if (r < 0)
      r = r + range;
   return lo + r;
}

float smoothstep(float edge0, float edge1, float x)
{
   float t = (x - edge0) / (edge1 - edge0);
   t = clampInt((int)(t * 1000.0), 0, 1000) / 1000.0;
   return t * t * (3.0 - 2.0 * t);
}

int manhattanDistance(int x1, int y1, int x2, int y2)
{
   int dx = x1 - x2;
   int dy = y1 - y2;
   if (dx < 0)
      dx = -dx;
   if (dy < 0)
      dy = -dy;
   return dx + dy;
}

int countNearbyParticlePairs(int radius)
{
   int count = 0;
   for (int i = 0; i < NUM_PARTICLES; i++)
   {
      for (int j = i + 1; j < NUM_PARTICLES; j++)
      {
         int xi = (int)(particles[i].x);
         int yi = (int)(particles[i].y);
         int xj = (int)(particles[j].x);
         int yj = (int)(particles[j].y);
         if (manhattanDistance(xi, yi, xj, yj) <= radius)
         {
            count++;
         }
      }
   }
   return count;
}

void runDiagnostics()
{
   int a = 48;
   int b = 18;
   int g = gcd(a, b);
   printfln("diag gcd(48,18): %d", g);

   int f = fib(12);
   printfln("diag fib(12): %d", f);

   int primesFound = 0;
   for (int n = 2; n < 30; n++)
   {
      if (isPrime(n))
         primesFound++;
   }
   printfln("diag primes<30: %d", primesFound);

   int wrapped = wrapInt(-5, 0, 10);
   printfln("diag wrapInt(-5,0,10): %d", wrapped);

   float smooth = smoothstep(0.0, 10.0, 5.0);
   int smoothPct = (int)(smooth * 100.0);
   printfln("diag smoothstep(0,10,5)*100: %d", smoothPct);

   int dist = manhattanDistance(0, 0, WIDTH - 1, HEIGHT - 1);
   printfln("diag manhattanDistance corner: %d", dist);

   int nearby = countNearbyParticlePairs(3);
   printfln("diag nearby particle pairs: %d", nearby);
}

// ==================== particle draw-order sort ====================

void sortParticlesByLife()
{
   for (int i = 0; i < NUM_PARTICLES; i++)
      sortIndex[i] = i;

   for (int i = 1; i < NUM_PARTICLES; i++)
   {
      int key = sortIndex[i];
      int keyLife = particles[key].life;
      int j = i - 1;
      bool shifting = true;
      while (j >= 0 && shifting)
      {
         int prevIdx = sortIndex[j];
         int prevLife = particles[prevIdx].life;
         if (prevLife > keyLife)
         {
            sortIndex[j + 1] = prevIdx;
            j--;
         }
         else
         {
            shifting = false;
         }
      }
      sortIndex[j + 1] = key;
   }
}

void sortStarsByBrightness()
{
   for (int i = 0; i < NUM_STARS; i++)
      sortStarIndex[i] = i;

   for (int i = 1; i < NUM_STARS; i++)
   {
      int key = sortStarIndex[i];
      int keyBrightness = stars[key].brightness;
      int j = i - 1;
      bool shifting = true;
      while (j >= 0 && shifting)
      {
         int prevIdx = sortStarIndex[j];
         int prevBrightness = stars[prevIdx].brightness;
         if (prevBrightness > keyBrightness)
         {
            sortStarIndex[j + 1] = prevIdx;
            j--;
         }
         else
         {
            shifting = false;
         }
      }
      sortStarIndex[j + 1] = key;
   }
}

// ==================== trail buffer (internal, no external decl) ====================

void fadeTrail()
{
   for (int y = 0; y < HEIGHT; y++)
   {
      for (int x = 0; x < WIDTH; x++)
      {
         int v = trail[y, x];
         v = v - TRAIL_FADE;
         trail[y, x] = clampInt(v, 0, 255);
      }
   }
}

void stampTrail(int cx, int cy)
{
   for (int dy = -2; dy <= 2; dy++)
   {
      for (int dx = -2; dx <= 2; dx++)
      {
         int px = cx + dx;
         int py = cy + dy;
         if (px >= 0 && px < WIDTH && py >= 0 && py < HEIGHT)
         {
            trail[py, px] = 255;
         }
      }
   }
}

uint32_t trailChecksum()
{
   uint32_t sum = 0;
   for (int y = 0; y < HEIGHT; y++)
   {
      for (int x = 0; x < WIDTH; x++)
      {
         sum = sum + trail[y, x];
         sum = sum % 1000000007;
      }
   }
   return sum;
}

uint32_t checksumHistory[8];
int checksumHistoryIndex = 0;
int checksumHistoryCount = 0;

void recordChecksum(uint32_t cs)
{
   checksumHistory[checksumHistoryIndex] = cs;
   checksumHistoryIndex = (checksumHistoryIndex + 1) % 8;
   if (checksumHistoryCount < 8)
   {
      checksumHistoryCount++;
   }
}

uint32_t averageChecksumHistory()
{
   if (checksumHistoryCount == 0)
      return 0;
   uint32_t sum = 0;
   for (int i = 0; i < checksumHistoryCount; i++)
   {
      sum = sum + checksumHistory[i];
   }
   return sum / checksumHistoryCount;
}

// ==================== effect renderers ====================

void renderParticles()
{
   for (int i = 0; i < NUM_PARTICLES; i++)
      particles[i].update();

   sortParticlesByLife();

   for (int i = 0; i < NUM_PARTICLES; i++)
   {
      int idx = sortIndex[i];
      particles[idx].draw();
   }

   for (int i = 0; i < NUM_PARTICLES; i++)
   {
      int px = (int)(particles[i].x);
      int py = (int)(particles[i].y);
      stampTrail(px, py);
   }
}

void renderStars()
{
   for (int i = 0; i < NUM_STARS; i++)
      stars[i].update();

   sortStarsByBrightness();

   for (int i = 0; i < NUM_STARS; i++)
   {
      int idx = sortStarIndex[i];
      stars[idx].draw();
   }
}

void renderRipples()
{
   for (int i = 0; i < NUM_RIPPLES; i++)
   {
      ripples[i].update();
      ripples[i].draw();
      if (!ripples[i].active)
      {
         ripples[i].reset();
      }
   }
}

void renderComets()
{
   for (int i = 0; i < NUM_COMETS; i++)
   {
      comets[i].update();
      comets[i].draw();
      int px = (int)(comets[i].x);
      int py = (int)(comets[i].y);
      stampTrail(px, py);
   }
}

float plasmaValue(int x, int y, uint32_t t)
{
   float fx = x;
   float fy = y;
   float ft = t;

   float v1 = sin((fx / 8.0) + (ft / 40.0));
   float v2 = sin((fy / 6.0) - (ft / 55.0));
   float v3 = sin(((fx + fy) / 10.0) + (ft / 30.0));

   float cx = fx - (WIDTH / 2);
   float cy = fy - (HEIGHT / 2);
   float dist = hypot(cx, cy);
   float v4 = sin((dist / 4.0) - (ft / 20.0));

   return (v1 + v2 + v3 + v4) / 4.0;
}

void renderPlasma()
{
   uint32_t t = millis();
   for (int y = 0; y < HEIGHT; y++)
   {
      for (int x = 0; x < WIDTH; x++)
      {
         float v = plasmaValue(x, y, t);
         int hue = (int)(lerp(0, 255, (v + 1.0) / 2.0));
         leds[y, x] = hsv(hue, 255, 200);
      }
   }
}

// ==================== mode switching ====================

void nextMode()
{
   mode = (mode + 1) % NUM_MODES;
   printfln("mode -> %d", mode);
}

void onModeButton()
{
   nextMode();
}

void drawModeIndicator()
{
   int hue = 0;
   if (mode == MODE_PARTICLES)
   {
      hue = 0;
   }
   else if (mode == MODE_STARS)
   {
      hue = 40;
   }
   else if (mode == MODE_RIPPLES)
   {
      hue = 90;
   }
   else if (mode == MODE_COMETS)
   {
      hue = 150;
   }
   else if (mode == MODE_PLASMA)
   {
      hue = 200;
   }
   else
   {
      hue = 255;
   }

   int barWidth = 4 + mode * 3;
   if (barWidth > WIDTH)
   {
      barWidth = WIDTH;
   }

   for (int x = 0; x < barWidth; x++)
   {
      leds[0, x] = hsv(hue, 255, 60);
   }
}

// ==================== main ====================

int main()
{
   clear();
   runDiagnostics();

   pinInterrupt(_handle_, "onModeButton", 4);

   uint32_t initialChecksum = trailChecksum();
   printfln("initial trail checksum: %d", initialChecksum);
   recordChecksum(initialChecksum);

   while (true)
   {
      fadeTrail();

      if (mode == MODE_PARTICLES)
      {
         renderParticles();
      }
      else if (mode == MODE_STARS)
      {
         renderStars();
      }
      else if (mode == MODE_RIPPLES)
      {
         renderRipples();
      }
      else if (mode == MODE_COMETS)
      {
         renderComets();
      }
      else
      {
         renderPlasma();
      }

      drawModeIndicator();

      frame++;
      if (frame % 500 == 0)
      {
         uint32_t cs = trailChecksum();
         printfln("trail checksum: %d", cs);
         recordChecksum(cs);
         uint32_t avgCs = averageChecksumHistory();
         printfln("trail checksum (8-sample average): %d", avgCs);
      }

      show();
      delay(16);
   }
}
)EOF";

void setup()
{
   Serial.begin(115200);

   bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", (void *)scriptHsv);
   bindVariable((char *)"CRGB", (char *)"leds", (char *)"[]", (void *)ledsBuffer);
   bindFunction((char *)"void", (char *)"show", NULL, (void *)scriptShow);
   bindFunction((char *)"void", (char *)"clear", NULL, (void *)scriptClear);
   bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", (void *)scriptRand);
   bindFunction((char *)"void", (char *)"delay", (char *)"uint32_t", (void *)scriptDelay);
   bindFunction((char *)"uint32_t", (char *)"millis", NULL, (void *)scriptMillis);
   bindFunction((char *)"float", (char *)"sin", (char *)"float", (void *)scriptSin);
   bindFunction((char *)"float", (char *)"hypot", (char *)"float,float", (void *)scriptHypot);
   bindFunction((char *)"void", (char *)"pinInterrupt", (char *)"uint32_t,char*,int", (void *)scriptPinInterrupt);
   // printfln is deliberately left unbound -- see the header comment.

   // `static` (not a plain local) so g_exec -- read from onScriptInterrupt()
   // for as long as the sketch runs -- stays valid; same lifetime pattern
   // KeyboardCallback.ino uses for the same reason. Direct-initialization
   // from parseScript()'s return value is what makes this safe (elided
   // construction, see script_executable.h's class comment) -- don't
   // replace this with a two-step "declare then assign".
   static ScriptExecutable holder = parseScript(script);
   g_exec = &holder;

   if (!g_exec->isExeExists())
   {
      printf("script failed to compile/load\n");
      return;
   }

   // main()'s own while(true) never returns -- this call does not either,
   // so loop() below is never actually reached. Same shape as
   // BouncingBalls.ino's setup().
   g_exec->execute("main");
}

void loop()
{
}
