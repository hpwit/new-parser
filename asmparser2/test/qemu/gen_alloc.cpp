// Proves out a possible answer to "how could I implement a memory alloc
// function in the script": bind alloc()/free()/readInt()/writeInt() as
// ordinary externals (no compiler changes at all -- a script-held
// uint32_t address is exactly what every other external-pointer case in
// this repo already uses, e.g. gen_arr_index.cpp's internal globals or
// StructsAndHostBindings.ino's leds[]). The one real gap this surfaces:
// the language has no dereference syntax of its own, so touching the
// allocated block at all needs read/write helper externals -- there's
// no way to write `*(int*)(buf+offset) = v;` directly in a script.
//
// Script: allocate a 10-int block, write i*i into each slot via
// writeInt(), sum them back via readInt(), printfln() the result
// (expected: sum of squares 0..9 = 285), then free() it. Verifies the
// full alloc -> write -> read -> free round trip actually works under
// real Xtensa calling-convention rules, not just that it assembles.
//
// Deliberately does NOT use createExecutableFromBinary() -- see
// gen_bouncing_repro.cpp's own header comment for why (it'd patch
// bound-function slots with host, not target, pointers). Walks
// bin.function_data by hand instead, same technique as every other
// QEMU repro here.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "parser.h"
#include "compiler_error.h"
#include "asm_parser.h"

#define MAX_TYPE0_RECORDS 16
#define MAX_TYPE2_RECORDS 16
#define MAX_TYPE3_RECORDS 16

int main()
{
    Parser p;
    p.clean();
    Script s;
    char *buf = strdup(
        "external uint32_t alloc(int size);\n"
        "external void free(uint32_t p);\n"
        "external void writeInt(uint32_t p, int offset, int value);\n"
        "external int readInt(uint32_t p, int offset);\n"
        "void main()\n"
        "{\n"
        "   uint32_t b;\n"
        "   b = alloc(40);\n"
        "   printfln(\"b=%u\", b);\n"
        "   int i;\n"
        "   for (i = 0; i < 10; i++)\n"
        "      writeInt(b, i*4, i*i);\n"
        "   int sum;\n"
        "   sum = 0;\n"
        "   for (i = 0; i < 10; i++)\n"
        "      sum = sum + readInt(b, i*4);\n"
        "   printfln(\"sum of squares 0..9 = %d\", sum);\n"
        "   free(b);\n"
        "}\n");
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

    uint32_t slotOffsets[MAX_TYPE0_RECORDS];
    uint32_t slotBincodes[MAX_TYPE0_RECORDS];
    int numSlots = 0;

    char extNames[MAX_TYPE2_RECORDS][64];
    uint32_t extSlotOffsets[MAX_TYPE2_RECORDS];
    int numExt = 0;

    uint32_t data3Addr[MAX_TYPE3_RECORDS];
    uint16_t data3TmpOffset[MAX_TYPE3_RECORDS];
    uint16_t data3Size[MAX_TYPE3_RECORDS];
    int numData3 = 0;

    uint32_t dataSize = bin.data_size;
    uint32_t mainAddr = 0xFFFFFFFF;

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
                char *textptr = (char *)hdr;
                hdr += text_size;
                uint16_t nb_data;
                memcpy(&nb_data, hdr, 2);
                hdr += 2;
                const char *pp = textptr + 2;
                const char *paren = strchr(pp, '(');
                size_t len = paren ? (size_t)(paren - pp) : strlen(pp);
                if (numExt < MAX_TYPE2_RECORDS && len < sizeof(extNames[0]))
                {
                    memcpy(extNames[numExt], pp, len);
                    extNames[numExt][len] = 0;
                    extSlotOffsets[numExt] = nb_data * 4;
                    numExt++;
                }
            }
            else if (type == 3)
            {
                uint32_t addr;
                memcpy(&addr, hdr, 4);
                hdr += 4;
                uint16_t tmp_data;
                memcpy(&tmp_data, hdr, 2);
                hdr += 2;
                uint16_t size;
                memcpy(&size, hdr, 2);
                hdr += 2;
                if (numData3 < MAX_TYPE3_RECORDS)
                {
                    data3Addr[numData3] = addr;
                    data3TmpOffset[numData3] = tmp_data;
                    data3Size[numData3] = size;
                    numData3++;
                }
            }
            else if (type == 4)
            {
                uint16_t text_size;
                memcpy(&text_size, hdr, 2);
                hdr += 2;
                char *namePtr = (char *)hdr;
                hdr += text_size;
                memcpy(&text_size, hdr, 2);
                hdr += 2;
                hdr += text_size;
                uint16_t args_num;
                memcpy(&args_num, hdr, 2);
                hdr += 2;
                uint32_t addr;
                memcpy(&addr, hdr, 4);
                hdr += 4;
                hdr += 4;
                const char *pp = namePtr + 2;
                if (pp[0] == '_')
                    pp++;
                const char *paren = strchr(pp, '(');
                size_t len = paren ? (size_t)(paren - pp) : strlen(pp);
                if (len == 4 && strncmp(pp, "main", 4) == 0)
                    mainAddr = addr;
            }
            else if (type == 5)
            {
                uint16_t text_size;
                memcpy(&text_size, hdr, 2);
                hdr += 2;
                hdr += text_size;
                hdr += 4 + 1;
            }
        }
    }
    if (mainAddr == 0xFFFFFFFF)
    {
        fprintf(stderr, "could not find main()'s entry point\n");
        return 1;
    }

    unsigned char *codeCopy = (unsigned char *)malloc(bin.instruction_size);
    memcpy(codeCopy, bin.binary_data, bin.instruction_size);
    int instrSize = bin.instruction_size;

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
    printf("#define DATA_SIZE %u\n", dataSize);
    printf("#define MAIN_ENTRY_OFFSET %u\n", mainAddr);

    printf("#define NUM_RELOC_SLOTS %d\n", numSlots);
    printf("static const unsigned int reloc_slot_offset[%d] = {", numSlots > 0 ? numSlots : 1);
    for (int i = 0; i < numSlots; i++)
        printf("%u%s", slotOffsets[i], i + 1 < numSlots ? "," : "");
    if (numSlots == 0)
        printf("0");
    printf("};\n");
    printf("static const unsigned int reloc_slot_bincode[%d] = {", numSlots > 0 ? numSlots : 1);
    for (int i = 0; i < numSlots; i++)
        printf("%u%s", slotBincodes[i], i + 1 < numSlots ? "," : "");
    if (numSlots == 0)
        printf("0");
    printf("};\n");

    printf("#define NUM_EXT_SLOTS %d\n", numExt);
    for (int i = 0; i < numExt; i++)
        printf("// ext[%d] = %s at slot byte offset %u\n", i, extNames[i], extSlotOffsets[i]);
    printf("static const unsigned int ext_slot_offset[%d] = {", numExt > 0 ? numExt : 1);
    for (int i = 0; i < numExt; i++)
        printf("%u%s", extSlotOffsets[i], i + 1 < numExt ? "," : "");
    if (numExt == 0)
        printf("0");
    printf("};\n");
    printf("static const char *ext_name[%d] = {", numExt > 0 ? numExt : 1);
    for (int i = 0; i < numExt; i++)
        printf("\"%s\"%s", extNames[i], i + 1 < numExt ? "," : "");
    if (numExt == 0)
        printf("\"\"");
    printf("};\n");

    printf("#define NUM_DATA3_RECORDS %d\n", numData3);
    printf("static const unsigned int data3_dest_addr[%d] = {", numData3 > 0 ? numData3 : 1);
    for (int i = 0; i < numData3; i++)
        printf("%u%s", data3Addr[i], i + 1 < numData3 ? "," : "");
    if (numData3 == 0)
        printf("0");
    printf("};\n");
    printf("static const unsigned int data3_size[%d] = {", numData3 > 0 ? numData3 : 1);
    for (int i = 0; i < numData3; i++)
        printf("%u%s", data3Size[i], i + 1 < numData3 ? "," : "");
    if (numData3 == 0)
        printf("0");
    printf("};\n");
    printf("static const unsigned int data3_blob_offset[%d] = {", numData3 > 0 ? numData3 : 1);
    {
        unsigned int running = 0;
        for (int i = 0; i < numData3; i++)
        {
            printf("%u%s", running, i + 1 < numData3 ? "," : "");
            running += data3Size[i];
        }
        if (numData3 == 0)
            printf("0");
    }
    printf("};\n");
    unsigned int totalBlobSize = 0;
    for (int i = 0; i < numData3; i++)
        totalBlobSize += data3Size[i];
    printf("#define DATA3_BLOB_SIZE %u\n", totalBlobSize > 0 ? totalBlobSize : 1);
    printf("static const unsigned char data3_blob[%u] = {", totalBlobSize > 0 ? totalBlobSize : 1);
    if (totalBlobSize == 0)
        printf("0");
    else
    {
        for (int i = 0; i < numData3; i++)
        {
            uint8_t *src = bin.binary_data + instrSize + data3TmpOffset[i];
            for (int b = 0; b < data3Size[i]; b++)
                printf("0x%02x,", src[b]);
        }
    }
    printf("};\n");

    return 0;
}
