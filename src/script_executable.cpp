#include "script_executable.h"
#include "parser.h"
#include "asm_parser.h"
#include "asm_serialize.h"
#include "compiler_error.h"
#include "tokenize.h"
#include "json_binding.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// v1's real compile entrypoints (Parser::parseScript() et al.) always
// prepend boilerplate before the user's own script text, and real
// scripts routinely depend on what it defines silently existing.
// parseScript() is meant to be the batteries-included entry point (see
// script_executable.h), so it does the same -- without this,
// `bool b = true;` would fail to compile with a confusing "impossible
// to find variable declaration" for something the script never
// declared itself. (_handle_, the other thing v1's boilerplate
// declared, is declared by Parser::parse() itself, for every script.)
// parseScript() below counts this prelude's own lines and offsets
// _tokenizer_start_line accordingly, so despite living in the same
// buffer the tokenizer reads, it doesn't shift reported parse-error
// line numbers away from the user's own script text.
static const char *kPrelude =
    "#define true 1\n"
    "#define false 0\n";

static char g_lastLoadError[240];

const char *lastLoadError()
{
    return g_lastLoadError;
}

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
    // runtime_functions.cpp's safeModeError() treats a script's _handle_
    // (this object's address) as the address of its `executable`.
    if ((void *)this != (void *)&exe)
    {
        printf("ScriptExecutable layout changed: exe must stay its first member\n");
        abort();
    }
    freeBinary(bin);
    if (exe.error.error)
    {
        snprintf(g_lastLoadError, sizeof(g_lastLoadError), "%s",
                 exe.error.error_message ? exe.error.error_message : "loader error");
        printf("loader error: %s\n", g_lastLoadError);
    }
    else
    {
        g_lastLoadError[0] = '\0';
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

bool ScriptExecutable::updateJson(const char *json)
{
    asm_error_message_struct err = updateJsonParameters(&exe, json);
    if (err.error)
        printf("json: %s\n", err.error_message ? err.error_message : "?");
    return !err.error;
}

bool ScriptExecutable::execute(const char *name, const char *json, int32_t *result)
{
    runFooterIfAny(&exe); // initialization first: the JSON values win
    if (!updateJson(json))
        return false;
    return callFunction(&exe, name, (const int32_t *)NULL, 0, result);
}

bool ScriptExecutable::executeOnly(const char *name, const char *json, int32_t *result)
{
    if (!updateJson(json))
        return false;
    return callFunction(&exe, name, (const int32_t *)NULL, 0, result);
}

ScriptExecutable::~ScriptExecutable()
{
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
    kill();
#endif
    freeExecutable(&exe);
}

void ScriptExecutable::free()
{
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
    kill();
#endif
    freeExecutable(&exe);
}

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
namespace
{
struct ExecuteAsTaskParams
{
    ScriptExecutable *self;
    const char *name;
    int nargs;        // parameters for `name`, 0 = none
    int32_t args[6];  // ints, or floats as their bit pattern (a10..a15)
};
} // namespace

void ScriptExecutable::taskMain(void *param)
{
    ExecuteAsTaskParams *p = (ExecuteAsTaskParams *)param;
    ScriptExecutable *self = p->self;
    const char *name = p->name;
    int32_t args[6];
    const int nargs = p->nargs;
    memcpy(args, p->args, sizeof(args));
    delete p;
    if (nargs > 0)
        self->execute(name, args, nargs);
    else
        self->execute(name);

    // Returned on its own. Record it, then wait to be deleted by kill(),
    // the next executeAsTask() or the destructor (see taskHandle). If
    // kill() got here first, it is already deleting this task.
    portENTER_CRITICAL(&self->taskLock);
    self->taskState = ScriptState::stopped;
    portEXIT_CRITICAL(&self->taskLock);
    while (true)
        vTaskSuspend(NULL);
}

bool ScriptExecutable::executeAsTask(const char *name, uint32_t stackSize, UBaseType_t priority, BaseType_t core,
                                     const char *taskName)
{
    return executeAsTask(name, (const int32_t *)NULL, 0, stackSize, priority, core, taskName);
}

bool ScriptExecutable::executeAsTask(const char *name, Arguments *args, uint32_t stackSize,
                                     UBaseType_t priority, BaseType_t core, const char *taskName)
{
    // Same conversion as callFunction(): a float travels in an integer
    // register as its raw bits.
    int32_t raw[6];
    int n = args != NULL ? args->size() : 0;
    if (n > 6)
        n = 6;
    for (int i = 0; i < n; i++)
    {
        _arguments a = args->_args.get(i);
        if (a.vartype == __float__)
            memcpy(&raw[i], &a.floatval, sizeof(int32_t));
        else
            raw[i] = a.intval;
    }
    return executeAsTask(name, raw, n, stackSize, priority, core, taskName);
}

bool ScriptExecutable::executeAsTask(const char *name, const int32_t *args, int nargs, uint32_t stackSize,
                                     UBaseType_t priority, BaseType_t core, const char *taskName)
{
    kill(); // a previous task of this script, running, paused or finished
    ScriptState before = taskState;

    ExecuteAsTaskParams *params = new ExecuteAsTaskParams{this, name, 0, {0}};
    if (args != NULL && nargs > 0)
    {
        params->nargs = nargs > 6 ? 6 : nargs;
        memcpy(params->args, args, sizeof(int32_t) * (size_t)params->nargs);
    }
    // Set before the task can run: xTaskCreatePinnedToCore() stores the
    // handle before the task is made ready.
    taskState = ScriptState::running;
    BaseType_t created = xTaskCreatePinnedToCore(taskMain, taskName != NULL ? taskName : name, stackSize,
                                                  params, priority, &taskHandle, core);
    if (created != pdPASS)
    {
        delete params;
        taskHandle = NULL;
        taskState = before;
        return false;
    }
    return true;
}

bool ScriptExecutable::pause()
{
    TaskHandle_t task = NULL;
    portENTER_CRITICAL(&taskLock);
    if (taskState == ScriptState::running)
    {
        task = taskHandle;
        taskState = ScriptState::paused;
    }
    portEXIT_CRITICAL(&taskLock);
    if (task == NULL)
        return false;
    vTaskSuspend(task);
    return true;
}

bool ScriptExecutable::restart()
{
    TaskHandle_t task = NULL;
    portENTER_CRITICAL(&taskLock);
    if (taskState == ScriptState::paused)
    {
        task = taskHandle;
        taskState = ScriptState::running;
    }
    portEXIT_CRITICAL(&taskLock);
    if (task == NULL)
        return false;
    vTaskResume(task);
    return true;
}

bool ScriptExecutable::kill()
{
    portENTER_CRITICAL(&taskLock);
    TaskHandle_t task = taskHandle;
    bool active = taskState == ScriptState::running || taskState == ScriptState::paused;
    taskHandle = NULL;
    if (task != NULL)
        taskState = ScriptState::stopped;
    portEXIT_CRITICAL(&taskLock);
    if (task == NULL)
        return false;
    // enterScript() (asm_execute.cpp) left its unwind point for a
    // safe_mode error on the task's stack, which is going away.
    exe.runtimeUnwind = NULL;
    vTaskDelete(task); // does not return if the task is the caller
    return active;
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
static CompileError g_lastCompileError;

const CompileError &lastCompileError()
{
    return g_lastCompileError;
}

void showCompileError()
{
    const CompileError &e = g_lastCompileError;
    if (!e.failed)
        return;
    printf("error   : %s\n", e.message);
    if (e.line > 0)
    {
        printf("line %-3d: %s\n", e.line, e.sourceLine);
        // a caret under the position (both count from 1)
        printf("          ");
        for (int i = 1; i < e.position; i++)
            printf(" ");
        printf("^\n");
    }
}

static Binary compileScriptToBinary(const char *script)
{
    memset(&g_lastCompileError, 0, sizeof(g_lastCompileError));
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
        // Copied out now: the token's line points into buf.
        CompileError &e = g_lastCompileError;
        e.failed = true;
        snprintf(e.message, sizeof(e.message), "%s", error_messages[Error.error]);
        size_t ml = strlen(e.message);
        while (ml > 0 && e.message[ml - 1] == ' ')
            e.message[--ml] = 0;
        if (error_detail[0] != 0)
            snprintf(e.message + ml, sizeof(e.message) - ml, ": %s", error_detail);
        if (Error.token != NULL)
        {
            e.line = Error.token->line;
            e.position = Error.token->pos;
            if (Error.token->lineref != NULL)
            {
                size_t n = strcspn(Error.token->lineref, "\n");
                if (n >= sizeof(e.sourceLine))
                    n = sizeof(e.sourceLine) - 1;
                memcpy(e.sourceLine, Error.token->lineref, n);
                e.sourceLine[n] = 0;
            }
        }
        free(buf);
        // The compiler's state is kept after a failed compile (unlike after a
        // good one, see below): Error.token points into it, and callers read
        // the error's line and position from there. The next compile frees it.
        Binary bin;
        bin.error.error = 1;
        bin.error.error_message = NULL;
        return bin;
    }
    free(buf);

    Binary bin = createBinary(&footer, &header, &content, false);
    // The parse tree, the tokens and their text, the functions and types:
    // the binary has its own copies of everything it needs. Freed here, they
    // used to stay until the next compile -- ~10 KB in ~500 small blocks
    // after Tetris, ~20 KB in ~1000 after a 1200-line script, scattered
    // through the heap, so that the next large block (a script's code and
    // data, which must be in one piece) no longer found room.
    p.clean();
    if (bin.error.error)
    {
        printf("assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
        g_lastCompileError.failed = true;
        snprintf(g_lastCompileError.message, sizeof(g_lastCompileError.message), "assembler error: %s",
                 bin.error.error_message ? bin.error.error_message : "?");
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
        snprintf(g_lastLoadError, sizeof(g_lastLoadError), "not a valid binary: %s",
                 bin.error.error_message ? bin.error.error_message : "?");
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
