// Bare-metal QEMU runner for one fuzz program (see fuzz.py). Applies the
// relocations listed in script_bytes.h (from compile_script.cpp), binds
// the host functions the generated programs may call, runs main(), and
// prints everything the program emitted, one "EMIT <value>" line each,
// then "DONE". A crash or a hang shows up as a missing DONE.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "script_bytes.h"

static void callVoid(void *entry)
{
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : : "r"(r8) : "memory", "a10");
}

static uint8_t dataRegion[DATA_SIZE];

// Must match the definitions fuzz.py writes into the C reference.
static void host_emit(int v)
{
    printf("EMIT %d\n", v);
}
static int host_ext1(int v)
{
    return v * 3 + 1;
}
static int host_ext2(int a, int b)
{
    return a - 2 * b;
}

int main()
{
    for (int i = 0; i < NUM_RELOC_SLOTS; i++)
        *(uint32_t *)(script_code + reloc_slot_offset[i]) = (uint32_t)dataRegion + reloc_slot_bincode[i];
    for (int i = 0; i < NUM_DATA3_RECORDS; i++)
        memcpy(dataRegion + data3_dest_addr[i], data3_blob + data3_blob_offset[i], data3_size[i]);
    if (NUM_VAR_SLOTS > 0)
    {
        printf("UNSUPPORTED external variable %s\n", var_name[0]);
        return 1;
    }
    for (int i = 0; i < NUM_EXT_SLOTS; i++)
    {
        uint32_t c = 0;
        // emitf(ffloat): the raw Q16.16 bits, compared exactly with a C model.
        if (!strcmp(ext_name[i], "emit") || !strcmp(ext_name[i], "emitf"))
            c = (uint32_t)(uintptr_t)host_emit;
        else if (!strcmp(ext_name[i], "ext1"))
            c = (uint32_t)(uintptr_t)host_ext1;
        else if (!strcmp(ext_name[i], "ext2"))
            c = (uint32_t)(uintptr_t)host_ext2;
        else
        {
            printf("UNSUPPORTED external function %s\n", ext_name[i]);
            return 1;
        }
        *(uint32_t *)(script_code + ext_slot_offset[i]) = c;
    }
#if HAS_FOOTER
    callVoid(script_code + FOOTER_ENTRY_OFFSET);
#endif
    callVoid(script_code + MAIN_ENTRY_OFFSET);
    printf("DONE\n");
    fflush(stdout);
    return 0;
}
