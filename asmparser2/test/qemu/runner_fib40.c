// Actually executes fib(40) (331,160,281 recursive calls) under QEMU's
// Xtensa CPU emulation and reports both the real hardware-equivalent
// time (via CCOUNT, the same cycle counter register gen_fib_timing.cpp's
// runner uses to *project* this same figure from smaller depths) and
// prints a start/end marker so the wrapping shell can also time actual
// QEMU wall-clock (software emulation) time from outside.
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
    printf("START fib(%d)\n", FIB_N);
    fflush(stdout);

    uint32_t before = readCcount();
    int32_t r = callFib(script_code + FIB_ENTRY_OFFSET, FIB_N);
    uint32_t cycles = readCcount() - before;

    double seconds = (double)cycles / (double)ESP32_CLOCK_HZ;
    printf("RESULT fib(%d)=%d expected=%d cycles=%u seconds_at_240MHz=%.3f %s\n",
           FIB_N, r, FIB_N_EXPECTED, cycles, seconds,
           r == FIB_N_EXPECTED ? "PASS" : "FAIL");
    fflush(stdout);
    return r == FIB_N_EXPECTED ? 0 : 1;
}
