#include "runtime_functions.h"
#include "binding.h"
#include <cstdarg>
#include <cstdio>

// Exactly upstream ESPLiveScript's execute_asm.h artiPrintf()/
// artiPrintfln() -- genuine C varargs functions, called from compiled
// script code through the same "char*,Args" external-call marshaling
// every other Args-typed external uses (see asm_execute.h's callFunction()
// doc comment and visitnode.cpp's _visitCallFunctionTemplate() for how
// that marshaling places arguments). This only works correctly as of the
// fix to asm_parser.cpp's ".bytes" pseudo-op handling (it used to
// silently discard a string literal's actual byte values, keeping only
// its reserved size -- so the format string pointer these receive used
// to point at zeroed memory, not real text).
static void artiPrintf(char const *format, ...)
{
    va_list argp;
    va_start(argp, format);
    vprintf(format, argp);
    va_end(argp);
}

static void artiPrintfln(char const *format, ...)
{
    va_list argp;
    va_start(argp, format);
    vprintf(format, argp);
    printf("\r\n");
    va_end(argp);
}

void registerBuiltinRuntimeFunctions()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    // bindFunction() always appends (binding.cpp has no dedup-by-name
    // check), and findLink() -- what the assembler/loader actually use
    // to resolve a call -- returns the *first* match. So skip a name
    // the caller already bound before Parser::parse() ran (whatever it
    // was bound to, including NULL as a deliberate placeholder, as
    // test_sc_examples.cpp's host test harness does) rather than add a
    // second, permanently-shadowed entry.
    if (findLink((char *)"printf", function) < 0)
        bindFunction((char *)"void", (char *)"printf", (char *)"char*,Args", (void *)artiPrintf);
    if (findLink((char *)"printfln", function) < 0)
        bindFunction((char *)"void", (char *)"printfln", (char *)"char*,Args", (void *)artiPrintfln);
}
