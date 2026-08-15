// ESP-IDF port of examples/FibonacciTiming/FibonacciTiming.ino -- times
// fib(40) on-device with esp_timer_get_time() (ESP-IDF's own
// microsecond-resolution monotonic clock) instead of Arduino's micros().
//
// fib(40) with this naive (no memoization) recursion makes
// 2*fib(41)-1 = 331,160,281 calls -- a real stress test of this
// compiler's call/return overhead. Measured under QEMU: ~6.1 cycles per
// call for the actual compiled fib() bytes, which projects to roughly
// 8.4 seconds on real hardware at the ESP32's default 240 MHz -- see
// FibonacciTiming.ino's header comment for the full measurement.
//
// Needs sdkconfig.defaults' CONFIG_ESP_MAIN_TASK_STACK_SIZE bump (16KB,
// up from ESP-IDF's 3584-byte default) -- app_main() runs directly on
// the "main task", and fib(40)'s leftmost recursive descent reaches ~39
// stack frames deep almost immediately (well before any output), each
// one using this compiler's windowed-register spill plus its own
// per-call stack-scratch slot. Without the bump this reliably overflows
// the default stack and corrupts the adjacent heap -- surfaces as a
// TLSF "block_locate_free" assert on the very next allocation, not as
// an obvious stack-overflow panic. Not a compiler bug and not
// QEMU-specific -- the same FreeRTOS task-stack sizing issue would hit
// real hardware too; Arduino's own loop-task stack (8KB) is just large
// enough that the .ino equivalent never hits it.
#include "script_executable.h"
#include "esp_timer.h"

static char script[] = R"EOF(
int fib(int n)
{
   if (n < 2)
   {
      return n;
   }
   return fib(n - 1) + fib(n - 2);
}

void main()
{
}
)EOF";

extern "C" void app_main(void)
{
   ScriptExecutable exec = parseScript(script);
   if (!exec.isExeExists())
   {
      return;
   }

   Arguments args;
   args.add(40);
   int32_t result = 0;

   int64_t startMicros = esp_timer_get_time();
   bool ok = exec.execute("fib", &args, &result);
   int64_t elapsedMicros = esp_timer_get_time() - startMicros;

   if (ok)
   {
      printf("fib(40) = %d, took %lld us (%.3f s)\n",
             (int)result, (long long)elapsedMicros, elapsedMicros / 1000000.0);
   }
   else
   {
      printf("execute(\"fib\") failed\n");
   }
}
