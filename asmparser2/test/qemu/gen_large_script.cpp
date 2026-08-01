// Compiles the REAL test/host/fixtures/multi_effect_controller.sc (the
// same several-hundred-line script test_large_script.cpp's `make
// run-large` budget-checks) and extracts entry points for its pure
// integer, no-external-call, no-global-variable helper functions --
// gcd(), fib(), isPrime(), clampInt() -- so run.sh's runner can execute
// them for real under QEMU.
//
// This is NOT full-script execution: the rest of the script (particle/
// star/plasma render loop) is float-heavy, which hangs this QEMU fork
// with no output (see README.md's "Not verified" section), and calls
// ten different `external`/auto-declared host functions (hsv, show,
// rand, delay, ...) that would each need a bridged/patched implementation
// in a bare-metal runner -- disproportionate scaffolding for a script
// that was never meant to run standalone. What *is* meaningfully
// verifiable, and what this does: the four helper functions are pure
// integer arithmetic/recursion with no external or global-variable
// dependencies, so they're callable directly (same scheme as the
// existing fibonacci/named_function cases) with zero bridging needed,
// and doing so against the actual compiled bytes of the real fixture
// (not a reimplementation) proves those bytes execute correctly on real
// Xtensa hardware semantics, not just that MiniXtensa/host review
// consider them plausible.
//
// IMPORTANT: createExecutableFromBinary() mutates bin.binary_data in
// place, so the code bytes must be captured before calling it (see
// gen_fibonacci_arg.cpp for the same note).
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include "parser.h"
#include "compiler_error.h"
#include "binding.h"
#include "asm_parser.h"
#include "asm_execute.h"

static const char *kScriptPath = "../host/fixtures/multi_effect_controller.sc";

static const char *kPrelude =
    "#define true 1\n"
    "#define false 0\n"
    "uint32_t _handle_;\n"
    "uint32_t _execaddr_;\n";

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

int main()
{
    std::string raw = readFile(kScriptPath);
    if (raw.empty())
    {
        fprintf(stderr, "could not read %s (run this from test/qemu/)\n", kScriptPath);
        return 1;
    }

    registerBindings();

    std::string processed = std::string(kPrelude) + raw;
    Parser p;
    p.clean();
    Script s;
    char *buf = strdup(processed.c_str());
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

    unsigned char *codeCopy = (unsigned char *)malloc(bin.instruction_size);
    memcpy(codeCopy, bin.binary_data, bin.instruction_size);
    int instrSize = bin.instruction_size;

    executable exe = createExecutableFromBinary(&bin);
    if (exe.error.error)
    {
        fprintf(stderr, "loader error: %s\n", exe.error.error_message);
        return 1;
    }

    globalcall *gcdFn = findPlainFunction(&exe, "gcd");
    globalcall *fibFn = findPlainFunction(&exe, "fib");
    globalcall *isPrimeFn = findPlainFunction(&exe, "isPrime");
    globalcall *clampIntFn = findPlainFunction(&exe, "clampInt");
    if (!gcdFn || !fibFn || !isPrimeFn || !clampIntFn)
    {
        fprintf(stderr, "could not find one of gcd/fib/isPrime/clampInt's plain records "
                         "(gcd=%p fib=%p isPrime=%p clampInt=%p)\n",
                (void *)gcdFn, (void *)fibFn, (void *)isPrimeFn, (void *)clampIntFn);
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
    printf("#define GCD_ENTRY_OFFSET %u\n", gcdFn->address);
    printf("#define FIB_ENTRY_OFFSET %u\n", fibFn->address);
    printf("#define ISPRIME_ENTRY_OFFSET %u\n", isPrimeFn->address);
    printf("#define CLAMPINT_ENTRY_OFFSET %u\n", clampIntFn->address);

    freeExecutable(&exe);
    return 0;
}
