#include <stdio.h>
#include <stdint.h>
#include "script_bytes.h"

/* Matches asm_execute.cpp's callXtensaDirect exactly: places the argument
   in a10 (which the callee sees as its own a2, per the windowed call8
   ABI -- the same register main() itself would use to call fib(10)
   directly) and calls via callx8, reading the result back from a10. */
static int32_t callFib(void *entry, int32_t n)
{
    register int32_t r10 __asm__("a10") = n;
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : "+r"(r10) : "r"(r8) : "memory");
    return r10;
}

int main()
{
    int32_t fib = callFib(script_code + FIB_ENTRY_OFFSET, FIB_N);

    printf("RESULT fib(%d)=%d expected=%d %s\n", FIB_N, fib, EXPECTED_FIB,
           fib == EXPECTED_FIB ? "PASS" : "FAIL");
    fflush(stdout);
    return fib == EXPECTED_FIB ? 0 : 1;
}
