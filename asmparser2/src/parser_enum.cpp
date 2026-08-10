#include "parser_enum.h"
const char * asmInstructionsName[] =
    {
        "s8i a%d,a%d,%d",
        "l8ui a%d,a%d,%d",
        "l16ui a%d,a%d,%d",
        "l16si a%d,a%d,%d",
        "s16i a%d,a%d,%d",
        "l32i a%d,a%d,%d",
        "s32i a%d,a%d,%d",
        "lsi f%d,a%d,%d",
        "ssi f%d,a%d,%d",
        "add a%d,a%d,a%d",
        "sub a%d,a%d,a%d",
        "quou a%d,a%d,a%d",
        "quos a%d,a%d,a%d",
        "mull a%d,a%d,a%d",
        "add.s f%d,f%d,f%d",
        "sub.s f%d,f%d,f%d",
        "quou a%d,a%d,a%d",
        "mul.s f%d,f%d,f%d",
        "addx2 a%d,a%d,a%d",
        "addx4 a%d,a%d,a%d",
        "addx8 a%d,a%d,a%d",
        "subx2 a%d,a%d,a%d",
        "subx4 a%d,a%d,a%d",
        "subx8 a%d,a%d,a%d",
        "neg a%d,a%d",
        "neg.s f%d,f%d",
        "entry a1,%d",
        ".bytes %d",
        "@_%s:",
        "retw.n",
        "l32r a%d,%s",
        "call8 @_%s",
        "@_%s:",
        " ",
        "trunc.s a%d,f%d,0",
        "float.s f%d,a%d,0",
        "movi a%d,%d",
        "mov a%d,a%d",
        "mov.s f%d,f%d",
        "wfr f%d,a%d",
        "movr a%d,a%d",
        "addi a%d,a%d,%d",
        "bt b0,%s%s",
        "bf b0,%s%s",
        "olt.s b0,f%d,f%d",
        "oeq.s b0,f%d,f%d",
        "ole.s b0,f%d,f%d",
        "blt%s a%d,a%d,%s%s",
        "beq%s a%d,a%d,%s%s",
        "bge%s a%d,a%d,%s%s",
        "bne%s a%d,a%d,%s%s",
        // Branch-immediate forms (blti/beqi/bgei/bnei): one register, one
        // *pre-encoded b4const table index* (0-15, not the actual
        // comparison value -- e.g. comparing against 10 encodes as 9; see
        // asm_encoders.h's bin_blti/bin_bgei/op_blti and visitnode.cpp's
        // isBranchImmediate()/valBranchImmediate()), then the label. No
        // "u" (unsigned) variant -- b4const's values are all small enough
        // (1..256) that signed and unsigned comparisons agree, so this
        // covers both without needing bltui/bgeui at all.
        "blti a%d,%d,%s%s",
        "beqi a%d,%d,%s%s",
        "bgei a%d,%d,%s%s",
        "bnei a%d,%d,%s%s",
        "ssl a%d",
        "sll a%d,a%d",
        "wsr a%d,%d",
        "srl a%d,a%d",
       "remu a%d,a%d,a%d",
       "and a%d,a%d,a%d",
        "or a%d,a%d,a%d",
        "abs.s f%d,f%d",
        "abs a%d,a%d",
        "rfr a%d,f%d",
        "extui a%d,a%d,%d,%d"
    


};