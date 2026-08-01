// Dumps the compiled machine code for an iterative Fibonacci script as a
// C byte array, for run.sh to embed in a QEMU test runner. Expected
// result: fib(10) = 55 (fib(0)=0, fib(1)=1 convention), left in `a`'s
// stack slot (offset 60) since `fib` itself never gets read after being
// assigned and so never gets its own slot -- see test/host/test_parser.cpp's
// "fibonacci: fib(10) == 55" test for the same script verified against
// MiniXtensa.
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
        "void main(){int n; n=10; int a; a=0; int b; b=1; int i; "
        "for (i=0;i<n;i++) { int t; t=a+b; a=b; b=t; } int fib; fib=a;}");
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
    printf("\n};\n#define SCRIPT_CODE_SIZE %d\n#define SCRIPT_ENTRY_OFFSET %d\n#define SCRIPT_ENTRY_FRAME_SIZE %d\n#define A_STACK_OFFSET 60\n#define EXPECTED_FIB 55\n",
           bin.instruction_size, entryOffset, frameSize);
    return 0;
}
