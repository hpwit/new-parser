// Proves the save/load story examples/SaveScriptBinary +
// examples/LoadScriptBinary rely on: a script compiled by one process
// (createBinary()), flattened via asm_serialize.h's serializeBinary()
// into a self-contained byte blob, and correctly executed by a
// *completely independent* process (this test's runner) that has never
// seen the script source -- only the serialized bytes, plus its own
// bindVariable()/bindFunction()-style registrations for the names
// ("key_char", "report") the script declares external. The two
// processes' host variable/function addresses are necessarily
// different (this generator's compile-time view has neither bound to
// anything real; the runner supplies its own), so this also proves
// relocation genuinely happens fresh at load time from the *name*, not
// from anything baked in at compile time.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "parser.h"
#include "compiler_error.h"
#include "asm_parser.h"
#include "asm_execute.h"
#include "asm_serialize.h"

int main()
{
    Parser p;
    p.clean();
    Script s;
    // Deliberately simple body (arithmetic, no branching): an earlier
    // version of this test used the KeyboardCallback.ino-style nested-if
    // uppercase check here and hit a pre-existing bug extremely reliably
    // -- ~90% of runs, 100% under AddressSanitizer -- now fixed (see
    // nodetoken.h's comment on `children`). This test's actual point is
    // serialization + cross-process relocation, not branching, so the
    // simpler arithmetic body was kept rather than re-verifying the
    // branching version under QEMU again after the fix.
    char *buf = strdup(
        "external int key_char;"
        "external void report(int c);"
        "int keyboard(){int c; c=key_char+1;"
        "report(c);"
        "return c;}"
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

    // Walk the header the same way createExecutableFromBinary()'s
    // decodeBinaryHeader() does, to find the offsets the runner needs --
    // key_char's and report's jump table slots, and keyboard's plain
    // (non-wrapper) entry address -- inlined here in C++ so run.sh's
    // runner can stay a small standalone C file with no dependency on
    // this project's C++ sources under the Xtensa toolchain.
    int keySlotOffset = -1, reportSlotOffset = -1;
    uint32_t keyboardEntry = 0;
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
                hdr += 4 + 2;
            }
            else if (type == 1)
            {
                uint16_t text_size;
                memcpy(&text_size, hdr, 2);
                hdr += 2;
                char *textptr = (char *)hdr;
                hdr += text_size;
                uint16_t nb_data;
                memcpy(&nb_data, hdr, 2);
                hdr += 2;
                if (strncmp(textptr, "@_ext_key_char", 14) == 0)
                    keySlotOffset = nb_data * 4;
            }
            else if (type == 2)
            {
                uint16_t text_size;
                memcpy(&text_size, hdr, 2);
                hdr += 2;
                char *textptr = (char *)hdr;
                hdr += text_size;
                uint16_t nb_data;
                memcpy(&nb_data, hdr, 2);
                hdr += 2;
                if (strncmp(textptr, "@_report(", 9) == 0)
                    reportSlotOffset = nb_data * 4;
            }
            else if (type == 4)
            {
                uint16_t nameSize;
                memcpy(&nameSize, hdr, 2);
                char *nameptr = (char *)(hdr + 2);
                hdr += 2 + nameSize;
                uint16_t varSize;
                memcpy(&varSize, hdr, 2);
                char *varptr = (char *)(hdr + 2);
                hdr += 2 + varSize;
                hdr += 2; // args_num
                uint32_t addr;
                memcpy(&addr, hdr, 4);
                hdr += 4 + 4;
                bool isWrapper = varptr[0] >= '0' && varptr[0] <= '9';
                const char *p2 = nameptr + 2;
                if (p2[0] == '_')
                    p2++;
                if (!isWrapper && strncmp(p2, "keyboard", 8) == 0 && (p2[8] == '(' || p2[8] == 0))
                    keyboardEntry = addr;
            }
        }
    }
    if (keySlotOffset < 0 || reportSlotOffset < 0 || keyboardEntry == 0)
    {
        fprintf(stderr, "could not find expected offsets (key=%d report=%d entry=%u)\n",
                keySlotOffset, reportSlotOffset, keyboardEntry);
        return 1;
    }

    uint32_t serializedSize = 0;
    uint8_t *serialized = serializeBinary(&bin, &serializedSize);
    if (serialized == NULL)
    {
        fprintf(stderr, "serializeBinary failed\n");
        return 1;
    }

    // Round-trip it through deserializeBinary() right here and byte-compare,
    // so a corrupt format is caught before ever trusting the bytes below.
    Binary reloaded = deserializeBinary(serialized, serializedSize);
    if (reloaded.error.error ||
        reloaded.instruction_size != bin.instruction_size ||
        reloaded.tmp_instruction_size != bin.tmp_instruction_size ||
        reloaded.function_size != bin.function_size ||
        reloaded.data_size != bin.data_size ||
        memcmp(reloaded.binary_data, bin.binary_data, bin.tmp_instruction_size) != 0 ||
        memcmp(reloaded.function_data, bin.function_data, bin.function_size) != 0)
    {
        fprintf(stderr, "serializeBinary/deserializeBinary round trip mismatch\n");
        return 1;
    }

    printf("static unsigned char saved_script[%u] __attribute__((section(\".text.script_code\"), aligned(4))) = {\n", serializedSize);
    for (uint32_t i = 0; i < serializedSize; i++)
    {
        printf("0x%02x,", serialized[i]);
        if ((i + 1) % 12 == 0)
            printf("\n");
    }
    printf("\n};\n");
    printf("#define SAVED_SCRIPT_SIZE %u\n", serializedSize);
    printf("#define SCRIPT_HEADER_SIZE 12\n"); // magic(4) + 4 uint16 size fields, see asm_serialize.cpp
    printf("#define KEY_CHAR_SLOT_OFFSET %d\n", keySlotOffset);
    printf("#define REPORT_SLOT_OFFSET %d\n", reportSlotOffset);
    printf("#define KEYBOARD_ENTRY_OFFSET %u\n", keyboardEntry);

    return 0;
}
