#include "asm_execute.h"
#include "binding.h"
#include "string_functions.h"
#include "binary_hex.h"
#include "runtime_functions.h"
#include "asm_serialize.h"
#include <setjmp.h>
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
#include "sdkconfig.h"
#include "esp_idf_version.h"
#if CONFIG_IDF_TARGET_ESP32S3 && CONFIG_SPIRAM && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
// The ESP32-S3's data and instruction buses share one MMU table: PSRAM seen
// at 0x3C000000 + n as data is at 0x42000000 + n as code.
#include "esp_cache.h"
#include "soc/soc.h"
#define ASM_EXEC_PSRAM 1
#define ASM_PSRAM_CODE_OFFSET (SOC_IROM_LOW - SOC_DROM_LOW)
#endif
#else
#define ASM_EXEC_ON_TARGET 0
#endif
#ifndef ASM_EXEC_PSRAM
#define ASM_EXEC_PSRAM 0
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

// Where a script's machine code goes: setExecutableAllocator() (see
// asm_execute.h), else MALLOC_CAP_EXEC memory on target, the plain heap
// on a host.
static ExecutableAllocFunction execAllocFunction = NULL;
static ExecutableFreeFunction execFreeFunction = NULL;

void setExecutableAllocator(ExecutableAllocFunction alloc, ExecutableFreeFunction release)
{
    execAllocFunction = alloc;
    execFreeFunction = release;
}

static ScriptCodeMemory codeMemory = SCRIPT_CODE_IRAM;

bool setScriptCodeMemory(ScriptCodeMemory where)
{
    if (where != SCRIPT_CODE_IRAM && !ASM_EXEC_PSRAM)
        return false;
    codeMemory = where;
    return true;
}

ScriptCodeMemory scriptCodeMemory()
{
    return codeMemory;
}

#if ASM_EXEC_PSRAM
static void *allocPsramCode(size_t size)
{
    void *code = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // Only PSRAM mapped in the data window has an instruction alias.
    if (code != NULL && ((uintptr_t)code < SOC_DROM_LOW || (uintptr_t)code + size > SOC_DROM_HIGH))
    {
        heap_caps_free(code);
        return NULL;
    }
    return code;
}
#endif

// The code's block in the memory setScriptCodeMemory() chose; *where says
// which one it is in (SCRIPT_CODE_AUTO: internal RAM, else PSRAM).
static void *allocExecutable(size_t size, ScriptCodeMemory *where)
{
    *where = SCRIPT_CODE_IRAM;
#if ASM_EXEC_PSRAM
    if (codeMemory == SCRIPT_CODE_PSRAM)
    {
        *where = SCRIPT_CODE_PSRAM;
        return allocPsramCode(size);
    }
#endif
    void *code;
    if (execAllocFunction != NULL)
        code = execAllocFunction(size);
    else
#if ASM_EXEC_ON_TARGET
        code = heap_caps_malloc(size, MALLOC_CAP_EXEC);
#else
        code = malloc(size);
#endif
#if ASM_EXEC_PSRAM
    if (code == NULL && codeMemory == SCRIPT_CODE_AUTO)
    {
        *where = SCRIPT_CODE_PSRAM;
        code = allocPsramCode(size);
    }
#endif
    return code;
}

// Frees code allocExecutable() gave, with the memory it was loaded into.
static void freeExecutableCodeIn(void *code, ScriptCodeMemory where)
{
#if ASM_EXEC_PSRAM
    if (where == SCRIPT_CODE_PSRAM)
    {
        heap_caps_free(code);
        return;
    }
#else
    (void)where;
#endif
    if (execFreeFunction != NULL)
        execFreeFunction(code);
    else
#if ASM_EXEC_ON_TARGET
        heap_caps_free(code);
#else
        free(code);
#endif
}

// The written code made visible to instruction fetch; returns the address it
// runs from. Without this a plain store only guarantees the data side sees
// the new bytes: a script can run cleanly once, then fault later (or a
// different script loaded at the same reused address does) -- a real,
// reproduced-on-hardware crash (InstrFetchProhibited / wild jump) that
// QEMU, which doesn't model the caches, never shows.
static uint32_t *finishCode(uint32_t *code, uint32_t size, ScriptCodeMemory where)
{
#if ASM_EXEC_PSRAM
    if (where == SCRIPT_CODE_PSRAM)
    {
        // As ESP-IDF's ELF loader: the data cache written back, then the
        // instruction cache's lines for the alias invalidated.
        esp_cache_msync(code, size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        uintptr_t run = (uintptr_t)code + ASM_PSRAM_CODE_OFFSET;
        const uintptr_t line = CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE;
        uintptr_t begin = run & ~(line - 1);
        uintptr_t end = (run + size + line - 1) & ~(line - 1);
        esp_cache_msync((void *)begin, end - begin, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_INST);
        return (uint32_t *)run;
    }
#else
    (void)where;
#endif
#if ASM_EXEC_ON_TARGET
    xthal_dcache_region_writeback(code, size);
    xthal_icache_region_invalidate(code, size);
#endif
    return code;
}

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
// Appends a missing binding's name to `list` ("a, b()"), once: a script
// that uses a name in several places lists it once.
static void addMissingBinding(char *list, size_t len, const char *label, size_t prefix, bool isFunction)
{
    const char *name = label + prefix;
    const char *paren = strchr(name, '(');
    size_t n = paren ? (size_t)(paren - name) : strlen(name);
    char item[72];
    snprintf(item, sizeof(item), "%.*s%s", (int)n, name, isFunction ? "()" : "");
    size_t itemLen = strlen(item);
    for (const char *p = list; (p = strstr(p, item)) != NULL; p += itemLen)
    {
        if ((p == list || p[-1] == ' ') && (p[itemLen] == ',' || p[itemLen] == '\0'))
            return;
    }
    if (list[0] != '\0')
        strncat(list, ", ", len - strlen(list) - 1);
    strncat(list, item, len - strlen(list) - 1);
}

// exec: the code to patch; literals: the area after the code that the
// embedded literal bytes (type 3) are copied from.
static asm_error_message_struct decodeBinaryHeader(uint8_t *exec, uint8_t *binary_header, uint8_t *_binary_data,
                                                     executable *finalexe, const uint8_t *literals)
{
    // The names the binary uses but this program has not bound (bindVariable()/
    // bindFunction()), all of them: "missing binding: led16, show16()".
    char missing[200] = "";
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
                addMissingBinding(missing, sizeof(missing), textptr, 6, false); // "@_ext_NAME"
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
                addMissingBinding(missing, sizeof(missing), textptr, 2, true); // "@_NAME(sig)"
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
            memcpy(_binary_data + addr, literals + tmp_data, size);
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
        case 6:
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
            if (type == 6) // the variable's size; unknown (0) in type 5
            {
                memcpy(&jv.size, binary_header, 2);
                binary_header += 2;
            }
            finalexe->jsonVars.push_back(jv);
            break;
        }
        default:
            error.error = 1;
            error.error_message = (char *)"Unknown type binary header invalid";
            return error;
        }
    }
    if (missing[0] != '\0')
    {
        error.error = 1;
        error.error_message = string_format("missing binding: %s", missing);
    }
    return error;
}

#if ASM_EXEC_ON_TARGET && CONFIG_IDF_TARGET_ESP32S3
#include "soc/soc.h"
#define ASM_SRAM_BLOCK 0x10000
// True when the code and the globals sit in the same 64 KB block of the
// ESP32-S3's internal RAM, which the executable heap (0x4037xxxx up) and
// the data heap (0x3FC8xxxx up) both map. A loop that reads its globals
// out of the block it is running from loses a cycle or two an iteration,
// or none, with the address it happened to load at: measured on the
// board, the same Mandelbrot loop 6% slower for a load address 4 bytes
// further, and flat for every address once the two were in different
// blocks.
static bool sharesSramBlock(const void *code, size_t codeSize, const void *data, size_t dataSize)
{
    uintptr_t c = (uintptr_t)code, d = (uintptr_t)data;
    if (c < SOC_DIRAM_IRAM_LOW || c >= SOC_DIRAM_IRAM_HIGH)
        return false;
    c = MAP_IRAM_TO_DRAM(c);
    return c / ASM_SRAM_BLOCK <= (d + dataSize - 1) / ASM_SRAM_BLOCK &&
           d / ASM_SRAM_BLOCK <= (c + codeSize - 1) / ASM_SRAM_BLOCK;
}

// The globals again, out of the code's blocks: the heap hands out the two
// one after the other, so they share a block more often than not. Each
// place that will not do is held, with a pad twice the size of the last
// one, while the heap is asked again: that uses up the scraps of a small
// heap in a few rounds, and walks a large one past the 64 KB boundary in
// ten. Left alone when internal RAM is too short to spend on placement,
// and given back as they were when no better place turns up.
static uint8_t *dataAwayFromCode(uint8_t *data, size_t dataSize, const void *code, size_t codeSize)
{
    const int rounds = 10;
    void *held[2 * rounds];
    int count = 0;
    uint8_t *first = data;
    size_t pad = 128;
    for (int i = 0; i < rounds && sharesSramBlock(code, codeSize, data, dataSize); i++)
    {
        if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 2 * ASM_SRAM_BLOCK)
            break;
        if (data != first)
            held[count++] = data;
        held[count++] = heap_caps_malloc(pad, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        pad *= 2;
        data = (uint8_t *)calloc(1, dataSize);
        if (data == NULL)
            break;
    }
    if (data == NULL || sharesSramBlock(code, codeSize, data, dataSize))
    {
        if (data != first)
            free(data);
        data = first;
    }
    else if (data != first)
        free(first);
    for (int i = 0; i < count; i++)
        free(held[i]);
    return data;
}
#endif

// The code's block (setExecutableAllocator()) and the globals' (zeroed);
// false, with exe's error saying which and how much, when one is missing.
// *where: the memory the code is in.
static bool allocScriptMemory(const Binary *bin, executable *exe, uint32_t **code, uint8_t **data,
                              ScriptCodeMemory *where)
{
    uint32_t *exec = (uint32_t *)allocExecutable(bin->instruction_size, where);
    // Zeroed, so a global declared without an initializer starts at 0 as
    // in C, not with whatever the heap held (initializers are copied in
    // over this by decodeBinaryHeader()). Scripts read such globals
    // routinely -- counters, accumulators, "has this run yet" flags.
    uint8_t *binary_data = (uint8_t *)calloc(1, bin->data_size > 0 ? bin->data_size : 4);

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
        // Which one, and by how much: the code needs one contiguous block.
        static char message[160];
        if (exec == NULL && codeMemory == SCRIPT_CODE_PSRAM)
            snprintf(message, sizeof(message),
                     "could not allocate %u bytes of PSRAM for this script's code",
                     (unsigned)bin->instruction_size);
        else if (exec == NULL && codeMemory == SCRIPT_CODE_AUTO)
            snprintf(message, sizeof(message),
                     "could not allocate %u bytes of executable memory or PSRAM for this script's code",
                     (unsigned)bin->instruction_size);
        else if (exec == NULL)
        {
#if ASM_EXEC_ON_TARGET
            snprintf(message, sizeof(message),
                     "could not allocate %u bytes of executable memory for this script "
                     "(largest MALLOC_CAP_EXEC block: %u bytes)",
                     (unsigned)bin->instruction_size,
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_EXEC));
#else
            snprintf(message, sizeof(message),
                     "could not allocate %u bytes of executable memory for this script",
                     (unsigned)bin->instruction_size);
#endif
        }
        else
            snprintf(message, sizeof(message),
                     "could not allocate %u bytes for this script's data (general heap)",
                     (unsigned)bin->data_size);
        exe->error.error = 1;
        exe->error.error_message = message;
        if (exec != NULL)
            freeExecutableCodeIn(exec, *where);
        if (binary_data != NULL)
            free(binary_data);
        return false;
    }
#if ASM_EXEC_ON_TARGET && CONFIG_IDF_TARGET_ESP32S3
    binary_data = dataAwayFromCode(binary_data, bin->data_size > 0 ? bin->data_size : 4, exec,
                                   bin->instruction_size);
#endif
    *code = exec;
    *data = binary_data;
    return true;
}

executable createExecutableFromBinary(Binary *bin)
{
    // registerBuiltinRuntimeFunctions() (runtime_functions.h) is
    // otherwise only ever triggered by Parser::parse() -- fine for
    // parseScript()/the raw parse -> createBinary -> here pipeline
    // (parsing always happens first, in the same process, before this
    // runs), but a save/load consumer (createExecutableFromBuffer(), or
    // hand-rolled deserializeBinary() + this) never calls Parser::parse()
    // at all: a fresh process that only ever loads a previously-compiled
    // binary would otherwise never register printf/printfln, so a saved
    // script that calls either fails to relocate here with "external
    // function @_printfln(char*|Args) not found" despite them being
    // documented as always available with no binding needed. Idempotent
    // (registerBuiltinRuntimeFunctions() guards itself with a static
    // bool), so unconditionally safe to call again even when parsing
    // already did.
    registerBuiltinRuntimeFunctions();

    executable exe;
    exe.error.error = 0;

    uint32_t *exec;
    uint8_t *binary_data;
    ScriptCodeMemory where;
    if (!allocScriptMemory(bin, &exe, &exec, &binary_data, &where))
        return exe;

    asm_error_message_struct error = decodeBinaryHeader(
        bin->binary_data, bin->function_data, binary_data,
        &exe, bin->binary_data + bin->instruction_size);

    if (exe.functions.size() == 0)
    {
        exe.error.error = 1;
        exe.error.error_message = (char *)"No global start function found";
        freeExecutableCodeIn(exec, where);
        free(binary_data);
        return exe;
    }
    if (error.error == 1)
    {
        exe.error = error;
        freeExecutableCodeIn(exec, where);
        free(binary_data);
        return exe;
    }

    memcpy(exec, bin->binary_data, bin->instruction_size);

    exe.code_data = exec;
    exe.start_program = finishCode(exec, bin->instruction_size, where);
    exe.data = binary_data;
    exe.binary_size = bin->instruction_size;
    exe.data_size = bin->data_size;
   // printExecutableHex(&exe);
   // printExecutableFunctions(&exe);
    return exe;
}

// createExecutableFromBinary() for a saved binary read through `read`
// (readBinaryHeader()'s format) without holding it all: the code goes from
// the reader straight into its executable block, a chunk at a time, and is
// relocated there; only the literals after it and the relocation table are
// read into the heap, and freed before returning.
executable loadExecutableFromStream(ScriptReadFunction read, void *ctx)
{
    registerBuiltinRuntimeFunctions(); // see createExecutableFromBinary()

    executable exe;
    exe.error.error = 0;

    Binary bin;
    readBinaryHeader(read, ctx, &bin);
    if (bin.error.error)
    {
        static char message[96];
        snprintf(message, sizeof(message), "not a valid binary: %s", bin.error.error_message);
        exe.error.error = 1;
        exe.error.error_message = message;
        return exe;
    }

    uint32_t *exec;
    uint8_t *binary_data;
    ScriptCodeMemory where;
    if (!allocScriptMemory(&bin, &exe, &exec, &binary_data, &where))
        return exe;

    // The code, through a small buffer: the destination is executable
    // memory, written with memcpy() as createExecutableFromBinary() does.
    uint32_t chunk[64];
    bool complete = true;
    for (uint32_t done = 0; complete && done < bin.instruction_size;)
    {
        size_t n = bin.instruction_size - done;
        if (n > sizeof(chunk))
            n = sizeof(chunk);
        complete = read(ctx, (uint8_t *)chunk, n) == n;
        memcpy((uint8_t *)exec + done, chunk, n);
        done += n;
    }
    size_t literalSize = bin.tmp_instruction_size - bin.instruction_size;
    uint8_t *literals = complete ? (uint8_t *)malloc(literalSize > 0 ? literalSize : 4) : NULL;
    uint8_t *relocations = literals ? (uint8_t *)malloc(bin.function_size > 0 ? bin.function_size : 4) : NULL;
    if (relocations != NULL)
        complete = read(ctx, literals, literalSize) == literalSize &&
                   read(ctx, relocations, bin.function_size) == bin.function_size;
    if (!complete || relocations == NULL)
    {
        exe.error.error = 1;
        exe.error.error_message = complete ? (char *)"could not allocate the binary's relocation table"
                                           : (char *)"not a valid binary: serialized script binary is truncated";
        free(literals);
        free(relocations);
        freeExecutableCodeIn(exec, where);
        free(binary_data);
        return exe;
    }

    asm_error_message_struct error = decodeBinaryHeader(
        (uint8_t *)exec, relocations, binary_data, &exe, literals);
    free(literals);
    free(relocations);
    if (exe.functions.size() == 0 || error.error == 1)
    {
        if (error.error == 1)
            exe.error = error;
        else
        {
            exe.error.error = 1;
            exe.error.error_message = (char *)"No global start function found";
        }
        freeExecutableCodeIn(exec, where);
        free(binary_data);
        return exe;
    }

    exe.code_data = exec;
    exe.start_program = finishCode(exec, bin.instruction_size, where);
    exe.data = binary_data;
    exe.binary_size = bin.instruction_size;
    exe.data_size = bin.data_size;
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
    printHexWords(ex->code_data != NULL ? ex->code_data : ex->start_program, ex->binary_size);

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

// Every entry into compiled script code goes through here, so that a
// failed safe_mode bounds check has somewhere to come back to:
// runtime_functions.cpp's safeModeError() longjmp()s to the jmp_buf
// armed below, abandoning whatever script functions were on the stack.
// Returns false in that case (and leaves *result alone), true if the
// script function returned normally.
//
// Entries nest -- a pinInterrupt() callback re-entering the script while
// main() is still running is one inside the other -- so the previous
// unwind point is saved and put back; a failure unwinds only the
// innermost entry. ex->runtimeError is cleared when entering from
// outside, so it always describes the most recent outermost call.
static bool enterScript(executable *ex, void *entry, const int32_t *args, int nargs, int32_t *result)
{
    jmp_buf unwind;
    void *outer = ex->runtimeUnwind;
    if (outer == NULL)
        ex->runtimeError = runtime_error_struct();

    // Written after setjmp() and read after a possible longjmp().
    volatile bool completed = false;
    volatile int32_t value = 0;
    if (setjmp(unwind) == 0)
    {
        ex->runtimeUnwind = &unwind;
        value = callXtensaDirect(entry, args, nargs);
        completed = true;
    }
    ex->runtimeUnwind = outer;

    if (completed && result != NULL)
        *result = value;
    return completed;
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
    bool completed = enterScript(ex, entry, args, nargs, result);
#ifdef ASM_PARSER_DEBUG
    printf("ASM_PARSER_DEBUG: %s() took %lld us\n", name, (long long)(debugNowMicros() - __debugStart));
#endif
    return completed;
}

bool runExecutableWithArgs(executable *ex, Arguments *args)
{
    int32_t raw[6];
    int n = marshalArguments(args, raw);
    return runExecutableWithArgs(ex, raw, n);
}

bool runExecutableWithArgs(executable *ex, const int32_t *args, int nargs)
{
    // main() is called exactly like any other function now -- direct
    // register passing via callXtensaDirect(), same as callFunction().
    // This used to marshal arguments through a wrapper's reserved
    // stack-storage area (matching upstream's own execute_asm.h
    // convention), but _visitdefFunctionNode() no longer emits that
    // wrapper (see asm_execute.cpp's isWrapperRecord() comment for the
    // full history): a parameterized function's single remaining record
    // is the real function body, which reads its incoming arguments
    // straight out of a2.. (see visitnode.cpp's _visitdefInputArgumentsNode),
    // the same registers callXtensaDirect() populates via a10..a15's
    // call8 window rotation. Marshaling through target->variableaddress
    // and then calling with zero actual registers set, as this used to
    // do, silently called main() with all-zero arguments regardless of
    // what the caller passed.
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

    uint8_t *entry = (uint8_t *)ex->start_program + target->address;
    return enterScript(ex, entry, args, nargs, NULL);
}

void runExecutable(executable *ex)
{
    runExecutableWithArgs(ex, NULL, 0);
}

ScriptCodeMemory executableCodeMemory(const executable *ex)
{
    // Run from somewhere else than where it was written: PSRAM.
    return ex->code_data != NULL && ex->code_data != ex->start_program ? SCRIPT_CODE_PSRAM : SCRIPT_CODE_IRAM;
}

void freeExecutable(executable *ex)
{
    if (ex->start_program != NULL)
    {
        freeExecutableCodeIn(ex->code_data != NULL ? ex->code_data : ex->start_program,
                             executableCodeMemory(ex));
        ex->start_program = NULL;
        ex->code_data = NULL;
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
