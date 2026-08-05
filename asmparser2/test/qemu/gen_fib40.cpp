// Dumps the compiled machine code for the same recursive fib() script as
// gen_fib_timing.cpp, but for a single *actually executed* fib(40) call
// (331,160,281 recursive calls) instead of the smaller, tractable depths
// that script measures-and-projects from. Exists to answer directly
// "how long does real fib(40) take under QEMU" rather than extrapolating
// -- see runner_fib40.c for the CCOUNT-based real-hardware-cycle
// measurement this pairs with.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "parser.h"
#include "compiler_error.h"
#include "asm_parser.h"
#include "asm_execute.h"

int main()
{
    Parser p;
    p.clean();
    Script s;
    char *buf = strdup(
        "int fib(int n){if(n<2){return n;}return fib(n-1)+fib(n-2);}"
        "void main(){}");
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

    unsigned char *codeCopy = (unsigned char *)malloc(bin.instruction_size);
    memcpy(codeCopy, bin.binary_data, bin.instruction_size);
    int instrSize = bin.instruction_size;

    executable exe = createExecutableFromBinary(&bin);
    if (exe.error.error)
    {
        fprintf(stderr, "loader error: %s\n", exe.error.error_message);
        return 1;
    }

    globalcall *fibPlain = NULL;
    for (int i = 0; i < exe.functions.size(); i++)
    {
        globalcall *gc = exe.functions.getptr(i);
        const char *label = gc->name;
        bool isWrapper = gc->variables != NULL && gc->variables[0] >= '0' && gc->variables[0] <= '9';
        if (isWrapper || !(label[0] == '@' && label[1] == '_'))
            continue;
        const char *p = label + 2;
        if (p[0] == '_')
            p++;
        if (strncmp(p, "fib", 3) == 0 && (p[3] == '(' || p[3] == 0))
        {
            fibPlain = gc;
            break;
        }
    }
    if (fibPlain == NULL)
    {
        fprintf(stderr, "could not find fib's plain (non-wrapper) record\n");
        return 1;
    }

    printf("static unsigned char script_code[%d] __attribute__((section(\".text.script_code\"), aligned(4))) = {\n",
           instrSize);
    for (int i = 0; i < instrSize; i++)
    {
        printf("0x%02x,", codeCopy[i]);
        if ((i + 1) % 12 == 0)
            printf("\n");
    }
    printf("\n};\n");
    printf("#define SCRIPT_CODE_SIZE %d\n", instrSize);
    printf("#define FIB_ENTRY_OFFSET %u\n", fibPlain->address);
    printf("#define FIB_N 40\n");
    printf("#define FIB_N_EXPECTED 102334155\n");
    printf("#define ESP32_CLOCK_HZ 240000000L\n");

    freeExecutable(&exe);
    return 0;
}
