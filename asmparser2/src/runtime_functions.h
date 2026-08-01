#pragma once
#ifndef __RUNTIME_FUNCTIONS__
#define __RUNTIME_FUNCTIONS__
// Built-in host-side implementations of upstream ESPLiveScript's
// "always available" script functions -- ported from execute_asm.h's
// artiPrintf()/artiPrintfln() and the INIT_PARSER class that
// auto-registers them via addExternalFunction() before any script is
// compiled.
//
// v2 doesn't use a global static object for the registration (unlike
// upstream's `INIT_PARSER initialization_parser;`) -- that pattern
// relies on binded_assets (binding.cpp) already being constructed by
// the time INIT_PARSER's own constructor runs, which C++ doesn't
// actually guarantee across translation units (the "static
// initialization order fiasco"). registerBuiltinRuntimeFunctions()
// instead runs lazily, called from Parser::parse() itself (parser.cpp)
// -- guaranteed safe under the standard, and covers every compile path
// (parseScript() and the low-level Parser/createBinary/
// createExecutableFromBinary pipeline both go through Parser::parse()),
// not just parseScript().
//
// Verified under QEMU with a real, compiled call site (not just that it
// assembles): printfln("i:%d 3*i:%d", a, b) with a=5, b=15 produced the
// exact expected output through this exact registration.
void registerBuiltinRuntimeFunctions();

#endif
