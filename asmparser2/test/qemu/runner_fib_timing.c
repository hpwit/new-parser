// Real-hardware-cycle-count check for examples/FibonacciTiming.ino's
// fib() -- naive recursive fib(40)/fib(50) make 331,160,281 and
// 40,730,022,147 calls respectively, far too many to actually simulate
// under QEMU's software emulation in a reasonable time. Instead this
// measures real Xtensa cycles (via CCOUNT, the same register the
// sc_examples corpus's own millis()/elapseMillis() __ASM__ functions
// use) for two much smaller, tractable depths, checks the resulting
// cycles-per-call figures agree (proving it's a real, linear, per-call
// cost and not a fluke), and projects fib(40)/fib(50)'s real-hardware
// time from that -- not a guess, since every fib() call does identical
// work regardless of recursion depth.
#include <stdio.h>
#include <stdint.h>
#include "script_bytes.h"

static int32_t callFib(void *entry, int32_t n)
{
    register int32_t r10 __asm__("a10") = n;
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : "+r"(r10) : "r"(r8) : "memory");
    return r10;
}

static inline uint32_t readCcount(void)
{
    uint32_t c;
    __asm__ volatile("rsr.ccount %0" : "=r"(c));
    return c;
}

int main()
{
    int allPass = 1;

    uint32_t before1 = readCcount();
    int32_t r1 = callFib(script_code + FIB_ENTRY_OFFSET, FIB_N1);
    uint32_t cycles1 = readCcount() - before1;
    double perCall1 = (double)cycles1 / (double)FIB_N1_CALLS;

    uint32_t before2 = readCcount();
    int32_t r2 = callFib(script_code + FIB_ENTRY_OFFSET, FIB_N2);
    uint32_t cycles2 = readCcount() - before2;
    double perCall2 = (double)cycles2 / (double)FIB_N2_CALLS;

    printf("fib(%d) = %d (expected %d) cycles=%u calls=%ld cycles/call=%.3f\n",
           FIB_N1, r1, FIB_N1_EXPECTED, cycles1, FIB_N1_CALLS, perCall1);
    printf("fib(%d) = %d (expected %d) cycles=%u calls=%ld cycles/call=%.3f\n",
           FIB_N2, r2, FIB_N2_EXPECTED, cycles2, FIB_N2_CALLS, perCall2);

    if (r1 != FIB_N1_EXPECTED || r2 != FIB_N2_EXPECTED)
    {
        printf("FAIL: wrong fib() result\n");
        allPass = 0;
    }

    /* The two cycles-per-call figures should agree closely (same code,
       same per-call cost, just measured over an ~11x difference in call
       count). Repeated runs of this exact check (on both esp32 and
       esp32s3 QEMU machines, ~15 runs total) mostly landed within 3-6%,
       occasionally spiked to ~11%, and once reached 18% (likely periodic
       timer-interrupt jitter in the runtime, not the fib() codegen
       itself -- the underlying per-call cost stayed in a narrow 5.8-6.5
       cycles/call band across every run regardless of what this ratio
       read). 25% comfortably covers the observed noise range (including
       that one outlier) while still catching a genuinely broken (non-
       linear) measurement, e.g. one off by 2x. */
    double ratio = perCall1 > perCall2 ? perCall1 / perCall2 : perCall2 / perCall1;
    printf("cycles/call agreement: %.2fx (want <= 1.25x)\n", ratio);
    if (ratio > 1.25)
    {
        printf("FAIL: cycles/call didn't agree between the two measurements -- "
               "per-call cost isn't behaving as a constant, projection below is unreliable\n");
        allPass = 0;
    }

    double avgPerCall = (perCall1 + perCall2) / 2.0;

    double projectedCycles3 = avgPerCall * (double)FIB_N3_CALLS;
    double projectedSeconds3 = projectedCycles3 / (double)ESP32_CLOCK_HZ;
    printf("projected fib(%d): %ld calls * %.3f cycles/call = %.0f cycles -> %.2f s at %.0f MHz\n",
           FIB_N3, FIB_N3_CALLS, avgPerCall, projectedCycles3, projectedSeconds3,
           ESP32_CLOCK_HZ / 1000000.0);

    /* FIB_N4_CALLS (40,730,022,147) exceeds a 32-bit long's range on this
       target -- long long, matching the LL suffix gen_fib_timing.cpp
       emits it with. */
    double projectedCycles4 = avgPerCall * (double)FIB_N4_CALLS;
    double projectedSeconds4 = projectedCycles4 / (double)ESP32_CLOCK_HZ;
    printf("projected fib(%d): %lld calls * %.3f cycles/call = %.0f cycles -> %.2f s (%.2f min) at %.0f MHz\n",
           FIB_N4, FIB_N4_CALLS, avgPerCall, projectedCycles4, projectedSeconds4,
           projectedSeconds4 / 60.0, ESP32_CLOCK_HZ / 1000000.0);

    printf("RESULT %s\n", allPass ? "PASS" : "FAIL");
    fflush(stdout);
    return allPass ? 0 : 1;
}
