// Dumps the compiled machine code for examples/FibonacciTiming.ino's
// exact fib() script and the offset run.sh's runner needs to call it
// directly by name (same scheme as gen_named_function.cpp), plus the
// call-count constants (2*fib(k+1)-1, the closed form for naive
// recursive fib(k)'s total call count) the runner needs to compute
// cycles-per-call for two different depths and check they agree.
//
// IMPORTANT: createExecutableFromBinary() mutates bin.binary_data in
// place, so the code bytes must be captured before calling it (see
// gen_fibonacci_arg.cpp for the same note).
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
    // n=25: fib(25)=75025, calls(25)=2*fib(26)-1=242785
    printf("#define FIB_N1 25\n");
    printf("#define FIB_N1_EXPECTED 75025\n");
    printf("#define FIB_N1_CALLS 242785L\n");
    // n=30: fib(30)=832040, calls(30)=2*fib(31)-1=2692537
    printf("#define FIB_N2 30\n");
    printf("#define FIB_N2_EXPECTED 832040\n");
    printf("#define FIB_N2_CALLS 2692537L\n");
    // n=40: calls(40)=2*fib(41)-1=331160281 -- not executed (would take
    // far too long under software emulation), only used to project real-
    // hardware timing from the two measured cycles-per-call figures above.
    printf("#define FIB_N3 40\n");
    printf("#define FIB_N3_CALLS 331160281L\n");
    printf("#define ESP32_CLOCK_HZ 240000000L\n");

    freeExecutable(&exe);
    return 0;
}
