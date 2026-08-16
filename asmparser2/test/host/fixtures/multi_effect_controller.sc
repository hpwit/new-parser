// Multi-Effect LED Matrix Controller
//
// A synthetic but realistic-scale .sc script for a WIDTH x HEIGHT LED
// matrix, written to exercise this compiler at a scale closer to a real
// project than the hand-written snippets in test_parser.cpp: five
// switchable effects (particle fountain, twinkling stars, plasma,
// expanding ripples, edge-to-edge comets), a struct-array + constructor
// pattern for each moving object, an internal (non-external) 2D trail
// buffer with its own fade/stamp/checksum logic, a button-driven mode
// switch, and a handful of general-purpose math helpers (recursive
// gcd/fibonacci, a prime sieve check, insertion sort) that aren't
// specific to LEDs at all -- there to exercise recursion and non-trivial
// expressions, not because a real effect needs them.
//
// Ripples and comets, plus a second insertion sort, a particle-proximity
// counter, a checksum-history ring buffer, and a mode-indicator bar,
// were added later specifically to grow this script's compiled size
// (roughly 2x instruction bytes, roughly 3x data bytes, vs. the original
// three-effect version -- instruction count tracks actual code volume,
// not the array-size #defines that drive most of the data growth, so
// getting both to scale by the same factor would need substantially
// more new code than was warranted here) while keeping gcd()/fib()/
// isPrime()/clampInt() -- the four pure-integer helpers
// test/qemu/gen_large_script.cpp extracts and calls directly under real
// Xtensa emulation -- untouched, so that verification stays valid
// unchanged. Both new effects deliberately reuse only already-bound
// externals (hsv, rand) and the existing stampTrail() helper, so no new
// host bindings were needed anywhere this fixture is compiled. Comet's
// trail stamping happens in renderComets(), not Comet::draw() itself --
// calling a free function from inside a struct method before that
// function's own definition is reached later in the file doesn't
// resolve (findFunction() only ever finds it via the plain, unqualified
// name lookup that requires the name to already be registered; nothing
// in this codebase had exercised a forward reference like that before).
// This mirrors the existing Particle/renderParticles() split exactly:
// Particle::draw() doesn't call stampTrail() either.
//
// See test/host/test_large_script.cpp for how this gets compiled and
// what "fits on an ESP32 without PSRAM" is checked against.

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
//external CRGB hsv(int h, int s, int v);
//external void show();
//external void clear();
//external uint32_t rand(uint32_t mod);
//external void delay(uint32_t d);
//external void printfln(char *fmt, Args a);
//external uint32_t millis();
///external float sin(float h);
//external float hypot(float x, float y);
//external void pinInterrupt(uint32_t handle, char *fname, int pin);

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

// wrapInt/smoothstep/manhattanDistance were added specifically to grow
// this fixture's instruction size (alongside Ripple/Comet growing its
// data size) without touching gcd()/fib()/isPrime()/clampInt() --
// test/qemu/gen_large_script.cpp extracts and calls those four directly
// under real Xtensa emulation with hardcoded expected values, so their
// signatures and bodies need to stay exactly as they are.

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

// Counts particle pairs currently within a small radius of each other --
// another diagnostics-only addition (see runDiagnostics()) exercising
// manhattanDistance() and a nested loop over the particle array, purely
// to grow this fixture's compiled size further. Placed right here
// (before runDiagnostics(), which calls it), not down near the trail
// buffer section where it's thematically closer -- calling a
// not-yet-defined free function doesn't resolve even outside a struct
// method; nothing in this codebase had a forward call like that either
// until this fixture's expansion tripped over it (see Comet's own
// note above about the same limitation from inside a struct method).
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
// Insertion sort of particle indices by remaining life, so older
// (shorter-lived) particles get drawn first and newer ones land on top
// when they overlap on the same pixel.

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

// Same insertion sort as sortParticlesByLife(), over stars' brightness
// instead of particles' remaining life -- added alongside Ripple/Comet
// specifically to grow this fixture's instruction size further; kept as
// a near-exact mirror of the already-working sort above rather than a
// new algorithm, to avoid exercising more untested language corners.
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

// A small ring buffer of recent trailChecksum() results -- another
// addition purely to grow this fixture's compiled size, called only
// from main() (which is always the last function in the file, so no
// forward-reference concern the way an earlier caller would have).
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

// A small always-on-top status bar along row 0, one color per mode --
// purely another size-growing addition (branch-heavy, not array-heavy,
// unlike the ring buffer/sort additions above), called once per frame
// from main()'s loop, after the active effect has drawn.
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
