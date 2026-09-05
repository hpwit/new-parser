#include "runtime_functions.h"
#include "binding.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

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

// A script-held uint32_t is just a real heap address, reinterpreted --
// the same trick every other external pointer in this repo already uses
// (an internal global's address, an external array's base). This DSL has
// no pointer-dereference syntax of its own, so writeInt()/readInt() stand
// in for the `*(int*)(buf+offset) = v`/`*(int*)(buf+offset)` a script
// can't write directly. See examples/MemoryAllocator/MemoryAllocator.ino
// for the design this was first prototyped as (bound by hand there via
// bindFunction() before script-author): built in here instead so every
// script gets alloc()/free()/writeInt()/readInt() the same way it
// already gets printf()/printfln(), with no external declaration or
// per-sketch bindFunction() call needed.
static uint32_t artiAlloc(int32_t size)
{
    return (uint32_t)(uintptr_t)malloc((size_t)size);
}
static void artiFree(uint32_t p)
{
    free((void *)(uintptr_t)p);
}
static void artiWriteInt(uint32_t p, int32_t offset, int32_t value)
{
    *(int32_t *)((uint8_t *)(uintptr_t)p + offset) = value;
}
static int32_t artiReadInt(uint32_t p, int32_t offset)
{
    return *(int32_t *)((uint8_t *)(uintptr_t)p + offset);
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
    if (findLink((char *)"alloc", function) < 0)
        bindFunction((char *)"uint32_t", (char *)"alloc", (char *)"int", (void *)artiAlloc);
    if (findLink((char *)"free", function) < 0)
        bindFunction((char *)"void", (char *)"free", (char *)"uint32_t", (void *)artiFree);
    if (findLink((char *)"writeInt", function) < 0)
        bindFunction((char *)"void", (char *)"writeInt", (char *)"uint32_t,int,int", (void *)artiWriteInt);
    if (findLink((char *)"readInt", function) < 0)
        bindFunction((char *)"int", (char *)"readInt", (char *)"uint32_t,int", (void *)artiReadInt);
}
