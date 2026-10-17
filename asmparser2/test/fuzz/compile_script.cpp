// Compiles a script file with the real compiler sources and prints it as
// a C header (script_bytes.h) for runner_fuzz.c: the machine code, the
// data size, main()'s offset, and the relocation records the runner has
// to apply by hand (data-region slots, external variables, external
// functions, initialized data). Same layout and record decoding as
// test/qemu/gen_ball_stress.cpp, with the script read from argv[1]
// instead of being built in.
//
// Exit status: 0 on success, 1 if the script was rejected (parse or
// assembler error, printed to stderr).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <iterator>
#include <cstdint>
#include <cstring>
#include "parser.h"
#include "compiler_error.h"
#include "asm_parser.h"

#define MAX_TYPE0_RECORDS 1024
#define MAX_TYPE1_RECORDS 256
#define MAX_TYPE2_RECORDS 256
#define MAX_TYPE4_RECORDS 256
#define MAX_TYPE3_RECORDS 1024

// A record that does not fit is a harness limit, not a compiler bug: stop
// rather than drop it, or the runner silently runs with part of the
// binary unrelocated (literals reading back 0, calls to nowhere...).
static void tooMany(const char *what)
{
    fprintf(stderr, "compile_script: too many %s records (raise the limit)\n", what);
    exit(3);
}

int main(int argc, char **argv)
{
    Parser p;
    p.clean();
    Script s;
    std::ifstream f(argv[1]);
    std::string txt((std::istreambuf_iterator<char>(f)), {});
    txt = "#define true 1\n#define false 0\nuint32_t _handle_;\n" + txt;
    char *buf = strdup(txt.c_str());
    s.addContent(buf);
    s.init();
    p.parse(&s, &__allTokens);
    if (Error.error)
    {
        display_error(&Error);
        return 1;
    }

    Binary bin = createBinary(&footer, &header, &content, false);
    if (bin.error.error)
    {
        fprintf(stderr, "assembler error: %s\n", bin.error.error_message);
        return 1;
    }

    // Walk the relocation header ourselves, BEFORE anything else touches
    // bin.function_data/bin.binary_data.
    uint32_t slotOffsets[MAX_TYPE0_RECORDS];
    uint32_t slotBincodes[MAX_TYPE0_RECORDS];
    int numSlots = 0;

    char varNames[MAX_TYPE1_RECORDS][64];
    uint32_t varSlotOffsets[MAX_TYPE1_RECORDS];
    int numVars = 0;

    char extNames[MAX_TYPE2_RECORDS][64];
    uint32_t extSlotOffsets[MAX_TYPE2_RECORDS];
    int numExt = 0;

    char fnNames[MAX_TYPE4_RECORDS][64];
    uint32_t fnAddrs[MAX_TYPE4_RECORDS];
    int numFn = 0;

    uint32_t data3Addr[MAX_TYPE3_RECORDS];
    uint16_t data3TmpOffset[MAX_TYPE3_RECORDS];
    uint16_t data3Size[MAX_TYPE3_RECORDS];
    int numData3 = 0;

    uint32_t dataSize = bin.data_size;

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
                if (numSlots >= MAX_TYPE0_RECORDS)
                    tooMany("data-slot");
                slotOffsets[numSlots] = nb_data * 4;
                slotBincodes[numSlots] = bincode;
                numSlots++;
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
                // textptr is "@_ext_NAME" -- strip that fixed 6-char prefix.
                const char *p = textptr + 6;
                size_t len = strlen(p);
                if (numVars >= MAX_TYPE1_RECORDS || len >= sizeof(varNames[0]))
                    tooMany("external-variable");
                memcpy(varNames[numVars], p, len);
                varNames[numVars][len] = 0;
                varSlotOffsets[numVars] = nb_data * 4;
                numVars++;
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
                // textptr is "@_NAME(sig)" -- strip "@_" and take up to '('.
                const char *p = textptr + 2;
                const char *paren = strchr(p, '(');
                size_t len = paren ? (size_t)(paren - p) : strlen(p);
                if (numExt >= MAX_TYPE2_RECORDS || len >= sizeof(extNames[0]))
                    tooMany("external-function");
                memcpy(extNames[numExt], p, len);
                extNames[numExt][len] = 0;
                extSlotOffsets[numExt] = nb_data * 4;
                numExt++;
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
                if (numData3 >= MAX_TYPE3_RECORDS)
                    tooMany("initialized-data");
                data3Addr[numData3] = addr;
                data3TmpOffset[numData3] = tmp_data;
                data3Size[numData3] = size;
                numData3++;
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
                hdr += text_size; // variables
                uint16_t args_num;
                memcpy(&args_num, hdr, 2);
                hdr += 2;
                uint32_t addr;
                memcpy(&addr, hdr, 4);
                hdr += 4;
                hdr += 4; // variableaddress
                const char *p = namePtr + 2;
                if (p[0] == '_')
                    p++;
                const char *paren = strchr(p, '(');
                size_t len = paren ? (size_t)(paren - p) : strlen(p);
                if (numFn >= MAX_TYPE4_RECORDS || len >= sizeof(fnNames[0]))
                    tooMany("function");
                memcpy(fnNames[numFn], p, len);
                fnNames[numFn][len] = 0;
                fnAddrs[numFn] = addr;
                numFn++;
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

    unsigned char *codeCopy = (unsigned char *)malloc(bin.instruction_size);
    memcpy(codeCopy, bin.binary_data, bin.instruction_size);
    int instrSize = bin.instruction_size;

    uint32_t footerAddr = 0xFFFFFFFF, mainAddr = 0xFFFFFFFF;
    for (int i = 0; i < numFn; i++)
    {
        if (strcmp(fnNames[i], "footer") == 0)
            footerAddr = fnAddrs[i];
        if (strcmp(fnNames[i], "main") == 0)
            mainAddr = fnAddrs[i];
    }
    if (mainAddr == 0xFFFFFFFF)
    {
        fprintf(stderr, "could not find main()'s entry point\n");
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
    printf("#define DATA_SIZE %u\n", dataSize);
    printf("#define MAIN_ENTRY_OFFSET %u\n", mainAddr);
    printf("#define HAS_FOOTER %d\n", footerAddr != 0xFFFFFFFF ? 1 : 0);
    printf("#define FOOTER_ENTRY_OFFSET %u\n", footerAddr == 0xFFFFFFFF ? 0 : footerAddr);

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

    printf("#define NUM_VAR_SLOTS %d\n", numVars);
    for (int i = 0; i < numVars; i++)
        printf("// var[%d] = %s at slot byte offset %u\n", i, varNames[i], varSlotOffsets[i]);
    printf("static const unsigned int var_slot_offset[%d] = {", numVars > 0 ? numVars : 1);
    for (int i = 0; i < numVars; i++)
        printf("%u%s", varSlotOffsets[i], i + 1 < numVars ? "," : "");
    if (numVars == 0)
        printf("0");
    printf("};\n");
    printf("static const char *var_name[%d] = {", numVars > 0 ? numVars : 1);
    for (int i = 0; i < numVars; i++)
        printf("\"%s\"%s", varNames[i], i + 1 < numVars ? "," : "");
    if (numVars == 0)
        printf("\"\"");
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

    // Type-3 (data) records: literal bytes (string constants) staged in
    // the temporary post-instruction area of bin.binary_data, copied by
    // the runner into its own dataRegion -- see gen_bouncing_repro.cpp's
    // own comment for what happens if this is skipped.
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
