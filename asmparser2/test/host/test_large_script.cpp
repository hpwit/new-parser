// Compiles fixtures/multi_effect_controller.sc -- a purpose-written,
// several-hundred-line .sc script (not from the sibling v1 repo's
// sc_examples/, unlike test_sc_examples.cpp) exercising this compiler at
// a scale and complexity closer to a real project: two struct types with
// constructors/methods and struct arrays, an internal (non-external) 2D
// buffer, recursion (gcd/fibonacci), a prime check, an insertion sort
// over struct-array fields, three switchable render paths, and a
// button-driven mode switch -- through the full
// parse -> createBinary -> createExecutableFromBinary pipeline, then
// checks the resulting binary against a documented resource budget for
// an ESP32 *without* PSRAM.
//
// Why a budget check, not just "did it compile": this compiler's own
// output -- instruction bytes (destined for IRAM) and the executable's
// data region (the script's globals/struct-arrays/buffers, destined for
// DRAM) -- are exactly the two things a PSRAM-less ESP32 can run out of.
// There's no ESP32 hardware in this environment (same limitation noted
// throughout test/qemu/README.md), so this can't be verified by actually
// running out of RAM on a real board. What it checks instead: the
// compiler's own, precise size accounting (Binary::instruction_size,
// executable::data_size) against numbers derived from public ESP32
// (original, non-S3/non-PSRAM) memory figures --
//   - Total SRAM is 520 KB, split into IRAM (~200 KB) and DRAM
//     (~320 KB), shared with the entire running firmware (WiFi/BT
//     stack, ESP-IDF/Arduino core, the sketch's own heap). A script
//     running inside a larger Arduino sketch should leave the large
//     majority of that for everything else, not consume it itself.
//   - Budgets picked here (64 KB instructions, 96 KB data) are
//     deliberately conservative slices of those totals -- enough
//     headroom for a script noticeably bigger than this one, while
//     still leaving most of a non-PSRAM board's RAM for WiFi and the
//     rest of the sketch. They're a compile-time proxy for "this script
//     is a reasonable size for a PSRAM-less board", not a guarantee
//     that any *particular* sketch embedding it will link/run within
//     its own remaining budget -- that depends on everything else the
//     sketch does too, which is outside what a compiler test can check.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "parser.h"
#include "compiler_error.h"
#include "binding.h"
#include "optimize.h"
#include "asm_parser.h"
#include "asm_execute.h"

static const char *kScriptPath = "fixtures/multi_effect_controller.sc";

// Same boilerplate v1's real compile entrypoints always prepend before a
// user script (see test_sc_examples.cpp's kPrelude for the full
// citation) -- true/false, and the _handle_/_execaddr_ globals
// pinInterrupt() calls here need.
static const char *kPrelude =
    "#define true 1\n"
    "#define false 0\n"
    "uint32_t _handle_;\n"
    "uint32_t _execaddr_;\n";

// ESP32 (non-PSRAM) resource budget this script's compiled output is
// checked against -- see the file header comment for how these were
// picked. kMaxInstructionBytes is deliberately kept well under 65536:
// Binary::instruction_size is itself a uint16_t (asm_types.h), so
// instruction bytes can never structurally exceed that regardless of
// budget -- comparing against something close to 65536 would be a
// tautological check that can never fail.
static const uint32_t kMaxInstructionBytes = 32 * 1024;
static const uint32_t kMaxDataBytes = 96 * 1024;

static std::string readFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return std::string();
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    fclose(f);
    return out;
}

static void registerBindings()
{
    bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", NULL);
    bindVariable((char *)"CRGB", (char *)"leds", (char *)"[]", NULL);
    bindFunction((char *)"void", (char *)"show", NULL, NULL);
    bindFunction((char *)"void", (char *)"clear", NULL, NULL);
    bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", NULL);
    bindFunction((char *)"void", (char *)"delay", (char *)"uint32_t", NULL);
    bindFunction((char *)"void", (char *)"printfln", (char *)"char*,Args", NULL);
    bindFunction((char *)"uint32_t", (char *)"millis", NULL, NULL);
    bindFunction((char *)"float", (char *)"sin", (char *)"float", NULL);
    bindFunction((char *)"float", (char *)"hypot", (char *)"float,float", NULL);
    bindFunction((char *)"void", (char *)"pinInterrupt", (char *)"uint32_t,char*,int", NULL);
}

int main()
{
    printf("Compiling %s against an ESP32-without-PSRAM budget "
           "(instructions <= %u bytes, data <= %u bytes)\n",
           kScriptPath, kMaxInstructionBytes, kMaxDataBytes);

    std::string raw = readFile(kScriptPath);
    if (raw.empty())
    {
        printf("FAIL: could not read %s (run this from test/host/)\n", kScriptPath);
        return 1;
    }
    printf("  script size: %zu bytes, %d lines\n", raw.size(),
           (int)(std::count(raw.begin(), raw.end(), '\n')));

    registerBindings();

    std::string processed = std::string(kPrelude) + raw;
    Parser p;
    p.clean();
    content.clear();
    header.clear();
    footer.clear();

    Script s;
    char *buf = strdup(processed.c_str());
    s.addContent(buf);
    s.init();
    p.parse(&s, &__allTokens);

    if (Error.error)
    {
        printf("FAIL: parse error=%d (%s) at line %d\n", Error.error,
               error_messages[Error.error], Error.token ? Error.token->line : -1);
        return 1;
    }
    printf("  parse: OK\n");

    Binary bin = createBinary(&footer, &header, &content, false);
    if (bin.error.error)
    {
        printf("FAIL: assembler error: %s\n",
               bin.error.error_message ? bin.error.error_message : "?");
        return 1;
    }
    if (bin.binary_data == NULL || bin.instruction_size == 0)
    {
        printf("FAIL: no binary data produced\n");
        return 1;
    }
    printf("  assemble: OK (%u instruction bytes, %u data bytes declared)\n",
           bin.instruction_size, bin.data_size);

    executable exe = createExecutableFromBinary(&bin);
    if (exe.error.error)
    {
        printf("FAIL: loader error: %s\n",
               exe.error.error_message ? exe.error.error_message : "?");
        return 1;
    }
    if (exe.functions.size() == 0 || exe.start_program == NULL)
    {
        printf("FAIL: loader produced no callable function\n");
        return 1;
    }
    printf("  load/relocate: OK (%d function record(s), %u byte data region)\n",
           exe.functions.size(), exe.data_size);

    bool ok = true;
    if (bin.instruction_size > kMaxInstructionBytes)
    {
        printf("FAIL: instruction_size %u exceeds the %u byte budget\n",
               bin.instruction_size, kMaxInstructionBytes);
        ok = false;
    }
    if (exe.data_size > kMaxDataBytes)
    {
        printf("FAIL: data_size %u exceeds the %u byte budget\n",
               exe.data_size, kMaxDataBytes);
        ok = false;
    }

    freeExecutable(&exe);

    if (ok)
    {
        printf("PASS: compiles to a binary within the ESP32-without-PSRAM budget "
               "(%u/%u instruction bytes, %u/%u data bytes)\n",
               bin.instruction_size, kMaxInstructionBytes, exe.data_size, kMaxDataBytes);
    }
    return ok ? 0 : 1;
}
