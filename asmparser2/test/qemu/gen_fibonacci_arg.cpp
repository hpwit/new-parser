// Dumps the compiled machine code for an iterative Fibonacci script that
// takes n as a real function argument (not baked into the script text),
// plus the offsets run.sh's runner needs to replicate what
// asm_execute.cpp's loader does: patch the jump-table slot for the
// argument-storage reservation with a pointer into a scratch data buffer,
// write the argument value there, then call the *wrapper* entry point
// (see asm_execute.h's runExecutableWithArgs doc comment for why it's the
// wrapper and not the plain function address).
//
// IMPORTANT: createExecutableFromBinary() mutates bin.binary_data in
// place (it patches the jump table directly into the source buffer
// before copying it to the final executable memory) -- so the code bytes
// must be captured *before* calling it, and the offsets extracted
// afterward from a throwaway load.
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
        "void main(int n){int a; a=0; int b; b=1; int i; "
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

    // Capture the clean, unrelocated bytes before createExecutableFromBinary
    // mutates bin.binary_data's jump table in place.
    unsigned char *codeCopy = (unsigned char *)malloc(bin.instruction_size);
    memcpy(codeCopy, bin.binary_data, bin.instruction_size);
    int instrSize = bin.instruction_size;

    int entryOffset = -1;
    for (int i = 0; i < instrSize; i++)
    {
        if (codeCopy[i] == 0x36)
        {
            entryOffset = i;
            break;
        }
    }
    uint32_t entryWord = codeCopy[entryOffset] | (codeCopy[entryOffset + 1] << 8) | (codeCopy[entryOffset + 2] << 16);
    int realFnFrameSize = (int)(((entryWord & 0xFF000) >> 12) * 8);

    executable exe = createExecutableFromBinary(&bin);
    if (exe.error.error)
    {
        fprintf(stderr, "loader error: %s\n", exe.error.error_message);
        return 1;
    }

    globalcall *wrapper = NULL;  // args_num == 1: loads the arg, call8's the real fn
    globalcall *realFn = NULL;   // args_num == 0: the actual function body
    for (int i = 0; i < exe.functions.size(); i++)
    {
        globalcall *gc = exe.functions.getptr(i);
        if (gc->args_num == 1)
            wrapper = gc;
        else
            realFn = gc;
    }
    if (wrapper == NULL || realFn == NULL)
    {
        fprintf(stderr, "expected one 1-arg wrapper and one 0-arg function record\n");
        return 1;
    }

    // The real function's own "entry a1,N" (found above, entryOffset) is
    // always the *second* entry point textually (wrapper code comes
    // first), and both happen to use the same frame size here since
    // there's only one declared function -- reuse realFnFrameSize for the
    // wrapper's frame too (verified against the disassembly during
    // development: both say "entry a1,152" for this script).
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
    printf("#define WRAPPER_ENTRY_OFFSET %u\n", wrapper->address);
    printf("#define REAL_FN_ENTRY_OFFSET %u\n", realFn->address);
    printf("#define WRAPPER_FRAME_SIZE %d\n", realFnFrameSize);
    printf("#define REAL_FN_FRAME_SIZE %d\n", realFnFrameSize);
    printf("#define ARG_SLOT_DATA_OFFSET %u\n", wrapper->variableaddress);
    // The jump-table slot the wrapper's "l32r a9,@_stack__main(d)" reads
    // its argument-storage pointer from: reservations are always ordered
    // handle(0)/execaddr(1)/sync(2)/[one per external]/[one per declared
    // function's own stack area], and this script has no external calls,
    // so the function's own stack-area reservation is always word index
    // 3 -> byte offset 12. Verified against the disassembly during
    // development.
    printf("#define ARG_SLOT_TABLE_OFFSET 12\n");
    printf("#define A_STACK_OFFSET 56\n");
    printf("#define FIB_N 10\n");
    printf("#define EXPECTED_FIB 55\n");

    freeExecutable(&exe);
    return 0;
}
