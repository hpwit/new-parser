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

/* Real backing storage for this script's data region (g's own 4 bytes,
   plus every other header reservation sharing it -- see
   gen_arr_index.cpp's header comment for why all of them need
   patching). */
static uint8_t dataRegion[G_DATA_SIZE];

int main()
{
    for (int i = 0; i < NUM_RELOC_SLOTS; i++)
    {
        uint32_t content = (uint32_t)dataRegion + reloc_slot_bincode[i];
        *(uint32_t *)(script_code + reloc_slot_offset[i]) = content;
    }

    /* execute()'s order: poison() (sentinel -1, bypassing the
       constructor), then footer() (g's real constructor, count=99),
       then read it back -- footer must overwrite the sentinel. */
    callInt(script_code + POISON_ENTRY_OFFSET, 0);
    callInt(script_code + FOOTER_ENTRY_OFFSET, 0);
    int32_t afterExecute = callInt(script_code + GETCOUNT_ENTRY_OFFSET, 0);
    int passExecute = (afterExecute == 99);
    printf("RESULT execute()-order: getCount()=%d expected=99 (footer's constructor "
           "must overwrite poison()'s -1) %s\n",
           afterExecute, passExecute ? "PASS" : "FAIL");

    /* executeOnly()'s order: poison(), then straight to the target --
       footer never called, the sentinel must survive untouched. */
    callInt(script_code + POISON_ENTRY_OFFSET, 0);
    int32_t afterExecuteOnly = callInt(script_code + GETCOUNT_ENTRY_OFFSET, 0);
    int passExecuteOnly = (afterExecuteOnly == -1);
    printf("RESULT executeOnly()-order: getCount()=%d expected=-1 (footer must stay "
           "skipped, poison()'s -1 must survive) %s\n",
           afterExecuteOnly, passExecuteOnly ? "PASS" : "FAIL");

    int ok = passExecute && passExecuteOnly;
    printf("RESULT %s\n", ok ? "PASS" : "FAIL");
    fflush(stdout);
    return ok ? 0 : 1;
}
