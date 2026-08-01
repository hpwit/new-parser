// Multi-Effect LED Matrix Controller
//
// A synthetic but realistic-scale .sc script for a WIDTH x HEIGHT LED
// matrix, written to exercise this compiler at a scale closer to a real
// project than the hand-written snippets in test_parser.cpp: three
// switchable effects (particle fountain, twinkling stars, plasma), a
// struct-array + constructor pattern for each moving object, an
// internal (non-external) 2D trail buffer with its own fade/stamp/
// checksum logic, a button-driven mode switch, and a handful of
// general-purpose math helpers (recursive gcd/fibonacci, a prime sieve
// check, insertion sort) that aren't specific to LEDs at all -- there to
// exercise recursion and non-trivial expressions, not because a real
// effect needs them.
//
// See test/host/test_large_script.cpp for how this gets compiled and
// what "fits on an ESP32 without PSRAM" is checked against.

#define WIDTH 64
#define HEIGHT 32
#define NUM_PARTICLES 40
#define NUM_STARS 60

#define MODE_PARTICLES 0
#define MODE_STARS 1
#define MODE_PLASMA 2
#define NUM_MODES 3

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
external void printfln(char *fmt, Args a);
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

Particle particles[NUM_PARTICLES];
Star stars[NUM_STARS];
int sortIndex[NUM_PARTICLES];

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
   {
      stars[i].update();
      stars[i].draw();
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

// ==================== main ====================

int main()
{
   clear();
   runDiagnostics();

   pinInterrupt(_execaddr_, "onModeButton", 4);

   uint32_t initialChecksum = trailChecksum();
   printfln("initial trail checksum: %d", initialChecksum);

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
      else
      {
         renderPlasma();
      }

      frame++;
      if (frame % 500 == 0)
      {
         uint32_t cs = trailChecksum();
         printfln("trail checksum: %d", cs);
      }

      show();
      delay(16);
   }
}
