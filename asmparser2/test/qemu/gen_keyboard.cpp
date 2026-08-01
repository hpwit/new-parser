// Dumps the compiled machine code for KeyboardCallback.ino's script: an
// `external int key_char;` variable plus a named, argument-free function
// that reads it (`int keyboard() { ... return c; }`). Verifies the
// combination examples/KeyboardCallback/KeyboardCallback.ino relies on
// but that no other test/qemu case exercises: a case-1 (external
// variable) jump table slot actually being read back correctly by
// compiled code, together with calling a zero-argument named function
// directly (not through main()).
//
// IMPORTANT: createExecutableFromBinary() mutates bin.binary_data in
// place, so the code bytes must be captured before calling it (see
// gen_fibonacci_arg.cpp for the same note).
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "parser.h"
#include "compiler_error.h"
#include "binding.h"
#include "asm_parser.h"
#include "asm_execute.h"

int main()
{
    // createExecutableFromBinary() requires every external variable to
    // resolve via findLink() (see decodeBinaryHeader's case 1) even
    // though this generator only wants the function records out of it --
    // the dummy target's address is discarded; KEY_CHAR_SLOT_OFFSET is
    // computed independently below from the raw header, and the real
    // runtime address gets patched by the runner itself.
    int dummy = 0;
    bindVariable((char *)"int", (char *)"key_char", NULL, (void *)&dummy);

    Parser p;
    p.clean();
    Script s;
    char *buf = strdup(
        "external int key_char;"
        "int keyboard(){int c; c=key_char;"
        "if (c>=97) { if (c<=122) { c=c-32; } }"
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

    // Walk the relocation header ourselves (before createExecutableFromBinary
    // consumes it) to find key_char's jump table slot -- the same type-1
    // (external_var_label) record asm_execute.cpp's decodeBinaryHeader
    // parses, whose nb_data field is the word index into the instruction
    // stream to patch.
    int keySlotOffset = -1;
    {
        uint8_t *hdr = bin.function_data;
        uint16_t nb_objects;
        memcpy(&nb_objects, hdr, 2);
        hdr += 2;
        for (int i = 0; i < nb_objects; i++)
        {
            uint8_t type = *hdr;
            hdr += 1;
            if (type == 1)
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
            else if (type == 0)
            {
                hdr += 4 + 2;
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
    if (keySlotOffset < 0)
    {
        fprintf(stderr, "could not find key_char's jump table slot\n");
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

    // The plain (non-wrapper) record for "keyboard" -- same distinction
    // as gen_named_function.cpp / asm_execute.cpp's isWrapperRecord.
    globalcall *kbPlain = NULL;
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
        if (strncmp(p, "keyboard", 8) == 0 && (p[8] == '(' || p[8] == 0))
        {
            kbPlain = gc;
            break;
        }
    }
    if (kbPlain == NULL)
    {
        fprintf(stderr, "could not find keyboard's plain (non-wrapper) record\n");
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
    printf("#define KEY_CHAR_SLOT_OFFSET %d\n", keySlotOffset);
    printf("#define KEYBOARD_ENTRY_OFFSET %u\n", kbPlain->address);

    freeExecutable(&exe);
    return 0;
}
