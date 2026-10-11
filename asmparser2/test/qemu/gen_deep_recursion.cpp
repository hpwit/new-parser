// Deep, non-branching recursion that keeps two values in stack locals
// across its own recursive call and counts, on the way back up, the
// frames in which both are still intact: f(40) must return 40.
//
// A call chain deeper than the register window makes the Xtensa
// window-overflow handler write each evicted frame's a4..a7 into that
// frame's own stack, in the 16 bytes just under its top 16. The compiler
// used to reserve only the top 16, so those bytes were where the last
// locals lived: this returned 6 (only the frames that never overflowed
// kept their locals). Fixed by reserving 32 (_FRAME_RESERVED, see
// _visitdefFunctionNode()).
//
// Derived from gen_fib_timing.cpp: same generator, different script. The
// function is still called fib so the entry-point lookup below is
// unchanged, and the timing #defines it prints are simply unused by
// runner_deep_recursion.c.
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
        "int fib(int n){int keep = n * 4; int keep2 = n * 6; if (n == 0) return 0; "
        "int r = fib(n - 1); if (keep == n * 4) { if (keep2 == n * 6) r = r + 1; } return r;}"
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
        if (!(label[0] == '@' && label[1] == '_'))
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
    // n=50: calls(50)=2*fib(51)-1=40730022147 -- exceeds int32_t/uint32_t
    // range (needs a 64-bit constant), same reasoning as n=40 above but
    // ~123x more calls. Also note: fib(50)=12586269025 itself overflows
    // the script's `int` (32-bit) return type -- irrelevant here since
    // this projects timing only, fib(50) is never actually called.
    printf("#define FIB_N4 50\n");
    printf("#define FIB_N4_CALLS 40730022147LL\n");
    printf("#define ESP32_CLOCK_HZ 240000000L\n");

    freeExecutable(&exe);
    return 0;
}
