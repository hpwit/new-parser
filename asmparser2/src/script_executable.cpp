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
// true/false and the _handle_/_execaddr_ globals (which pinInterrupt()
// needs, see KeyboardCallback.ino-style scripts) real scripts routinely
// depend on silently existing come from here. parseScript() is meant to
// be the batteries-included entry point (see script_executable.h), so it
// does the same -- without this, `bool b = true;` or
// `pinInterrupt(_execaddr_, "fn", pin);` would fail to compile with a
// confusing "impossible to find variable declaration" for something the
// script never declared itself. One user-visible side effect: parse
// error line numbers are offset by 4 (this prelude's own line count)
// from the user's original script text.
static const char *kPrelude =
    "#define true 1\n"
    "#define false 0\n"
    "uint32_t _handle_;\n"
    "uint32_t _execaddr_;\n";

ScriptExecutable::ScriptExecutable(Binary *bin) : exe(createExecutableFromBinary(bin))
{
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

bool ScriptExecutable::execute(const char *name, int32_t *result)
{
    return callFunction(&exe, name, (const int32_t *)NULL, 0, result);
}

bool ScriptExecutable::execute(const char *name, Arguments *args, int32_t *result)
{
    return callFunction(&exe, name, args, result);
}

bool ScriptExecutable::execute(const char *name, const int32_t *args, int nargs, int32_t *result)
{
    return callFunction(&exe, name, args, nargs, result);
}

ScriptExecutable parseScript(const char *script)
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

    Parser p;
    p.clean();
    p.parse(&s, &__allTokens);

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
        return ScriptExecutable();
    }
    free(buf);

    Binary bin = createBinary(&footer, &header, &content, false);
    if (bin.error.error)
    {
        printf("assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
        freeBinary(&bin);
        return ScriptExecutable();
    }

    // A single, uninterrupted prvalue construction -- see
    // ScriptExecutable's class comment for why this specific shape (not
    // a named local variable then `return that;`) is load-bearing.
    return ScriptExecutable(&bin);
}
