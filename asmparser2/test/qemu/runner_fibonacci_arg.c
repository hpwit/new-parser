#include <stdio.h>
#include <stdint.h>
#include "script_bytes.h"

static int32_t callInt(void *entry, int32_t arg)
{
    register int32_t r10 __asm__("a10") = arg;
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : "+r"(r10) : "r"(r8) : "memory");
    return r10;
}

/* Real backing storage for this script's data region (fib's own 4 bytes,
   plus every other header reservation sharing it -- see
   gen_arr_index.cpp's header comment for why all of them need
   patching). */
static uint8_t dataRegion[FIB_DATA_SIZE];

int main()
{
    for (int i = 0; i < NUM_RELOC_SLOTS; i++)
    {
        uint32_t content = (uint32_t)dataRegion + reloc_slot_bincode[i];
        *(uint32_t *)(script_code + reloc_slot_offset[i]) = content;
    }

    /* Same convention as asm_execute.cpp's runExecutableWithArgs()/
       callFunction(): the argument goes directly in a10 (the callee sees
       it as its own a2, per the windowed call8 ABI), no wrapper or
       reserved stack area involved. */
    callInt(script_code + MAIN_ENTRY_OFFSET, FIB_N);
    int32_t fib = callInt(script_code + GETFIB_ENTRY_OFFSET, 0);

    printf("RESULT fib(%d)=%d expected=%d %s\n", FIB_N, fib, EXPECTED_FIB,
           fib == EXPECTED_FIB ? "PASS" : "FAIL");
    fflush(stdout);
    return fib == EXPECTED_FIB ? 0 : 1;
}
