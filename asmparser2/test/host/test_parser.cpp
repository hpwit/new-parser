// Host-side test harness for the ESPLiveScript2 parser/compiler.
//
// Compiles the parser sources directly (no Arduino/ESP-IDF dependency) and
// feeds them script strings, checking the resulting error code. Each test
// runs in its own forked process so a compiler crash on one script doesn't
// take down the rest of the suite -- it's reported as CRASH instead.
//
// Build/run with `make run` in this directory.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#include "parser.h"
#include "compiler_error.h"
#include "binding.h"
#include "optimize.h"
#include "mini_xtensa.h"
#include "asm_parser.h"
#include "asm_execute.h"
#include "asm_serialize.h"
#include "arguments.h"
#include "json_binding.h"
#include "script_executable.h"

struct TestCase
{
    const char *name;
    const char *script;
    int expectedError;          // noError (0) if the script should parse cleanly
    const char *mustContainAsm; // optional substring expected in generated code, or NULL
    bool knownCrash;            // documents a known compiler crash (see comment at call site)
};

static int passed = 0;
static int failed = 0;
static int knownCrashes = 0;

static bool textContains(Text &t, const char *needle)
{
    for (int i = 0; i < t.size(); i++)
    {
        if (strstr(t.getText(i), needle) != NULL)
            return true;
    }
    return false;
}

// Runs in the child process. Never returns.
static void runInChild(const TestCase &tc)
{
    Parser p;
    p.clean();
    content.clear();
    header.clear();
    footer.clear();

    Script s;
    char *buf = strdup(tc.script);
    s.addContent(buf);
    s.init();

    p.parse(&s, &__allTokens);

    bool ok = (Error.error == tc.expectedError);
    if (ok && tc.mustContainAsm != NULL)
    {
        ok = textContains(content, tc.mustContainAsm) ||
             textContains(header, tc.mustContainAsm) ||
             textContains(footer, tc.mustContainAsm);
        if (!ok)
            printf("       expected generated asm to contain \"%s\"\n", tc.mustContainAsm);
    }

    if (!ok && Error.error)
        display_error(&Error);

    _exit(ok ? 0 : 1);
}

static void runTest(const TestCase &tc)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0)
        runInChild(tc);

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFSIGNALED(status))
    {
        if (tc.knownCrash)
        {
            knownCrashes++;
            printf("[XFAIL] %s (known crash, signal %d - see comment at test site)\n",
                   tc.name, WTERMSIG(status));
        }
        else
        {
            failed++;
            printf("[CRASH] %s (signal %d)\n", tc.name, WTERMSIG(status));
        }
        return;
    }

    bool childOk = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (tc.knownCrash && childOk)
    {
        // The bug this test documents seems to be fixed - flip knownCrash to
        // false and let the test assert normally.
        failed++;
        printf("[FAIL] %s (marked knownCrash but it now parses cleanly - update the test)\n", tc.name);
        return;
    }

    if (childOk)
    {
        passed++;
        printf("[PASS] %s\n", tc.name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s (expected error %d: \"%s\")\n",
               tc.name, tc.expectedError, error_messages[tc.expectedError]);
    }
}

// Registers the host bindings used by the "bouncing balls" effect script
// below. Bindings live in the global binded_assets vect and are visible to
// every test that runs after this, so it only needs to happen once.
static void registerBallEffectBindings()
{
    bindFunction((char *)"uint32_t", (char *)"rand", (char *)"uint32_t", NULL);
    bindFunction((char *)"void", (char *)"setPixel", (char *)"int,int,int", NULL);
    bindFunction((char *)"void", (char *)"clear", NULL, NULL);
    bindFunction((char *)"void", (char *)"show", NULL, NULL);
    bindFunction((char *)"void", (char *)"printfln", (char *)"char*,Args", NULL);
}

// A realistic effect script: struct with a constructor and two member
// functions, nested for loops, +=, the ^ (power) operator, an array of
// structs, and five host-bound calls (rand/setPixel/clear/show/printfln)
// used with no `external` declaration in the script -- they're resolved
// purely from registerBallEffectBindings() above.
//
// Note: `true` is not a language keyword here -- like the original
// examples/test.ino, the script must #define it itself.
static const char *ballEffectScript = R"SCRIPT(
#define max_nb_balls 20
#define rmax 8
#define rmin 8
#define width 384
#define height 168
#define true 1

struct ball
{
   float vx, vy, xc, yc, r;
   int color;
   void drawBall()
   {
      int startx = xc - r;
      int r2 = r * r;
      int starty = yc - r;
      int _xc = xc;
      int _yc = yc;
      for (int i = startx; i <= _xc; i++)
         for (int j = starty; j <= _yc; j++)
         {
            int distance = (i - xc) ^ 2 + (j - yc) ^ 2;
            if (distance <= r2)
            {
               setPixel(i,j,1);
               setPixel((int)(2 * xc - i),j,1);
               setPixel((int)(2 * xc - i),(int)(2 * yc - j),1);
               setPixel(i,(int)(2 * yc - j),1);
            }
         }
   }
   void updateBall()
   {
      xc += vx;
      yc += vy;
      if (xc >= (width - r - 1))
      {
         xc = width - r - 1;
         vx = -vx;
      }
      if (xc < (r + 1))
      {
         xc = r + 1;
         vx = -vx;
      }
      if (yc >= height - r - 1)
      {
         yc = height - r - 1;
         vy = -vy;
      }
      if (yc < r + 1)
      {
         yc = r + 1;
         vy = -vy;
      }
      drawBall();
   }
   ball()
   {
      vx = rand(300) / 255 + 0.7;
      vy = rand(280) / 255 + 0.5;
      r = (rmax - rmin) * (rand(280) / 180) + rmin;
      xc = width / 2 * (rand(280) / 255 + 0.3) + 15;
      yc = height / 2 * (rand(280) / 255 + 0.3) + 15;
      color = rand(255);
   }
}

ball Balls[max_nb_balls];

void main()
{
   int num = 8;
   if (num > max_nb_balls)
   {
      num = max_nb_balls;
   }
   if (num <= 0)
   {
      num = 1;
   }
   printfln("numberof balls:%d",num);
   uint32_t h;
   while (true)
   {
      clear();
      for (int i = 0; i < num; i++)
         Balls[i].updateBall();
      h++;
      show();
   }
}
)SCRIPT";

// Exercises optimize() directly on a hand-built instruction stream, rather
// than through Parser::parse(), so the three peephole patterns it's
// supposed to catch are checked precisely and independent of whatever the
// codegen happens to emit for a given script.
static void runOptimizeUnitTest()
{
    printf("RUNNING: optimize() peephole patterns\n");
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Text t;
        t.addAfter("movi a3,10");    // 0: kept (first load into a3)
        t.addAfter("movi a3,10");    // 1: redundant reload of a3 -> blanked
        t.addAfter("movi a5,20");    // 2: different register -> kept
        t.addAfter("mov a4,a4");     // 3: no-op self move -> blanked
        t.addAfter("s32i a3,a1,32"); // 4: store -> kept
        t.addAfter("l32i a3,a1,32"); // 5: reload of the register just stored -> blanked
        t.addAfter("l32i a6,a1,32"); // 6: different destination register -> kept

        optimize(&t);

        bool ok = true;
        const char *expected[] = {
            "movi a3,10",
            " ",
            "movi a5,20",
            " ",
            "s32i a3,a1,32",
            " ",
            "l32i a6,a1,32",
        };
        for (int i = 0; i < 7; i++)
        {
            char *got = *t.getChildAtPos(i);
            if (got == NULL || strcmp(got, expected[i]) != 0)
            {
                printf("       line %d: expected \"%s\", got \"%s\"\n", i, expected[i], got ? got : "(null)");
                ok = false;
            }
        }
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] optimize() peephole patterns (signal %d)\n", WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] optimize() peephole patterns\n");
    }
    else
    {
        failed++;
        printf("[FAIL] optimize() peephole patterns\n");
    }
}

static vect<char *> snapshotText(Text &t)
{
    vect<char *> snap;
    for (int i = 0; i < t.size(); i++)
        snap.push_back(string_format("%s", t.getText(i)));
    return snap;
}

// Compiles `script` up through codegen, snapshots the *unoptimized* content
// buffer, runs optimize() on the live buffer to get the optimized version,
// then interprets both with MiniXtensa (see mini_xtensa.h) and checks they
// leave identical final memory state -- i.e. optimize() didn't change what
// the program computes. Restricted to single-function, call-free, float-
// free scripts (see mini_xtensa.h for why); MiniXtensa itself refuses to
// "guess" on anything outside its supported subset, so a script this
// doesn't apply to fails loudly (as an interpretation error) rather than
// silently reporting a false pass.
static void runEquivalenceTest(const char *name, const char *script)
{
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Parser p;
        p.clean();
        content.clear();
        header.clear();
        footer.clear();

        Script s;
        char *buf = strdup(script);
        s.addContent(buf);
        s.init();

        p._tks = &__allTokens;
        p._tks->tokenize(&s, true, true, 1);
        p.parseProgram();
        if (Error.error)
        {
            printf("       parse error=%d (%s)\n", Error.error, error_messages[Error.error]);
            _exit(1);
        }
        buildParents(&program);
        program.visitNode();

        vect<char *> before = snapshotText(content);
        optimize(&content);
        vect<char *> after = snapshotText(content);

        const char *err1 = NULL, *err2 = NULL;
        MiniXtensa vmBefore, vmAfter;
        bool ok1 = vmBefore.run(before, &err1);
        bool ok2 = vmAfter.run(after, &err2);

        bool ok = true;
        if (!ok1)
        {
            printf("       could not interpret unoptimized output (unsupported: %s)\n", err1 ? err1 : "?");
            ok = false;
        }
        if (!ok2)
        {
            printf("       could not interpret optimized output (unsupported: %s)\n", err2 ? err2 : "?");
            ok = false;
        }
        if (ok1 && ok2 && memcmp(vmBefore.mem, vmAfter.mem, MiniXtensa::kMemSize) != 0)
        {
            printf("       final memory state differs between unoptimized and optimized runs\n");
            ok = false;
        }

        before.empty();
        after.empty();
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

// Compiles `script` through the normal pipeline (parse -> codegen ->
// optimize, same as Parser::parse()), interprets the result with
// MiniXtensa, and checks the 32-bit value at `memOffset` (an a1-relative
// stack offset, found by inspecting the generated assembly -- see the
// script's own comment for how) equals `expected`. Unlike
// runEquivalenceTest (which only checks optimized == unoptimized without
// knowing what either should compute), this checks the compiler produced
// the mathematically correct answer.
static void runCorrectnessTest(const char *name, const char *script, int memOffset, int32_t expected)
{
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Parser p;
        p.clean();
        content.clear();
        header.clear();
        footer.clear();

        Script s;
        char *buf = strdup(script);
        s.addContent(buf);
        s.init();
        p.parse(&s, &__allTokens);

        bool ok = true;
        if (Error.error)
        {
            printf("       parse error=%d (%s)\n", Error.error, error_messages[Error.error]);
            ok = false;
        }
        else
        {
            vect<char *> lines = snapshotText(content);
            const char *err = NULL;
            MiniXtensa vm;
            if (!vm.run(lines, &err))
            {
                printf("       could not interpret (unsupported: %s)\n", err ? err : "?");
                ok = false;
            }
            else
            {
                int32_t got = *(int32_t *)(vm.mem + memOffset);
                if (got != expected)
                {
                    printf("       expected %d at offset %d, got %d\n", expected, memOffset, got);
                    ok = false;
                }
            }
            lines.empty();
        }

        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

// Compiles a script all the way through to real Xtensa machine code via
// createBinary() (src/asm_parser.cpp) and checks it actually produced
// something real. Every script starts with a jump table -- reserved,
// zeroed 4-byte slots for the runtime loader to patch (at minimum the 3
// built-in handle/execaddr/sync slots, plus one per declared function and
// one per external reference) -- followed by the first function's
// "entry a1,N" (see visitnode.cpp). bin_entry's low byte is always 0x36
// regardless of the frame size N, so this looks for the first 0x36 byte,
// confirms everything before it is a zeroed reservation slot (proving
// addInstr's jump-table bookkeeping is consistent), and confirms it lands
// on a 4-byte boundary at or past the mandatory 3-slot prefix.
static void runGenerateBinaryTest(const char *name, const char *script)
{
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Parser p;
        p.clean();
        content.clear();
        header.clear();
        footer.clear();

        Script s;
        char *buf = strdup(script);
        s.addContent(buf);
        s.init();
        p.parse(&s, &__allTokens);

        if (Error.error)
        {
            printf("       parse error=%d (%s)\n", Error.error, error_messages[Error.error]);
            _exit(1);
        }

        Binary bin = createBinary(&footer, &header, &content, false);

        bool ok = true;
        if (bin.error.error)
        {
            printf("       assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
            ok = false;
        }
        else if (bin.binary_data == NULL || bin.instruction_size == 0)
        {
            printf("       no binary data produced\n");
            ok = false;
        }
        else
        {
            int entryOffset = -1;
            for (int i = 0; i < bin.instruction_size && i < 128; i++)
            {
                if (bin.binary_data[i] == 0x36)
                {
                    entryOffset = i;
                    break;
                }
            }
            if (entryOffset < 12 || entryOffset % 4 != 0)
            {
                printf("       no plausible entry opcode found in the jump-table-prefixed instruction stream\n");
                ok = false;
            }
            else
            {
                for (int i = 0; i < entryOffset; i++)
                {
                    if (bin.binary_data[i] != 0x00)
                    {
                        printf("       reserved jump-table slot at offset %d is not zeroed (0x%02x)\n", i, bin.binary_data[i]);
                        ok = false;
                        break;
                    }
                }
            }
            if (ok)
                printf("       entry at offset %d, %d bytes of instructions, %d bytes of relocation header\n",
                       entryOffset, bin.instruction_size, bin.function_size);
        }

        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

// Regression test for the auto-declare/codegen-tree bug described at the
// "bouncing balls effect (struct + host bindings, no external decls)" test
// case above: a call/variable reference resolved only through
// bindFunction()/bindVariable() (no `external` line in the script) used to
// parse fine but silently drop its jump-table header reservation, so
// createBinary() either produced a corrupt binary or createExecutableFromBinary()
// failed to relocate it. Runs the full script (struct with constructor +
// methods, array of structs, 5 host-bound calls, all auto-declared) all the
// way through parse -> createBinary -> createExecutableFromBinary and checks
// every step succeeds -- before the fix this failed 0/1 (createBinary()
// either errored or produced a binary the loader rejected); the underlying
// bug was in src/parser.cpp's parseFunctionCall()/getVariable(), which
// parsed the synthesized declaration into a throwaway scratch NodeToken
// (extra_parser) instead of `program`.
static void runAutoDeclareBinaryTest()
{
    const char *name = "auto-declared (bindFunction()/bindVariable()-only) calls survive createBinary()+createExecutableFromBinary()";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Parser p;
        p.clean();
        content.clear();
        header.clear();
        footer.clear();

        Script s;
        char *buf = strdup(ballEffectScript);
        s.addContent(buf);
        s.init();
        p.parse(&s, &__allTokens);

        bool ok = true;
        if (Error.error)
        {
            printf("       parse error=%d (%s)\n", Error.error, error_messages[Error.error]);
            ok = false;
        }
        else
        {
            Binary bin = createBinary(&footer, &header, &content, false);
            if (bin.error.error)
            {
                printf("       assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
                ok = false;
            }
            else
            {
                executable exe = createExecutableFromBinary(&bin);
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
                    printf("       loaded %d function(s)\n", exe.functions.size());
                }
                freeExecutable(&exe);
            }
        }

        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

// Regression test found via a purpose-written several-hundred-line script
// (test/host/fixtures/multi_effect_controller.sc, test/host/test_large_script.cpp)
// exercising `structArray[index].field` read from *outside* the struct's own
// methods -- a pattern none of the sc_examples corpus or this file's other
// struct tests happen to use (they only ever call struct-array methods, e.g.
// `Balls[i].updateBall()`). parser.cpp's TokenMember-on-a-pointer handling had
// an accidentally-live line (v1's ESPLiveScript.h:584 has the identical
// formula, but commented out/dead) that computed the per-element byte stride
// as `1000 * _total_size + v->sizes[i]` instead of leaving it as the struct's
// real total_size. For a 2-int-field struct (real size 8 bytes) this produces
// 1000*8+4=8004 -- not just wrong (multiplying an array index by the wrong
// stride computes a wrong address: silent memory corruption at runtime, not
// merely a compile error) but also large enough to overflow `movi`'s 12-bit
// (-2048..2047) immediate range, which is what actually surfaced it as an
// assembler error ("incorrect value 8004 should be between -2048 and 2047").
// Fixed by removing the errant line so this branch does nothing, exactly
// matching v1. Checks the *exact* stride value in the generated instruction
// stream (not just "does it assemble"), since a variant of this bug that
// stays under 2047 would otherwise assemble fine while still computing a
// wrong address.
static void runStructArrayFieldStrideTest()
{
    const char *name = "struct-array element field read from outside its methods (arr[i].field) uses the correct byte stride";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Parser p;
        p.clean();
        content.clear();
        header.clear();
        footer.clear();

        Script s;
        char *buf = strdup(
            "struct Foo{int a,b; Foo(){a=0;b=0;}}"
            "Foo arr[3];"
            "int readB(int i){return arr[i].b;}"
            "void main(){}");
        s.addContent(buf);
        s.init();
        p.parse(&s, &__allTokens);

        bool ok = true;
        if (Error.error)
        {
            printf("       parse error=%d (%s)\n", Error.error, error_messages[Error.error]);
            ok = false;
        }
        else
        {
            // The correct per-element stride for Foo (two int fields) is
            // 8 bytes -- look for "movi a<reg>,8" immediately followed by
            // a "mull" in the generated instruction stream (matching
            // visitnode.cpp's movi-then-mull index-scaling codegen at
            // ~line 678/2246); the pre-fix bug would instead emit
            // "movi a<reg>,8004" here (and fail to assemble at all, but
            // this check catches the underlying wrong-value bug directly,
            // not just its assembler-range side effect).
            // Matches the exact 3-instruction sequence visitnode.cpp emits
            // for this case (~line 678-680): "movi <reg>,<stride>", then
            // "mull ...", then "l32r <reg>,@_arr" -- so this can't
            // accidentally match some unrelated movi/mull pair elsewhere
            // in the script.
            bool foundCorrectStride = false;
            int wrongStrideSeen = -1;
            for (int i = 0; i + 2 < content.size(); i++)
            {
                char *line = content.getText(i);
                char *next = content.getText(i + 1);
                char *after = content.getText(i + 2);
                if (line == NULL || next == NULL || after == NULL)
                    continue;
                int reg = -1, imm = -1;
                if (strncmp(next, "mull", 4) == 0 && strstr(after, "@_arr") != NULL &&
                    sscanf(line, "movi a%d,%d", &reg, &imm) == 2)
                {
                    if (imm == 8)
                        foundCorrectStride = true;
                    else
                        wrongStrideSeen = imm;
                }
            }
            if (foundCorrectStride == false && wrongStrideSeen != -1)
            {
                printf("       found a wrong stride (movi ...,%d) instead of the correct 8 in the generated instructions\n",
                       wrongStrideSeen);
                ok = false;
            }
            else if (!foundCorrectStride)
            {
                printf("       did not find the expected 'movi a<reg>,8' / 'mull' / 'l32r ...,@_arr' sequence in the generated instructions\n");
                ok = false;
            }

            Binary bin = createBinary(&footer, &header, &content, false);
            if (bin.error.error)
            {
                printf("       assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
                ok = false;
            }
        }

        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

// Regression test for a bug found while investigating a *different* fix
// (parser.cpp's method-call branch, ~line 2858, used to build a struct
// method call's self-pointer argument from the bare variable
// *declaration*, discarding any `[index]` expression already parsed
// onto current_node -- fixed by restoring a copy-current_node's-children
// loop that was sitting there commented out). That fix makes
// `arr[i].method()`'s self-pointer correctly compute `i * sizeof(elem)`
// via the same `movi <reg>,<stride>` / `mull` / `l32r <reg>,@_arr`
// sequence runStructArrayFieldStrideTest() above already verifies for
// plain field reads (visitnode.cpp's _visitglobalVariableNode/
// _visitlocalVariableNode, ~line 678/2246) -- but revealed a second,
// independent bug in the very next instruction, the one that actually
// combines that computed offset into the base address: it emits
//   addi a<X>,a<base>,<mull's destination register NUMBER>
// (an *immediate* add of e.g. 15, since asmInstructionsName[addi]'s
// template is "addi a%d,a%d,%d" -- a literal, not a register) instead of
//   add a<X>,a<base>,a<mull's destination register>
// (asmInstructionsName[add]'s template is "add a%d,a%d,a%d" -- all three
// are registers) which would add that register's actual runtime value.
// Confirmed live in BouncingBalls.ino's own `Balls[i].updateBall()`:
// every call resolves to `@_Balls + 15` regardless of `i` instead of
// `@_Balls + i*sizeof(ball)`. This test checks the *combining*
// instruction specifically (register_numl.get()'s value at that call
// site happens to be a real register number, so an "addi ...,<N>" bug
// and a legitimate "addi ...,<N>" for some unrelated small constant N
// look identical as bare text -- the only reliable way to tell them
// apart is cross-checking N against the specific register mull just
// wrote the real offset into, which is what this does), not just that
// the preceding stride computation is correct.
static void runStructArrayMethodSelfPointerTest()
{
    const char *name = "struct-array element method call (arr[i].method()) combines the computed index offset, not the register number as a literal";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Parser p;
        p.clean();
        content.clear();
        header.clear();
        footer.clear();

        Script s;
        char *buf = strdup(
            "struct Foo{int a,b; Foo(){a=0;b=0;} void bump(){a=a+1;}}"
            "Foo arr[3];"
            "void callBump(int i){arr[i].bump();}"
            "void main(){}");
        s.addContent(buf);
        s.init();
        p.parse(&s, &__allTokens);

        bool ok = true;
        if (Error.error)
        {
            printf("       parse error=%d (%s)\n", Error.error, error_messages[Error.error]);
            ok = false;
        }
        else
        {
            int mullDestReg = -1;
            for (int i = 0; i + 2 < content.size(); i++)
            {
                char *line = content.getText(i);
                char *next = content.getText(i + 1);
                char *after = content.getText(i + 2);
                if (line == NULL || next == NULL || after == NULL)
                    continue;
                int reg = -1, imm = -1;
                int mullDest = -1, mullA = -1, mullB = -1;
                if (sscanf(line, "movi a%d,%d", &reg, &imm) == 2 && imm == 8 &&
                    sscanf(next, "mull a%d,a%d,a%d", &mullDest, &mullA, &mullB) == 3 &&
                    strstr(after, "@_arr") != NULL)
                {
                    mullDestReg = mullDest;
                    break;
                }
            }
            if (mullDestReg == -1)
            {
                printf("       did not find the expected 'movi a<reg>,8' / 'mull' / 'l32r ...,@_arr' sequence in the generated instructions\n");
                ok = false;
            }
            else
            {
                bool foundCorrectCombine = false;
                bool foundBuggyCombine = false;
                for (int i = 0; i < content.size(); i++)
                {
                    char *line = content.getText(i);
                    if (line == NULL)
                        continue;
                    int a = -1, b = -1, c = -1;
                    if (sscanf(line, "add a%d,a%d,a%d", &a, &b, &c) == 3 && c == mullDestReg)
                    {
                        foundCorrectCombine = true;
                        break;
                    }
                    if (sscanf(line, "addi a%d,a%d,%d", &a, &b, &c) == 3 && c == mullDestReg)
                    {
                        foundBuggyCombine = true;
                        break;
                    }
                }
                if (foundBuggyCombine)
                {
                    printf("       self-pointer combined via 'addi ...,%d' (the mull destination register's "
                           "NUMBER used as a literal immediate) instead of 'add ...,a%d' (that register's "
                           "actual runtime value)\n",
                           mullDestReg, mullDestReg);
                    ok = false;
                }
                else if (!foundCorrectCombine)
                {
                    printf("       did not find the expected 'add a<x>,a<base>,a%d' combining instruction\n", mullDestReg);
                    ok = false;
                }
            }

            Binary bin = createBinary(&footer, &header, &content, false);
            if (bin.error.error)
            {
                printf("       assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
                ok = false;
            }
        }

        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

// Exercises createExecutableFromBinary()/freeExecutable() (src/asm_execute.cpp)
// on a script with an external call, checking the load+relocate step
// completes and reports a start function -- without actually calling into
// the generated Xtensa code, which would crash immediately on a host CPU.
// runExecutable() itself is verified separately: see the QEMU-based check
// documented in test/qemu/README.md, which runs this exact relocation
// scheme against a real Xtensa CPU emulation and confirms the external
// call's result is correct end to end.
static void runLoadAndRelocateTest()
{
    const char *name = "load and relocate an external-call binary (execution itself is QEMU-verified, see test/qemu/)";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        // bindFunction() registers the host pointer findLink()/the loader
        // resolves at load time; the explicit `external` declaration in
        // the script itself is still needed for the assembler's header
        // reservation (see the known bindFunction-auto-declare gap noted
        // on the "generates executable binary: external function call"
        // test above).
        bindFunction((char *)"void", (char *)"setResult", (char *)"int", NULL);

        Parser p;
        p.clean();
        content.clear();
        header.clear();
        footer.clear();

        Script s;
        char *buf = strdup("external void setResult(int a); void main(){int a; a=7; setResult(a*6);}");
        s.addContent(buf);
        s.init();
        p.parse(&s, &__allTokens);

        if (Error.error)
        {
            printf("       parse error=%d\n", Error.error);
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
            printf("       loaded %d function(s), entry offset %u\n", exe.functions.size(), exe.functions.get(0).address);
        }

        freeExecutable(&exe);
        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

// A function declared with parameters actually gets *two* header records
// (see asm_parser.cpp's createBinaryHeader and asm_execute.h's
// runExecutableWithArgs doc comment): a plain one at the real function's
// address reporting args_num=0 (an upstream quirk), and a wrapper one
// with the correct args_num and a valid variableaddress, generated only
// because there's a parameter to marshal. This checks
// createExecutableFromBinary() decodes both records and that
// runExecutableWithArgs(ex, args, 1) actually finds the wrapper (args_num
// == 1) rather than defaulting to whichever came first. Doesn't call
// into the generated code -- MiniXtensa can't interpret this script (it
// involves a call8, which it explicitly refuses -- see mini_xtensa.h),
// and calling on a host CPU would crash immediately regardless. Real
// execution is QEMU-verified: test/qemu/gen_fibonacci_arg.cpp runs this
// exact script with n=10 and confirms fib(10)==55 on a real Xtensa CPU
// emulation.
static void runArgumentPassingLoadTest()
{
    const char *name = "load a with-argument function and select the correct wrapper record (execution itself is QEMU-verified, see test/qemu/)";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Parser p;
        p.clean();
        content.clear();
        header.clear();
        footer.clear();

        Script s;
        char *buf = strdup(
            "void main(int n){int a; a=0; int b; b=1; int i; "
            "for (i=0;i<n;i++) { int t; t=a+b; a=b; b=t; } int fib; fib=a;}");
        s.addContent(buf);
        s.init();
        p.parse(&s, &__allTokens);

        if (Error.error)
        {
            printf("       parse error=%d\n", Error.error);
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

        executable exe = createExecutableFromBinary(&bin);

        bool ok = true;
        if (exe.error.error)
        {
            printf("       loader error: %s\n", exe.error.error_message ? exe.error.error_message : "?");
            ok = false;
        }
        else if (exe.functions.size() != 2)
        {
            printf("       expected 2 function records (plain + wrapper), got %d\n", exe.functions.size());
            ok = false;
        }
        else
        {
            globalcall *wrapper = NULL;
            int zeroArgCount = 0;
            for (int i = 0; i < exe.functions.size(); i++)
            {
                globalcall *gc = exe.functions.getptr(i);
                if (gc->args_num == 1)
                    wrapper = gc;
                else
                    zeroArgCount++;
            }
            if (wrapper == NULL || zeroArgCount != 1)
            {
                printf("       expected exactly one args_num==1 record and one args_num==0 record\n");
                ok = false;
            }
            else if (wrapper->variableaddress >= exe.data_size)
            {
                printf("       wrapper's variableaddress (%u) is out of range for the data buffer (%u bytes)\n",
                       wrapper->variableaddress, exe.data_size);
                ok = false;
            }
            else
            {
                printf("       found wrapper: address=%u variableaddress=%u (data_size=%u)\n",
                       wrapper->address, wrapper->variableaddress, exe.data_size);
            }
        }

        freeExecutable(&exe);
        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

// callFunction() (src/asm_execute.cpp) calls a *named*, non-main function
// directly (bypassing the argument-marshaling wrapper -- see its doc
// comment in asm_execute.h for why) and reads its return value back from
// a10. Checks the loader produces a plain (non-wrapper) record named
// "fib" distinguishable from its wrapper -- the actual register-passing
// call is QEMU-verified: test/qemu/gen_named_function.cpp calls this
// exact script's fib(10) this way and confirms it returns 55 on a real
// Xtensa CPU emulation.
static void runNamedFunctionLoadTest()
{
    const char *name = "load a script with a named, non-main function and find its plain (callable) record (execution itself is QEMU-verified, see test/qemu/)";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Parser p;
        p.clean();
        content.clear();
        header.clear();
        footer.clear();

        Script s;
        char *buf = strdup(
            "int fib(int n){int a; a=0; int b; b=1; int i; "
            "for (i=0;i<n;i++) { int t; t=a+b; a=b; b=t; } return a;}"
            "void main(){int x; x=fib(10);}");
        s.addContent(buf);
        s.init();
        p.parse(&s, &__allTokens);

        if (Error.error)
        {
            printf("       parse error=%d\n", Error.error);
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

        executable exe = createExecutableFromBinary(&bin);

        bool ok = true;
        if (exe.error.error)
        {
            printf("       loader error: %s\n", exe.error.error_message ? exe.error.error_message : "?");
            ok = false;
        }
        else
        {
            globalcall *fibPlain = NULL;
            globalcall *fibWrapper = NULL;
            for (int i = 0; i < exe.functions.size(); i++)
            {
                globalcall *gc = exe.functions.getptr(i);
                const char *label = gc->name;
                bool isFib = label[0] == '@' && label[1] == '_' &&
                             (strncmp(label + 2, "fib", 3) == 0 || strncmp(label + 3, "fib", 3) == 0);
                if (!isFib)
                    continue;
                bool isWrapper = gc->variables != NULL && gc->variables[0] >= '0' && gc->variables[0] <= '9';
                if (isWrapper)
                    fibWrapper = gc;
                else
                    fibPlain = gc;
            }
            if (fibPlain == NULL || fibWrapper == NULL)
            {
                printf("       expected both a plain and a wrapper record named fib, found plain=%p wrapper=%p\n",
                       (void *)fibPlain, (void *)fibWrapper);
                ok = false;
            }
            else if (fibPlain->address == fibWrapper->address)
            {
                printf("       plain and wrapper records should be at different addresses\n");
                ok = false;
            }
            else
            {
                printf("       plain fib at offset %u, wrapper at offset %u\n", fibPlain->address, fibWrapper->address);
            }
        }

        freeExecutable(&exe);
        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

// asm_serialize.h's serializeBinary()/deserializeBinary() let a script be
// compiled once, flattened into a self-contained byte blob, saved
// somewhere (a file, flash, EEPROM), and loaded back later -- even by a
// process that has never seen the script source. This proves both
// halves of that story: (1) the round trip is byte-exact (writes to a
// real temp file, not just an in-memory buffer, so it also exercises
// actual file I/O), and (2) a *freshly bound* copy of the external names
// the script declares -- deliberately different host pointers than the
// ones in scope when it was compiled, via binding.h's replaceExternal(),
// simulating a second, independent sketch's own bindVariable()/
// bindFunction() calls -- still loads and produces the same function
// records. Real execution (proving the second "sketch"'s callback
// actually fires and its variable is actually read) is QEMU-verified
// instead: see test/qemu/gen_saveload.cpp, whose script uses simple
// arithmetic rather than the branching KeyboardCallback.ino-style logic
// this test was originally written with -- at the time, that combination
// reliably triggered what turned out to be nodetoken.cpp's old value-
// array children storage letting raw NodeToken* pointers dangle across a
// realloc (see nodetoken.h's comment on `children` for the fix, now
// applied). Left as simple arithmetic since re-verifying under QEMU
// isn't free and the simpler script already covers what this test is
// actually about (serialization + cross-process relocation).
static void runSaveLoadBinaryTest()
{
    const char *name = "save a compiled script to a file and load+relocate it under a different binding (execution itself is QEMU-verified, see test/qemu/)";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        int compileTimeKeyChar = 0;
        int compileTimeReportCalls = 0;
        bindVariable((char *)"int", (char *)"key_char", NULL, (void *)&compileTimeKeyChar);
        bindFunction((char *)"void", (char *)"report", (char *)"int", (void *)&compileTimeReportCalls);

        Parser p;
        p.clean();
        content.clear();
        header.clear();
        footer.clear();

        Script s;
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
            printf("       parse error=%d\n", Error.error);
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

        uint32_t serializedSize = 0;
        uint8_t *serialized = serializeBinary(&bin, &serializedSize);
        bool ok = true;
        if (serialized == NULL)
        {
            printf("       serializeBinary failed\n");
            ok = false;
        }

        // Round-trip through an actual file, not just an in-memory buffer --
        // the realistic "save to storage, load elsewhere" path.
        char path[] = "/tmp/asmparser2_saveload_test_XXXXXX";
        if (ok)
        {
            int fd = mkstemp(path);
            if (fd < 0 || write(fd, serialized, serializedSize) != (ssize_t)serializedSize)
            {
                printf("       failed to write serialized binary to %s\n", path);
                ok = false;
            }
            if (fd >= 0)
                close(fd);
        }

        uint8_t *reloadedBytes = NULL;
        if (ok)
        {
            FILE *f = fopen(path, "rb");
            if (f == NULL)
            {
                printf("       failed to reopen %s\n", path);
                ok = false;
            }
            else
            {
                reloadedBytes = (uint8_t *)malloc(serializedSize);
                size_t got = fread(reloadedBytes, 1, serializedSize, f);
                fclose(f);
                if (got != serializedSize)
                {
                    printf("       short read from %s (%zu of %u bytes)\n", path, got, serializedSize);
                    ok = false;
                }
            }
            unlink(path);
        }

        if (ok && memcmp(reloadedBytes, serialized, serializedSize) != 0)
        {
            printf("       save/load round trip through a real file is not byte-exact\n");
            ok = false;
        }

        if (ok)
        {
            Binary reloadedBin = deserializeBinary(reloadedBytes, serializedSize);
            if (reloadedBin.error.error)
            {
                printf("       deserializeBinary error: %s\n",
                       reloadedBin.error.error_message ? reloadedBin.error.error_message : "?");
                ok = false;
            }
            else
            {
                // Simulate a second, independent sketch: same external
                // *names* already registered above, but point them at
                // different host storage -- proving relocation resolves
                // by name at load time, not anything fixed at compile
                // time.
                int loadTimeKeyChar = 0;
                int loadTimeReportCalls = 0;
                replaceExternal((char *)"key_char", (void *)&loadTimeKeyChar);
                replaceExternal((char *)"report", (void *)&loadTimeReportCalls);

                executable exe = createExecutableFromBinary(&reloadedBin);
                if (exe.error.error)
                {
                    printf("       loader error after reload: %s\n",
                           exe.error.error_message ? exe.error.error_message : "?");
                    ok = false;
                }
                else
                {
                    globalcall *keyboardPlain = NULL;
                    for (int i = 0; i < exe.functions.size(); i++)
                    {
                        globalcall *gc = exe.functions.getptr(i);
                        bool isWrapper = gc->variables != NULL && gc->variables[0] >= '0' && gc->variables[0] <= '9';
                        const char *p2 = gc->name + 2;
                        if (p2[0] == '_')
                            p2++;
                        if (!isWrapper && strncmp(p2, "keyboard", 8) == 0 && (p2[8] == '(' || p2[8] == 0))
                            keyboardPlain = gc;
                    }
                    if (keyboardPlain == NULL)
                    {
                        printf("       reloaded binary is missing keyboard's plain (callable) record\n");
                        ok = false;
                    }
                    else
                    {
                        printf("       reloaded and relocated: %d function records, keyboard at offset %u\n",
                               exe.functions.size(), keyboardPlain->address);
                    }
                    freeExecutable(&exe);
                }
            }
            freeBinary(&reloadedBin);
        }

        free(serialized);
        free(reloadedBytes);
        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

// arguments.h's Arguments/_arguments classes (ported from upstream
// ESPLiveScript's ESPLivescriptRuntime.h) let a caller build a typed,
// ordered list of int/float values instead of a raw int32_t[] -- see
// asm_execute.h's callFunction()/runExecutableWithArgs() Arguments
// overloads. Verifies the class itself (add/size/clear/types) and that
// the callFunction() overload finds the target function and doesn't
// crash; the int argument's marshaled *value* arriving correctly in
// a register is QEMU-verified elsewhere (test/qemu/'s named_function
// case uses the same direct-register-call path this overload delegates
// to). The float case specifically is NOT QEMU-verified: an attempt hit
// what looks like an unrelated QEMU-fork limitation (any float
// arithmetic/comparison instruction hangs the emulator, independent of
// argument passing) documented in test/qemu/README.md's "Not verified"
// section -- only host-structurally-verified here, i.e. this test
// confirms marshalArguments() reinterprets the float's bits and the call
// doesn't crash, not that the script reads the value back correctly.
static void runArgumentsClassTest()
{
    const char *name = "Arguments class builds a typed argument list and the callFunction() overload accepts it";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Arguments args;
        args.add(10);
        args.add(2.5f);
        bool ok = (args.size() == 2);
        if (ok)
        {
            _arguments a0 = args._args.get(0);
            _arguments a1 = args._args.get(1);
            ok = (a0.vartype == __int__ && a0.intval == 10 &&
                  a1.vartype == __float__ && a1.floatval == 2.5f);
            if (!ok)
                printf("       Arguments/_arguments basic add/size/type check failed\n");
        }
        else
        {
            printf("       Arguments::size() wrong after two add() calls\n");
        }

        args.clear();
        if (args.size() != 0)
        {
            printf("       Arguments::clear() didn't empty the list\n");
            ok = false;
        }

        if (ok)
        {
            Parser p;
            p.clean();
            content.clear();
            header.clear();
            footer.clear();

            Script s;
            char *buf = strdup("int add(int a, float b){return a;} void main(){}");
            s.addContent(buf);
            s.init();
            p.parse(&s, &__allTokens);
            if (Error.error)
            {
                printf("       parse error=%d\n", Error.error);
                ok = false;
            }
            else
            {
                Binary bin = createBinary(&footer, &header, &content, false);
                if (bin.error.error)
                {
                    printf("       assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
                    ok = false;
                }
                else
                {
                    executable exe = createExecutableFromBinary(&bin);
                    if (exe.error.error)
                    {
                        printf("       loader error: %s\n", exe.error.error_message ? exe.error.error_message : "?");
                        ok = false;
                    }
                    else
                    {
                        Arguments callArgs;
                        callArgs.add(7);
                        callArgs.add(1.5f);
                        int32_t result = 0;
                        // Execution itself is a no-op off-target (see
                        // asm_execute.cpp's callXtensaDirect) -- this
                        // just checks the overload finds "add" and
                        // doesn't crash marshaling a mixed int/float
                        // argument list.
                        bool found = callFunction(&exe, "add", &callArgs, &result);
                        if (!found)
                        {
                            printf("       callFunction(Arguments overload) didn't find \"add\"\n");
                            ok = false;
                        }
                        freeExecutable(&exe);
                    }
                }
            }
        }

        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

// ScriptExecutable/parseScript() (script_executable.h) -- the v1-style
// convenience layer that collapses parse -> createBinary ->
// createExecutableFromBinary into one call, matching upstream's
// Parser::parseScript()/Executable::isExeExists()/Executable::execute().
// Found and fixed two real bugs building this (both reproduced and
// confirmed fixed under AddressSanitizer, not just by inspection):
// (1) `executable` (asm_types.h) holds vect<globalcall>/vect<jsonVariable>
// members; vect<T> has a destructor but no matching copy/move
// constructor or assignment operator (a pre-existing Rule-of-Three gap),
// so an early version of ScriptExecutable's move constructor that did
// `exe = other.exe` triggered the compiler-generated shallow copy,
// leaving two objects sharing one vect<T> backing-array pointer --
// whichever got destroyed first freed it out from under the other
// (heap-use-after-free in isWrapperRecord()). Fixed by deleting copy
// *and* move entirely and restructuring construction so `exe` is always
// direct-initialized from a prvalue call expression (guaranteed elided
// since C++17, so copy/move is never actually invoked) -- see the class
// comment in script_executable.h for the full reasoning. (2) An early
// version of parseScript() freed its strdup'd copy of the script text
// right after Parser::parse() returned, unconditionally -- but
// display_error() (called on a parse error) reads the offending line
// back out of that same buffer (Token::lineref points into it, never
// copied out the way a successfully-parsed token's text is), so freeing
// it before checking Error.error was a heap-use-after-free on the error
// path specifically. Fixed by moving the free() after display_error().
static void runScriptExecutableTest()
{
    const char *name = "ScriptExecutable/parseScript(): success, error, and two-independent-instances paths";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        bool ok = true;

        // Success path: compiles, loads, execute() finds both main() and
        // a named function via callFunction() without crashing.
        {
            ScriptExecutable exec = parseScript(
                "int fact(int h){if(h==1){return 1;}return h*fact(h-1);}"
                "void main(){}");
            if (!exec.isExeExists())
            {
                printf("       success-path script unexpectedly failed to compile/load\n");
                ok = false;
            }
            else
            {
                if (!exec.execute("main"))
                {
                    printf("       execute(\"main\") didn't find main()\n");
                    ok = false;
                }
                Arguments args;
                args.add(6);
                int32_t result = 0;
                if (!exec.execute("fact", &args, &result))
                {
                    printf("       execute(\"fact\", Arguments) didn't find fact()\n");
                    ok = false;
                }
            }
        }

        // Error path: a script that fails to parse must not crash, and
        // isExeExists() must report false.
        {
            ScriptExecutable bad = parseScript("this is not valid syntax @#$");
            if (bad.isExeExists())
            {
                printf("       isExeExists() true for a script that should have failed to parse\n");
                ok = false;
            }
        }

        // Regression test: parseScript() prepends the same boilerplate
        // v1's real compile entrypoints always do (#define true/false,
        // uint32_t _handle_/_execaddr_) -- an earlier version of this
        // function didn't, so a script using a bare `true`/`false`
        // literal or referencing _execaddr_ (both routine -- the latter
        // is what pinInterrupt(_execaddr_, "fn", pin) needs, see
        // KeyboardCallback-style scripts) failed to compile with a
        // confusing "impossible to find variable declaration" for
        // something the script never declared itself. No external
        // binding needed here -- just referencing _execaddr_/_handle_ as
        // plain variables is enough to prove they're declared.
        {
            ScriptExecutable exec = parseScript(
                "void main(){"
                "   uint32_t x = _execaddr_;"
                "   uint32_t y = _handle_;"
                "   bool b = true;"
                "   bool c = false;"
                "}");
            if (!exec.isExeExists())
            {
                printf("       script using true/false/_execaddr_/_handle_ (no explicit declaration) failed to compile\n");
                ok = false;
            }
        }

        // Two independent instances (each its own parseScript() call, not
        // a copy/move of one into the other -- see the class comment on
        // why that's deliberately not supported) coexist and are each
        // destroyed cleanly.
        {
            ScriptExecutable a = parseScript("void main(){}");
            ScriptExecutable b = parseScript("int fib(int n){return n;} void main(){}");
            if (!a.isExeExists() || !b.isExeExists())
            {
                printf("       two independent ScriptExecutables in the same scope didn't both load\n");
                ok = false;
            }
        }

        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

// `json "path" as <type> name;` (parser.cpp's jsonBindingNode handling,
// ported from upstream ESPLiveScript's ESPLiveScript.h/execute_asm.h)
// lets a script variable be populated from a JSON document at execution
// time. Verifies the structural half -- parsing, assembling, loading,
// and the relocation header round trip populating executable.jsonVars
// with the right path/address/type -- which needs no ArduinoJson
// dependency at all (see asm_types.h's jsonVariable comment). The other
// half -- updateJsonParameters() actually parsing a JSON document and
// writing values into the data segment -- needs __JSON_OPTION__ and
// ArduinoJson, and is verified separately: see test/host/test_json.cpp.
static void runJsonBindingStructureTest()
{
    const char *name = "json \"path\" as <type> name; parses, assembles, and populates executable.jsonVars with the right path/address/type";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        Parser p;
        p.clean();
        content.clear();
        header.clear();
        footer.clear();

        Script s;
        char *buf = strdup(
            "json \"wifi.ssid\" as int myInt;\n"
            "json \"wifi.count\" as float myFloat;\n"
            "void main(){myInt=myInt+1;}\n");
        s.addContent(buf);
        s.init();
        p.parse(&s, &__allTokens);

        bool ok = true;
        if (Error.error)
        {
            printf("       parse error=%d\n", Error.error);
            ok = false;
        }
        else
        {
            Binary bin = createBinary(&footer, &header, &content, false);
            if (bin.error.error)
            {
                printf("       assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
                ok = false;
            }
            else
            {
                executable exe = createExecutableFromBinary(&bin);
                if (exe.error.error)
                {
                    printf("       loader error: %s\n", exe.error.error_message ? exe.error.error_message : "?");
                    ok = false;
                }
                else if (exe.jsonVars.size() != 2)
                {
                    printf("       expected 2 jsonVars, got %d\n", exe.jsonVars.size());
                    ok = false;
                }
                else
                {
                    jsonVariable jv0 = exe.jsonVars.get(0);
                    jsonVariable jv1 = exe.jsonVars.get(1);
                    if (strcmp(jv0.json, "wifi.ssid") != 0 || jv0.type != __int__)
                    {
                        printf("       jsonVars[0] wrong: path=\"%s\" type=%d\n", jv0.json, jv0.type);
                        ok = false;
                    }
                    if (strcmp(jv1.json, "wifi.count") != 0 || jv1.type != __float__)
                    {
                        printf("       jsonVars[1] wrong: path=\"%s\" type=%d\n", jv1.json, jv1.type);
                        ok = false;
                    }
                    if (jv0.address == jv1.address)
                    {
                        printf("       jsonVars[0] and [1] shouldn't share the same address\n");
                        ok = false;
                    }

                    // The default build has no __JSON_OPTION__ (see
                    // json_binding.h) -- updateJsonParameters() should
                    // still be safely callable, just reporting an error
                    // for a non-empty json string instead of silently
                    // doing nothing.
                    asm_error_message_struct res = updateJsonParameters(&exe, "{}");
                    if (res.error == 0)
                    {
                        printf("       expected updateJsonParameters() to report an error without __JSON_OPTION__\n");
                        ok = false;
                    }
                    asm_error_message_struct res2 = updateJsonParameters(&exe, NULL);
                    if (res2.error != 0)
                    {
                        printf("       updateJsonParameters(NULL) should always be a no-op success\n");
                        ok = false;
                    }

                    freeExecutable(&exe);
                }
            }
        }

        fflush(stdout);
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
        failed++;
        printf("[CRASH] %s (signal %d)\n", name, WTERMSIG(status));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        passed++;
        printf("[PASS] %s\n", name);
    }
    else
    {
        failed++;
        printf("[FAIL] %s\n", name);
    }
}

int main()
{
    registerBallEffectBindings();
    runAutoDeclareBinaryTest();
    runStructArrayFieldStrideTest();
    runStructArrayMethodSelfPointerTest();
    runOptimizeUnitTest();
    runGenerateBinaryTest(
        "generates executable binary: empty function",
        "void main(){}");
    runGenerateBinaryTest(
        "generates executable binary: arithmetic + control flow",
        "void main(){int a; int b; int sum; a=1; b=0; "
        "for (a = 0; a < 5; a++) { if (a < 3) { b = b + a; } else { b = b - a; } } "
        "sum = b;}");
    // Regression test for a real bug found via QEMU execution: addInstr()
    // was silently dropping opCodeType::data entries (the ".bytes 4" jump-
    // table reservations that precede every external reference and every
    // declared function's own stack-size slot), so the jump table never
    // reserved instruction-stream space and every subsequent l32r/callExt
    // pointed at the wrong address. This script's external call exercises
    // that path directly -- entryOffset must be at or past 4 reserved
    // slots (handle/execaddr/sync/setResult) instead of 3.
    runGenerateBinaryTest(
        "generates executable binary: external function call",
        "external void setResult(int a); void main(){int a; a=7; setResult(a*6);}");
    // Regression test found via the sc_examples corpus (ballscardputer.sc):
    // visitnode.cpp's comparisonNode codegen emits "blt%s"/"bge%s" with a
    // "u" suffix (asmInstructionsName[]'s blt/bge entries) whenever either
    // compared operand is uint32_t, but asm_parser.cpp's opcode dispatch
    // only recognized the signed "blt"/"bge" strings -- any script
    // comparing a uint32_t (`while (h < 700)` with `uint32_t h`, as
    // ballscardputer.sc's main loop does) failed to assemble with "Opcode
    // bltu not found", even though bin_bltu()/bin_bgeu() (asm_encoders.h)
    // already correctly encode the real BLTU/BGEU Xtensa opcodes -- they
    // just weren't wired into the dispatch. Fixed by adding "bltu"/"bgeu"
    // dispatch entries alongside "blt"/"bge".
    runGenerateBinaryTest(
        "generates executable binary: unsigned (uint32_t) comparison uses bltu",
        "void main(){uint32_t h; h=0; while (h < 700) { h = h + 1; }}");
    // Regression test found via the sc_examples corpus (squaresani.sc,
    // which has three __ASM__ functions -- setTime()/millis()/
    // elapseMillis() -- all starting with "entry a1,32" and ending with
    // "retw.n"): visitnode.cpp's _visitstringNode() (which strips the
    // surrounding quotes off each __ASM__ instruction-line string
    // literal) used to do it with an in-place memmove() on
    // nd->getText()'s buffer. Text::addText() (stackfunctions.cpp)
    // deduplicates identical string literals to the same underlying
    // buffer, so a second __ASM__ function repeating a line verbatim from
    // an earlier one shared that buffer -- and got it stripped a second
    // time, eating one more character off each end ("entry a1,32" ->
    // "ntry a1,3" -> "try a1," the 3rd time), which the assembler then
    // rejected as an unrecognized opcode. Fixed by building a fresh
    // stripped copy instead of mutating the (possibly shared) source
    // buffer in place.
    runGenerateBinaryTest(
        "generates executable binary: two __ASM__ functions sharing an identical instruction line",
        "uint32_t __baseTime[1];"
        "__ASM__ void setTime(){\"entry a1,32\" \"retw.n\"}"
        "__ASM__ void otherTime(){\"entry a1,32\" \"retw.n\"}"
        "void main(){}");
    // Regression test found via the sc_examples corpus (tetris.sc, which
    // calls `!checkCollision(...)` in three different functions):
    // visitnode.cpp's TokenNot codegen (the `!expr` operator) labels its
    // branch target "label_not_<for_if_num2>" but, unlike every other
    // user of that shared counter (the __test_safe_%d array-bounds check
    // and loop_label_%d), never incremented it afterward -- so every `!`
    // anywhere in a script reused the exact same label name. Fine for a
    // script with a single `!`; "label label_not_999 is already declared"
    // (an assembler error, since two different functions both defined
    // "label_not_999") for any script using it more than once. Fixed by
    // adding the same `for_if_num2++` the other three call sites already
    // have.
    runGenerateBinaryTest(
        "generates executable binary: '!' operator used in two different functions gets distinct labels",
        "bool f(int a){ if (!a) { return 1; } return 0; }"
        "bool g(int a){ if (!a) { return 1; } return 0; }"
        "void main(){ int x; x = f(1); int y; y = g(0); }");
    runLoadAndRelocateTest();
    // Not included here: ballEffectScript (structs + external calls) reliably
    // hits a pre-existing bug in Parser::parse() itself under ASan -- a raw
    // NodeToken* cached across a p_realloc() that can move the underlying
    // array (see parseDefFunction/addChildFront), unrelated to the
    // assembler. It's the same class of issue as the change_type bug found
    // earlier this session, just triggered more reliably by this script's
    // nesting. createBinary() is never reached when it happens.
    runEquivalenceTest(
        "optimized/unoptimized equivalence: nested for/if with +=/-=",
        "void main(){int a; int b; int sum; a=1; b=0; "
        "for (a = 0; a < 5; a++) { if (a < 3) { b = b + a; } else { b = b - a; } } "
        "sum = b;}");
    // Branch-immediate (blti/beqi/bgei/bnei) codegen correctness, for
    // every comparison operator, in the "small" (target _end) comparator
    // block -- see visitnode.cpp's isBranchImmediate()/valBranchImmediate()
    // and _visitcomparatorNode(). Each single-statement if body keeps
    // nd->_total_size well under the 127 threshold that would instead pick
    // the "large" (target _if) block (already covered, for '<' only, by
    // the nested for/if test above and gen_arithmetic.cpp's QEMU case,
    // both real-hardware-verified).
    //
    // r1 collects one distinct bit per operator's TRUE case, plus the
    // 256-vs-128 boundary case (regression coverage for a real bug found
    // in upstream v1's valBranchImmediate(): it mapped both 128 and 256 to
    // b4const index 14, when 256 should be index 15 -- v2's port fixes
    // this, see visitnode.cpp) and one non-eligible-immediate case (11 is
    // not in Xtensa's b4const table, so `a<11` must fall back to the
    // ordinary register-register `bge`, exercising that this new
    // eligibility check doesn't regress the pre-existing path). Expected
    // 0xFF (all eight bits set) only if every TRUE case is actually taken.
    //
    // r2 mirrors the same eight cases with each comparison's FALSE
    // outcome (or, for the boundary/fallback pair, the value that must
    // NOT satisfy them) -- expected 0 only if none of them wrongly
    // branch. Together these catch both "branch never taken" and "branch
    // always taken" classes of bug per operator, e.g. an inverted compop
    // or a wrong leftl/numl register swap for that specific case.
    runCorrectnessTest(
        "branch-immediate (blti/beqi/bgei/bnei): true cases all taken (r1 == 0xFF)",
        "void main(){int a; int r1; int r2; r1=0; r2=0; "
        "a=3; if(a<5){r1=r1+1;} "
        "a=5; if(a==5){r1=r1+2;} "
        "a=6; if(a!=5){r1=r1+4;} "
        "a=5; if(a>=5){r1=r1+8;} "
        "a=6; if(a>5){r1=r1+16;} "
        "a=5; if(a<=5){r1=r1+32;} "
        "a=256; if(a==256){r1=r1+64;} "
        "a=9; if(a<11){r1=r1+128;} "
        "a=7; if(a<5){r2=r2+1;} "
        "a=6; if(a==5){r2=r2+2;} "
        "a=5; if(a!=5){r2=r2+4;} "
        "a=4; if(a>=5){r2=r2+8;} "
        "a=5; if(a>5){r2=r2+16;} "
        "a=6; if(a<=5){r2=r2+32;} "
        "a=128; if(a==256){r2=r2+64;} "
        "a=12; if(a<11){r2=r2+128;} "
        "}",
        60, 255);
    runCorrectnessTest(
        "branch-immediate (blti/beqi/bgei/bnei): false cases all skipped (r2 == 0)",
        "void main(){int a; int r1; int r2; r1=0; r2=0; "
        "a=3; if(a<5){r1=r1+1;} "
        "a=5; if(a==5){r1=r1+2;} "
        "a=6; if(a!=5){r1=r1+4;} "
        "a=5; if(a>=5){r1=r1+8;} "
        "a=6; if(a>5){r1=r1+16;} "
        "a=5; if(a<=5){r1=r1+32;} "
        "a=256; if(a==256){r1=r1+64;} "
        "a=9; if(a<11){r1=r1+128;} "
        "a=7; if(a<5){r2=r2+1;} "
        "a=6; if(a==5){r2=r2+2;} "
        "a=5; if(a!=5){r2=r2+4;} "
        "a=4; if(a>=5){r2=r2+8;} "
        "a=5; if(a>5){r2=r2+16;} "
        "a=6; if(a<=5){r2=r2+32;} "
        "a=128; if(a==256){r2=r2+64;} "
        "a=12; if(a<11){r2=r2+128;} "
        "}",
        64, 0);
    runEquivalenceTest(
        "optimized/unoptimized equivalence: repeated-read arithmetic",
        "void main(){int a; int b; a = 7; b = a + a + a; a = b - a; }");

    // Iterative Fibonacci (fib(0)=0, fib(1)=1 convention -> fib(10)=55).
    // `fib` itself never gets its own stack slot -- "fib=a;" is the last
    // statement and its result is never read afterward, so the compiler
    // just leaves the answer sitting in `a`'s slot (stack offset 60,
    // found by dumping the generated assembly -- see content.display()
    // output for "s32i a15,a1,60"). Verified by hand and cross-checked
    // against MiniXtensa (this test) and real QEMU execution
    // (test/qemu/gen_fibonacci.cpp) before being added here.
    static const char *fibonacciScript =
        "void main(){int n; n=10; int a; a=0; int b; b=1; int i; "
        "for (i=0;i<n;i++) { int t; t=a+b; a=b; b=t; } int fib; fib=a;}";
    runCorrectnessTest("fibonacci: fib(10) == 55", fibonacciScript, 60, 55);
    runEquivalenceTest("optimized/unoptimized equivalence: fibonacci", fibonacciScript);
    runGenerateBinaryTest("generates executable binary: fibonacci", fibonacciScript);

    // Same computation, but n is passed as a real function argument
    // (void main(int n)) instead of baked into the script text -- see
    // runArgumentPassingLoadTest()'s comment and asm_execute.h's
    // runExecutableWithArgs for how a parameterized function's argument
    // actually gets marshaled in.
    static const char *fibonacciArgScript =
        "void main(int n){int a; a=0; int b; b=1; int i; "
        "for (i=0;i<n;i++) { int t; t=a+b; a=b; b=t; } int fib; fib=a;}";
    runGenerateBinaryTest("generates executable binary: fibonacci (n as argument)", fibonacciArgScript);
    runArgumentPassingLoadTest();
    runNamedFunctionLoadTest();
    runSaveLoadBinaryTest();
    runArgumentsClassTest();
    runScriptExecutableTest();
    runJsonBindingStructureTest();

    TestCase tests[] = {
        {
            "empty function",
            "void main(){}",
            noError, NULL, false,
        },
        {
            "arithmetic + variable declaration",
            "void main(){int a; a = 1 + 2 * 3;}",
            noError, "movi", false,
        },
        {
            "if/else",
            "void main(){int a; a = 1; if (a < 2) {a = 3;} else {a = 4;}}",
            noError, NULL, false,
        },
        {
            "while loop",
            "void main(){int a; a = 0; while (a < 10) {a = a + 1;}}",
            noError, NULL, false,
        },
        {
            "for loop",
            "void main(){int i; for (i = 0; i < 10; i++) {int j; j = i;}}",
            noError, NULL, false,
        },
        {
            "float type + ternary",
            "void main(){float a; a = 1.0; int b; b = (a < 2.0) ? 1 : 0;}",
            noError, NULL, false,
        },
        {
            // Regression test: NodeToken::NodeToken(char *_target, nodeType
            // tt) in nodetoken.cpp used to leave _c_size uninitialized,
            // which made addChild() walk phantom children and dereference
            // a null pointer for break/continue nodes. Fixed by adding
            // `_c_size = 0;` in that constructor.
            "continue inside for loop",
            "void main(){int i; for (i = 0; i < 10; i++) {continue;}}",
            noError, NULL, false,
        },
        {
            "break inside while loop",
            "void main(){int i; i = 0; while (i < 10) {break;}}",
            noError, NULL, false,
        },
        {
            "struct definition and use",
            "struct point {int x; int y;} point p; void main(){p.x = 1; p.y = 2;}",
            noError, NULL, false,
        },
        // Regression test found via the sc_examples corpus (see
        // test/sc_examples/README.md's psion32/snake.sc entry): a
        // trailing ';' after a struct definition -- `struct point
        // {...};`, a natural C habit, invalid in this language in both
        // v1 and v2 -- followed by a global declaration of that struct
        // type used to leave the declared variable's type unresolved
        // (_vartype stuck at EOF_VARTYPE) instead of erroring out
        // immediately. copyPrty() (nodetoken.cpp) then dereferenced
        // getVarTypeObj()'s NULL return and segfaulted. v1 handles the
        // same input cleanly (its own "expected identifier" error); fixed
        // by having copyPrty() report impossibletofindvariabledeclaration
        // instead of crashing when the source type doesn't resolve --
        // some other check downstream (here, the missing-semicolon check
        // right after the struct body) ends up reporting first, which is
        // fine: the goal is "doesn't crash and reports *some* real parse
        // error", not a specific error code.
        {
            "struct definition with stray trailing ';' then a global of that type (must not crash)",
            "struct point {int x; int y;}; point p; void main(){}",
            expectingSemicolon, NULL, false,
        },
        {
            "external function + variable declaration and call",
            "external void show(); external uint32_t rnd(uint32_t s); "
            "void main(){uint32_t h; h = rnd(10); show();}",
            noError, "callExt", false,
        },
        {
            "missing semicolon is a parse error",
            "void main(){int a; a = 1}",
            expectingSemicolon, NULL, false,
        },
        {
            "using an undeclared variable is a parse error",
            "void main(){a = 1;}",
            impossibletofindvariabledeclaration, NULL, false,
        },
        {
            "calling an undeclared function is a parse error",
            "void main(){doesNotExist();}",
            functionnotfound, NULL, false,
        },
        // This test only exercises Parser::parse() (checked below via
        // mustContainAsm), not createBinary() -- with the float-division
        // and FP-opcode-dispatch gaps fixed (see visitnode.cpp's _div[]
        // and asm_parser.cpp's div0.s/nexp01.s/etc. dispatch), the
        // resulting assembly for this exact script (given explicit
        // `external` declarations, see examples/BouncingBalls.ino) does
        // now assemble to a valid binary, and reliably so: nodetoken.h's
        // `children` used to be an inline array of NodeToken values,
        // grown via realloc -- which can move, silently invalidating any
        // raw NodeToken* held into it (only a few of the many such
        // pointers, e.g. current_node, were patched up after the fact;
        // current_cntx and others weren't). A script this size (struct
        // with a constructor + 2 methods, an array of structs, several
        // external declarations, nested loops) reliably triggered it --
        // 0/30 successful runs of the full create+execute pipeline before
        // the fix, 30/30 after. See nodetoken.h's comment on `children`
        // for the actual fix (children are now individually
        // heap-allocated and never move).
        //
        // *This* script -- without explicit externals, relying only on
        // registerBallEffectBindings()'s bindFunction() auto-declare --
        // used to be unable to reach createBinary() at all: the
        // auto-declare path (parseFunctionCall()/getVariable() in
        // parser.cpp) parsed the synthesized declaration into a throwaway
        // scratch NodeToken (extra_parser) that's never a descendant of
        // `program`, so program.visitNode() never walked it and its
        // jump-table header reservation never got emitted -- even though
        // the declaration was correctly registered for name lookup
        // (functions/main_context), so parsing itself succeeded. Fixed by
        // parsing straight into `program` instead of `extra_parser` in
        // both auto-declare sites. See runAutoDeclareBinaryTest() below
        // for the createBinary()/createExecutableFromBinary() regression
        // coverage this comment used to say was missing.
        {
            "bouncing balls effect (struct + host bindings, no external decls)",
            ballEffectScript,
            noError, "callExt", false,
        },
    };

    int n = sizeof(tests) / sizeof(tests[0]);
    for (int i = 0; i < n; i++)
        runTest(tests[i]);

    printf("\n%d passed, %d failed, %d known-crash (of %d)\n", passed, failed, knownCrashes, passed + failed + knownCrashes);
    return failed ? 1 : 0;
}
