#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "script_bytes.h"

static void callVoid(void *entry)
{
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : : "r"(r8) : "memory", "a10");
}

/* Real backing storage for this script's data region. */
static uint8_t dataRegion[DATA_SIZE];

/* Real, genuinely-on-target implementations -- this is the whole point
   of the "bind alloc()/free() as ordinary externals" answer: a
   script-held uint32_t address is just newlib's own malloc() return
   value, reinterpreted, exactly the same way any other external
   pointer (an internal global's address, an external array's base)
   already crosses the script/host boundary in this compiler. */
static uint32_t target_alloc(int32_t size)
{
    return (uint32_t)(uintptr_t)malloc((size_t)size);
}
static void target_free(uint32_t p)
{
    free((void *)(uintptr_t)p);
}
static void target_writeInt(uint32_t p, int32_t offset, int32_t value)
{
    *(int32_t *)((uint8_t *)(uintptr_t)p + offset) = value;
}
static int32_t target_readInt(uint32_t p, int32_t offset)
{
    return *(int32_t *)((uint8_t *)(uintptr_t)p + offset);
}

int main()
{
    printf("START alloc\n");
    fflush(stdout);

    for (int i = 0; i < NUM_RELOC_SLOTS; i++)
    {
        uint32_t content = (uint32_t)dataRegion + reloc_slot_bincode[i];
        *(uint32_t *)(script_code + reloc_slot_offset[i]) = content;
    }

    for (int i = 0; i < NUM_DATA3_RECORDS; i++)
        memcpy(dataRegion + data3_dest_addr[i], data3_blob + data3_blob_offset[i], data3_size[i]);

    for (int i = 0; i < NUM_EXT_SLOTS; i++)
    {
        uint32_t content = 0;
        if (strcmp(ext_name[i], "alloc") == 0)
            content = (uint32_t)(uintptr_t)target_alloc;
        else if (strcmp(ext_name[i], "free") == 0)
            content = (uint32_t)(uintptr_t)target_free;
        else if (strcmp(ext_name[i], "writeInt") == 0)
            content = (uint32_t)(uintptr_t)target_writeInt;
        else if (strcmp(ext_name[i], "readInt") == 0)
            content = (uint32_t)(uintptr_t)target_readInt;
        else if (strcmp(ext_name[i], "printfln") == 0)
        {
            /* printfln is a compiler built-in normally auto-registered
               by registerBuiltinRuntimeFunctions() -- this generator
               never calls Parser::parse()'s full pipeline through that
               path, so bind it here directly to a real varargs printf,
               same as every other QEMU repro's own target_printfln. */
            extern void target_printfln_impl(const char *format, ...);
            content = (uint32_t)(uintptr_t)target_printfln_impl;
        }
        else
        {
            printf("RESULT unknown external '%s' -- runner doesn't implement it\n", ext_name[i]);
            fflush(stdout);
            return 1;
        }
        *(uint32_t *)(script_code + ext_slot_offset[i]) = content;
        printf("patched ext '%s' slot @%u -> %p\n", ext_name[i], ext_slot_offset[i], (void *)(uintptr_t)content);
    }
    fflush(stdout);

    printf("calling main() -- alloc(40), write i*i into 10 slots, sum them back, printfln, free...\n");
    fflush(stdout);
    callVoid(script_code + MAIN_ENTRY_OFFSET);
    printf("main() returned OK\n");
    fflush(stdout);

    /* 0^2+1^2+...+9^2 = 285. Checked against the value target_printfln_impl
       actually captured from the script's own printfln() call -- not just
       "didn't crash" -- so this fails loudly if alloc()/writeInt()/
       readInt() round-trip through the wrong address or the wrong bytes. */
    extern int32_t g_captured_sum;
    extern int g_captured_sum_valid;
    if (!g_captured_sum_valid)
    {
        printf("RESULT FAIL (printfln never called -- script didn't reach it)\n");
        fflush(stdout);
        return 1;
    }
    if (g_captured_sum != 285)
    {
        printf("RESULT FAIL (sum=%d, expected 285)\n", g_captured_sum);
        fflush(stdout);
        return 1;
    }

    printf("RESULT PASS\n");
    fflush(stdout);
    return 0;
}

#include <stdarg.h>
int32_t g_captured_sum = 0;
int g_captured_sum_valid = 0;
void target_printfln_impl(const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    vprintf(format, argp);
    printf("\r\n");
    if (strcmp(format, "sum of squares 0..9 = %d") == 0)
    {
        va_list argp2;
        va_start(argp2, format);
        g_captured_sum = va_arg(argp2, int32_t);
        g_captured_sum_valid = 1;
        va_end(argp2);
    }
    va_end(argp);
}
