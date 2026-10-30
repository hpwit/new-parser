#pragma once
#include <assert.h>
#ifndef __ASM_TYPES__
#define __ASM_TYPES__
// Types shared by the assembler (asm_encoders.h / asm_parser.h): operand
// kinds and their valid ranges, per-line parse results, and the final
// Binary output. Ported from the upstream ESPLiveScript asm_struct_enum.h,
// using this codebase's vect<T>/Text instead of std::vector/std::string --
// Text/Stack/externalType/asmInstruction already exist here (stackfunctions.h,
// binding.h, parser_enum.h) so they aren't redefined.
#include "stackfunctions.h"
#include "vect.h"
#include "parser_define.h"
#include <stdint.h>

enum class operandeType
{
    registers,
    floatregisters,
    boolregisters,
    l0_255,
    lm32_95,
    lm2048_2047,
    l0_15,
    lm1_15,
    l0_32760,
    l0_240,
    l0_31,
    lm128_127,
    lm32768_32512,
    l1_31,
    l1_16,
    l7_22,
    l0_60,
    lm64_m4,
    l0_510,
    lm8_7,
    l0_1020,
    label,
    l0_FFFFFFFF,
};

enum class opCodeType : uint8_t
{
    standard,
    call,
    jump,
    jump_32aligned,
    call_32aligned,
    label,
    function_declaration,
    external_var,
    external_var_label,
    external_call,
    ext_function_declaration,
    external_call_label,
    data,
    number,
    number_label,
    data_label,
    variable,
    json_binding,
    not_known
};

// A single already-split "mnemonic  operands" line, as produced by
// splitOpcodeOperande() from a raw Text buffer line.
struct asm_line
{
    char *opcde;
    char *operandes;
    int error;
};

struct asm_error_message_struct
{
    char *error_message;
    int error;
};

// The Text pool backing result_parse_line's label/name text -- kept
// separate from tokenize.cpp's all_text (which Parser::clean() wipes) so
// assembler-owned strings have their own lifetime.
extern Text asm_text;

struct result_parse_operande
{
    asm_error_message_struct error;
    int value;
    char *label; // non-owning: points into the source line's Text storage
};

class result_parse_line
{
public:
    result_parse_line() {}
    char *getText() { return asm_text.getText(nameref); }
    void addText(char *t) { nameref = asm_text.addText(t); }
    void addText(char *t, uint16_t si) { nameref = asm_text.addText(t, si); }
    // Ordered by size so that the struct packs to 24 bytes on the ESP32:
    // a large script assembles to thousands of these, all alive at once.
    uint32_t bincode = 0;
    uint32_t address = 0;
    uint32_t (*calculateOfssetJump)(uint32_t value, uint32_t current_address,
                                     uint32_t destination_address) = NULL;
    uint16_t size = 0;
    int16_t nameref = EOF_TEXTARRAY;
    uint16_t line = 0;
    opCodeType op = opCodeType::standard;
    bool align = false;
    // A branch target: padded with a nop.n when needed so that its first
    // instruction doesn't straddle a 4-byte boundary (see asm_parser.cpp).
    bool noCross = false;
    // For a short conditional branch, the bits of its offset (8 or 12), so
    // that the assembler can check it reached; 0 otherwise.
    uint8_t branchBits = 0;
};

// The assembled lines, in one array (not one heap block each: on the ESP32
// every block costs 16 bytes on top of its size, more than half a line).
// Pointers into it are only valid until the next push_back() -- reserve()
// the expected count first to keep them stable.
class parsedLines
{
public:
    parsedLines() {}
    int size() { return count; }
    result_parse_line *getChildAtPos(int pos)
    {
        if (pos >= 0 && pos < count)
            return &items[pos];
        return NULL;
    }
    void reserve(int n)
    {
        if (n <= capacity)
            return;
        result_parse_line *grown = (result_parse_line *)realloc(items, n * sizeof(result_parse_line));
        assert(grown != NULL);
        items = grown;
        capacity = n;
    }
    result_parse_line *push_back(result_parse_line res)
    {
        if (count == capacity)
            reserve(capacity < 16 ? 16 : capacity + capacity / 2);
        memcpy(&items[count], &res, sizeof(result_parse_line));
        return &items[count++];
    }
    result_parse_line *last() { return &items[count - 1]; }
    void clear()
    {
        free(items);
        items = NULL;
        count = 0;
        capacity = 0;
    }

private:
    result_parse_line *items = NULL;
    int count = 0;
    int capacity = 0;
};

struct Binary
{
    asm_error_message_struct error;
    uint8_t *binary_data = NULL;
    uint8_t *function_data = NULL;
    uint16_t instruction_size = 0;
    uint16_t tmp_instruction_size = 0;
    uint16_t function_size = 0;
    uint16_t data_size = 0;
};

// A declared script function, decoded from a Binary's relocation header
// (type 4 entries) -- name/variables are owned heap strings (load-time
// data, not compile-time, so they don't go through the Text pool).
struct globalcall
{
    char *name = NULL;
    uint32_t address = 0;
    char *variables = NULL;
    uint32_t variableaddress = 0;
    int args_num = 0;
};

// A `json "path" as <type> name;` binding (see parser.cpp's
// jsonBindingNode handling), decoded from a Binary's relocation header
// (type 5 entries): which variable (by its data-region address, same
// units as ex.data + address) gets populated from which path in a JSON
// document at execution time, and as what type. Kept dependency-free
// (no ArduinoJson here) so declaring/loading a script that happens to use
// `json ... as ...;` never requires it -- only actually applying a JSON
// document to populate these does (see json_binding.h's
// __JSON_OPTION__-guarded updateJsonParameters()).
struct jsonVariable
{
    char *json = NULL;
    uint32_t address = 0;
    uint8_t type = 0;
};

// What a script's safe_mode bounds check reported the last time one
// failed (see visitnode.cpp's emitSafeBoundsCheck() and
// runtime_functions.cpp's safeModeError()). Cleared each time the host
// enters the script from outside.
struct runtime_error_struct
{
    bool error = false;
    // Name of the array as declared in the script (truncated to fit).
    char array[32] = {0};
    // Its declared element count, and the offending (flattened) index.
    // The index is kept signed: a negative one fails the check too.
    uint32_t size = 0;
    int32_t index = 0;
};

// A relocated, loaded script ready to run: start_program/data are real
// addresses (executable memory + a scratch data region), and functions
// lists each declared entry point by name so the caller can pick which
// one to call.
struct executable
{
    asm_error_message_struct error;
    vect<globalcall> functions;
    vect<jsonVariable> jsonVars;
    uint32_t *start_program = NULL;
    uint8_t *data = NULL;
    uint32_t binary_size = 0;
    uint32_t data_size = 0;
    // Set by ScriptExecutable's constructor (script_executable.cpp) to its
    // own `this`, so asm_execute.cpp's fillHandleWord() can give a
    // script's _handle_ global the address of the ScriptExecutable that
    // owns it. NULL for an `executable` built directly via the raw
    // parse -> createBinary() -> createExecutableFromBinary() pipeline
    // (no ScriptExecutable wrapper involved, e.g. LanguageBasics.ino) --
    // fillHandleWord() falls back to the executable's own address in
    // that case.
    void *owner = NULL;
    // Last safe_mode failure, if any -- see runtime_error_struct.
    runtime_error_struct runtimeError;
    // While the host is inside this script (asm_execute.cpp's
    // enterScript()), points at the jmp_buf of the innermost such entry,
    // so a failed bounds check can abandon the script and come straight
    // back to it. NULL whenever no host call is in progress.
    void *runtimeUnwind = NULL;
};

#endif
