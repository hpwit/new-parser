// Host-side pipeline verification for the real-world *.sc example scripts
// shipped with the sibling v1 repo (asmparser/sc_examples/) -- fibonacci,
// Mandelbrot renderers, Game of Life, bouncing balls, tetris, etc. Unlike
// test_parser.cpp's hand-written snippets, these are the actual scripts
// end users write, so they exercise syntax combinations (comma-form 2D
// arrays, struct arrays with constructors, __ASM__ functions, CRGB(...)
// constructor calls, ...) no synthetic test bothered to combine.
//
// Scope (see test/sc_examples/README.md for the full per-script writeup):
// every script is host-structurally-verified -- parse -> createBinary ->
// createExecutableFromBinary must all succeed with no crash/ASan error.
// None of the 21 scripts qualify for deeper execution verification: every
// one uses either floats (which hang this environment's QEMU fork -- see
// test/qemu/README.md), structs, or 3+ functions (the "not verified" set
// test/qemu/README.md already documents), and every one calls at least
// one function, which rules out test/host/mini_xtensa.h's interpreter
// too (it explicitly refuses call8/callExt). So "run correctly" here
// means "produces a loadable, relocated executable" -- the strongest
// claim this environment can actually back up without real hardware.
//
// Same fork-per-test crash isolation as test_parser.cpp, for the same
// reason: one script's crash must not hide the other 20 results.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "parser.h"
#include "compiler_error.h"
#include "binding.h"
#include "optimize.h"
#include "asm_parser.h"
#include "asm_execute.h"

// The example corpus lives in the sibling v1 repo, not in this one --
// asmparser2/ and asmparser/ are sibling directories under .../libraries/.
static const char *kExamplesDir = "../../../asmparser/sc_examples/";

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

// Two of the sc_examples corpus's real, confirmed-in-v1-too quirks (see
// README.md's "v1 ground truth" section -- checked by compiling v1's own
// ESPLiveScript.h standalone on host) are worked around here at the text
// level, not in the compiler, so the *rest* of each script's syntax can
// still be verified:
//
// 1. Bare `define NAME val` (no leading '#') is rejected by *both* v1
//    and v2's tokenizer -- tokenize.cpp only special-cases TokenKeywordDefine
//    when the immediately preceding token is '#' (TokenDiese); otherwise
//    it becomes TokenUnknown and parsing fails immediately. octo.sc,
//    octo2.sc, squaresani.sc, Mandel320.sc, tetris.sc and scroll.sc all
//    use the bare form for some/all of their constants -- a real bug in
//    those example scripts, present in v1 as shipped, not a v2
//    regression. Normalized to `#define` here so the rest of each
//    script's syntax is still exercised.
// 2. `import rand` (balls.sc/ballsbw.sc/ballscardputer.sc/tetris.sc) is
//    v1-valid: tokenize.cpp's tokenizer special-cases `import <stdlib
//    name>` by splicing in a real __ASM__ implementation of that name
//    (see v1's functionlib.h) in place of the two tokens. v2's tokenize.cpp
//    has the same mechanism sketched but left commented-out (references
//    an undefined findLibFunction()/_stdlib -- see the block right after
//    the TokenKeywordDefine handling), so `import rand` fails to parse in
//    v2 as-is: a real, root-caused, but NOT-fixed-here porting gap (see
//    README.md -- reviving safely would mean re-verifying mid-tokenization
//    character-stream splicing, which is a bigger, riskier change than
//    this session's other fixes). Worked around here by stripping the
//    `import rand` text and declaring+binding rand() as an ordinary
//    external instead, so the rest of the script (struct arrays, ball
//    physics, tetris board logic, ...) still gets verified even though
//    `import` itself doesn't.
static std::string fixBareDefine(const std::string &in)
{
    std::string out;
    size_t pos = 0;
    while (pos < in.size())
    {
        size_t eol = in.find('\n', pos);
        size_t lineEnd = (eol == std::string::npos) ? in.size() : eol;
        size_t i = pos;
        while (i < lineEnd && (in[i] == ' ' || in[i] == '\t'))
            i++;
        static const char kDefine[] = "define";
        size_t kw = sizeof(kDefine) - 1;
        if (i + kw <= lineEnd && in.compare(i, kw, kDefine) == 0 &&
            (i + kw == lineEnd || in[i + kw] == ' ' || in[i + kw] == '\t'))
        {
            out.append(in, pos, i - pos);
            out.push_back('#');
            out.append(in, i, lineEnd - i);
        }
        else
        {
            out.append(in, pos, lineEnd - pos);
        }
        if (eol == std::string::npos)
            break;
        out.push_back('\n');
        pos = eol + 1;
    }
    return out;
}

static std::string stripImportRand(const std::string &in)
{
    std::string out = in;
    size_t p;
    while ((p = out.find("import rand")) != std::string::npos)
        out.erase(p, strlen("import rand"));
    return out;
}

// v1's real compile entrypoints (Parser::parseScript() et al. in
// ESPLiveScript.h) always prepend this exact boilerplate -- a sync()
// implementation plus `#define true 1` / `#define false 0` -- before the
// user's own script text (confirmed by reading the four near-identical
// addContent() call sequences at ESPLiveScript.h:303-356). v2's
// Parser::parse() is the raw, low-level entrypoint (matching how
// test_parser.cpp itself calls it) and has no higher-level "compile a
// user script" wrapper yet to attach this policy to, so it isn't done
// automatically -- a caller has to do it, same as this harness does here.
// `sync()` itself is registered as an ordinary bindFunction() stub below
// rather than reimplemented as v1's raw-asm trampoline (which just calls
// into a host-bound "sync" external anyway -- see v1's functionlib.h's
// `_sync` string), since that's behaviorally equivalent for the
// structural verification this harness does.
static const char *kPrelude =
    "#define true 1\n"
    "#define false 0\n"
    "uint32_t _handle_;\n"
    "uint32_t _execaddr_;\n"
    "external uint32_t rand(uint32_t mod);\n";

static std::string preprocess(const std::string &raw)
{
    return kPrelude + fixBareDefine(stripImportRand(raw));
}

// A generous common host-binding set covering every external name used
// across the corpus (see README.md's per-script inventory), following
// the same signature-string convention as examples/BouncingBalls.ino and
// test_parser.cpp's registerBallEffectBindings(). Registering a name a
// given script never calls is harmless -- it just adds an unused
// binded_assets entry.
static void registerCommonBindings()
{
    bindFunction((char *)"CRGB", (char *)"hsv", (char *)"int,int,int", NULL);
    bindVariable((char *)"CRGB", (char *)"leds", (char *)"[]", NULL);
    bindFunction((char *)"void", (char *)"show", NULL, NULL);
    bindFunction((char *)"void", (char *)"clear", NULL, NULL);
    bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", NULL);
    bindFunction((char *)"void", (char *)"setPixel", (char *)"int,int,int", NULL);
    bindFunction((char *)"void", (char *)"setPixelsize", (char *)"int", NULL);
    bindFunction((char *)"void", (char *)"resetStat", NULL, NULL);
    bindFunction((char *)"void", (char *)"sync", NULL, NULL);
    bindFunction((char *)"void", (char *)"printfln", (char *)"char*,Args", NULL);
    bindFunction((char *)"void", (char *)"printf", (char *)"char*,Args", NULL);
    bindFunction((char *)"void", (char *)"delay", (char *)"uint32_t", NULL);
    bindFunction((char *)"uint8_t", (char *)"sin8", (char *)"uint8_t", NULL);
    bindFunction((char *)"float", (char *)"hypot", (char *)"float,float", NULL);
    bindFunction((char *)"float", (char *)"atan2", (char *)"float,float", NULL);
    bindFunction((char *)"float", (char *)"sin", (char *)"float", NULL);
    bindFunction((char *)"float", (char *)"cos", (char *)"float", NULL);
    bindFunction((char *)"float", (char *)"log", (char *)"float", NULL);
    bindFunction((char *)"uint32_t", (char *)"millis", NULL, NULL);
    bindFunction((char *)"void", (char *)"display", (char *)"int", NULL);
    bindFunction((char *)"void", (char *)"dp", (char *)"float", NULL);
    bindFunction((char *)"void", (char *)"pinInterrupt", (char *)"uint32_t,char*,int", NULL);
    bindVariable((char *)"uint8_t", (char *)"font", (char *)"[]", NULL);
}

// gameoflifecarputer.sc is the one script in the corpus that assigns
// hsv()'s result to a uint16_t and declares `external uint16_t
// leds[...]` itself -- everywhere else in the corpus that auto-declares
// (no `external` line of its own) expects hsv()/leds to be CRGB. Kept
// separate from registerCommonBindings() rather than layered on top of
// it, since bindFunction()/bindVariable() would otherwise add a second,
// shadowing "hsv"/"leds" entry -- each forked child process only needs
// whichever one its own script actually wants.
static void registerGameOfLifeCarputerBindings()
{
    bindFunction((char *)"uint16_t", (char *)"hsv", (char *)"int,int,int", NULL);
    bindVariable((char *)"uint16_t", (char *)"leds", (char *)"[]", NULL);
    bindFunction((char *)"void", (char *)"show", NULL, NULL);
    bindFunction((char *)"void", (char *)"clear", NULL, NULL);
}

enum ExpectedOutcome
{
    ExpectPass,
    ExpectFail, // known-broken (v1-side bug or genuinely out of scope) -- see README.md
};

struct ScriptCase
{
    const char *name;
    const char *relpath; // relative to kExamplesDir
    ExpectedOutcome expected;
    const char *note;
    bool gameOfLifeCarputerBindings;
};

static int passed = 0, failed = 0, expectedFails = 0;

// Runs in the child process. Never returns.
static void runInChild(const ScriptCase &sc)
{
    if (sc.gameOfLifeCarputerBindings)
        registerGameOfLifeCarputerBindings();
    else
        registerCommonBindings();

    std::string path = std::string(kExamplesDir) + sc.relpath;
    std::string raw = readFile(path.c_str());
    if (raw.empty())
    {
        printf("       could not read %s (or file is empty)\n", path.c_str());
        fflush(stdout);
        _exit(1);
    }
    std::string processed = preprocess(raw);

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
        printf("       parse error=%d (%s) at line %d\n", Error.error,
               error_messages[Error.error], Error.token ? Error.token->line : -1);
        fflush(stdout);
        _exit(1);
    }

    Binary bin = createBinary(&footer, &header, &content, false);
    if (bin.error.error)
    {
        printf("       assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
        fflush(stdout);
        _exit(1);
    }
    if (bin.binary_data == NULL || bin.instruction_size == 0)
    {
        printf("       no binary data produced\n");
        fflush(stdout);
        _exit(1);
    }

    executable exe = createExecutableFromBinary(&bin);
    bool ok = true;
    if (exe.error.error)
    {
        printf("       loader error: %s\n", exe.error.error_message ? exe.error.error_message : "?");
        ok = false;
    }
    else if (exe.functions.size() == 0 || exe.start_program == NULL)
    {
        printf("       loader produced no callable function\n");
        ok = false;
    }
    else
    {
        printf("       OK: %d bytes instructions, %d function record(s)\n",
               bin.instruction_size, exe.functions.size());
    }

    freeExecutable(&exe);
    fflush(stdout);
    _exit(ok ? 0 : 1);
}

static void runScriptTest(const ScriptCase &sc)
{
    printf("RUNNING: %-55s [%s]\n", sc.name, sc.expected == ExpectPass ? "expect pass" : "expect fail");
    if (sc.note)
        printf("       %s\n", sc.note);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
        runInChild(sc);

    int status = 0;
    waitpid(pid, &status, 0);

    bool crashed = WIFSIGNALED(status);
    bool childOk = !crashed && WIFEXITED(status) && WEXITSTATUS(status) == 0;

    if (crashed)
    {
        // A crash is never an "expected" outcome, even for scripts we
        // otherwise expect to fail cleanly with a parse/assembler/loader
        // error -- those should always exit(1), not signal.
        failed++;
        printf("[CRASH] %s (signal %d)\n", sc.name, WTERMSIG(status));
        return;
    }

    if (sc.expected == ExpectPass)
    {
        if (childOk)
        {
            passed++;
            printf("[PASS] %s\n", sc.name);
        }
        else
        {
            failed++;
            printf("[FAIL] %s (expected the full pipeline to succeed)\n", sc.name);
        }
    }
    else
    {
        if (!childOk)
        {
            expectedFails++;
            printf("[XFAIL] %s (known-broken, see README.md -- as expected)\n", sc.name);
        }
        else
        {
            // The bug this case documents seems to be fixed.
            failed++;
            printf("[FAIL] %s (marked expect-fail but the full pipeline now succeeds -- update README.md and this test)\n", sc.name);
        }
    }
}

int main()
{
    ScriptCase cases[] = {
        {"fibonacci.sc", "fibonacci.sc", ExpectPass, NULL, false},
        {"Madel.sc", "Madel.sc", ExpectPass, NULL, false},
        {
            "Madel320.sc",
            "Madel320.sc",
            ExpectFail,
            "v1 ground truth: fails identically in v1 (\"impossible to find "
            "declaraiton for sizex\") -- sizex/sizey are assigned in main() "
            "with no declaration anywhere; a bug in the original example, "
            "not a v2 regression.",
            false,
        },
        {
            "Mandel320.sc",
            "Mandel320.sc",
            ExpectFail,
            "Same sizex/sizey bug as Madel320.sc (confirmed via v1 host "
            "build), on top of also using bare `define` for its first "
            "constant.",
            false,
        },
        {"mandelBW.sc", "mandelBW.sc", ExpectPass, NULL, false},
        {"octo.sc", "octo.sc", ExpectPass, "Uses bare `define` throughout -- normalized to `#define` by this harness (see preprocess()).", false},
        {"octo2.sc", "octo2.sc", ExpectPass, "Same bare-`define` normalization as octo.sc; every external is auto-declared (all commented out in the source).", false},
        {"gamebw.sc", "gamebw.sc", ExpectPass, NULL, false},
        {"gameoflife.sc", "gameoflife.sc", ExpectPass, NULL, false},
        {"gameoflifecarputer.sc", "gameoflifecarputer.sc", ExpectPass, NULL, true},
        {"circles.sc", "circles.sc", ExpectPass, "No `external` declarations at all -- fully exercises the auto-declare codegen fix (see README.md).", false},
        {"scroll.sc", "scroll.sc", ExpectPass, "Uses bare `define` for led_width -- normalized by this harness.", false},
        {
            "dsiplayicon.sc",
            "dsiplayicon.sc",
            ExpectFail,
            "v1 ground truth: fails in v1 too (\"function displaypic(...) not "
            "found\"). displaypic() declares its pixel-map parameter as "
            "`char * pic` but every call site passes a `uint8_t[]` array "
            "(ghostp/mario/cerise); binding.cpp's/v1's type classifier "
            "treats uint8_t as \"num\", not \"char\", so the call-site "
            "signature never matches the declared one. A parameter-type "
            "bug in the original example script, not a v2 regression.",
            false,
        },
        {"balls.sc", "balls.sc", ExpectPass, "`import rand` stripped/rebound by this harness (see preprocess()).", false},
        {"ballscardputer.sc", "ballscardputer.sc", ExpectPass, "`import rand` stripped/rebound by this harness.", false},
        {"ballsbw.sc", "ballsbw.sc", ExpectPass, "`import rand` stripped/rebound by this harness.", false},
        {"animwle.sc", "animwle.sc", ExpectPass, NULL, false},
        {"squaresani.sc", "squaresani.sc", ExpectPass, "Uses bare `define` throughout -- normalized by this harness.", false},
        {
            "psion32/arkanoid.sc",
            "psion32/arkanoid.sc",
            ExpectFail,
            "v1 ground truth: v1's parseProgram() has no `typedef` handling "
            "at all (only `struct NAME {...}`), confirmed via v1 host "
            "build (\"expecting external, __ASM__ or variable type ... "
            "typedef\"). Also relies on undeclared globals (led16ffer, "
            "framebuffer) and bodyless non-external hardware-abstraction "
            "prototypes. Not this DSL's syntax in either version.",
            false,
        },
        {
            "psion32/snake.sc",
            "psion32/snake.sc",
            ExpectFail,
            "SDL2 C code (SDL_Renderer*, switch/case, char *argv[]), not "
            "this DSL at all -- fails cleanly in v1. Its own first parse "
            "error is a real v2 crash this session fixed: `struct Segment "
            "{...};` (trailing ';', a natural C habit v1 also rejects, "
            "just without crashing) followed by a global `Segment "
            "snake[MAX_SNAKE];` used to segfault in nodetoken.cpp's "
            "copyPrty() (NULL from getVarTypeObj()) instead of reporting a "
            "parse error -- see README.md. Now reports a clean error, same "
            "as v1.",
            false,
        },
        {
            "tetris.sc",
            "tetris.sc",
            ExpectPass,
            "Bare `define`/`import rand` normalized by this harness. Uses "
            "`!checkCollision(...)` in three different functions -- "
            "exercises the for_if_num2 fix (see README.md).",
            false,
        },
        {
            "testjson.sc",
            "testjson.sc",
            ExpectFail,
            "Empty file (0 bytes) -- nothing to compile.",
            false,
        },
    };

    int n = sizeof(cases) / sizeof(cases[0]);
    for (int i = 0; i < n; i++)
        runScriptTest(cases[i]);

    printf("\n%d passed, %d failed, %d expected-fail (of %d)\n", passed, failed, expectedFails, n);
    return failed ? 1 : 0;
}
