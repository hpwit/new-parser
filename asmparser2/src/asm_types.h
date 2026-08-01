#pragma once
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
    l7_22,
    l0_60,
    lm64_m4,
    l0_510,
    lm8_7,
    l0_1020,
    label,
    l0_FFFFFFFF,
};

enum class opCodeType
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
    uint32_t bincode = 0;
    uint16_t size = 0;
    opCodeType op = opCodeType::standard;
    int16_t nameref = EOF_TEXTARRAY;
    uint32_t address = 0;
    bool align = false;
    int line = 0;
    uint32_t (*calculateOfssetJump)(uint32_t value, uint32_t current_address,
                                     uint32_t destination_address) = NULL;
};

class parsedLines
{
public:
    parsedLines() {}
    int size() { return parsed_lines.size(); }
    result_parse_line *getChildAtPos(int pos)
    {
        if (pos >= 0 && pos < parsed_lines.size())
            return parsed_lines.get(pos);
        return NULL;
    }
    result_parse_line *push_back(result_parse_line res)
    {
        result_parse_line *tmp = (result_parse_line *)malloc(sizeof(result_parse_line));
        memcpy(tmp, &res, sizeof(result_parse_line));
        parsed_lines.push_back(tmp);
        return parsed_lines.get(parsed_lines.size() - 1);
    }
    result_parse_line *last() { return parsed_lines.get(parsed_lines.size() - 1); }
    void clear()
    {
        for (int i = 0; i < parsed_lines.size(); i++)
            free(parsed_lines.get(i));
        parsed_lines.clear();
    }
    vect<result_parse_line *> parsed_lines;
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
};

#endif
