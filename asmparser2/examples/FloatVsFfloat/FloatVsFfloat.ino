// float vs ffloat: the same calculation written twice in one script --
// simFloat() with float, simFfloat() with ffloat (32-bit fixed point,
// 16 integer and 16 fraction bits, computed with integer instructions) --
// each run 100 times, timed with micros(). Prints the time per run, the
// speed ratio and both results.
//
// The calculation uses every kind of operation:
//   - 64 particles moved for 20 steps under gravity and drag, bouncing on
//     the floor and on two walls (multiplies, adds, comparisons, arrays);
//   - their kinetic energy and centroid (a division by an integer);
//   - the square root of the energy by Newton's method (six divisions);
//   - a cubic polynomial per particle (Horner's scheme).
// Each function returns a checksum: its four results times 100, added up.
// The two differ by a few hundredths -- ffloat's rounding: 16298 for
// float, 16296 for ffloat (checked under QEMU, and against C models).
//
// What to expect. The ESP32 and ESP32-S3 have a hardware floating-point
// unit: a float add or multiply is one instruction, as an integer one is,
// while an ffloat multiply takes four (mulsh, mull, ssai, src). Float
// division, on the other hand, is a software routine, where ffloat
// divides by an integer with one quos. Counted under QEMU, one call of
// each executes about the same number of instructions (about 132,000 for
// float, 134,000 for ffloat), so the difference you see here comes down
// to the chip's real cycle timings -- which only a real board measures.
// ffloat's other advantages are that it is exact and repeatable (the same
// bits on every chip) and that it does not need an FPU at all.
#include "script_executable.h"

char script[] = R"EOF(
#define N 64
#define STEPS 20

float px[N], py[N], vx[N], vy[N];
ffloat qx[N], qy[N], wx[N], wy[N];

// Particles under gravity and drag, bouncing on the floor and two walls,
// then their kinetic energy, centroid, the energy's square root (Newton's
// method: six divisions) and a cubic evaluated per particle (Horner).
// Returns a checksum: the four results times 100, added up.
int simFloat()
{
   for (int i = 0; i < N; i++)
   {
      px[i] = i * 1.5;
      py[i] = 10 + i * 0.25;
      vx[i] = 0.3 + i * 0.01;
      vy[i] = 0;
   }
   for (int step = 0; step < STEPS; step++)
   {
      for (int i = 0; i < N; i++)
      {
         vy[i] = vy[i] - 0.981 * 0.05;
         vx[i] = vx[i] * 0.995;
         px[i] = px[i] + vx[i] * 0.5;
         py[i] = py[i] + vy[i] * 0.5;
         if (py[i] < 0)
         {
            py[i] = -py[i];
            vy[i] = -vy[i] * 0.8;
         }
         if (px[i] > 100)
         {
            px[i] = 200 - px[i];
            vx[i] = -vx[i];
         }
         if (px[i] < 0)
         {
            px[i] = -px[i];
            vx[i] = -vx[i];
         }
      }
   }
   float energy = 0;
   float cx = 0;
   float poly = 0;
   for (int i = 0; i < N; i++)
   {
      energy = energy + (vx[i] * vx[i] + vy[i] * vy[i]) * 0.5;
      cx = cx + px[i];
      float t = py[i] * 0.1;
      poly = poly + ((0.25 * t - 1.5) * t + 2.75) * t - 0.5;
   }
   cx = cx / N;
   float root = energy / 2 + 1;
   for (int k = 0; k < 6; k++)
      root = (root + energy / root) * 0.5;
   int check = (int)(energy * 100);
   check = check + (int)(cx * 100);
   check = check + (int)(root * 100);
   check = check + (int)(poly * 100);
   return check;
}

// The same, line for line, in ffloat.
int simFfloat()
{
   for (int i = 0; i < N; i++)
   {
      qx[i] = i * 1.5;
      qy[i] = 10 + i * 0.25;
      wx[i] = 0.3 + i * 0.01;
      wy[i] = 0;
   }
   for (int step = 0; step < STEPS; step++)
   {
      for (int i = 0; i < N; i++)
      {
         wy[i] = wy[i] - 0.981 * 0.05;
         wx[i] = wx[i] * 0.995;
         qx[i] = qx[i] + wx[i] * 0.5;
         qy[i] = qy[i] + wy[i] * 0.5;
         if (qy[i] < 0)
         {
            qy[i] = -qy[i];
            wy[i] = -wy[i] * 0.8;
         }
         if (qx[i] > 100)
         {
            qx[i] = 200 - qx[i];
            wx[i] = -wx[i];
         }
         if (qx[i] < 0)
         {
            qx[i] = -qx[i];
            wx[i] = -wx[i];
         }
      }
   }
   ffloat energy = 0;
   ffloat cx = 0;
   ffloat poly = 0;
   for (int i = 0; i < N; i++)
   {
      energy = energy + (wx[i] * wx[i] + wy[i] * wy[i]) * 0.5;
      cx = cx + qx[i];
      ffloat t = qy[i] * 0.1;
      poly = poly + ((0.25 * t - 1.5) * t + 2.75) * t - 0.5;
   }
   cx = cx / N;
   ffloat root = energy / 2 + 1;
   for (int k = 0; k < 6; k++)
      root = (root + energy / root) * 0.5;
   int check = (int)(energy * 100);
   check = check + (int)(cx * 100);
   check = check + (int)(root * 100);
   check = check + (int)(poly * 100);
   return check;
}
)EOF";

static ScriptExecutable *g_exec = NULL;

// Runs `function` `runs` times; returns the total time in microseconds and
// the function's result (the checksum) in *result.
static uint32_t timeRuns(const char *function, int runs, int32_t *result)
{
   uint32_t start = micros();
   for (int i = 0; i < runs; i++)
      g_exec->executeOnly(function, result);
   return micros() - start;
}

static void benchmark()
{
   const int runs = 100;
   int32_t floatResult = 0, ffloatResult = 0;

   // One call each first, so that both measurements start alike.
   g_exec->executeOnly("simFloat", &floatResult);
   g_exec->executeOnly("simFfloat", &ffloatResult);

   uint32_t floatTime = timeRuns("simFloat", runs, &floatResult);
   uint32_t ffloatTime = timeRuns("simFfloat", runs, &ffloatResult);

   printf("float : %d runs in %lu us, %lu us per run, checksum %ld\n", runs, (unsigned long)floatTime,
          (unsigned long)(floatTime / runs), (long)floatResult);
   printf("ffloat: %d runs in %lu us, %lu us per run, checksum %ld\n", runs, (unsigned long)ffloatTime,
          (unsigned long)(ffloatTime / runs), (long)ffloatResult);
   if (floatTime > 0 && ffloatTime > 0)
   {
      if (ffloatTime < floatTime)
         printf("ffloat is %.2f times faster than float\n\n", (double)floatTime / ffloatTime);
      else
         printf("ffloat is %.2f times slower than float\n\n", (double)ffloatTime / floatTime);
   }
}

void setup()
{
   Serial.begin(115200);

   static ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      printf("script failed to compile/load\n");
      return;
   }
   g_exec = &exec;
   benchmark();
}

void loop()
{
   // The measurement again every 5 seconds: the numbers should hardly move.
   delay(5000);
   if (g_exec != NULL)
      benchmark();
}
