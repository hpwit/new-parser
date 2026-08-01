#include <stdio.h>
#include <stdint.h>
#include "script_bytes.h"

static volatile int32_t received_value = -999;

void hostSetResult(int32_t v)
{
    received_value = v;
}

int main()
{
    /* Patch the jump table slot for setResult with the real host
       function's address -- exactly what asm_execute.cpp's
       decodeBinaryHeader (case 2, external_call) does at runtime. */
    uint32_t fn_addr = (uint32_t)hostSetResult;
    *(uint32_t *)(script_code + SETRESULT_SLOT_OFFSET) = fn_addr;

    void (*fn)() = (void (*)())(script_code + ENTRY_OFFSET);
    fn();

    printf("RESULT received=%d expected=%d %s\n", received_value, EXPECTED_RESULT,
           received_value == EXPECTED_RESULT ? "PASS" : "FAIL");
    fflush(stdout);
    return received_value == EXPECTED_RESULT ? 0 : 1;
}
