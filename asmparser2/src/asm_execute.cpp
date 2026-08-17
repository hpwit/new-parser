#include "asm_execute.h"
#include "binding.h"
#include "string_functions.h"
#include "binary_hex.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#define ASM_PARSER_DEBUG
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include "esp_heap_caps.h"
// xthal_dcache_region_writeback()/xthal_icache_region_invalidate(): the
// Xtensa HAL's own cache-maintenance primitives (part of the toolchain's
// Xtensa HAL, not an ESP-IDF-version-specific wrapper -- stable across
// ESP-IDF/Arduino-ESP32 releases). Needed below, after memcpy()-ing
// freshly compiled code into exec (heap_caps_malloc(..., MALLOC_CAP_EXEC)
// memory): a plain memcpy only guarantees the *data* side sees the new
// bytes, not that the CPU's instruction fetch path does too -- Xtensa's
// D-cache/I-cache (and, for a loop like a struct array's constructor
// calls, potentially loop/branch-prediction state keyed off the old
// content that used to live at this same heap address) aren't
// automatically kept coherent with plain stores the way x86 is.
#include "xtensa/hal.h"
#define ASM_EXEC_ON_TARGET 1
#else
#define ASM_EXEC_ON_TARGET 0
#endif

#ifdef ASM_PARSER_DEBUG
// ASM_PARSER_DEBUG is opt-in, defined by the caller's own build (same
// convention as json_binding.h's __JSON_OPTION__) -- a plain #define in
// a sketch's .ino won't reach this file (Arduino compiles each .cpp as
// its own translation unit), so it needs to be a project-wide build flag
// (e.g. platformio.ini's build_flags, or a boards.txt/board_build.flags
// entry) to actually take effect here.
#if ASM_EXEC_ON_TARGET
#include "esp_timer.h"
// esp_timer_get_time(): ESP-IDF's own microsecond-resolution monotonic
// clock, not Arduino's micros() -- this file already talks to ESP-IDF
// directly elsewhere (heap_caps_malloc, xthal_*), and using it here
// avoids adding an Arduino.h dependency just for timing.
static inline int64_t debugNowMicros() { return esp_timer_get_time(); }
#else
#include <chrono>
static inline int64_t debugNowMicros()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
#endif
#endif

// Walks a Binary's relocation header (createBinaryHeader's output) and
// patches every reference it describes:
//   type 0 (data_label/number_label): an internal constant/scratch word --
//     patch the jump-table slot at word index nb_data with data_base+bincode.
//   type 1 (external_var_label): patch with the bound host variable's
//     address, found via binding.h's findLink() using binded_assets
//     (matching bindVariable/an `external` variable declaration). The
//     label text is "@_ext_NAME"; findLink wants the bare name.
//   type 2 (external_call): same, but for a bound function's address. The
//     label text is "@_NAME(sig)"; findLink already strips from '(' on.
//   type 3 (data): copies embedded literal bytes (e.g. string constants)
//     from the temporary post-instruction area into the real data buffer.
//   type 4 (function_declaration): records a callable entry point.
static asm_error_message_struct decodeBinaryHeader(uint8_t *exec, uint8_t *binary_header, uint8_t *_binary_data,
                                                     uint32_t /*exec_base*/, executable *finalexe, int offset)
{
    asm_error_message_struct error;
    error.error = 0;
    error.error_message = NULL;

    uint16_t nb_data = 0, tmp_data = 0, nb_objects = 0;
    uint8_t type;
    uint16_t text_size, size;
    uint32_t bincode, addr;
    char *textptr;

    uint32_t data_base = (uint32_t)(uintptr_t)_binary_data;

    memcpy(&nb_objects, binary_header, 2);
    binary_header += 2;

    for (int i = 0; i < nb_objects; i++)
    {
        memcpy(&type, binary_header, 1);
        binary_header += 1;

        switch (type)
        {
        case 0:
        {
            memcpy(&bincode, binary_header, 4);
            binary_header += 4;
            memcpy(&nb_data, binary_header, 2);
            binary_header += 2;
            uint32_t content = bincode + data_base;
            memcpy((uint32_t *)exec + nb_data, &content, 4);
            break;
        }
        case 1:
        {
            memcpy(&text_size, binary_header, 2);
            binary_header += 2;
            textptr = (char *)binary_header;
            binary_header += text_size;
            memcpy(&nb_data, binary_header, 2);
            binary_header += 2;
            int index = findLink(textptr + 6, value); // strip "@_ext_"
            if (index > -1)
            {
                uint32_t content = (uint32_t)(uintptr_t)binded_assets.get(index).ptr;
                memcpy((uint32_t *)exec + nb_data, &content, 4);
            }
            else
            {
                error.error = 1;
                error.error_message = string_format("external variable %s not found\n", textptr);
            }
            break;
        }
        case 2:
        {
            memcpy(&text_size, binary_header, 2);
            binary_header += 2;
            textptr = (char *)binary_header;
            binary_header += text_size;
            memcpy(&nb_data, binary_header, 2);
            binary_header += 2;
            int index = findLink(textptr + 2, function); // strip "@_"
            if (index > -1)
            {
                uint32_t content = (uint32_t)(uintptr_t)binded_assets.get(index).ptr;
                memcpy((uint32_t *)exec + nb_data, &content, 4);
            }
            else
            {
                error.error = 1;
                error.error_message = string_format("external function %s not found\n", textptr);
            }
            break;
        }
        case 3:
        {
            memcpy(&addr, binary_header, 4);
            binary_header += 4;
            memcpy(&tmp_data, binary_header, 2);
            binary_header += 2;
            memcpy(&size, binary_header, 2);
            binary_header += 2;
            memcpy(_binary_data + addr, exec + offset + tmp_data, size);
            break;
        }
        case 4:
        {
            globalcall gc;
            memcpy(&text_size, binary_header, 2);
            binary_header += 2;
            textptr = (char *)binary_header;
            binary_header += text_size;
            gc.name = string_format("%s", textptr);
            memcpy(&text_size, binary_header, 2);
            binary_header += 2;
            textptr = (char *)binary_header;
            binary_header += text_size;
            gc.variables = string_format("%s", textptr);
            memcpy(&text_size, binary_header, 2);
            binary_header += 2;
            gc.args_num = text_size;
            memcpy(&addr, binary_header, 4);
            binary_header += 4;
            gc.address = addr;
            memcpy(&addr, binary_header, 4);
            binary_header += 4;
            gc.variableaddress = addr;
            finalexe->functions.push_back(gc);
            break;
        }
        case 5:
        {
            // A `json "path" as <type> name;` binding -- see asm_parser.cpp's
            // createBinaryHeader() json_binding case for the matching
            // encode and asm_types.h's jsonVariable for why this doesn't
            // need __JSON_OPTION__/ArduinoJson itself.
            jsonVariable jv;
            memcpy(&text_size, binary_header, 2);
            binary_header += 2;
            textptr = (char *)binary_header;
            binary_header += text_size;
            jv.json = string_format("%s", textptr);
            memcpy(&addr, binary_header, 4);
            binary_header += 4;
            jv.address = addr;
            jv.type = *binary_header;
            binary_header += 1;
            finalexe->jsonVars.push_back(jv);
            break;
        }
        default:
            error.error = 1;
            error.error_message = (char *)"Unknown type binary header invalid";
            return error;
        }
    }
    return error;
}

executable createExecutableFromBinary(Binary *bin)
{
    executable exe;
    exe.error.error = 0;

#if ASM_EXEC_ON_TARGET
    uint32_t *exec = (uint32_t *)heap_caps_malloc(bin->instruction_size, MALLOC_CAP_EXEC);
#else
    uint32_t *exec = (uint32_t *)malloc(bin->instruction_size);
#endif
    uint8_t *binary_data = (uint8_t *)malloc(bin->data_size > 0 ? bin->data_size : 4);

    // Neither allocation's result used to be checked before being handed
    // to decodeBinaryHeader()/memcpy() below -- harmless on host, where
    // a script this size never comes close to exhausting memory, but a
    // real crash on target: confirmed via a decoded on-device backtrace
    // landing right on this function's own memcpy(exec, ...) with
    // EXCVADDR/A2 both 0x0 (a NULL destination) -- MALLOC_CAP_EXEC
    // memory (must be IRAM-eligible, executable) is a far scarcer pool
    // than general RAM, especially on an ESP32-S3 without PSRAM, and
    // heap_caps_malloc() legitimately returns NULL rather than crashing
    // itself when it can't find a large enough contiguous block. Turn
    // that into the same clean "loader error" ScriptExecutable's
    // constructor already knows how to report, instead of a silent
    // NULL-pointer store.
    if (exec == NULL || binary_data == NULL)
    {
        exe.error.error = 1;
        exe.error.error_message = (char *)"could not allocate executable/data memory for this script "
                                           "(out of MALLOC_CAP_EXEC or general heap)";
#if ASM_EXEC_ON_TARGET
        if (exec != NULL)
            heap_caps_free(exec);
#else
        if (exec != NULL)
            free(exec);
#endif
        if (binary_data != NULL)
            free(binary_data);
        return exe;
    }

    asm_error_message_struct error = decodeBinaryHeader(
        bin->binary_data, bin->function_data, binary_data,
        (uint32_t)(uintptr_t)exec, &exe, bin->instruction_size);

    if (exe.functions.size() == 0)
    {
        exe.error.error = 1;
        exe.error.error_message = (char *)"No global start function found";
#if ASM_EXEC_ON_TARGET
        heap_caps_free(exec);
#else
        free(exec);
#endif
        free(binary_data);
        return exe;
    }
    if (error.error == 1)
    {
        exe.error = error;
#if ASM_EXEC_ON_TARGET
        heap_caps_free(exec);
#else
        free(exec);
#endif
        free(binary_data);
        return exe;
    }

    memcpy(exec, bin->binary_data, bin->instruction_size);

#if ASM_EXEC_ON_TARGET
    // Without this, a script that runs cleanly the first time (or that
    // never got called through this exact heap_caps_malloc() address
    // before) can still fault later, unpredictably, on a *different*
    // script loaded at the same now-reused address, or partway through
    // its own execution once instruction fetch outruns what the data
    // side actually flushed to memory -- a real, reproduced-on-hardware
    // crash (Guru Meditation Error: InstrFetchProhibited/wild jump) that
    // never shows up under QEMU (its simple TCG-based CPU emulation
    // doesn't model instruction/data cache incoherency for freshly
    // written code the way real Xtensa silicon does), confirmed by
    // comparing execution of identical compiled bytes from a
    // statically-linked .text location (fine) against a copy freshly
    // written into a heap buffer immediately before being called (not).
    xthal_dcache_region_writeback(exec, bin->instruction_size);
    xthal_icache_region_invalidate(exec, bin->instruction_size);
#endif

    exe.start_program = exec;
    exe.data = binary_data;
    exe.binary_size = bin->instruction_size;
    exe.data_size = bin->data_size;
   // printExecutableHex(&exe);
   // printExecutableFunctions(&exe);
    return exe;
}

// A globalcall's name is the raw label text -- "@_fib(d)" (plain) or
// "@__fib(d)" (wrapper), or "@_main()"/"@__main()" for a no-argument
// function. Strips the "@_" or "@__" prefix and "(...)" suffix and
// compares what's left against `name`.
static bool functionNameMatches(const char *label, const char *name)
{
    const char *p = label;
    if (p[0] == '@' && p[1] == '_')
    {
        p += 2;
        if (p[0] == '_')
            p += 1;
    }
    const char *paren = strchr(p, '(');
    size_t len = paren ? (size_t)(paren - p) : strlen(p);
    return strlen(name) == len && strncmp(p, name, len) == 0;
}

// Same "@_"/"@__" prefix + "(...)" suffix strip as functionNameMatches(),
// but returning the bare name (into `out`, capped at outSize-1 bytes)
// instead of comparing it -- for display purposes only.
static void cleanFunctionName(const char *label, char *out, size_t outSize)
{
    const char *p = label;
    if (p[0] == '@' && p[1] == '_')
    {
        p += 2;
        if (p[0] == '_')
            p += 1;
    }
    const char *paren = strchr(p, '(');
    size_t len = paren ? (size_t)(paren - p) : strlen(p);
    if (len > outSize - 1)
        len = outSize - 1;
    memcpy(out, p, len);
    out[len] = 0;
}

// A function declared with N>0 parameters used to get a *wrapper* record
// (variables field "N size1 size2 ...", digit-led) alongside its *plain*
// record (variables field just the wrapper's own raw label text, '@'-led
// -- an upstream quirk, see asm_execute.h's callFunction doc comment),
// distinguished by that leading-digit check. visitnode.cpp's
// _visitdefFunctionNode() no longer emits the wrapper's own ".global
// @__NAME" declaration or its marshaling body at all (see its git
// history), so every function -- argument-taking or not -- now gets
// exactly one record, always the real, callable one: there is currently
// no code path that can produce a genuine wrapper record for this to
// distinguish. Always false rather than actually inspecting `variables`
// so a record's own real argument-size descriptor (restored so
// args_num/variables decode correctly again -- see
// _visitdefFunctionNode()'s own ".var" comment) can't be misread as "this
// is a wrapper" and cause the one real record to be skipped/never found.
// If the wrapper mechanism is ever restored, this needs restoring (or
// replacing with something that doesn't infer wrapper-vs-plain from
// `variables`' own content) alongside it.
static bool isWrapperRecord(globalcall *gc)
{
    (void)gc;
    return false;
}

void printExecutableFunctions(executable *ex)
{
    if (ex == NULL || ex->functions.size() == 0)
    {
        printf("(no functions)\n");
        return;
    }
    for (int i = 0; i < ex->functions.size(); i++)
    {
        globalcall *gc = ex->functions.getptr(i);
        if (isWrapperRecord(gc))
            continue;
        char name[64];
        cleanFunctionName(gc->name, name, sizeof(name));
        printf("%s: offset=0x%04x  start=0x%04x (%u)\n", name, gc->address,
               (uint32_t)(uintptr_t)ex->start_program, gc->address);
    }
}

void printExecutableHex(executable *ex)
{
    if (ex == NULL || ex->start_program == NULL)
        return;

    // start_program is real MALLOC_CAP_EXEC/IRAM memory on-target --
    // printHexWords() reads it a 32-bit word at a time, since a plain
    // byte load from IRAM faults on real Xtensa/ESP32 silicon (see
    // binary_hex.h). ex->data, in contrast, is plain malloc()'d RAM
    // (createExecutableFromBinary() never puts it in MALLOC_CAP_EXEC), so
    // ordinary byte access via printHex() is fine there.
    printf("instructions (%u bytes):\n", ex->binary_size);
    printHexWords(ex->start_program, ex->binary_size);

    printf("data (%u bytes):\n", ex->data_size);
    printHex(ex->data, ex->data_size);
}

// Places up to 6 int32/float(-as-bits) values into a10..a15 and calls
// `entry` via callx8 -- the same registers (and the same a10 = first
// argument mapping to the callee's a2) a compiled call8 caller like the
// script's own main() uses, matching the plain (non-wrapper) record's
// calling convention exactly. Returns whatever ends up in a10 after the
// call, i.e. the standard Xtensa windowed-ABI return-value register --
// also where the script's `return expr;` leaves its value (see
// visitnode.cpp). A no-op returning 0 when not actually built for
// Xtensa, since these register names aren't valid on any other target.
static int32_t callXtensaDirect(void *entry, const int32_t *args, int nargs)  
{
   // printf("callXtensaDirect: calling entry=%p\n", entry);
    
#ifdef __xtensa__
    register int32_t r10 __asm__("a10") = nargs > 0 ? args[0] : 0;
    register int32_t r11 __asm__("a11") = nargs > 1 ? args[1] : 0;
    register int32_t r12 __asm__("a12") = nargs > 2 ? args[2] : 0;
    register int32_t r13 __asm__("a13") = nargs > 3 ? args[3] : 0;
    register int32_t r14 __asm__("a14") = nargs > 4 ? args[4] : 0;
    register int32_t r15 __asm__("a15") = nargs > 5 ? args[5] : 0;
    register void *r8 __asm__("a8") = entry;
    __asm__ volatile("callx8 a8\n\t"
                      : "+r"(r10)
                      : "r"(r11), "r"(r12), "r"(r13), "r" (r14), "r"(r15),"r"(r8)
                      : "memory");
    return r10;
#else
    (void)entry;
    (void)args;
    (void)nargs;
    return 0;
#endif

}

// Converts a typed Arguments list into the raw int32_t[] the register-
// passing calling convention actually uses -- a float's value is its raw
// bit pattern reinterpreted as int32_t (see visitnode.cpp's
// _visitdefInputArgumentsNode: a float parameter arrives in the same
// integer register an int one would, stored with the same s32i
// instruction, so there's nothing more to it than the reinterpret).
// Caps at 6 slots (a10..a15); anything past that is silently dropped,
// same as the raw-array overloads' own hardware limit.
static int marshalArguments(Arguments *args, int32_t out[6])
{
    int n = args->size();
    if (n > 6)
        n = 6;
    for (int i = 0; i < n; i++)
    {
        _arguments a = args->_args.get(i);
        out[i] = (a.vartype == __float__) ? *(int32_t *)&a.floatval : a.intval;
    }
    return n;
}

// _handle_ scratch word some scripts (pinInterrupt()-style re-entry, see
// README's "Pointer to the executable, and interrupts") expect at the
// start of the data region. v1's executeBinary() (execute_asm.h) wrote
// two separate words here -- `handle`, a small integer index into its
// multi-program task registry (a feature v2 doesn't have, see README's
// "Known limitations"), and `exePtr`, the caller's own running-script
// controller object (`this`) -- but only ever read the first one back
// (functionlib.h's sync()/syncExt() __ASM__ helpers both only reference
// `_handle_`, never v1's second word), so v2 collapses them into this one
// word instead of carrying the unused duplicate forward. It gets
// `ex->owner` (set by ScriptExecutable's constructor, see asm_types.h's
// doc comment on it) when one exists, falling back to the executable's
// own address for the raw parse -> createBinary() ->
// createExecutableFromBinary() pipeline (no ScriptExecutable wrapper
// involved, e.g. LanguageBasics.ino). Shared by every call path that
// actually enters compiled code (callFunction() and
// runExecutableWithArgs()).
static void fillHandleWord(executable *ex)
{
    if (ex->data != NULL && ex->data_size >= 4)
    {
        uint32_t *t = (uint32_t *)ex->data;
        t[0] = (uint32_t)(uintptr_t)(ex->owner != NULL ? ex->owner : (void *)ex);
    }
}

bool callFunction(executable *ex, const char *name, Arguments *args, int32_t *result)
{
    int32_t raw[6];
    int n = marshalArguments(args, raw);
    return callFunction(ex, name, raw, n, result);
}

bool callFunction(executable *ex, const char *name, const int32_t *args, int nargs, int32_t *result)
{
    if (ex->functions.size() == 0 || ex->start_program == NULL)
        return false;

    globalcall *target = NULL;
    for (int i = 0; i < ex->functions.size(); i++)
    {
        globalcall *gc = ex->functions.getptr(i);
        if (!isWrapperRecord(gc) && functionNameMatches(gc->name, name))
        {
            target = gc;
            break;
        }
    }
    if (target == NULL)
        return false;

    fillHandleWord(ex);

    // target->address is a byte offset from the start of start_program
    // (see the type-4 case in decodeBinaryHeader).
    uint8_t *entry = (uint8_t *)ex->start_program + target->address;
#ifdef ASM_PARSER_DEBUG
    int64_t __debugStart = debugNowMicros();
#endif
    int32_t r = callXtensaDirect(entry, args, nargs);
#ifdef ASM_PARSER_DEBUG
    printf("ASM_PARSER_DEBUG: %s() took %lld us\n", name, (long long)(debugNowMicros() - __debugStart));
#endif
    if (result != NULL)
        *result = r;
    return true;
}

bool runExecutableWithArgs(executable *ex, Arguments *args)
{
    int32_t raw[6];
    int n = marshalArguments(args, raw);
    return runExecutableWithArgs(ex, raw, n);
}

bool runExecutableWithArgs(executable *ex, const int32_t *args, int nargs)
{
    // main() is always called through its wrapper (matching upstream's
    // own execute_asm.h convention for the program's entry point), not
    // callFunction()'s direct-register path -- arguments are marshaled
    // through the reserved stack-storage area the wrapper reads from,
    // same as before.
    if (ex->functions.size() == 0 || ex->start_program == NULL)
        return false;

    globalcall *target = NULL;
    for (int i = 0; i < ex->functions.size(); i++)
    {
        globalcall *gc = ex->functions.getptr(i);
        if (gc->args_num == nargs && functionNameMatches(gc->name, "main"))
        {
            target = gc;
            break;
        }
    }
    if (target == NULL)
        return false;

    fillHandleWord(ex);

    if (nargs > 0 && args != NULL && ex->data != NULL)
        memcpy(ex->data + target->variableaddress, args, (size_t)nargs * 4);

    uint8_t *entry = (uint8_t *)ex->start_program + target->address;
    void (*fn)() = (void (*)())(void *)entry;
    printf("runExecutableWithArgs: calling entry=%p\n", (void *)entry);
    fn();
    return true;
}

void runExecutable(executable *ex)
{
    runExecutableWithArgs(ex, NULL, 0);
}

void freeExecutable(executable *ex)
{
    if (ex->start_program != NULL)
    {
#if ASM_EXEC_ON_TARGET
        heap_caps_free(ex->start_program);
#else
        free(ex->start_program);
#endif
        ex->start_program = NULL;
    }
    if (ex->data != NULL)
    {
        free(ex->data);
        ex->data = NULL;
    }
    for (int i = 0; i < ex->functions.size(); i++)
    {
        free(ex->functions.get(i).name);
        free(ex->functions.get(i).variables);
    }
    ex->functions.clear();
}
