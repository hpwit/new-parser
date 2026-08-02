// Dumps the compiled machine code for a script with a global struct
// instance (`Foo g;`, a real constructor setting g.count=99) and two
// named, non-main functions -- poison() (writes -1 directly into
// g.count, bypassing the constructor entirely) and getCount() (reads it
// back) -- plus footer's own entry point, so run.sh's runner can call
// them in a specific order and check the numeric result.
//
// This verifies ScriptExecutable::execute()'s fix (script_executable.h/
// .cpp) to match upstream ESPLiveScript's own execute.h: `execute(name)`
// must call `@__footer` (a script's top-level initialization -- most
// commonly, as here, a global struct's constructor) *before* `name`,
// every time; the new executeOnly(name) must skip straight to `name`,
// footer never called. Since callXtensaDirect() (asm_execute.cpp) is a
// no-op on a non-Xtensa host (real Xtensa opcodes can't run there),
// ScriptExecutable::execute() itself can't meaningfully be exercised
// from a host-side generator -- and cross-compiling the whole parser/
// codegen pipeline for real Xtensa to call it from *inside* a bare-metal
// runner hits unrelated build-configuration issues of its own. This
// instead calls the exact same underlying primitive
// (asm_execute.cpp's callFunction(), by address, via callx8)
// ScriptExecutable::execute()/executeOnly() are themselves thin wrappers
// over, in the exact order the fixed implementation calls them --
// footer() then the target for execute()'s case, straight to the target
// for executeOnly()'s -- so what's actually being verified here is the
// same runtime behavior on real Xtensa hardware semantics, just without
// going through the C++ wrapper class itself.
//
// poison()-then-check is deterministic proof either way (unlike relying
// on a plain 0, which the loader's malloc'd-not-zeroed data region could
// coincidentally already be showing, non-Xtensa or not): footer's
// constructor sets g.count=99, a value poison()'s -1 can only be
// overwritten by if footer genuinely ran in between the two checked
// calls.
//
// Also the second QEMU case (after gen_arr_index.cpp) to exercise an
// internal (non-external) global variable -- same relocation-record
// patching technique, see its header comment for the full explanation
// of why every type-0 record needs patching, not just g's own.
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
        "struct Foo{int count; Foo(){count=99;}}"
        "Foo g;"
        "void poison(){g.count=-1;}"
        "int getCount(){return g.count;}"
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
    // consumes it) to find every type-0 (internal global/reserved-slot
    // indirection) record -- see gen_arr_index.cpp's header comment for
    // why all of them need patching, not just g's own.
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

    globalcall *footerFn = findPlainFunction(&exe, "footer");
    globalcall *poisonFn = findPlainFunction(&exe, "poison");
    globalcall *getCountFn = findPlainFunction(&exe, "getCount");
    if (!footerFn || !poisonFn || !getCountFn)
    {
        fprintf(stderr, "could not find footer/poison/getCount's plain record "
                         "(footer=%p poison=%p getCount=%p)\n",
                (void *)footerFn, (void *)poisonFn, (void *)getCountFn);
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
    printf("#define FOOTER_ENTRY_OFFSET %u\n", footerFn->address);
    printf("#define POISON_ENTRY_OFFSET %u\n", poisonFn->address);
    printf("#define GETCOUNT_ENTRY_OFFSET %u\n", getCountFn->address);
    printf("#define G_DATA_SIZE %u\n", dataSize);
    printf("#define NUM_RELOC_SLOTS %d\n", numSlots);
    printf("static const unsigned int reloc_slot_offset[%d] = {", numSlots);
    for (int i = 0; i < numSlots; i++)
        printf("%u%s", slotOffsets[i], i + 1 < numSlots ? "," : "");
    printf("};\n");
    printf("static const unsigned int reloc_slot_bincode[%d] = {", numSlots);
    for (int i = 0; i < numSlots; i++)
        printf("%u%s", slotBincodes[i], i + 1 < numSlots ? "," : "");
    printf("};\n");

    freeExecutable(&exe);
    return 0;
}
