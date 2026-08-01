// Dumps the compiled machine code for a script that calls one external
// function as a C byte array, for run.sh to embed in a QEMU test runner.
// Expected result: the host function receives 42 (a=7, setResult(a*6)).
#include <cstdio>
#include <cstring>
#include "parser.h"
#include "compiler_error.h"
#include "binding.h"
#include "asm_parser.h"

int main()
{
    Parser p;
    p.clean();
    Script s;
    char *buf = strdup("external void setResult(int a); void main(){int a; a=7; setResult(a*6);}");
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

    printf("static unsigned char script_code[%d] __attribute__((section(\".text.script_code\"), aligned(4))) = {\n",
           bin.instruction_size);
    for (int i = 0; i < bin.instruction_size; i++)
    {
        printf("0x%02x,", bin.binary_data[i]);
        if ((i + 1) % 12 == 0)
            printf("\n");
    }
    // setResult is the 4th reservation (word index 3, byte offset 12):
    // header always orders the 3 built-in handle/execaddr/sync slots
    // before external references, and this script has exactly one.
    // Verified against the disassembly during development.
    printf("\n};\n#define SCRIPT_CODE_SIZE %d\n#define SETRESULT_SLOT_OFFSET 12\n#define ENTRY_OFFSET %d\n#define EXPECTED_RESULT 42\n",
           bin.instruction_size, entryOffset);
    return 0;
}
