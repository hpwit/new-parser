#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "script_bytes.h"

static void callVoid(void *entry)
{
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : : "r"(r8) : "memory", "a10");
}

/* Real backing storage for this script's data region -- see
   gen_arr_index.cpp's own runner for why every type-0 slot (not just
   Balls[]'s own) needs patching into it. */
static uint8_t dataRegion[DATA_SIZE];

/* Real, genuinely-on-target implementations for this script's two
   external calls -- rand()/printfln() -- so patching their jump-table
   slots with &these produces a function pointer that's actually valid
   Xtensa code to jump to under QEMU, unlike createExecutableFromBinary()
   patching in a host (Mac) address (see gen_bouncing_repro.cpp's header
   comment for why that path is deliberately avoided here). */
static uint32_t target_rand(uint32_t s)
{
    static uint32_t state = 12345;
    state = state * 1103515245u + 12345u;
    return (state >> 8) % (s + 1);
}

/* Mirrors runtime_functions.cpp's artiPrintfln() exactly -- genuine C
   varargs, called through the same register-passed "char*,Args"
   convention every other Args-typed external uses (confirmed by reading
   visitnode.cpp's call-site codegen: a plain int argument after the
   format string is just placed in the next register, no packed-buffer
   indirection -- so a real C varargs callee should receive it exactly
   like any other varargs call under Xtensa's windowed ABI). This is the
   most important thing this repro tests: whether that assumption is
   actually correct under real Xtensa calling-convention rules, not just
   plausible from reading the codegen. */
static void target_printfln(const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    vprintf(format, argp);
    printf("\r\n");
    va_end(argp);
}

static void target_printf(const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    vprintf(format, argp);
    va_end(argp);
}

int main()
{
    printf("START bouncing_repro\n");
    fflush(stdout);

    /* Matches asm_execute.cpp's createExecutableFromBinary(): the real
       loader heap_caps_malloc()s (MALLOC_CAP_EXEC, on-target) a fresh
       buffer and memcpy()s the compiled bytes into it, then executes
       *that* copy -- it never executes straight out of a statically-
       linked .text array the way earlier versions of this repro did.
       Testing whether that distinction (heap-allocated, dynamically
       placed code vs. a fixed link-time .text address) matters here. */
    unsigned char *heapCode = (unsigned char *)malloc(SCRIPT_CODE_SIZE);
    memcpy(heapCode, script_code, SCRIPT_CODE_SIZE);
    printf("heapCode = %p (script_code was %p)\n", (void *)heapCode, (void *)script_code);
    fflush(stdout);

    for (int i = 0; i < NUM_RELOC_SLOTS; i++)
    {
        uint32_t content = (uint32_t)dataRegion + reloc_slot_bincode[i];
        *(uint32_t *)(heapCode + reloc_slot_offset[i]) = content;
    }

    /* Copy every type-3 (data) record's literal bytes -- string constants,
       float literals -- into dataRegion at their real destination
       address, exactly matching decodeBinaryHeader's own case 3
       (memcpy(_binary_data + addr, ...)). Skipping this (an earlier
       version of this repro did) doesn't crash -- it silently leaves
       whatever string literal(s) the script has pointing at zeroed
       memory, so e.g. printfln's format string reads as "" instead of
       real text. */
    for (int i = 0; i < NUM_DATA3_RECORDS; i++)
        memcpy(dataRegion + data3_dest_addr[i], data3_blob + data3_blob_offset[i], data3_size[i]);

    for (int i = 0; i < NUM_EXT_SLOTS; i++)
    {
        uint32_t content = 0;
        if (strcmp(ext_name[i], "rand") == 0)
            content = (uint32_t)(uintptr_t)target_rand;
        else if (strcmp(ext_name[i], "printfln") == 0)
            content = (uint32_t)(uintptr_t)target_printfln;
        else if (strcmp(ext_name[i], "printf") == 0)
            content = (uint32_t)(uintptr_t)target_printf;
        else
        {
            printf("RESULT unknown external '%s' -- runner doesn't implement it\n", ext_name[i]);
            fflush(stdout);
            return 1;
        }
        *(uint32_t *)(heapCode + ext_slot_offset[i]) = content;
        printf("patched ext '%s' slot @%u -> %p\n", ext_name[i], ext_slot_offset[i], (void *)(uintptr_t)content);
    }
    fflush(stdout);

#if HAS_FOOTER
    printf("calling footer() -- constructs Balls[20], each ball() calling printfln(\"test\")...\n");
    fflush(stdout);
    callVoid(heapCode + FOOTER_ENTRY_OFFSET);
    printf("footer() returned OK\n");
    fflush(stdout);
#endif

    printf("calling diag() -- printf(\"tesxt\\n\")...\n");
    fflush(stdout);
    callVoid(heapCode + DIAG_ENTRY_OFFSET);
    printf("diag() returned OK\n");
    fflush(stdout);

    printf("RESULT PASS\n");
    fflush(stdout);
    return 0;
}
