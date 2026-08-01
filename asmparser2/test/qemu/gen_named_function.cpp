// Dumps the compiled machine code for a script declaring a *named*,
// non-main function with a return value (`int fib(int n) {... return
// a;}`), plus the offsets run.sh's runner needs to call it directly by
// name (not through main()) via the same scheme asm_execute.cpp's
// callFunction() uses: find the wrapper record for "fib" with args_num
// matching, marshal the argument through its variableaddress, call it,
// and read the return value from a2 (the Xtensa C ABI's return register,
// which is also where the script's `return a;` leaves its value -- see
// visitnode.cpp).
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
        "int fib(int n){int a; a=0; int b; b=1; int i; "
        "for (i=0;i<n;i++) { int t; t=a+b; a=b; b=t; } return a;}"
        "void main(){int x; x=fib(10);}");
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

    // The *plain* record (fib's real address) rather than the wrapper --
    // see asm_execute.h's callFunction doc comment for why: the wrapper
    // never copies its call8's result into a10 before its own retw.n, so
    // calling it wouldn't give back a real return value. The plain
    // record's `variables` field holds the wrapper's raw label text
    // (starts with '@'); the wrapper's own holds "1 4" (starts with a
    // digit) -- same distinction asm_execute.cpp's isWrapperRecord uses.
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
    printf("#define FIB_N 10\n");
    printf("#define EXPECTED_FIB 55\n");

    freeExecutable(&exe);
    return 0;
}
