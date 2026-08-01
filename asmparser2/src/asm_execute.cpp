#include "asm_execute.h"
#include "binding.h"
#include "string_functions.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include "esp_heap_caps.h"
#define ASM_EXEC_ON_TARGET 1
#else
#define ASM_EXEC_ON_TARGET 0
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

    exe.start_program = exec;
    exe.data = binary_data;
    exe.binary_size = bin->instruction_size;
    exe.data_size = bin->data_size;

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

// A function declared with N>0 parameters gets a *wrapper* record whose
// `variables` field is "N size1 size2 ..." (createBinaryHeader reads it
// from the ".var N size..." line) and a *plain* record -- the real
// function body's own address -- whose `variables` field is, due to an
// upstream quirk (see asm_execute.h's callFunction doc comment), just
// the other record's raw label text, which always starts with '@'. A
// 0-argument function has no wrapper at all -- both header records
// alias the same address, so "is this a wrapper" is moot for it, and
// this correctly falls through to treating it as the plain record too.
static bool isWrapperRecord(globalcall *gc)
{
    return gc->variables != NULL && gc->variables[0] >= '0' && gc->variables[0] <= '9';
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
                      : "r"(r11), "r"(r12), "r"(r13), "r"(r14), "r"(r15), "r"(r8)
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

    // target->address is a byte offset from the start of start_program
    // (see the type-4 case in decodeBinaryHeader).
    uint8_t *entry = (uint8_t *)ex->start_program + target->address;
    int32_t r = callXtensaDirect(entry, args, nargs);
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

    // handle/execaddr scratch words some scripts' sync() expects at the
    // start of the data region -- written for parity even though none of
    // this library's examples call sync() yet.
    if (ex->data != NULL && ex->data_size >= 8)
    {
        uint32_t *t = (uint32_t *)ex->data;
        t[0] = 0;
        t[1] = 0;
    }

    if (nargs > 0 && args != NULL && ex->data != NULL)
        memcpy(ex->data + target->variableaddress, args, (size_t)nargs * 4);

    uint8_t *entry = (uint8_t *)ex->start_program + target->address;
    void (*fn)() = (void (*)())(void *)entry;
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
