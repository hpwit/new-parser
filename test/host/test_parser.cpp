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
#include <cstddef>
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
};

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

        // A reload is only redundant while its base register still holds
        // the same address. Reduced from examples/GameOfLife's
        // `line1[i + 1] + line1[i]`: both loads are the same text, but a5
        // is re-derived in between. Checked for both reload passes
        // (optimize() tracks a3..a10, optimizeSpeed() a11..a15).
        Text r;
        r.addAfter("l8ui a14,a5,0"); // 0: kept
        r.addAfter("l8ui a14,a5,0"); // 1: same base value -> blanked
        r.addAfter("add a5,a5,a4");  // 2: base rewritten
        r.addAfter("l8ui a14,a5,0"); // 3: different address -> kept
        r.addAfter("l8ui a6,a5,0");  // 4: kept
        r.addAfter("addi a5,a5,1");  // 5: base rewritten
        r.addAfter("l8ui a6,a5,0");  // 6: different address -> kept
        r.addAfter("l32i a7,a7,0");  // 7: kept
        r.addAfter("l32i a7,a7,0");  // 8: second dereference -> kept

        optimize(&r);
        optimizeSpeed(&r);

        const char *expectedReload[] = {
            "l8ui a14,a5,0",
            " ",
            "add a5,a5,a4",
            "l8ui a14,a5,0",
            "l8ui a6,a5,0",
            "addi a5,a5,1",
            "l8ui a6,a5,0",
            "l32i a7,a7,0",
            "l32i a7,a7,0",
        };
        for (int i = 0; i < 9; i++)
        {
            char *got = *r.getChildAtPos(i);
            if (got == NULL || strcmp(got, expectedReload[i]) != 0)
            {
                printf("       reload line %d: expected \"%s\", got \"%s\"\n", i, expectedReload[i], got ? got : "(null)");
                ok = false;
            }
        }

        // Copy propagation must keep a copy that is read again after the
        // use it rewrote. Shape of a safe_mode check: the index copied
        // into a15 is compared (that use is rewritten to read a2), then
        // read again on both paths after it. The `movr` has to stay.
        Text k;
        k.addAfter("movr a15,a2");
        k.addAfter("bltu a15,a6,__test_safe_1");
        k.addAfter("mov a12,a15");
        k.addAfter("callExt a8,@_safeModeError(char*|num|num|num)");
        k.addAfter("retw.n");
        k.addAfter("__test_safe_1:");
        k.addAfter("add a15,a15,a15");
        optimize(&k);
        if (strcmp(*k.getChildAtPos(0), "movr a15,a2") != 0)
        {
            printf("       copy line 0: expected \"movr a15,a2\" to be kept, got \"%s\"\n", *k.getChildAtPos(0));
            ok = false;
        }

        // preserveAcrossCalls(): a15 holds a value across the first call
        // (read by the `add` after it) and has to survive it; around the
        // second call nothing is live (a15 is rewritten before being
        // read, a10 is the return register).
        //
        // In @_f a3..a7 all appear, so there is no spare window register
        // and a15 is saved to a frame slot. The slot goes where the
        // frame's reserved top 32 bytes start (96 - 32 = 64) and the frame
        // grows by 8 so that area stays intact. The read at offset 100 is
        // past the old frame size (an argument in the caller's frame) and
        // has to move with it.
        //
        // In @_h nothing names a3, so a15's whole life (movi .. add) is
        // renamed into a3 instead: no slot, no frame growth, no copies.
        Text c;
        c.addAfter("@_f():");
        c.addAfter("entry a1,96");
        c.addAfter("l32i a14,a1,100");
        c.addAfter("s32i a14,a1,60");
        c.addAfter("s32i a3,a1,0");
        c.addAfter("s32i a4,a1,4");
        c.addAfter("s32i a5,a1,8");
        c.addAfter("s32i a6,a1,12");
        c.addAfter("s32i a7,a1,16");
        c.addAfter("movi a15,20");
        c.addAfter("movi a10,200");
        c.addAfter("callExt a8,@_rand(num)");
        c.addAfter("add a15,a15,a10");
        c.addAfter("s32i a15,a1,56");
        c.addAfter("callExt a8,@_show()");
        c.addAfter("mov a15,a10");
        c.addAfter("retw.n");
        c.addAfter("@_h():");
        c.addAfter("entry a1,96");
        c.addAfter("movi a15,20");
        c.addAfter("movi a10,200");
        c.addAfter("callExt a8,@_rand(num)");
        c.addAfter("add a15,a15,a10");
        c.addAfter("s32i a15,a1,56");
        c.addAfter("callExt a8,@_show()");
        c.addAfter("mov a2,a10");
        c.addAfter("retw.n");
        c.addAfter("@_g():");
        c.addAfter("entry a1,80");
        c.addAfter("callExt a8,@_show()");
        c.addAfter("retw.n");

        preserveAcrossCalls(&c);

        const char *expectedCalls[] = {
            "@_f():",
            "entry a1,104",
            "l32i a14,a1,108",
            "s32i a14,a1,60",
            "s32i a3,a1,0",
            "s32i a4,a1,4",
            "s32i a5,a1,8",
            "s32i a6,a1,12",
            "s32i a7,a1,16",
            "movi a15,20",
            "movi a10,200",
            "s32i a15,a1,64",
            "callExt a8,@_rand(num)",
            "l32i a15,a1,64",
            "add a15,a15,a10",
            "s32i a15,a1,56",
            "callExt a8,@_show()",
            "mov a15,a10",
            "retw.n",
            "@_h():",
            "entry a1,96",
            "movi a3,20",
            "movi a10,200",
            "callExt a8,@_rand(num)",
            "add a15,a3,a10",
            "s32i a15,a1,56",
            "callExt a8,@_show()",
            "mov a2,a10",
            "retw.n",
            "@_g():",
            "entry a1,80",
            "callExt a8,@_show()",
            "retw.n",
        };
        const int nExpected = (int)(sizeof(expectedCalls) / sizeof(expectedCalls[0]));
        if (c.size() != nExpected)
        {
            printf("       preserveAcrossCalls: expected %d lines, got %d\n", nExpected, c.size());
            ok = false;
        }
        for (int i = 0; i < nExpected && i < c.size(); i++)
        {
            char *got = *c.getChildAtPos(i);
            if (got == NULL || strcmp(got, expectedCalls[i]) != 0)
            {
                printf("       call line %d: expected \"%s\", got \"%s\"\n", i, expectedCalls[i], got ? got : "(null)");
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
        promoteFrameSlotsEnabled = false; // the result is read from its frame slot

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
            if (entryOffset < 4 || entryOffset % 4 != 0)
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

// safe_mode code generation. What the checks *do* at runtime (stop the
// script, report, unwind across script functions) can only be seen by
// running them -- test/qemu_full/SafeModeTest does that on an emulated
// ESP32. This checks the compile-time half: which stores get a check,
// what the header declares, that the result assembles and the error
// handler's slot relocates, and that safe_mode does not leak from one
// compiled script into the next.
static int countLinesContaining(Text &t, const char *needle)
{
    int n = 0;
    for (int i = 0; i < t.size(); i++)
        if (strstr(t.getText(i), needle) != NULL)
            n++;
    return n;
}

// The two lines after `label` in the header, joined, e.g.
// "@_size_buf: .bytes 4 0a 00 00 00".
static bool headerEntryIs(const char *label, const char *bytesLine)
{
    for (int i = 0; i + 1 < header.size(); i++)
        if (strcmp(header.getText(i), label) == 0)
            return strcmp(header.getText(i + 1), bytesLine) == 0;
    return false;
}

static bool compileForTest(const char *script)
{
    Parser p;
    p.clean();
    content.clear();
    header.clear();
    footer.clear();
    Script s;
    s.addContent(strdup(script));
    s.init();
    p.parse(&s, &__allTokens);
    if (Error.error)
    {
        printf("       parse error=%d (%s)\n", Error.error, error_messages[Error.error]);
        return false;
    }
    return true;
}

static void runSafeModeTest()
{
    const char *name = "safe_mode: bounds checks on sized arrays only, assembles, relocates, does not leak";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        static uint8_t hostExt[6];
        static uint8_t hostRaw[4];
        bindVariable((char *)"uint8_t", (char *)"ext", (char *)"[]", (void *)hostExt);
        bindVariable((char *)"uint8_t", (char *)"raw", (char *)"[]", (void *)hostRaw);

        bool ok = true;
        const char *safeScript =
            "uint32_t _handle_;\n"
            "safe_mode\n"
            "external uint8_t ext[6];\n"
            "external uint8_t raw[];\n"
            "int buf[10];\n"
            "uint8_t grid[3, 4];\n"
            "uint8_t *p;\n"
            "void main()\n"
            "{\n"
            "   for (int i = 0; i < 10; i++)\n"
            "      buf[i] = i;\n"
            "   grid[2, 3] = 1;\n"
            "   ext[5] = 2;\n"
            "   raw[1] = 3;\n"
            "   p[1] = 4;\n"
            "}\n";
        if (!compileForTest(safeScript))
            ok = false;
        else
        {
            // One check per store into buf, grid and ext; none for the
            // unsized external `raw[]` or the pointer `p`.
            int checks = countLinesContaining(content, "bltu ");
            if (checks != 3)
            {
                printf("       expected 3 bounds checks, found %d\n", checks);
                ok = false;
            }
            // (buf's check is in a loop: its name may be loaded once
            // before it, into a register other than a10)
            const char *expectedChecks[] = {",@_name_buf", "l32r a10,@_name_grid", "l32r a10,@_name_ext",
                                            "callExt a8,@_safeModeError(char*|num|num|num)", "l32r a13,@__handle_"};
            for (int i = 0; i < 5; i++)
                if (!textContains(content, expectedChecks[i]))
                {
                    printf("       missing \"%s\" in the code\n", expectedChecks[i]);
                    ok = false;
                }
            if (!headerEntryIs("@_size_buf:", ".bytes 4 0a 00 00 00") ||
                !headerEntryIs("@_size_grid:", ".bytes 4 0c 00 00 00") ||
                !headerEntryIs("@_size_ext:", ".bytes 4 06 00 00 00") ||
                !headerEntryIs("@_name_buf:", ".bytes 4 62 75 66 00"))
            {
                printf("       size/name header entries missing or wrong\n");
                ok = false;
            }
            if (textContains(header, "@_size_raw:") || textContains(header, "@_size_p:"))
            {
                printf("       a pointer or unsized array got a size entry\n");
                ok = false;
            }
            if (countLinesContaining(header, "@_safeModeError(") != 1)
            {
                printf("       expected exactly one safeModeError slot in the header\n");
                ok = false;
            }

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
                freeExecutable(&exe);
            }
        }

        // Same kind of script without the keyword, compiled right after:
        // no checks, no size entries, no error-handler slot.
        const char *plainScript =
            "uint32_t _handle_;\n"
            "int buf[10];\n"
            "void main()\n"
            "{\n"
            "   buf[3] = 1;\n"
            "}\n";
        if (!compileForTest(plainScript))
            ok = false;
        else if (textContains(content, "bltu ") || textContains(header, "@_size_buf:") ||
                 textContains(header, "@_safeModeError("))
        {
            printf("       safe_mode carried over into the next compiled script\n");
            ok = false;
        }

        // A script without the usual `uint32_t _handle_;` prelude still
        // gets a _handle_ word (the compiler always declares it), so the
        // check can always pass the handler its executable.
        const char *noPreludeScript =
            "safe_mode\n"
            "int buf[4];\n"
            "void main()\n"
            "{\n"
            "   buf[2] = 1;\n"
            "}\n";
        if (!compileForTest(noPreludeScript))
            ok = false;
        else
        {
            if (!textContains(content, "l32r a13,@__handle_"))
            {
                printf("       expected the check to load _handle_ without the prelude too\n");
                ok = false;
            }
            Binary bin = createBinary(&footer, &header, &content, false);
            if (bin.error.error)
            {
                printf("       assembler error without the prelude: %s\n", bin.error.error_message ? bin.error.error_message : "?");
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

// printfln's arguments after the format are varargs (Args). Calls with an
// Args parameter used to skip the argument ordering every other call gets,
// so `printfln("%d %d", busy(1), x)` loaded x into a12 before calling
// busy(), whose a4 -- busy's inner loop counter here -- is our a12: x
// printed as 3 (checked under QEMU). Same with a char* returned by a
// script function. Each of the call's argument registers a11..lastArgReg
// must be written after the last call nested in the arguments.
static bool argsLoadedAfterNestedCalls(const char *call, int lastArgReg)
{
    int callAt = -1;
    for (int i = 0; i < content.size(); i++)
        if (strstr(content.getText(i), call) != NULL)
            callAt = i;
    if (callAt < 0)
    {
        printf("       no %s in the generated code\n", call);
        return false;
    }
    // Only the function the call is in: from its label on.
    int start = 0;
    for (int i = 0; i < callAt; i++)
        if (strncmp(content.getText(i), "@_", 2) == 0)
            start = i;
    int lastNested = -1;
    for (int i = start; i < callAt; i++)
        if (strstr(content.getText(i), "call8 ") != NULL || strstr(content.getText(i), "callExt ") != NULL)
            lastNested = i;
    bool ok = true;
    for (int i = start; i <= lastNested; i++)
    {
        char line[64];
        strncpy(line, content.getText(i), sizeof(line) - 1);
        line[sizeof(line) - 1] = 0;
        char *t = line;
        while (*t == ' ' || *t == '\t')
            t++;
        // "op aR,..." writes aR (stores don't: their first operand is
        // read).
        char *sp = strchr(t, ' ');
        if (sp == NULL || strncmp(t, "s32i", 4) == 0 || strncmp(t, "s8i", 3) == 0 || strncmp(t, "s16i", 4) == 0)
            continue;
        int r = -1;
        if (sscanf(sp + 1, "a%d,", &r) == 1 && r >= 11 && r <= lastArgReg)
        {
            // Fine if no nested call follows it (it is then that call's
            // own argument, or the value the outer call gets), or if the
            // register is written again once the nested calls are done.
            bool callAfter = false;
            for (int k = i + 1; k <= lastNested; k++)
                if (strstr(content.getText(k), "call8 ") != NULL || strstr(content.getText(k), "callExt ") != NULL)
                    callAfter = true;
            bool reloadedLater = false;
            for (int k = lastNested + 1; k < callAt; k++)
            {
                char want[8];
                snprintf(want, sizeof(want), "a%d,", r);
                const char *u = content.getText(k);
                while (*u == ' ' || *u == '\t')
                    u++;
                const char *usp = strchr(u, ' ');
                if (usp != NULL && strncmp(usp + 1, want, strlen(want)) == 0 && strncmp(u, "s32i", 4) != 0)
                    reloadedLater = true;
            }
            if (callAfter && !reloadedLater)
            {
                printf("       a%d set up before a later nested call: %s\n", r, content.getText(i));
                ok = false;
            }
        }
    }
    return ok;
}

// A char * variable, parameter or array, or a call returning char *, as a
// call argument: its type (char) never reached the call's signature, which
// said "none*", so no function taking a char * matched -- only a string
// literal, which sets its type itself, did ("Function not found").
static void runCharPointerArgumentsTest()
{
    const char *name = "char * variables, parameters, arrays and results as arguments";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        const char *scripts[] = {
            "void show(char *t)\n{\n}\nvoid main()\n{\n   char *s = \"hi\";\n   show(s);\n}\n",
            "char str[6];\nvoid show(char *t)\n{\n}\nvoid main()\n{\n   show(str);\n}\n",
            "char *g;\nvoid show(char *t)\n{\n}\nvoid main()\n{\n   show(g);\n}\n",
            "void inner(char *t)\n{\n}\nvoid outer(char *t)\n{\n   inner(t);\n}\nvoid main()\n{\n   outer(\"x\");\n}\n",
            "external void showText(char *t);\nchar *name(int i)\n{\n   return \"x\";\n}\nvoid main()\n{\n   showText(name(1));\n}\n",
        };
        bool ok = true;
        for (int i = 0; i < (int)(sizeof(scripts) / sizeof(scripts[0])); i++)
            if (!compileForTest(scripts[i]))
            {
                printf("       case %d does not compile\n", i);
                ok = false;
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

// switch/case/default: misuse is reported with its own message (running
// switches is checked under QEMU against C, test/fuzz/regressions/switch_*).
static void runSwitchErrorsTest()
{
    const char *name = "switch: case/default misuse reported with its own error";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        struct
        {
            const char *script;
            int error;
        } cases[] = {
            {"void main()\n{\n   int x = 1;\ncase 1:\n   x = 2;\n}\n", caseOutsideSwitch},
            {"void main()\n{\n   int x = 1;\n   switch (x)\n   {\n   case 1:\n   {\n   case 2:\n      break;\n   }\n   }\n}\n", caseOutsideSwitch},
            {"void main()\n{\n   int x = 1;\n   switch (x)\n   {\n   case 1:\n      break;\n   case 1:\n      break;\n   }\n}\n", duplicateCase},
            {"void main()\n{\n   int x = 1;\n   switch (x)\n   {\n   default:\n      break;\n   default:\n      break;\n   }\n}\n", duplicateDefault},
            {"void main()\n{\n   float x = 1.5;\n   switch (x)\n   {\n   case 1:\n      break;\n   }\n}\n", switchNeedsInteger},
            {"void main()\n{\n   int x = 1;\n   int y = 2;\n   switch (x)\n   {\n   case y:\n      break;\n   }\n}\n", expectingCaseConstant},
            {"void main()\n{\n   int x = 1;\n   switch (x)\n   {\n   case 1\n      break;\n   }\n}\n", expectingColon},
            {"void main()\n{\n   int x = 1;\n   switch (x)\n   {\n   case 1:\n      continue;\n   }\n}\n", noWhileorForFound},
            {"void main()\n{\n   int x = 1;\n   switch (x)\n   {\n   case 1:\n      break;\n   }\n}\n", noError},
        };
        bool ok = true;
        for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++)
        {
            Parser p;
            p.clean();
            content.clear();
            header.clear();
            footer.clear();
            Script sc;
            sc.addContent(strdup(cases[i].script));
            sc.init();
            p.parse(&sc, &__allTokens);
            if (Error.error != cases[i].error)
            {
                printf("       case %d: error %d (%s), expected %d (%s)\n", i, Error.error, error_messages[Error.error],
                       cases[i].error, error_messages[cases[i].error]);
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

// ffloat: an expression is computed in fixed point, and an all-constant
// one is folded at compile time -- `ffloat m = 2.3/4 + 2;` is the single
// constant 2.575 * 65536 = 168755 (0x00029333), with no float instruction.
// (ffloat arithmetic itself is checked bit-exactly under QEMU against a C
// model: test/fuzz/regressions/ffloat_*, test/effects mandelbrot_ffloat.)
static void runFfloatConstantTest()
{
    const char *name = "ffloat: constant expression folded to one fixed-point literal";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        bool ok = compileForTest("ffloat g;\nvoid main()\n{\n   ffloat m = 2.3/4 + 2;\n   g = m;\n}\n");
        if (ok && !textContains(header, ".bytes 4 33 93 02 00"))
        {
            printf("       no 168755 (0x00029333) literal in the header\n");
            ok = false;
        }
        const char *floatOps[] = {"lsi ", "add.s", "mul.s", "___div", "float.s", "trunc.s"};
        for (int i = 0; ok && i < (int)(sizeof(floatOps) / sizeof(floatOps[0])); i++)
            if (textContains(content, floatOps[i]))
            {
                printf("       float instruction left: %s\n", floatOps[i]);
                ok = false;
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

static void runVarargsOrderTest()
{
    const char *name = "printfln: arguments loaded after calls nested in later arguments";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        bool ok = true;
        const char *ints =
            "int total;\n"
            "int busy(int i)\n"
            "{\n"
            "   for (int a = 0; a < 3; a++)\n"
            "      for (int b = 0; b < 3; b++)\n"
            "         total = total + a * b;\n"
            "   return i + 100;\n"
            "}\n"
            "void main()\n"
            "{\n"
            "   int x = 42;\n"
            "   printfln(\"%d %d\", busy(1), x);\n"
            "}\n";
        if (!compileForTest(ints) || !argsLoadedAfterNestedCalls("callExt a8,@_printfln", 12))
            ok = false;

        const char *strings =
            "int total;\n"
            "char *pick(int i)\n"
            "{\n"
            "   for (int a = 0; a < 3; a++)\n"
            "      for (int b = 0; b < 3; b++)\n"
            "         total = total + a * b;\n"
            "   if (i == 0)\n"
            "      return \"zero\";\n"
            "   return \"other\";\n"
            "}\n"
            "void main()\n"
            "{\n"
            "   char *s = pick(1);\n"
            "   printfln(\"%s %s\", pick(0), s);\n"
            "}\n";
        if (!compileForTest(strings) || !argsLoadedAfterNestedCalls("callExt a8,@_printfln", 12))
            ok = false;

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

// _handle_ (the executable's address, filled in on every entry) is
// declared by Parser::parse() itself, so it exists however a script is
// compiled -- the raw Parser path included, which used to reject it as
// undeclared (only parseScript()'s prelude declared it). A script that
// declares it itself, as v1-era scripts and test preludes do, still
// compiles, and the extra line in front of the script text does not move
// reported error lines.
static void runBuiltinHandleTest()
{
    const char *name = "_handle_ is declared for every script; own declaration accepted; error lines unchanged";
    printf("RUNNING: %s\n", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0)
    {
        bool ok = true;

        const char *undeclared =
            "uint32_t saved;\n"
            "void main()\n"
            "{\n"
            "   saved = _handle_;\n"
            "}\n";
        if (!compileForTest(undeclared))
        {
            printf("       raw Parser path: _handle_ not declared\n");
            ok = false;
        }
        else
        {
            if (!textContains(content, "@__handle_"))
            {
                printf("       expected main() to read @__handle_\n");
                ok = false;
            }
            Binary bin = createBinary(&footer, &header, &content, false);
            if (bin.error.error)
            {
                printf("       assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
                ok = false;
            }
        }

        const char *declared =
            "uint32_t _handle_;\n"
            "uint32_t saved;\n"
            "void main()\n"
            "{\n"
            "   saved = _handle_;\n"
            "}\n";
        if (!compileForTest(declared))
        {
            printf("       a script declaring _handle_ itself no longer compiles\n");
            ok = false;
        }

        // Still an error for any other name.
        if (compileForTest("int a;\nint a;\nvoid main()\n{\n}\n"))
        {
            printf("       duplicate global accepted\n");
            ok = false;
        }

        // `int a = ;` on line 3: reported on line 3 by both entry points.
        const char *broken = "void main()\n{\n   int a = ;\n}\n";
        compileForTest(broken);
        if (!Error.error || Error.token == NULL || Error.token->line != 3)
        {
            printf("       raw Parser path: error reported on line %d, expected 3\n",
                   (Error.error && Error.token) ? (int)Error.token->line : -1);
            ok = false;
        }
        {
            ScriptExecutable exe = parseScript(broken);
            if (exe.isExeExists() || !Error.error || Error.token == NULL || Error.token->line != 3)
            {
                printf("       parseScript(): error reported on line %d, expected 3\n",
                       (Error.error && Error.token) ? (int)Error.token->line : -1);
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
            "struct Foo{int a,b; Foo(){a=0;b=0;}};"
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
                int reg = -1, imm = -1, src = -1;
                if (strncmp(next, "mull", 4) == 0 && strstr(after, "@_arr") != NULL &&
                    sscanf(line, "movi a%d,%d", &reg, &imm) == 2)
                {
                    if (imm == 8)
                        foundCorrectStride = true;
                    else
                        wrongStrideSeen = imm;
                }
                // allocateLoopRegisters() turns a movi of a power of two
                // and its mull into one shift
                if (strstr(next, "@_arr") != NULL && sscanf(line, "slli a%d,a%d,%d", &reg, &src, &imm) == 3)
                {
                    if (imm == 3)
                        foundCorrectStride = true;
                    else
                        wrongStrideSeen = 1 << imm;
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
                printf("       did not find the expected 'movi a<reg>,8' / 'mull' (or 'slli ...,3') / 'l32r ...,@_arr' sequence in the generated instructions\n");
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
            "struct Foo{int a,b; Foo(){a=0;b=0;} void bump(){a=a+1;}};"
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
                // the same as one shift (see the field-read test above)
                if (sscanf(line, "slli a%d,a%d,%d", &mullDest, &mullA, &imm) == 3 && imm == 3 &&
                    strstr(next, "@_arr") != NULL)
                {
                    mullDestReg = mullDest;
                    break;
                }
            }
            if (mullDestReg == -1)
            {
                printf("       did not find the expected 'movi a<reg>,8' / 'mull' (or 'slli ...,3') / 'l32r ...,@_arr' sequence in the generated instructions\n");
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

// A function declared with parameters gets exactly *one* header record
// (see asm_parser.cpp's createBinaryHeader and asm_execute.cpp's
// isWrapperRecord() comment for the argument-marshaling wrapper this
// project used to also generate, and why it doesn't anymore): the real
// function's own address, with the correct args_num and a valid
// variableaddress. This checks createExecutableFromBinary() decodes that
// one record correctly and that runExecutableWithArgs(ex, args, 1) can
// find it by args_num == 1. Doesn't call into the generated code --
// MiniXtensa can't interpret this script (it involves a call8, which it
// explicitly refuses -- see mini_xtensa.h), and calling on a host CPU
// would crash immediately regardless. Real execution is QEMU-verified:
// test/qemu/gen_fibonacci_arg.cpp runs this exact script with n=10 and
// confirms fib(10)==55 on a real Xtensa CPU emulation.
//
// The string-literal variant: a literal becomes a global, emitted into
// the header after the function was already compiled (per-function
// compile). It once landed between the function's ".global" and ".var"
// lines, and the record decoded with args_num 0.
static void runArgumentPassingLoadTest(const char *name, const char *script)
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
        else if (exe.functions.size() != 1)
        {
            printf("       expected 1 function record, got %d\n", exe.functions.size());
            ok = false;
        }
        else
        {
            globalcall *fn = exe.functions.getptr(0);
            if (fn->args_num != 1)
            {
                printf("       expected args_num==1, got %d\n", fn->args_num);
                ok = false;
            }
            else
            {
                printf("       found main(int): address=%u\n", fn->address);
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
// directly and reads its return value back from a10. Checks the loader
// produces exactly one record named "fib", at its real address (see
// asm_execute.cpp's isWrapperRecord() comment: a parameterized function
// used to also get a second, argument-marshaling wrapper record, which
// this project no longer generates) -- the actual register-passing call
// is QEMU-verified: test/qemu/gen_named_function.cpp calls this exact
// script's fib(10) this way and confirms it returns 55 on a real Xtensa
// CPU emulation.
static void runNamedFunctionLoadTest()
{
    const char *name = "load a script with a named, non-main function and find its callable record (execution itself is QEMU-verified, see test/qemu/)";
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
            globalcall *fib = NULL;
            int fibCount = 0;
            for (int i = 0; i < exe.functions.size(); i++)
            {
                globalcall *gc = exe.functions.getptr(i);
                const char *label = gc->name;
                bool isFib = label[0] == '@' && label[1] == '_' &&
                             (strncmp(label + 2, "fib", 3) == 0 || strncmp(label + 3, "fib", 3) == 0);
                if (!isFib)
                    continue;
                fib = gc;
                fibCount++;
            }
            if (fib == NULL || fibCount != 1)
            {
                printf("       expected exactly one record named fib, found %d\n", fibCount);
                ok = false;
            }
            else
            {
                printf("       fib at offset %u\n", fib->address);
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
                    // No isWrapper check needed here anymore -- see
                    // asm_execute.cpp's isWrapperRecord() comment: with the
                    // wrapper mechanism's own ".global @__NAME"/marshaling
                    // body disabled (visitnode.cpp's _visitdefFunctionNode()),
                    // every function gets exactly one record, always the
                    // real, callable one.
                    globalcall *keyboardPlain = NULL;
                    for (int i = 0; i < exe.functions.size(); i++)
                    {
                        globalcall *gc = exe.functions.getptr(i);
                        const char *p2 = gc->name + 2;
                        if (p2[0] == '_')
                            p2++;
                        if (strncmp(p2, "keyboard", 8) == 0 && (p2[8] == '(' || p2[8] == 0))
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
        // uint32_t _handle_) -- an earlier version of this function
        // didn't, so a script using a bare `true`/`false` literal or
        // referencing _handle_ (both routine -- the latter is what
        // pinInterrupt(_handle_, "fn", pin) needs, see KeyboardCallback-
        // style scripts) failed to compile with a confusing "impossible
        // to find variable declaration" for something the script never
        // declared itself. No external binding needed here -- just
        // referencing _handle_ as a plain variable is enough to prove
        // it's declared.
        {
            ScriptExecutable exec = parseScript(
                "void main(){"
                "   uint32_t y = _handle_;"
                "   bool b = true;"
                "   bool c = false;"
                "}");
            if (!exec.isExeExists())
            {
                printf("       script using true/false/_handle_ (no explicit declaration) failed to compile\n");
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

// lastCompileError(): what a failed parseScript() reports to the sketch,
// and that a compile that works clears it.
static void runCompileErrorReportTest()
{
    const char *name = "lastCompileError(): message, line, position and source line of a failed compile";
    printf("RUNNING: %s\n", name);
    fflush(stdout);
    bool ok = true;
    struct Case
    {
        const char *script, *message, *source;
        int line, position;
    } cases[] = {
        {"void main()\n{\n   int a = 3;\n   b = a + 1;\n}\n", "Impossible to find variable declaration", "   b = a + 1;", 4, 4},
        {"void main()\n{\n   int a = 3\n   a = a + 1;\n}\n", "Expecting ;", "   a = a + 1;", 4, 4},
    };
    for (int i = 0; i < 2; i++)
    {
        ScriptExecutable exec = parseScript(cases[i].script);
        const CompileError &e = lastCompileError();
        if (exec.isExeExists() || !e.failed || strcmp(e.message, cases[i].message) != 0 ||
            e.line != cases[i].line || e.position != cases[i].position || strcmp(e.sourceLine, cases[i].source) != 0)
        {
            printf("       case %d: failed=%d message=\"%s\" line=%d position=%d source=\"%s\"\n", i, e.failed,
                   e.message, e.line, e.position, e.sourceLine);
            ok = false;
        }
    }
    ScriptExecutable good = parseScript("void main()\n{\n   int a = 3;\n}\n");
    if (!good.isExeExists() || lastCompileError().failed)
    {
        printf("       a script that compiles left lastCompileError().failed set\n");
        ok = false;
    }
    if (ok)
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

// Globals past 64KB: the data size is 32 bits, through compiling and a
// save/load round trip (serializeBinary() writes it on 2 bytes, or as
// 0xFFFF + 4 bytes when it does not fit).
static void runLargeGlobalsTest()
{
    const char *name = "globals past 64KB: 32-bit data size, kept by serialize/deserialize";
    printf("RUNNING: %s\n", name);
    fflush(stdout);
    bool ok = true;
    const char *script = "uint8_t copy[480, 800];\nuint8_t line1[800];\nvoid main()\n{\n   copy[479, 799] = 1;\n   line1[0] = 2;\n}\n";
    if (!compileForTest(script))
        ok = false;
    else
    {
        Binary bin = createBinary(&footer, &header, &content, false);
        if (bin.error.error || bin.data_size < 480 * 800 + 800)
        {
            printf("       data_size %u, expected at least %u\n", (unsigned)bin.data_size, 480u * 800u + 800u);
            ok = false;
        }
        else
        {
            uint32_t size = 0;
            uint8_t *buf = serializeBinary(&bin, &size);
            Binary back = deserializeBinary(buf, size);
            if (back.error.error || back.data_size != bin.data_size ||
                back.tmp_instruction_size != bin.tmp_instruction_size || back.function_size != bin.function_size)
            {
                printf("       round trip: data_size %u -> %u\n", (unsigned)bin.data_size, (unsigned)back.data_size);
                ok = false;
            }
            free(buf);
            freeBinary(&back);
        }
        freeBinary(&bin);
    }
    if (ok)
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

// Indexing a pointer variable loads the pointer first: `led16[i]` with
// `external uint16_t *led16;` (bound to the address of a host pointer),
// or a global `uint16_t *gp;`. An array (`uint16_t arr[8];`) is indexed
// from its own address. Both pointer cases used to index from the
// pointer's own storage, writing over whatever followed it.
static void runPointerIndexTest()
{
    const char *name = "p[i] on a T* variable loads the pointer first; arrays are indexed directly";
    printf("RUNNING: %s\n", name);
    fflush(stdout);
    static uint16_t *hostBuf = NULL;
    bindVariable((char *)"uint16_t *", (char *)"testPtr16", NULL, (void *)&hostBuf);
    // declared explicitly: equivalence tests further down drive the
    // parser without parse()'s own reset, which an auto-declared external
    // (a bound name used undeclared) would leave state behind for
    const char *script = "external uint16_t *testPtr16;\nuint16_t *gp;\nuint16_t arr[8];\nvoid main()\n{\n   int i = 3;\n   testPtr16[i] = 7;\n   gp[i] = 8;\n   arr[i] = 9;\n}\n";
    bool compiled = compileForTest(script);
    bool ok = compiled;
    int derefs = 0, arrayLoads = 0;
    for (int i = 0; ok && i + 1 < content.size(); i++)
    {
        char *line = content.getText(i);
        char *next = content.getText(i + 1);
        int r1 = -1, r2 = -2, r3 = -3;
        bool base = strstr(line, "@_ext_testPtr16") != NULL || strstr(line, "@_gp") != NULL;
        if (base && sscanf(next, "l32i a%d,a%d,%d", &r1, &r2, &r3) == 3 && r1 == r2 && r3 == 0)
            derefs++;
        if (strstr(line, "@_arr") != NULL && strncmp(next, "l32i", 4) == 0)
            arrayLoads++;
    }
    if (ok && (derefs != 2 || arrayLoads != 0))
    {
        printf("       pointer loads %d (expected 2), loads after the array's address %d (expected 0)\n", derefs,
               arrayLoads);
        ok = false;
    }
    if (compiled)
    {
        // consumes the generated text, as every other test leaves it
        Binary bin = createBinary(&footer, &header, &content, false);
        if (bin.error.error)
        {
            printf("       assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
            ok = false;
        }
        freeBinary(&bin);
    }
    if (ok)
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

// consumes the generated text, as every other test leaves it
static bool assembleForTest()
{
    Binary bin = createBinary(&footer, &header, &content, false);
    bool ok = !bin.error.error;
    if (!ok)
        printf("       assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
    freeBinary(&bin);
    return ok;
}

struct HostMix
{
    char a;
    int32_t b;
    uint16_t c;
    char d;
    float e;
    uint8_t f;
};

static void runStructLayoutTest()
{
    const char *name = "structs use C's layout; bindStruct() defines a host struct for every script";
    printf("RUNNING: %s\n", name);
    fflush(stdout);
    bool ok = true;

    // 1. member offsets and sizeof are C's
    const char *mix = "struct HostMix { char a; int b; uint16_t c; char d; float e; uint8_t f; };\nHostMix m;\nvoid main(){ m.e = 1.5; }\n";
    if (!compileForTest(mix))
        ok = false;
    else
    {
        int k = findStruct((char *)"HostMix");
        varType *v = k > -1 ? _userDefinedTypes.getptr(k) : NULL;
        size_t expected[6] = {offsetof(HostMix, a), offsetof(HostMix, b), offsetof(HostMix, c),
                              offsetof(HostMix, d), offsetof(HostMix, e), offsetof(HostMix, f)};
        if (v == NULL || v->_varSize != sizeof(HostMix))
        {
            printf("       sizeof %d, C says %d\n", v ? v->_varSize : -1, (int)sizeof(HostMix));
            ok = false;
        }
        for (int i = 0; v != NULL && i < 6; i++)
            if (v->starts[i] != expected[i])
            {
                printf("       member %d at %d, C says %d\n", i, v->starts[i], (int)expected[i]);
                ok = false;
            }
        ok = assembleForTest() && ok;
    }

    // 2. a bound struct needs no definition in the script
    static struct { char key; char val; } hostKey = {'a', 4};
    bindStruct("struct BoundKey { char key; char val; };");
    bindVariable((char *)"BoundKey", (char *)"boundKey", NULL, (void *)&hostKey);
    if (!compileForTest("external BoundKey boundKey;\nint main(){ return boundKey.val; }\n") || !assembleForTest())
    {
        printf("       bound struct not usable without a definition\n");
        ok = false;
    }

    // 3. repeating it identically is accepted
    if (!compileForTest("struct BoundKey { char key; char val; };\nexternal BoundKey boundKey;\nint main(){ return boundKey.key; }\n") ||
        !assembleForTest())
    {
        printf("       identical repeated definition refused\n");
        ok = false;
    }

    // 4. a different definition is an error
    if (compileForTest("struct BoundKey { int key; char val; };\nint main(){ return 0; }\n") ||
        Error.error != structRedefined)
    {
        printf("       differing definition: error %d, expected %d\n", Error.error, (int)structRedefined);
        ok = false;
    }

    // 5. reported lines are still the script's own
    if (compileForTest("void main()\n{\n   int x = ;\n}\n") || Error.token == NULL || Error.token->line != 3)
    {
        printf("       error line %d, expected 3\n", Error.token ? Error.token->line : -1);
        ok = false;
    }

    // 6. an array of a bound struct steps over whole structs (8 bytes),
    //    not over the field's size
    static char hostRecs[4 * 8];
    bindStruct("struct BoundRec { char a; int b; };");
    bindVariable((char *)"BoundRec", (char *)"boundRecs", (char *)"[4]", (void *)hostRecs);
    if (!compileForTest("external BoundRec boundRecs[4];\nint main(){ int i = 1; boundRecs[i].b = 5; return boundRecs[i].a; }\n"))
        ok = false;
    else
    {
        int strides = 0;
        for (int i = 0; i < content.size(); i++)
        {
            int d = -1, r = -1, k = -1;
            if (sscanf(content.getText(i), "slli a%d,a%d,%d", &d, &r, &k) == 3 && k == 3)
                strides++;
        }
        if (strides != 2)
        {
            printf("       %d index scalings by 8 (expected 2, one per access)\n", strides);
            ok = false;
        }
        ok = assembleForTest() && ok;
    }

    binded_structs.clear();
    Error.error = 0;
    if (ok)
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

static bool hasLine(const char *prefix)
{
    for (int i = 0; i < content.size(); i++)
        if (strncmp(content.getText(i), prefix, strlen(prefix)) == 0)
            return true;
    return false;
}

static void runStructArgumentsTest()
{
    const char *name = "structs and pointers as arguments: by value, by pointer, ->, &, *p; errors";
    printf("RUNNING: %s\n", name);
    fflush(stdout);
    bool ok = true;

    // a pointer as first parameter was a register variable: p[1] = 5
    // became `movi a2,5`, overwriting the pointer instead of storing
    if (!compileForTest("int arr[4];\nvoid f(int *p){ p[1] = 5; }\nvoid main(){ f(arr); }\n"))
        ok = false;
    else
    {
        if (!hasLine("s32i") || hasLine("movi a2,5"))
        {
            printf("       p[1] = 5 does not store through p\n");
            ok = false;
        }
        ok = assembleForTest() && ok;
    }

    // every form compiles and assembles (their results are checked
    // against GCC in test/qemu_full/StructArguments)
    const char *forms =
        "struct P { char c; int x; uint16_t h; };\nP g;\nP arr[3];\n"
        "int byValue(P s){ s.x = s.x + 1; return s.x + s.h + s.c; }\n"
        "void byPointer(P *p){ p->x = 7; p[1].h = 9; p.c = 1; }\n"
        "int through(P *p){ byPointer(&p[1]); return byValue(p[0]); }\n"
        "void main(){ P loc; int a = byValue(g) + byValue(arr[2]) + byValue(loc);\n"
        "   byPointer(&g); byPointer(arr); byPointer(&arr[1]); byPointer(&loc); a = a + through(arr);\n"
        "   int *q = &g.x; *q = 11; a = a + *q; }\n";
    if (!compileForTest(forms) || !assembleForTest())
    {
        printf("       the struct argument forms don't compile\n");
        ok = false;
    }

    struct
    {
        const char *script;
        int error;
    } errors[] = {
        {"void main(){ int x = 3; int *p = &x; }\n", addressNotPossible},
        {"int g;\nvoid main(){ int r = *g; }\n", derefNotPointer},
        {"struct O { int v; CRGB col; };\nvoid f(O *o){ int r = o->col; }\n", pointerMemberType},
        {"struct P { int x; };\nvoid f(int a, int b, int c, int d, int e, int f2, P s){ }\n", structArgument},
    };
    for (int i = 0; i < 4; i++)
    {
        if (compileForTest(errors[i].script) || Error.error != errors[i].error)
        {
            printf("       error case %d: error %d, expected %d\n", i, Error.error, errors[i].error);
            ok = false;
        }
    }
    Error.error = 0;
    if (ok)
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

// as a library would define them: the sketch can't use SCRIPT_STRUCT
struct LibKey
{
    unsigned char key_char;
    unsigned char key_code;
};
typedef struct
{
    char a;
    int32_t b;
    uint16_t c;
    bool d;
    float e;
    int16_t f;
} LibMix;

static void runBindStructMacroTest()
{
    const char *name = "BIND_STRUCT: a struct from a library, defined from its C type and checked against its layout";
    printf("RUNNING: %s\n", name);
    fflush(stdout);
    bool ok = true;

    if (!BIND_STRUCT(LibKey, key_char, key_code) ||
        strcmp(binded_structs[binded_structs.size() - 1], "struct LibKey { uint8_t key_char; uint8_t key_code; };\n") != 0)
    {
        printf("       LibKey: %s\n", binded_structs.size() ? binded_structs[binded_structs.size() - 1] : "not bound");
        ok = false;
    }
    if (!BIND_STRUCT(LibMix, a, b, c, d, e, f))
        ok = false;
    static LibKey hostKey;
    static LibMix hostMix;
    bindVariable((char *)"LibKey", (char *)"libKey", NULL, (void *)&hostKey);
    bindVariable((char *)"LibMix", (char *)"libMix", NULL, (void *)&hostMix);
    if (!compileForTest("external LibKey libKey;\nexternal LibMix libMix;\n"
                        "int main(){ libMix.f = libKey.key_code; return libMix.b + libKey.key_char; }\n"))
        ok = false;
    else
    {
        varType *v = _userDefinedTypes.getptr(findStruct((char *)"LibMix"));
        size_t expected[6] = {offsetof(LibMix, a), offsetof(LibMix, b), offsetof(LibMix, c),
                              offsetof(LibMix, d), offsetof(LibMix, e), offsetof(LibMix, f)};
        for (int i = 0; i < 6; i++)
            if (v->starts[i] != expected[i])
            {
                printf("       LibMix field %d at %d, C says %d\n", i, v->starts[i], (int)expected[i]);
                ok = false;
            }
        if (v->_varSize != sizeof(LibMix))
        {
            printf("       LibMix: %d bytes, C says %d\n", v->_varSize, (int)sizeof(LibMix));
            ok = false;
        }
        ok = assembleForTest() && ok;
    }

    // a field left out or out of order: refused, nothing bound
    int before = binded_structs.size();
    if (BIND_STRUCT(LibMix, a, b, c, d, e) || BIND_STRUCT(LibMix, b, a, c, d, e, f) || binded_structs.size() != before)
    {
        printf("       an incomplete or reordered field list was bound\n");
        ok = false;
    }

    binded_structs.clear();
    Error.error = 0;
    if (ok)
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

static int countLines(const char *prefix)
{
    int n = 0;
    for (int i = 0; i < content.size(); i++)
        if (strncmp(content.getText(i), prefix, strlen(prefix)) == 0)
            n++;
    return n;
}

static void runConstantIndexAndFormatTest()
{
    const char *name = "constant indexes in the instruction offset; by-value copies skip padding; floats to printf; Function not found details";
    printf("RUNNING: %s\n", name);
    fflush(stdout);
    bool ok = true;

    // arr[3] and p[2].x: the offset is in the instruction, no index maths
    if (!compileForTest("struct P { char c; int x; uint16_t h; };\nint arr[8];\n"
                        "int f(P *p){ p[2].x = 7; return p[1].x; }\n"
                        "int main(){ arr[3] = 5; return arr[3]; }\n"))
        ok = false;
    else
    {
        bool storeArr = false, loadArr = false, storeP = false, loadP = false;
        for (int i = 0; i < content.size(); i++)
        {
            const char *l = content.getText(i);
            storeArr |= strncmp(l, "s32i", 4) == 0 && strstr(l, ",12") != NULL;
            loadArr |= strncmp(l, "l32i", 4) == 0 && strstr(l, ",12") != NULL;
            storeP |= strncmp(l, "s32i", 4) == 0 && strstr(l, ",a2,28") != NULL;
            loadP |= strncmp(l, "l32i", 4) == 0 && strstr(l, ",a2,16") != NULL;
        }
        if (!storeArr || !loadArr || !storeP || !loadP || hasLine("mull") || hasLine("slli"))
        {
            printf("       constant indexes are still computed at run time\n");
            for (int i = 0; i < content.size(); i++)
                printf("         %s\n", content.getText(i));
            ok = false;
        }
        ok = assembleForTest() && ok;
    }

    // by value: 3 fields copied, the 3 padding components skipped
    if (!compileForTest("struct P { char c; int x; uint16_t h; };\nint byValue(P s){ return s.c + s.x + s.h; }\n"))
        ok = false;
    else
    {
        int loads = countLines("l8ui") + countLines("l16ui") + countLines("l32i");
        if (loads != 3)
        {
            printf("       by-value copy: %d loads, expected 3\n", loads);
            ok = false;
        }
        ok = assembleForTest() && ok;
    }

    // printfln("%f", x): the float reaches a11 (it used to pass nothing)
    if (!compileForTest("float g = 1.5;\nvoid main(){ float x = g * 2.0; printfln(\"%f\", x); }\n"))
        ok = false;
    else
    {
        if (!hasLine("rfr a11,"))
        {
            printf("       the float argument doesn't reach a11\n");
            ok = false;
        }
        ok = assembleForTest() && ok;
    }

    // Function not found says what was looked for and what exists
    if (compileForTest("struct P { int x; };\nP g;\nvoid setTag(P *p, char t){ }\nvoid main(){ setTag(&g, 90); }\n") ||
        Error.error != functionnotfound ||
        strcmp(error_detail, "setTag(P *, number); there is setTag(P *, char)") != 0)
    {
        printf("       detail: \"%s\"\n", error_detail);
        ok = false;
    }
    if (compileForTest("void main(){ blink(3); }\n") ||
        strcmp(error_detail, "blink(number); no function has that name") != 0)
    {
        printf("       detail: \"%s\"\n", error_detail);
        ok = false;
    }
    Error.error = 0;
    error_detail[0] = 0;
    if (ok)
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

static void runManyArgumentsTest()
{
    const char *name = "printfln with more than six values: the 7th on reach the stack; a string as 5th value keeps its address";
    printf("RUNNING: %s\n", name);
    fflush(stdout);
    bool ok = true;
    // 8 values: the 7th and 8th go at a1+0 and a1+4, where the called
    // function reads them (frame-slot promotion used to delete the stores)
    if (!compileForTest("int steps[3];\nvoid main(){ printfln(\"%d %d %d %d %d %d %d\", 1, 2, 3, 4, 5, steps[1], steps[2]); }\n"))
        ok = false;
    else
    {
        bool at0 = false, at4 = false;
        for (int i = 0; i < content.size(); i++)
        {
            const char *l = content.getText(i);
            at0 |= strncmp(l, "s32i", 4) == 0 && strstr(l, ",a1,0") != NULL;
            at4 |= strncmp(l, "s32i", 4) == 0 && strstr(l, ",a1,4") != NULL;
        }
        if (!at0 || !at4)
        {
            printf("       7th/8th values stored at a1+0: %d, a1+4: %d\n", at0, at4);
            ok = false;
        }
        ok = assembleForTest() && ok;
    }
    // a char array as 5th value: parked in a stack slot, reloaded whole
    // (it used to come back through l8ui, one byte of the address)
    if (!compileForTest("int n;\nchar color[12];\nvoid main(){ printfln(\"%d %d %d %s\", n, n, n, color); }\n"))
        ok = false;
    else
    {
        if (hasLine("l8ui"))
        {
            printf("       the string's address is reloaded with l8ui\n");
            ok = false;
        }
        ok = assembleForTest() && ok;
    }
    if (ok)
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
    runSafeModeTest();
    runCompileErrorReportTest();
    runLargeGlobalsTest();
    runPointerIndexTest();
    runStructLayoutTest();
    runStructArgumentsTest();
    runBindStructMacroTest();
    runConstantIndexAndFormatTest();
    runManyArgumentsTest();
    runBuiltinHandleTest();
    runVarargsOrderTest();
    runCharPointerArgumentsTest();
    runSwitchErrorsTest();
    runFfloatConstantTest();
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
    // that path directly -- entryOffset must be at or past 2 reserved
    // slots (handle/setResult) instead of 1.
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


    // `else if` chain, with and without braces, ending in a plain else:
    // i=0 adds 1, i=1,2 add 10 each, i=3,4 add 100 each, i=5 adds 1000.
    // The trailing else used to attach to the first `if` instead of the
    // last `else if` (see the TokenKeywordElse branch of
    // Parser::parseStatement()): the first branch then also ran the final
    // else block and the "nothing matched" case ran nothing. r's slot is
    // stack offset 60 (second declared local, same
    // as `a` in the fibonacci script above).
    static const char *elseIfChainScript =
        "void main(){int i; int r; r=0; for(i=0;i<6;i++){ if(i<1) r=r+1; "
        "else if(i<3) r=r+10; else if(i<5){ r=r+100; } else r=r+1000; } }";
    runCorrectnessTest("else-if chain picks exactly one branch: r == 1221", elseIfChainScript, 60, 1221);
    runEquivalenceTest("optimized/unoptimized equivalence: else-if chain", elseIfChainScript);

    // A multiply by 2/3/5/9 folded into add/addx2/addx4/addx8 inside an
    // `if` condition. The condition is generated after the if's body with
    // the buffer iterator rewound in front of it, and Text::blankCurrent()
    // (which the fold uses to drop the constant's movi) used to free the
    // buffer's *last* line instead of the one at the iterator: here the
    // inner if's `label_.._end:` and main()'s `entry`, so assembly failed
    // with "label not found".
    static const char *foldInConditionScript =
        "void main(){int n; n=5; int k; k=10; if (n*3 == 15) { if (n*5 == 25) k=k+1; } "
        "if (n*9 == 45) k=k+1; }";
    runGenerateBinaryTest("generates executable binary: constant-multiply fold inside an if condition", foldInConditionScript);
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
    runArgumentPassingLoadTest(
        "load a with-argument function and find its one function record (execution itself is QEMU-verified, see test/qemu/)",
        "void main(int n){int a; a=0; int b; b=1; int i; "
        "for (i=0;i<n;i++) { int t; t=a+b; a=b; b=t; } int fib; fib=a;}");
    runArgumentPassingLoadTest(
        "with-argument function containing a string literal keeps args_num",
        "void main(int n){char *s; s=\"hi\"; int a; a=n;}");
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
            "struct point {int x; int y;}; point p; void main(){p.x = 1; p.y = 2;}",
            noError, NULL, false,
        },
        // A trailing ';' after a struct definition -- `struct point
        // {...};`, the natural C habit -- is now *required* (matching v1's
        // sibling projects and ordinary C/C++ struct syntax), not merely
        // tolerated. Omitting it must be caught as an ordinary missing-
        // semicolon parse error, the same way `int a; a = 1}` is caught
        // below, rather than silently accepted or misparsed.
        {
            "struct definition missing its required trailing ';' is a parse error",
            "struct point {int x; int y;} point p; void main(){p.x = 1; p.y = 2;}",
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
