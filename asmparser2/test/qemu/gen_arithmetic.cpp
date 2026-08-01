// Dumps the compiled machine code for a nested for/if arithmetic script
// (no external calls) as a C byte array, for run.sh to embed in a QEMU
// test runner. Expected result: sum = -4 (see README.md).
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
        "void main(){int a; int b; int sum; a=1; b=0; "
        "for (a = 0; a < 5; a++) { if (a < 3) { b = b + a; } else { b = b - a; } } "
        "sum = b;}");
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
    printf("\n};\n#define SCRIPT_CODE_SIZE %d\n#define SCRIPT_ENTRY_OFFSET %d\n#define SCRIPT_ENTRY_FRAME_SIZE %d\n#define SUM_STACK_OFFSET 64\n#define EXPECTED_SUM -4\n",
           bin.instruction_size, entryOffset, frameSize);
    return 0;
}
