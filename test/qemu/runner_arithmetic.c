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
    int32_t sum = *(volatile int32_t *)(frame_base + SUM_STACK_OFFSET);

    printf("RESULT sum=%d expected=%d %s\n", sum, EXPECTED_SUM,
           sum == EXPECTED_SUM ? "PASS" : "FAIL");
    fflush(stdout);
    return sum == EXPECTED_SUM ? 0 : 1;
}
