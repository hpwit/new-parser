// Dumps the compiled machine code for a script exercising every
// comparison operator's branch-immediate (blti/beqi/bgei/bnei) codegen
// path (see visitnode.cpp's isBranchImmediate()/valBranchImmediate() and
// _visitcomparatorNode()) -- this is the real-hardware complement to
// test/host/test_parser.cpp's two "branch-immediate (...)" host tests,
// which run the identical script through MiniXtensa. That host coverage
// is cheap to iterate on but only proves the interpreter agrees with the
// compiler's own encoder; this proves the *actual encoded bytes* execute
// correctly on a real Xtensa CPU (QEMU), the same way gen_arithmetic.cpp
// (a nested for/if using '<' only) already does for the "large" (target
// _if) comparator block -- this case instead targets the "small"
// (target _end) block for all six operators, plus the 256-vs-128
// b4const boundary fix and one non-eligible-immediate (11, not in the
// b4const table) fallback case. See test_parser.cpp's own comment on
// the identical script for the r1/r2 bit-per-case design.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "parser.h"
#include "compiler_error.h"
#include "asm_parser.h"

int main()
{
    Parser p;
    p.clean();
    Script s;
    char *buf = strdup(
        "void main(){int a; int r1; int r2; r1=0; r2=0; "
        "a=3; if(a<5){r1=r1+1;} "
        "a=5; if(a==5){r1=r1+2;} "
        "a=6; if(a!=5){r1=r1+4;} "
        "a=5; if(a>=5){r1=r1+8;} "
        "a=6; if(a>5){r1=r1+16;} "
        "a=5; if(a<=5){r1=r1+32;} "
        "a=256; if(a==256){r1=r1+64;} "
        "a=9; if(a<11){r1=r1+128;} "
        "a=7; if(a<5){r2=r2+1;} "
        "a=6; if(a==5){r2=r2+2;} "
        "a=5; if(a!=5){r2=r2+4;} "
        "a=4; if(a>=5){r2=r2+8;} "
        "a=5; if(a>5){r2=r2+16;} "
        "a=6; if(a<=5){r2=r2+32;} "
        "a=128; if(a==256){r2=r2+64;} "
        "a=12; if(a<11){r2=r2+128;} "
        "}");
    s.addContent(buf);
    s.init();
    p.parse(&s, &__allTokens);
    if (Error.error)
    {
        fprintf(stderr, "parse error=%d\n", Error.error);
        return 1;
    }

    Binary bin = createBinary(&footer, &header, &content, false);
    if (bin.error.error)
    {
        fprintf(stderr, "assembler error: %s\n", bin.error.error_message);
        return 1;
    }

    int entryOffset = -1;
    for (int i = 0; i < bin.instruction_size; i++)
    {
        if (bin.binary_data[i] == 0x36)
        {
            entryOffset = i;
            break;
        }
    }
    // Decode "entry a1,N" (bin_entry: 0x36 | (1<<8) | ((N/8)<<12)) to
    // recover N directly from the bytes, rather than hardcoding it.
    uint32_t entryWord = bin.binary_data[entryOffset] |
                          (bin.binary_data[entryOffset + 1] << 8) |
                          (bin.binary_data[entryOffset + 2] << 16);
    int frameSize = (int)(((entryWord & 0xFF000) >> 12) * 8);

    printf("static const unsigned char script_code[%d] __attribute__((section(\".text.script_code\"), aligned(4))) = {\n",
           bin.instruction_size);
    for (int i = 0; i < bin.instruction_size; i++)
    {
        printf("0x%02x,", bin.binary_data[i]);
        if ((i + 1) % 12 == 0)
            printf("\n");
    }
    printf("\n};\n#define SCRIPT_CODE_SIZE %d\n#define SCRIPT_ENTRY_OFFSET %d\n#define SCRIPT_ENTRY_FRAME_SIZE %d\n"
           "#define R1_STACK_OFFSET 60\n#define R2_STACK_OFFSET 64\n#define EXPECTED_R1 255\n#define EXPECTED_R2 0\n",
           bin.instruction_size, entryOffset, frameSize);
    return 0;
}
