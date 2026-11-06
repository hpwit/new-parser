#include <stdio.h>
#include <stdint.h>
#include "script_bytes.h"

static int32_t host_key_char = 0;

/* Matches asm_execute.cpp's callXtensaDirect exactly: no arguments (the
   script reads key_char through the patched jump table slot, not a
   parameter), calls via callx8, reads the result back from a10 -- the
   same register the script's `return c;` leaves its value in. */
static int32_t callKeyboard(void *entry)
{
    register int32_t r10 __asm__("a10") = 0;
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t" : "+r"(r10) : "r"(r8) : "memory");
    return r10;
}

static int checkKey(char key, char expected)
{
    /* Exactly what KeyboardCallback.ino's loop() does per keypress:
       write the new key into the bound variable, then re-enter the
       script by name. */
    host_key_char = key;
    int32_t result = callKeyboard(script_code + KEYBOARD_ENTRY_OFFSET);
    int pass = (result == (int32_t)expected);
    printf("key='%c' -> result='%c' expected='%c' %s\n", key, (char)result, expected,
           pass ? "PASS" : "FAIL");
    return pass;
}

int main()
{
    /* Patch the jump table slot for key_char with the real host
       variable's address -- exactly what asm_execute.cpp's
       decodeBinaryHeader (case 1, external_var_label) does at runtime. */
    uint32_t var_addr = (uint32_t)&host_key_char;
    *(uint32_t *)(script_code + KEY_CHAR_SLOT_OFFSET) = var_addr;

    int allPass = 1;
    allPass &= checkKey('a', 'A');
    allPass &= checkKey('B', 'B');
    allPass &= checkKey('z', 'Z');
    allPass &= checkKey('9', '9');
    allPass &= checkKey('!', '!');

    printf("RESULT %s\n", allPass ? "PASS" : "FAIL");
    fflush(stdout);
    return allPass ? 0 : 1;
}
