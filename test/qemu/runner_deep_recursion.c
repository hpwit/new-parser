// Runs gen_deep_recursion.cpp's function at depth 40 and checks that every
// frame's stack locals survived the recursion -- see that file's header.
#include <stdio.h>
#include <stdint.h>
#include "script_bytes.h"

static int32_t callFn(void *entry, int32_t n)
{
    register int32_t r10 __asm__("a10") = n;
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : "+r"(r10) : "r"(r8) : "memory");
    return r10;
}

int main()
{
    int32_t r = callFn(script_code + FIB_ENTRY_OFFSET, 40);
    printf("depth 40: %d frames kept their stack locals (expected 40)\n", (int)r);
    printf("RESULT %s\n", r == 40 ? "PASS" : "FAIL");
    fflush(stdout);
    return 0;
}
