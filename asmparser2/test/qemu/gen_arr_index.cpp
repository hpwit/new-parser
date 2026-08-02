// Dumps the compiled machine code for a script with a *global struct
// array* and two named, non-main functions -- one that writes a
// distinct value into each element's field (setValues()), one that
// reads a given element's field back by a runtime-computed index
// (getCount(int i) { return arr[i].count; }) -- plus the offsets run.sh's
// runner needs to call both directly by name.
//
// This targets a real bug found while investigating a *different* fix
// (see parser.cpp's method-call self-pointer fix and the accompanying
// test/host/test_parser.cpp regression test,
// runStructArrayMethodSelfPointerTest): visitnode.cpp's
// _visitglobalVariableNode had the read-side "combine the computed
// index-scaled offset into the base address" step wired to `addi`
// (register a%d, a%d, <IMMEDIATE>) instead of `add` (register a%d, a%d,
// a%d) for the `arr[i].field`/`arr[i].method()` case specifically --
// register_numl.get() (a register *number*, e.g. 15) was rendered
// straight into addi's immediate slot, so the actual runtime-computed
// offset sitting in that register was silently discarded and every
// element resolved to the same fixed (and generally wrong) address
// regardless of i. The host-side regression test above checks the
// generated *instruction text* for this pattern; this is the real-
// hardware-semantics complement -- actually storing three different
// values into three different array elements, reading each back through
// the exact codegen path that had the bug, and checking the *numeric
// results* are distinct and correct on a real Xtensa CPU emulation, not
// just that the right mnemonic appears in the assembly.
//
// This is also the first QEMU case in this repo to exercise an
// *internal* (non-external) global variable read back through compiled
// code (every other case with a global -- gen_large_script.cpp's
// gcd/fib/isPrime/clampInt -- was deliberately chosen to have none, see
// its own header comment). Turns out that needed its own runtime
// relocation step, discovered the hard way in two stages:
//
//  1. An internal global's `l32r @_name` literal is not a self-contained
//     PC-relative reference resolved at assemble time -- it's a
//     placeholder patched at load time (asm_execute.cpp's
//     decodeBinaryHeader, case 0: `content = bincode + data_base`, where
//     data_base is createExecutableFromBinary()'s separately-allocated
//     data buffer), the same way an *external* variable's slot is (case
//     1, what gen_keyboard.cpp/runner_keyboard.c already patch by hand
//     for key_char). Skipping that patch step entirely (an initial
//     version of this file did, by only ever dumping instruction bytes)
//     leaves every such slot at 0 -- confirmed via
//     `qemu-system-xtensa -d guest_errors`: "Invalid write at addr 0x0".
//  2. Patching *only* arr's own slot still crashed the same way: type-0
//     records turn out to exist for *every* header reservation this
//     script has -- `_handle_`/`_execaddr_`/`_sync`, each function's own
//     stack-scratch slot, and arr -- eight records total here, each
//     with its own bincode (offset within one shared data region sized
//     bin.data_size) and its own nb_data (which word-slot in the
//     instruction stream to patch). The real loader
//     (decodeBinaryHeader) patches all of them unconditionally in one
//     pass; skipping the other seven left at least one of them (a
//     function's own stack-scratch indirection, referenced from that
//     function's prologue regardless of whether the script itself
//     declared any locals needing it) at 0 too. Fixed by collecting and
//     patching every type-0 record, not just arr's.
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

static globalcall *findPlainFunction(executable *exe, const char *fname)
{
    size_t flen = strlen(fname);
    for (int i = 0; i < exe->functions.size(); i++)
    {
        globalcall *gc = exe->functions.getptr(i);
        const char *label = gc->name;
        bool isWrapper = gc->variables != NULL && gc->variables[0] >= '0' && gc->variables[0] <= '9';
        if (isWrapper || !(label[0] == '@' && label[1] == '_'))
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
        "struct Foo{int count; Foo(){count=0;}}"
        "Foo arr[3];"
        "void setValues(){arr[0].count=10; arr[1].count=20; arr[2].count=30;}"
        "int getCount(int i){return arr[i].count;}"
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

    // Walk the relocation header ourselves (before createExecutableFromBinary
    // consumes it) to find *every* type-0 (internal global/reserved-slot
    // indirection) record -- see the header comment above for why all of
    // them, not just arr's own, need patching.
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

    globalcall *setValuesFn = findPlainFunction(&exe, "setValues");
    globalcall *getCountFn = findPlainFunction(&exe, "getCount");
    if (!setValuesFn || !getCountFn)
    {
        fprintf(stderr, "could not find setValues/getCount's plain record (setValues=%p getCount=%p)\n",
                (void *)setValuesFn, (void *)getCountFn);
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
    printf("#define SETVALUES_ENTRY_OFFSET %u\n", setValuesFn->address);
    printf("#define GETCOUNT_ENTRY_OFFSET %u\n", getCountFn->address);
    printf("#define ARR_DATA_SIZE %u\n", dataSize);
    printf("#define NUM_RELOC_SLOTS %d\n", numSlots);
    printf("static const unsigned int reloc_slot_offset[%d] = {", numSlots);
    for (int i = 0; i < numSlots; i++)
        printf("%u%s", slotOffsets[i], i + 1 < numSlots ? "," : "");
    printf("};\n");
    printf("static const unsigned int reloc_slot_bincode[%d] = {", numSlots);
    for (int i = 0; i < numSlots; i++)
        printf("%u%s", slotBincodes[i], i + 1 < numSlots ? "," : "");
    printf("};\n");
    printf("#define EXPECTED_ARR0 10\n");
    printf("#define EXPECTED_ARR1 20\n");
    printf("#define EXPECTED_ARR2 30\n");

    freeExecutable(&exe);
    return 0;
}
