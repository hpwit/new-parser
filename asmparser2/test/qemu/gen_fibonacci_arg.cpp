// Dumps the compiled machine code for a script whose main() takes a real
// function argument (iterative Fibonacci, `void main(int n){...}`), plus
// the offsets run.sh's runner needs to call it with a register-passed
// argument and read the result back through a getter function.
//
// main(int n) used to be called through a second, argument-marshaling
// *wrapper* record (see asm_execute.cpp's isWrapperRecord() comment for
// the full history) that copied its argument out of a reserved
// stack-storage area into a2 before call8-ing the real body --
// asm_execute.cpp's runExecutableWithArgs() used that wrapper the same
// way. That wrapper is gone: a parameterized function's one remaining
// record reads its arguments straight out of a2.. (visitnode.cpp's
// _visitdefInputArgumentsNode), the same registers callXtensaDirect()
// (asm_execute.cpp) -- and now runExecutableWithArgs() too -- populate
// via a10..a15's call8 window rotation. This verifies that calling
// convention on real Xtensa hardware semantics: n goes straight into
// a10, no wrapper or reserved stack area involved.
//
// The result is read back through a getFib() getter (`int fib;` a real
// internal global, `int getFib(){return fib;}`) rather than reconstructed
// from a guessed offset into main()'s own stack frame, the way this file
// used to when it still had a wrapper frame to account for -- same
// pattern (and same reloc-slot patching, needed for the same reason) as
// gen_footer_check.cpp/gen_arr_index.cpp.
//
// IMPORTANT: createExecutableFromBinary() mutates bin.binary_data in
// place, so the code bytes must be captured before calling it.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "parser.h"
#include "compiler_error.h"
#include "asm_parser.h"
#include "asm_execute.h"

static globalcall *findPlainFunction(executable *exe, const char *fname)
{
    size_t flen = strlen(fname);
    for (int i = 0; i < exe->functions.size(); i++)
    {
        globalcall *gc = exe->functions.getptr(i);
        const char *label = gc->name;
        if (!(label[0] == '@' && label[1] == '_'))
            continue;
        const char *p = label + 2;
        if (p[0] == '_')
            p++;
        if (strncmp(p, fname, flen) == 0 && (p[flen] == '(' || p[flen] == 0))
            return gc;
    }
    return NULL;
}

#define MAX_TYPE0_RECORDS 64

int main()
{
    Parser p;
    p.clean();
    Script s;
    char *buf = strdup(
        "int fib;"
        "void main(int n){int a; a=0; int b; b=1; int i; "
        "for (i=0;i<n;i++) { int t; t=a+b; a=b; b=t; } fib=a;}"
        "int getFib(){return fib;}");
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

    // Walk the relocation header ourselves (before createExecutableFromBinary
    // consumes it) to find *every* type-0 (internal global/reserved-slot
    // indirection) record -- see gen_arr_index.cpp's header comment for
    // why all of them, not just fib's own, need patching.
    uint32_t slotOffsets[MAX_TYPE0_RECORDS];
    uint32_t slotBincodes[MAX_TYPE0_RECORDS];
    int numSlots = 0;
    {
        uint8_t *hdr = bin.function_data;
        uint16_t nb_objects;
        memcpy(&nb_objects, hdr, 2);
        hdr += 2;
        for (int i = 0; i < nb_objects; i++)
        {
            uint8_t type = *hdr;
            hdr += 1;
            if (type == 0)
            {
                uint32_t bincode;
                uint16_t nb_data;
                memcpy(&bincode, hdr, 4);
                hdr += 4;
                memcpy(&nb_data, hdr, 2);
                hdr += 2;
                if (numSlots < MAX_TYPE0_RECORDS)
                {
                    slotOffsets[numSlots] = nb_data * 4;
                    slotBincodes[numSlots] = bincode;
                    numSlots++;
                }
            }
            else if (type == 1)
            {
                uint16_t text_size;
                memcpy(&text_size, hdr, 2);
                hdr += 2;
                hdr += text_size;
                hdr += 2;
            }
            else if (type == 2)
            {
                uint16_t text_size;
                memcpy(&text_size, hdr, 2);
                hdr += 2;
                hdr += text_size;
                hdr += 2;
            }
            else if (type == 3)
            {
                uint16_t size;
                hdr += 4 + 2;
                memcpy(&size, hdr, 2);
                hdr += 2;
            }
            else if (type == 4)
            {
                for (int f = 0; f < 2; f++)
                {
                    uint16_t text_size;
                    memcpy(&text_size, hdr, 2);
                    hdr += 2;
                    hdr += text_size;
                }
                hdr += 2 + 4 + 4;
            }
        }
    }
    if (numSlots == 0)
    {
        fprintf(stderr, "found no type-0 (internal global/reserved-slot) relocation records\n");
        return 1;
    }

    unsigned char *codeCopy = (unsigned char *)malloc(bin.instruction_size);
    memcpy(codeCopy, bin.binary_data, bin.instruction_size);
    int instrSize = bin.instruction_size;
    uint32_t dataSize = bin.data_size;

    executable exe = createExecutableFromBinary(&bin);
    if (exe.error.error)
    {
        fprintf(stderr, "loader error: %s\n", exe.error.error_message);
        return 1;
    }

    globalcall *mainFn = findPlainFunction(&exe, "main");
    globalcall *getFibFn = findPlainFunction(&exe, "getFib");
    if (!mainFn || !getFibFn)
    {
        fprintf(stderr, "could not find main/getFib's record (main=%p getFib=%p)\n",
                (void *)mainFn, (void *)getFibFn);
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
    printf("#define MAIN_ENTRY_OFFSET %u\n", mainFn->address);
    printf("#define GETFIB_ENTRY_OFFSET %u\n", getFibFn->address);
    printf("#define FIB_DATA_SIZE %u\n", dataSize);
    printf("#define NUM_RELOC_SLOTS %d\n", numSlots);
    printf("static const unsigned int reloc_slot_offset[%d] = {", numSlots);
    for (int i = 0; i < numSlots; i++)
        printf("%u%s", slotOffsets[i], i + 1 < numSlots ? "," : "");
    printf("};\n");
    printf("static const unsigned int reloc_slot_bincode[%d] = {", numSlots);
    for (int i = 0; i < numSlots; i++)
        printf("%u%s", slotBincodes[i], i + 1 < numSlots ? "," : "");
    printf("};\n");
    printf("#define FIB_N 10\n");
    printf("#define EXPECTED_FIB 55\n");

    freeExecutable(&exe);
    return 0;
}
