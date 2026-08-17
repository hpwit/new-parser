#include "script_executable.h"
#include "parser.h"
#include "asm_parser.h"
#include "asm_serialize.h"
#include "compiler_error.h"
#include "tokenize.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// v1's real compile entrypoints (Parser::parseScript() et al.) always
// prepend this exact boilerplate before the user's own script text --
// true/false and the _handle_ global (which pinInterrupt() needs, see
// KeyboardCallback.ino-style scripts) real scripts routinely depend on
// silently existing come from here. parseScript() is meant to be the
// batteries-included entry point (see script_executable.h), so it does
// the same -- without this, `bool b = true;` or
// `pinInterrupt(_handle_, "fn", pin);` would fail to compile with a
// confusing "impossible to find variable declaration" for something the
// script never declared itself. parseScript() below counts this
// prelude's own lines and offsets _tokenizer_start_line accordingly, so
// despite living in the same buffer the tokenizer reads, it doesn't
// shift reported parse-error line numbers away from the user's own
// script text.
static const char *kPrelude =
    "#define true 1\n"
    "#define false 0\n"
    "uint32_t _handle_;\n";

ScriptExecutable::ScriptExecutable(Binary *bin) : exe(createExecutableFromBinary(bin))
{
    // Safe to take `this` here despite the class's own copy/move-deletion
    // comment being all about avoiding a *second*, differently-addressed
    // copy existing: C++17's mandatory copy elision guarantees `this` is
    // already the object's final, permanent address (every call site
    // either direct-initializes a fresh local from this constructor's own
    // prvalue, or -- parseScript() -- returns one straight through,
    // itself elided into the caller's local by the same rule). See
    // asm_types.h's `executable::owner` doc comment for what reads this.
    exe.owner = this;
    freeBinary(bin);
    if (exe.error.error)
    {
        printf("loader error: %s\n", exe.error.error_message ? exe.error.error_message : "?");
    }
}

bool ScriptExecutable::isExeExists()
{
    return !exe.error.error && exe.start_program != NULL && exe.functions.size() > 0;
}

// "footer" (matching visitnode.cpp's `.global @__footer` label, stripped
// of the "@__" callFunction()'s own name matching already expects --
// see functionNameMatches(), asm_execute.cpp) may not exist for a given
// script (nothing at its top level needed any). callFunction() simply
// returns false in that case; ignored here on purpose, matching
// upstream's own execute()'s equivalent call, whose result gets
// discarded the same way (execute.h overwrites it with the real call's
// result before ever checking it).
static void runFooterIfAny(executable *exe)
{
    callFunction(exe, "footer", (const int32_t *)NULL, 0, NULL);
}

bool ScriptExecutable::execute(const char *name, int32_t *result)
{
    runFooterIfAny(&exe);
    return callFunction(&exe, name, (const int32_t *)NULL, 0, result);
}

bool ScriptExecutable::execute(const char *name, Arguments *args, int32_t *result)
{
    runFooterIfAny(&exe);
    return callFunction(&exe, name, args, result);
}

bool ScriptExecutable::execute(const char *name, const int32_t *args, int nargs, int32_t *result)
{
    runFooterIfAny(&exe);
    return callFunction(&exe, name, args, nargs, result);
}

bool ScriptExecutable::executeOnly(const char *name, int32_t *result)
{
    return callFunction(&exe, name, (const int32_t *)NULL, 0, result);
}

bool ScriptExecutable::executeOnly(const char *name, Arguments *args, int32_t *result)
{
    return callFunction(&exe, name, args, result);
}

bool ScriptExecutable::executeOnly(const char *name, const int32_t *args, int nargs, int32_t *result)
{
    return callFunction(&exe, name, args, nargs, result);
}

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
namespace
{
struct ExecuteAsTaskParams
{
    ScriptExecutable *self;
    const char *name;
};

void executeAsTaskTrampoline(void *param)
{
    ExecuteAsTaskParams *p = (ExecuteAsTaskParams *)param;
    ScriptExecutable *self = p->self;
    const char *name = p->name;
    delete p;
    self->execute(name);
    vTaskDelete(NULL);
}
} // namespace

bool ScriptExecutable::executeAsTask(const char *name, uint32_t stackSize, UBaseType_t priority, BaseType_t core)
{
    ExecuteAsTaskParams *params = new ExecuteAsTaskParams{this, name};
    TaskHandle_t handle = NULL;
    BaseType_t created = xTaskCreatePinnedToCore(executeAsTaskTrampoline, name, stackSize, params,
                                                  priority, &handle, core);
    if (created != pdPASS)
    {
        delete params;
        return false;
    }
    return true;
}
#endif

// Shared front half of parseScript()/parseScriptToBinary(): prepends
// kPrelude, tokenizes+parses (offsetting reported line numbers past the
// prelude), and assembles via createBinary(). Returns a Binary with
// .error.error set on any failure -- the specific problem (a parse
// error via display_error(), an assembler error via printf) is already
// printed by the time this returns, matching every hand-written
// example's own error handling, so callers just need to check
// .error.error before using the result (and freeBinary() it either way,
// see freeBinary()'s own NULL-safety -- a parse failure returns a
// Binary whose buffers never got allocated).
static Binary compileScriptToBinary(const char *script)
{
    Script s;
    // addContent()/the tokenizer need a mutable buffer they can own for
    // the duration of parsing -- build kPrelude+script fresh rather than
    // require the caller to hand over a non-const, long-lived buffer the
    // way every example's `char script[] = R"EOF(...)EOF";` does. Freed
    // below once parsing (which is also where codegen happens --
    // Parser::parse() walks the whole AST and emits into content/header/
    // footer synchronously before returning) is done with it; nothing
    // downstream (createBinary()/createExecutableFromBinary()) reads the
    // original script text again.
    size_t preludeLen = strlen(kPrelude);
    size_t scriptLen = strlen(script);
    char *buf = (char *)malloc(preludeLen + scriptLen + 1);
    memcpy(buf, kPrelude, preludeLen);
    memcpy(buf + preludeLen, script, scriptLen + 1); // +1 copies script's own NUL
    s.addContent(buf);
    s.init();

    // kPrelude precedes the user's own script text in the buffer the
    // tokenizer actually reads, so without this, every reported error
    // line would be offset by kPrelude's own line count -- counted here
    // (not hardcoded) so it stays correct if kPrelude's content ever
    // changes. See tokenize.h's _tokenizer_start_line comment: starting
    // the line counter at 1 minus that count makes it land on exactly 1
    // right as tokenizing crosses into the user's real script.
    int preludeLines = 0;
    for (const char *p = kPrelude; *p != 0; p++)
        if (*p == '\n')
            preludeLines++;
    _tokenizer_start_line = 1 - preludeLines;

    Parser p;
    p.clean();
    p.parse(&s, &__allTokens);
    _tokenizer_start_line = 1;

    if (Error.error)
    {
        // display_error() reads the offending line back out of the
        // original script text (Token::lineref points into it, never
        // copied out the way a successfully-parsed token's text is via
        // getText()'s Text-pool copies) -- buf must still be alive here,
        // confirmed the hard way via AddressSanitizer catching a heap-
        // use-after-free from an earlier version of this function that
        // freed buf unconditionally right after parse() returned.
        display_error(&Error);
        free(buf);
        Binary bin;
        bin.error.error = 1;
        bin.error.error_message = NULL;
        return bin;
    }
    free(buf);

    Binary bin = createBinary(&footer, &header, &content, false);
    if (bin.error.error)
    {
        printf("assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
    }
    return bin;
}

ScriptExecutable parseScript(const char *script)
{
    Binary bin = compileScriptToBinary(script);
    if (bin.error.error)
    {
        freeBinary(&bin);
        return ScriptExecutable();
    }

    // A single, uninterrupted prvalue construction -- see
    // ScriptExecutable's class comment for why this specific shape (not
    // a named local variable then `return that;`) is load-bearing.
    return ScriptExecutable(&bin);
}

uint8_t *parseScriptToBinary(const char *script, uint32_t *size)
{
    Binary bin = compileScriptToBinary(script);
    if (bin.error.error)
    {
        freeBinary(&bin);
        if (size != NULL)
        {
            *size = 0;
        }
        return NULL;
    }

    uint8_t *serialized = serializeBinary(&bin, size);
    freeBinary(&bin);
    return serialized;
}

ScriptExecutable createExecutableFromBuffer(const uint8_t *buf, uint32_t size)
{
    Binary bin = deserializeBinary(buf, size);
    if (bin.error.error)
    {
        printf("deserializeBinary error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
        // createExecutableFromBinary() doesn't validate bin->error.error
        // itself (it unconditionally uses bin->instruction_size/
        // binary_data/function_data, trusting a well-formed Binary) --
        // so a deserialize failure has to short-circuit here, matching
        // parseScript()'s own error path (compileScriptToBinary()'s
        // failure case) instead of ever handing a bad Binary off to it.
        // No freeBinary() needed here -- deserializeBinary() only ever
        // fails before allocating binary_data/function_data, leaving
        // both still NULL.
        return ScriptExecutable();
    }

    // A single, uninterrupted prvalue construction -- see
    // ScriptExecutable's class comment for why this specific shape (not
    // a named local variable then `return that;`) is load-bearing.
    // ScriptExecutable's own constructor calls createExecutableFromBinary(),
    // freeBinary()s bin, and prints any loader error -- same as
    // parseScript()'s equivalent call.
    return ScriptExecutable(&bin);
}
