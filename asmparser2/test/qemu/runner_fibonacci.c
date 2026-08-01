#include <stdio.h>
#include <stdint.h>
#include "script_bytes.h"

int main()
{
    uint32_t caller_a1;
    __asm__ volatile("mov %0, a1" : "=r"(caller_a1));

    void (*fn)() = (void (*)())(script_code + SCRIPT_ENTRY_OFFSET);
    fn();

    uint32_t frame_base = caller_a1 - SCRIPT_ENTRY_FRAME_SIZE;
    int32_t fib = *(volatile int32_t *)(frame_base + A_STACK_OFFSET);

    printf("RESULT fib=%d expected=%d %s\n", fib, EXPECTED_FIB,
           fib == EXPECTED_FIB ? "PASS" : "FAIL");
    fflush(stdout);
    return fib == EXPECTED_FIB ? 0 : 1;
}
