// Compiles the user's LED-matrix polar-mapping script (init() precomputes
// per-pixel angle/radius lookup tables into two global uint8_t arrays;
// main() renders forever, calling the external show() once per frame) and
// dumps everything runner_led_timing.c needs to load + relocate it by hand
// on real Xtensa QEMU and measure real CCOUNT cycles between show() calls
// -- same rationale as gen_ball_repro.cpp for not using
// createExecutableFromBinary() (would patch in host/Mac pointers,
// meaningless once these bytes are embedded and run under QEMU), and same
// binary-header-walking technique, extended to also capture type-1
// (external_var_label -- this script's `leds` array) records, which
// gen_ball_repro.cpp's own walker deliberately skips (it has no external
// variable in its own repro script).
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "parser.h"
#include "compiler_error.h"
#include "binding.h"
#include "asm_parser.h"
#include "tokenize.h"

// Mirrors script_executable.cpp's parseScript()'s own private kPrelude --
// `while (true)` needs `true` defined, which isn't a real keyword without
// it. Not using parseScript() itself since it also runs createBinary()
// internally and frees content/header/footer's text before this file gets
// a chance at bin.function_data (parseScript() is fire-and-forget, this
// generator needs the intermediate Binary).
static const char *kPrelude =
    "#define true 1\n"
    "#define false 0\n"
    "uint32_t _handle_;\n";

#define MAX_TYPE0_RECORDS 64
#define MAX_TYPE1_RECORDS 16
#define MAX_TYPE2_RECORDS 16
#define MAX_TYPE4_RECORDS 16
#define MAX_TYPE3_RECORDS 16

void hostFn() {}

int main()
{
    // Registered purely so the parser accepts these as external
    // declarations (bindFunction()'s own auto-declare path, see
    // parser.cpp) -- the *runner* patches their jump-table slots with its
    // own real, on-target implementations at load time, same split as
    // gen_ball_repro.cpp/runner_ball_repro.c already use for rand/printfln.
    bindFunction((char *)"void", (char *)"show", NULL, (void *)hostFn);
    bindFunction((char *)"float", (char *)"atan2", (char *)"float,float", (void *)hostFn);
    bindFunction((char *)"float", (char *)"hypot", (char *)"float,float", (void *)hostFn);
    bindFunction((char *)"uint8_t", (char *)"sin8", (char *)"uint8_t", (void *)hostFn);
    bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", (void *)hostFn);
    static uint8_t ledsBufferDummy[1];
    bindVariable((char *)"CRGB", (char *)"leds", (char *)"[]", (void *)ledsBufferDummy);

    Parser p;
    p.clean();
    Script s;
    char *buf = strdup(
        "#define LED_COLS 128\n"
        "#define LED_ROWS 96\n"
        "#define NUM_LEDS 12288\n"
        "#define PI 3.1415926535\n"
        "#define panel_width 128\n"
        "#define speed 1\n"
        "#define nb_branches 5\n"
        "uint8_t C_X, C_Y, mapp;\n"
        "uint8_t rMapRadius[NUM_LEDS];\n"
        "uint8_t rMapAngle[NUM_LEDS];\n"
        "void init()\n"
        "{\n"
        "   C_X = LED_COLS / 2;\n"
        "   C_Y = LED_ROWS / 2;\n"
        "   mapp = 255 / LED_COLS;\n"
        "   for (int x = -C_X; x < C_X + (LED_COLS % 2); x++)\n"
        "   {\n"
        "      for (int y = -C_Y; y < C_Y + (LED_ROWS % 2); y++)\n"
        "      {\n"
        "         float h = 128 * (atan2(y, x) / PI);\n"
        "         rMapAngle[(x + C_X) * LED_ROWS + y + C_Y] = h;\n"
        "         h = hypot(x, y) * mapp;\n"
        "         rMapRadius[(x + C_X) * LED_ROWS + y + C_Y] = h;\n"
        "      }\n"
        "   }\n"
        "}\n"
        "void main()\n"
        "{\n"
        "   init();\n"
        "   uint32_t t;\n"
        "\n"
        "      for (uint8_t x = 0; x < LED_COLS; x++)\n"
        "      {\n"
        "         for (uint8_t y = 0; y < LED_ROWS; y++)\n"
        "         {\n"
        "            uint8_t angle = rMapAngle[x * LED_ROWS + y];\n"
        "            uint8_t radius = rMapRadius[x * LED_ROWS + y];\n"
        "            leds[y * panel_width + x] = hsv(t + radius, 255, sin8(sin8((angle * 4 - radius * mapp) / 4 + t) + angle * nb_branches - 2 * t));\n"
        "         }\n"
        "      }\n"
        "      show();\n"
        "      t = t + speed;\n"
        "\n"
        "}\n");
    size_t preludeLen = strlen(kPrelude);
    size_t scriptLen = strlen(buf);
    char *fullBuf = (char *)malloc(preludeLen + scriptLen + 1);
    memcpy(fullBuf, kPrelude, preludeLen);
    memcpy(fullBuf + preludeLen, buf, scriptLen + 1);
    int preludeLines = 0;
    for (const char *pp = kPrelude; *pp != 0; pp++)
        if (*pp == '\n')
            preludeLines++;
    _tokenizer_start_line = 1 - preludeLines;

    s.addContent(fullBuf);
    s.init();
    p.parse(&s, &__allTokens);
    _tokenizer_start_line = 1;
    if (Error.error)
    {
        fprintf(stderr, "parse error=%d\n", Error.error);
        display_error(&Error);
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

    char varNames[MAX_TYPE1_RECORDS][64];
    uint32_t varSlotOffsets[MAX_TYPE1_RECORDS];
    int numVar = 0;

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
                char *textptr = (char *)hdr;
                hdr += text_size;
                uint16_t nb_data;
                memcpy(&nb_data, hdr, 2);
                hdr += 2;
                // textptr is "@_ext_NAME" -- strip "@_ext_" (matches
                // decodeBinaryHeader's own `findLink(textptr + 6, value)`).
                const char *p = textptr + 6;
                if (numVar < MAX_TYPE1_RECORDS && strlen(p) < sizeof(varNames[0]))
                {
                    strcpy(varNames[numVar], p);
                    varSlotOffsets[numVar] = nb_data * 4;
                    numVar++;
                }
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
                hdr += text_size;
                uint16_t args_num;
                memcpy(&args_num, hdr, 2);
                hdr += 2;
                uint32_t addr;
                memcpy(&addr, hdr, 4);
                hdr += 4;
                hdr += 4;
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

    uint32_t initAddr = 0xFFFFFFFF, mainAddr = 0xFFFFFFFF;
    for (int i = 0; i < numFn; i++)
    {
        if (strcmp(fnNames[i], "init") == 0)
            initAddr = fnAddrs[i];
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
    printf("#define INIT_ENTRY_OFFSET %u\n", initAddr == 0xFFFFFFFF ? 0 : initAddr);
    printf("#define HAS_INIT %d\n", initAddr != 0xFFFFFFFF ? 1 : 0);

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

    printf("#define NUM_VAR_SLOTS %d\n", numVar);
    for (int i = 0; i < numVar; i++)
        printf("// var[%d] = %s at slot byte offset %u\n", i, varNames[i], varSlotOffsets[i]);
    printf("static const unsigned int var_slot_offset[%d] = {", numVar > 0 ? numVar : 1);
    for (int i = 0; i < numVar; i++)
        printf("%u%s", varSlotOffsets[i], i + 1 < numVar ? "," : "");
    if (numVar == 0)
        printf("0");
    printf("};\n");
    printf("static const char *var_name[%d] = {", numVar > 0 ? numVar : 1);
    for (int i = 0; i < numVar; i++)
        printf("\"%s\"%s", varNames[i], i + 1 < numVar ? "," : "");
    if (numVar == 0)
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
