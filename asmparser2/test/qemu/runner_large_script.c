#include <stdio.h>
#include <stdint.h>
#include "script_bytes.h"

/* Matches asm_execute.cpp's callXtensaDirect: arguments in a10, a11, a12...
   (what the callee sees as its own a2, a3, a4 per the windowed call8 ABI),
   call via callx8, result read back from a10. */
static int32_t call1(void *entry, int32_t a0)
{
    register int32_t r10 __asm__("a10") = a0;
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : "+r"(r10) : "r"(r8) : "memory");
    return r10;
}

static int32_t call2(void *entry, int32_t a0, int32_t a1)
{
    register int32_t r10 __asm__("a10") = a0;
    register int32_t r11 __asm__("a11") = a1;
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : "+r"(r10) : "r"(r8), "r"(r11) : "memory");
    return r10;
}

static int32_t call3(void *entry, int32_t a0, int32_t a1, int32_t a2)
{
    register int32_t r10 __asm__("a10") = a0;
    register int32_t r11 __asm__("a11") = a1;
    register int32_t r12 __asm__("a12") = a2;
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : "+r"(r10) : "r"(r8), "r"(r11), "r"(r12) : "memory");
    return r10;
}

static int check(const char *label, int32_t got, int32_t expected)
{
    int pass = (got == expected);
    printf("%s: got=%d expected=%d %s\n", label, got, expected, pass ? "PASS" : "FAIL");
    return pass;
}

int main()
{
    int allPass = 1;

    /* gcd(48, 18) = 6 */
    allPass &= check("gcd(48,18)",
                      call2(script_code + GCD_ENTRY_OFFSET, 48, 18), 6);

    /* fib(12) = 144 (fib(0)=0, fib(1)=1, ...) */
    allPass &= check("fib(12)",
                      call1(script_code + FIB_ENTRY_OFFSET, 12), 144);

    /* isPrime(29) = true, isPrime(28) = false, isPrime(2) = true */
    allPass &= check("isPrime(29)",
                      call1(script_code + ISPRIME_ENTRY_OFFSET, 29), 1);
    allPass &= check("isPrime(28)",
                      call1(script_code + ISPRIME_ENTRY_OFFSET, 28), 0);
    allPass &= check("isPrime(2)",
                      call1(script_code + ISPRIME_ENTRY_OFFSET, 2), 1);

    /* clampInt(v, lo, hi) */
    allPass &= check("clampInt(300,0,255)",
                      call3(script_code + CLAMPINT_ENTRY_OFFSET, 300, 0, 255), 255);
    allPass &= check("clampInt(-5,0,255)",
                      call3(script_code + CLAMPINT_ENTRY_OFFSET, -5, 0, 255), 0);
    allPass &= check("clampInt(100,0,255)",
                      call3(script_code + CLAMPINT_ENTRY_OFFSET, 100, 0, 255), 100);

    printf("RESULT %s\n", allPass ? "PASS" : "FAIL");
    fflush(stdout);
    return allPass ? 0 : 1;
}
