#include <stdio.h>
#include <stdint.h>
#include "script_bytes.h"

/* Scratch data region -- what asm_execute.cpp's separate exe.data
   allocation stands in for here. */
static uint8_t data_buffer[64];

int main()
{
    /* Replicates decodeBinaryHeader's case-0 handling for the function's
       own argument-storage reservation: patch the jump table slot with a
       pointer into the data buffer. */
    uint32_t arg_area_addr = (uint32_t)(data_buffer + ARG_SLOT_DATA_OFFSET);
    *(uint32_t *)(script_code + ARG_SLOT_TABLE_OFFSET) = arg_area_addr;

    /* Replicates runExecutableWithArgs: write the argument value where
       the wrapper's l32r/l32i pair will find it. */
    *(int32_t *)(data_buffer + ARG_SLOT_DATA_OFFSET) = FIB_N;

    uint32_t caller_a1;
    __asm__ volatile("mov %0, a1" : "=r"(caller_a1));

    void (*fn)() = (void (*)())(script_code + WRAPPER_ENTRY_OFFSET);
    fn();

    /* The wrapper's own entry a1,WRAPPER_FRAME_SIZE runs before it
       call8's the real function, which allocates its own frame on top --
       "a" lives inside the *real* function's frame. */
    uint32_t real_fn_frame_base = caller_a1 - WRAPPER_FRAME_SIZE - REAL_FN_FRAME_SIZE;
    int32_t fib = *(volatile int32_t *)(real_fn_frame_base + A_STACK_OFFSET);

    printf("RESULT fib(%d)=%d expected=%d %s\n", FIB_N, fib, EXPECTED_FIB,
           fib == EXPECTED_FIB ? "PASS" : "FAIL");
    fflush(stdout);
    return fib == EXPECTED_FIB ? 0 : 1;
}
