// Dumps the compiled machine code for a script that calls one external
// function as a C byte array, for run.sh to embed in a QEMU test runner.
// Expected result: the host function receives 42 (a=7, setResult(a*6)).
//
// The runner binds `setResult` itself -- runner_external.c's hostSetResult()
// -- by patching the jump-table slot decodeBinaryHeader's case-2
// (external_call) handling would otherwise fill in via binding.h's
// findLink()/binded_assets; this generator never calls bindFunction()
// itself since the whole point is exercising that raw relocation slot
// directly, the same way a real loader would.
//
// SETRESULT_SLOT_OFFSET used to be hardcoded to 12 (word index 3) on the
// assumption the header always reserves 3 built-in slots -- handle,
// execaddr, sync -- before any external reference. f4068c4 dropped the
// dead execaddr/sync reservations (see asm_execute.cpp's fillHandleWord()
// doc comment), leaving only `_handle_` before setResult's own slot, so
// that assumption silently went stale: the runner was patching the wrong
// jump-table word, `fn()` jumped through leftover garbage, and the
// script crashed before ever reaching its own RESULT printf (confirmed
// via `qemu-system-xtensa -d guest_errors`: "Invalid read at addr 0x218").
// Walking the real relocation header instead of assuming its layout, the
// same fix already applied to gen_arr_index.cpp/gen_footer_check.cpp/etc
// for the analogous type-0 case, is the fix here too.
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

    // Walk the relocation header ourselves to find setResult's own
    // type-2 (external_call) record's jump-table slot, and main's own
    // type-4 (function_declaration) entry address -- rather than
    // assuming either's position/encoding, see the header comment above
    // for why that assumption already broke once.
    int setResultSlotOffset = -1;
    int mainEntryOffset = -1;
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
                hdr += text_size;
                hdr += 2;
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
                if (strncmp(textptr, "@_setResult", 11) == 0)
                    setResultSlotOffset = (int)nb_data * 4;
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
                uint16_t text_size;
                memcpy(&text_size, hdr, 2);
                hdr += 2;
                char *nameptr = (char *)hdr;
                hdr += text_size;
                memcpy(&text_size, hdr, 2);
                hdr += 2;
                hdr += text_size;
                hdr += 2; // args_num
                uint32_t addr;
                memcpy(&addr, hdr, 4);
                hdr += 4;
                hdr += 4; // variableaddress
                if (strncmp(nameptr, "@_main", 6) == 0)
                    mainEntryOffset = (int)addr;
            }
        }
    }
    if (setResultSlotOffset < 0 || mainEntryOffset < 0)
    {
        fprintf(stderr, "could not find setResult's relocation slot and/or main's entry "
                         "(setResultSlotOffset=%d mainEntryOffset=%d)\n",
                setResultSlotOffset, mainEntryOffset);
        return 1;
    }

    printf("static unsigned char script_code[%d] __attribute__((section(\".text.script_code\"), aligned(4))) = {\n",
           bin.instruction_size);
    for (int i = 0; i < bin.instruction_size; i++)
    {
        printf("0x%02x,", bin.binary_data[i]);
        if ((i + 1) % 12 == 0)
            printf("\n");
    }
    printf("\n};\n#define SCRIPT_CODE_SIZE %d\n#define SETRESULT_SLOT_OFFSET %d\n#define ENTRY_OFFSET %d\n#define EXPECTED_RESULT 42\n",
           bin.instruction_size, setResultSlotOffset, mainEntryOffset);
    return 0;
}
