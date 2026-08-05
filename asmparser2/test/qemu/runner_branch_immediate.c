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
    int32_t r1 = *(volatile int32_t *)(frame_base + R1_STACK_OFFSET);
    int32_t r2 = *(volatile int32_t *)(frame_base + R2_STACK_OFFSET);

    int pass = (r1 == EXPECTED_R1) && (r2 == EXPECTED_R2);
    printf("RESULT r1=%d expected=%d r2=%d expected=%d %s\n", r1, EXPECTED_R1, r2, EXPECTED_R2,
           pass ? "PASS" : "FAIL");
    fflush(stdout);
    return pass ? 0 : 1;
}
