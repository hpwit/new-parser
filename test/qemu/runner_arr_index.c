#include <stdio.h>
#include <stdint.h>
#include "script_bytes.h"

/* Matches asm_execute.cpp's callXtensaDirect: places the argument in a10
   (seen as a2 by the callee, per the windowed call8 ABI) and calls via
   callx8, reading the result back from a10 -- same technique
   runner_named_function.c uses for fib(n). setValues() takes no
   meaningful argument; called the same way with a dummy 0, result
   ignored. */
static int32_t callInt(void *entry, int32_t arg)
{
    register int32_t r10 __asm__("a10") = arg;
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : "+r"(r10) : "r"(r8) : "memory");
    return r10;
}

/* Real backing storage for this script's data region (arr's own 12
   bytes, plus every other header reservation -- _handle_, each
   function's stack-scratch slot -- that shares the same region, sized
   ARR_DATA_SIZE == bin.data_size). On a loaded script
   (createExecutableFromBinary()) this would be a slice of the loader's
   own malloc'd buffer; here, since this runner calls compiled code
   directly with no loader involved, it's just a plain static buffer. */
static uint8_t dataRegion[ARR_DATA_SIZE];

int main()
{
    /* Patch every type-0 (internal global/reserved-slot indirection)
       literal pool slot with a real address -- exactly what
       asm_execute.cpp's decodeBinaryHeader (case 0) does at runtime for
       each one: `content = bincode + data_base`. Here data_base is
       dataRegion's own address instead of a loader-allocated buffer's.
       See gen_arr_index.cpp's header comment for why *all* of them need
       this, not just arr's own slot. */
    for (int i = 0; i < NUM_RELOC_SLOTS; i++)
    {
        uint32_t content = (uint32_t)dataRegion + reloc_slot_bincode[i];
        *(uint32_t *)(script_code + reloc_slot_offset[i]) = content;
    }

    callInt(script_code + SETVALUES_ENTRY_OFFSET, 0);

    int32_t c0 = callInt(script_code + GETCOUNT_ENTRY_OFFSET, 0);
    int32_t c1 = callInt(script_code + GETCOUNT_ENTRY_OFFSET, 1);
    int32_t c2 = callInt(script_code + GETCOUNT_ENTRY_OFFSET, 2);

    int ok = (c0 == EXPECTED_ARR0) && (c1 == EXPECTED_ARR1) && (c2 == EXPECTED_ARR2);
    printf("RESULT arr[0].count=%d arr[1].count=%d arr[2].count=%d expected=%d,%d,%d %s\n",
           c0, c1, c2, EXPECTED_ARR0, EXPECTED_ARR1, EXPECTED_ARR2, ok ? "PASS" : "FAIL");
    fflush(stdout);
    return ok ? 0 : 1;
}
