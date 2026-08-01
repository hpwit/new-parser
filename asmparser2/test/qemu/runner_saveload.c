#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "script_bytes.h"

#define SCRIPT_BINARY_MAGIC 0x31504353u

static int32_t host_key_char = 0;
static volatile int32_t reported_value = -999;

void hostReport(int32_t v)
{
    reported_value = v;
}

/* Matches asm_execute.cpp's callXtensaDirect exactly: no arguments (the
   script reads key_char through the patched jump table slot, not a
   parameter), calls via callx8, reads the result back from a10. */
static int32_t callKeyboard(void *entry)
{
    register int32_t r10 __asm__("a10") = 0;
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : "+r"(r10) : "r"(r8) : "memory");
    return r10;
}

static int checkKey(unsigned char *code, int32_t key, int32_t expected)
{
    host_key_char = key;
    reported_value = -999;
    int32_t result = callKeyboard(code + KEYBOARD_ENTRY_OFFSET);
    int pass = (result == expected) && (reported_value == expected);
    printf("key_char=%d -> returned=%d reported=%d expected=%d %s\n",
           key, result, reported_value, expected, pass ? "PASS" : "FAIL");
    return pass;
}

int main()
{
    /* Parse the tiny serialized-binary header ourselves -- exactly the
       format asm_serialize.cpp's serializeBinary()/deserializeBinary()
       use -- proving the saved bytes are genuinely self-describing, not
       just "skip N bytes because we happen to know the layout". A real
       .ino would call deserializeBinary() directly instead of
       reimplementing this; it's inlined here only because this runner is
       built with the plain Xtensa GCC, not this project's C++ sources. */
    uint32_t magic;
    memcpy(&magic, saved_script, 4);
    if (magic != SCRIPT_BINARY_MAGIC)
    {
        printf("RESULT FAIL (bad magic 0x%08x)\n", magic);
        return 1;
    }
    unsigned char *code = saved_script + SCRIPT_HEADER_SIZE;

    /* Patch the jump table slots for key_char and report with *this*
       process's own host variable/function addresses -- exactly what
       createExecutableFromBinary()'s decodeBinaryHeader (case 1 and case
       2) does at runtime. This runner never saw gen_saveload.cpp's
       script source or its (nonexistent) bindings -- only these
       serialized bytes and its own registration of the same two names --
       which is the whole point: a second, independent "sketch" can run a
       binary compiled and saved elsewhere as long as it binds what the
       script declares external. */
    *(uint32_t *)(code + KEY_CHAR_SLOT_OFFSET) = (uint32_t)&host_key_char;
    *(uint32_t *)(code + REPORT_SLOT_OFFSET) = (uint32_t)hostReport;

    int allPass = 1;
    allPass &= checkKey(code, 0, 1);
    allPass &= checkKey(code, 41, 42);
    allPass &= checkKey(code, 99, 100);

    printf("RESULT %s\n", allPass ? "PASS" : "FAIL");
    fflush(stdout);
    return allPass ? 0 : 1;
}
