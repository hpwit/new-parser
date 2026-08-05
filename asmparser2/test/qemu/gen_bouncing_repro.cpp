// Reproduces, under real Xtensa QEMU emulation, the exact construction +
// first-call path that crashes on real ESP32-S3 hardware running
// examples/BouncingBalls.ino: Guru Meditation Error / InstrFetchProhibited,
// PC=0x00000000 -- the same crash the printfln-shadowing fix (see that
// example and binding.h's _binding::ptr default) was meant to resolve,
// but which the user reports still happens byte-for-byte identically
// after that fix. This isolates footer (which constructs the global
// `ball Balls[20]` array -- 20 constructor calls, each calling the
// external `rand()` 5 times and doing 5 float divisions) plus a `diag()`
// function that does exactly what main() does up to and including its
// first `printfln()` call, but returns instead of entering the real
// while(true) loop -- so QEMU can observe a clean pass/fail instead of
// running forever.
//
// Deliberately does NOT use createExecutableFromBinary(): that patches
// bound-function jump-table slots with the *host* (this Mac's) pointer
// value for anything registered via bindFunction()/registerBuiltinRuntime
// Functions() -- meaningless, and itself a wild-jump hazard, once copied
// onto the actual Xtensa target. Instead this walks bin.function_data by
// hand (same technique gen_arr_index.cpp already uses for type-0
// records) to find every slot/entry-point offset the runner needs, and
// the runner patches rand/printfln's jump-table slots with its own,
// genuinely-on-target function addresses at its own runtime -- exactly
// how runner_external.c/runner_arr_index.c already do this for their own
// external calls/internal globals.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "parser.h"
#include "compiler_error.h"
#include "asm_parser.h"

#define MAX_TYPE0_RECORDS 64
#define MAX_TYPE2_RECORDS 16
#define MAX_TYPE4_RECORDS 16
#define MAX_TYPE3_RECORDS 16

int main()
{
    Parser p;
    p.clean();
    Script s;
    char *buf = strdup(
        "external uint32_t rand(uint32_t s);\n"
        "external void setPixel(int x, int y, int c);\n"
        "external void clear();\n"
        "external void show();\n"
        "#define max_nb_balls 20\n"
        "#define rmax 8\n"
        "#define rmin 8\n"
        "#define width 384\n"
        "#define height 168\n"
        "#define true 1\n"
        "struct ball\n"
        "{\n"
        "   float vx, vy, xc, yc, r;\n"
        "   int color;\n"
        "   ball()\n"
        "   {\n"
        "      vx = rand(300) / 255 + 0.7;\n"
        "      vy = rand(280) / 255 + 0.5;\n"
        "      r = (rmax - rmin) * (rand(280) / 180) + rmin;\n"
        "      xc = width / 2 * (rand(280) / 255 + 0.3) + 15;\n"
        "      yc = height / 2 * (rand(280) / 255 + 0.3) + 15;\n"
        "      color = rand(255);\n"
        "   }\n"
        "}\n"
        "ball Balls[max_nb_balls];\n"
        "void diag()\n"
        "{\n"
        "   int num = 8;\n"
        "   if (num > max_nb_balls) { num = max_nb_balls; }\n"
        "   if (num <= 0) { num = 1; }\n"
        "   printfln(\"numberof balls:%d\",num);\n"
        "}\n"
        "void main(){}\n");
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

    // Walk the relocation header ourselves, BEFORE anything else touches
    // bin.function_data/bin.binary_data (see the file header comment).
    uint32_t slotOffsets[MAX_TYPE0_RECORDS];
    uint32_t slotBincodes[MAX_TYPE0_RECORDS];
    int numSlots = 0;

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
                // textptr is "@_NAME(sig)" -- strip "@_" and take up to '('.
                const char *p = textptr + 2;
                const char *paren = strchr(p, '(');
                size_t len = paren ? (size_t)(paren - p) : strlen(p);
                if (numExt < MAX_TYPE2_RECORDS && len < sizeof(extNames[0]))
                {
                    memcpy(extNames[numExt], p, len);
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
                hdr += text_size; // variables
                uint16_t args_num;
                memcpy(&args_num, hdr, 2);
                hdr += 2;
                uint32_t addr;
                memcpy(&addr, hdr, 4);
                hdr += 4;
                hdr += 4; // variableaddress
                // namePtr is "@_NAME(...)"/"@__NAME" -- strip leading "@_"
                // (and a second "_" for the wrapper-shadowed footer/main
                // style names) and any "(...)" suffix.
                const char *p = namePtr + 2;
                if (p[0] == '_')
                    p++;
                const char *paren = strchr(p, '(');
                size_t len = paren ? (size_t)(paren - p) : strlen(p);
                if (numFn < MAX_TYPE4_RECORDS && len < sizeof(fnNames[0]))
                {
                    memcpy(fnNames[numFn], p, len);
                    fnNames[numFn][len] = 0;
                    fnAddrs[numFn] = addr;
                    numFn++;
                }
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

    uint32_t footerAddr = 0xFFFFFFFF, diagAddr = 0xFFFFFFFF;
    for (int i = 0; i < numFn; i++)
    {
        if (strcmp(fnNames[i], "footer") == 0)
            footerAddr = fnAddrs[i];
        if (strcmp(fnNames[i], "diag") == 0)
            diagAddr = fnAddrs[i];
    }
    if (diagAddr == 0xFFFFFFFF)
    {
        fprintf(stderr, "could not find diag()'s entry point\n");
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
    printf("#define DIAG_ENTRY_OFFSET %u\n", diagAddr);
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
    // the temporary post-instruction area of bin.binary_data (past
    // instrSize, within the larger tmp_instruction_size allocation),
    // that decodeBinaryHeader's own case 3 copies into the final data
    // buffer at `addr` -- dumped here as one flat byte array plus
    // per-record (destAddr, size) pairs so the runner can replicate that
    // copy into its own on-target dataRegion. Missing this step (an
    // earlier version of this repro did) doesn't crash anything -- it
    // just leaves whatever string literal(s) this script has pointing at
    // zeroed memory, silently printing nothing instead of real text.
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
