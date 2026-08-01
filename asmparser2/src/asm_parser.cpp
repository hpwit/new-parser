#include "asm_parser.h"
#include "asm_encoders.h"
#include "string_functions.h"
#include "vect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALIGN_INSTR 4
#define ALIGN_DATA 4

static asm_error_message_struct asm_Error;
static uint8_t *tmp_exec = NULL;
static uint8_t *tmp_binary_data = NULL;
static uint8_t *binary_header = NULL;
static uint32_t _address_instr = 0;
static uint32_t _address_data = 0;
static uint32_t _tmp_data_address = 0;
static uint16_t _instr_size = 0;
static uint16_t tmp_instr_size = 0;
static uint16_t _data_size = 0;
static uint16_t tmp_data_size = 0;
static uint16_t binary_header_size = 0;
static parsedLines _asm_parsed;

// RAII wrapper around a space/comma split -- see optimize.cpp for the same
// pattern and why (str_split mallocs every token; this frees them
// automatically on scope exit instead of at every call site by hand).
struct SplitResult
{
    vect<char *> parts;
    SplitResult(char *str, const char *delim) { str_split(&parts, str, (char *)delim); }
    ~SplitResult() { parts.empty(); }
    int size() { return parts.size(); }
    char *get(int i) { return parts.get(i); }
};

// Truncates `s` at the first "//" and strips leading/trailing whitespace,
// in place. Returns a pointer into `s` (possibly offset past leading
// whitespace) -- not a fresh allocation, so callers must keep their own
// copy of `s` if they need to free() it later.
static char *trim(char *s)
{
    char *comment = strstr(s, "//");
    if (comment)
        *comment = 0;
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == '\t'))
        s[--len] = 0;
    char *start = s;
    while (*start == ' ' || *start == '\n' || *start == '\r' || *start == '\t')
        start++;
    return start;
}

static result_parse_line *getInstrAtPos(int pos) { return _asm_parsed.getChildAtPos(pos); }

// Appends `operande` to asm_parsed and encodes it into tmp_exec, handling
// Xtensa's variable-width (2 or 3 byte) instruction alignment: labels
// marked `align` must land on a 4-byte boundary, so a 2/3/5-byte run of
// nop padding is inserted first if needed.
static void addInstr(result_parse_line operande, parsedLines *asm_parsed)
{
    if (operande.op != opCodeType::data && operande.op != opCodeType::number)
    {
        operande.address = _address_instr;
        if (operande.align)
        {
            int add_size = 0;
            uint32_t unst_local;
            int op = (_address_instr % ALIGN_INSTR);
            switch (op)
            {
            case 2:
                add_size = 2;
                unst_local = 0xF03D;
                memcpy(tmp_exec + operande.address, &unst_local, 2);
                break;
            case 1:
                add_size = 3;
                unst_local = 0x0020F0;
                memcpy(tmp_exec + operande.address, &unst_local, 3);
                break;
            case 3:
                add_size = 5;
                unst_local = 0x0020F0;
                memcpy(tmp_exec + operande.address, &unst_local, 3);
                unst_local = 0xF03D;
                memcpy(tmp_exec + operande.address + 3, &unst_local, 2);
                break;
            default:
                add_size = 0;
                break;
            }
            operande.address += add_size;
        }
        memcpy(tmp_exec + operande.address, &operande.bincode, operande.size);
        _address_instr = operande.address + operande.size;
        asm_parsed->push_back(operande);
    }
    else
    {
        // A .bytes reservation (data/number): doesn't emit an instruction,
        // but still reserves 4 bytes of instruction-stream address space --
        // that's the jump-table slot a later l32r/callExt resolves through.
        // The *preceding* label (already pushed, since labels take the `if`
        // branch above) records where this reservation's value lives in the
        // separate data region, via its bincode field.
        int add_size = 0;
        int op = (int)(_address_data % ALIGN_DATA);
        if (op > 0)
        {
            add_size = ALIGN_DATA - op;
            _address_data += add_size;
        }
        asm_parsed->last()->bincode = _address_data;

        _address_instr += 4;
        if (operande.nameref != EOF_TEXTARRAY)
        {
            operande.address = _address_data;
            memcpy(tmp_binary_data + _tmp_data_address, operande.getText(), operande.size);
            _tmp_data_address += operande.size;
            asm_parsed->push_back(operande);
        }
        _address_data += operande.size;
    }
}

// ---- operand parsing ----

static result_parse_operande parseRegisterLike(char *s, char prefix)
{
    result_parse_operande res;
    res.error.error = 0;
    s = trim(s);
    if (s[0] == prefix && strlen(s) > 1)
    {
        char *endptr = NULL;
        long value = strtol(s + 1, &endptr, 10);
        if (*endptr == 0 && value >= 0 && value <= 15)
        {
            res.value = (int)value;
            return res;
        }
    }
    res.error.error = 1;
    res.error.error_message = string_format("Unknown register %s\n", s);
    return res;
}

static result_parse_operande parseNumberRange(char *s, int min, int max)
{
    result_parse_operande res;
    res.error.error = 0;
    s = trim(s);
    if (strlen(s) > 0)
    {
        char *endptr = NULL;
        long value = strtol(s, &endptr, 10);
        if (*endptr == 0 && value >= min && value <= max)
        {
            res.value = (int)value;
            return res;
        }
    }
    res.error.error = 1;
    res.error.error_message = string_format("incorrect value %s should be between %d and %d\n", s, min, max);
    return res;
}

static result_parse_operande parseHexRange(char *s, uint32_t min, uint32_t max)
{
    result_parse_operande res;
    res.error.error = 0;
    s = trim(s);
    if (strlen(s) > 0)
    {
        char *endptr = NULL;
        long value = strtol(s, &endptr, 10);
        if (*endptr == 0 && (uint32_t)value >= min && (uint32_t)value <= max)
        {
            res.value = (int)value;
            return res;
        }
    }
    res.error.error = 1;
    res.error.error_message = string_format("incorrect value %s should be between %u and %u\n", s, min, max);
    return res;
}

static result_parse_operande parseLabelOperand(char *s)
{
    result_parse_operande res;
    res.error.error = 0;
    s = trim(s);
    if (strlen(s) > 0)
    {
        res.label = s; // non-owning: points into the caller's split token
        return res;
    }
    res.error.error = 1;
    res.error.error_message = string_format("label %s is not valid\n", s);
    return res;
}

static result_parse_operande operandeParse(char *s, operandeType optype)
{
    switch (optype)
    {
    case operandeType::registers:
        return parseRegisterLike(s, 'a');
    case operandeType::floatregisters:
        return parseRegisterLike(s, 'f');
    case operandeType::boolregisters:
        return parseRegisterLike(s, 'b');
    case operandeType::l0_255:
        return parseNumberRange(s, 0, 255);
    case operandeType::lm32_95:
        return parseNumberRange(s, -32, 95);
    case operandeType::lm2048_2047:
        return parseNumberRange(s, -2048, 2047);
    case operandeType::l0_15:
        return parseNumberRange(s, 0, 15);
    case operandeType::l0_32760:
        return parseNumberRange(s, 0, 32760);
    case operandeType::l0_240:
        return parseNumberRange(s, 0, 240);
    case operandeType::l0_31:
        return parseNumberRange(s, 0, 31);
    case operandeType::lm128_127:
        return parseNumberRange(s, -128, 127);
    case operandeType::lm32768_32512:
        return parseNumberRange(s, -32768, 35512);
    case operandeType::l1_31:
        return parseNumberRange(s, 1, 31);
    case operandeType::l7_22:
        return parseNumberRange(s, 7, 22);
    case operandeType::l0_60:
        return parseNumberRange(s, 0, 60);
    case operandeType::lm64_m4:
        return parseNumberRange(s, -64, -4);
    case operandeType::l0_510:
        return parseNumberRange(s, 0, 510);
    case operandeType::lm8_7:
        return parseNumberRange(s, -8, 7);
    case operandeType::l0_1020:
        return parseNumberRange(s, 0, 1020);
    case operandeType::lm1_15:
        return parseNumberRange(s, -1, 15);
    case operandeType::l0_FFFFFFFF:
        return parseHexRange(s, 0, 0xFFFFFFFF);
    case operandeType::label:
        return parseLabelOperand(s);
    }
    result_parse_operande res;
    res.error.error = 1;
    res.error.error_message = (char *)"Unknown operande";
    return res;
}

static result_parse_line parseOperandes(char *str, int nboperande, operandeType *optypes, int size,
                                         uint32_t (*createbin)(uint32_t *val))
{
    result_parse_line result;
    uint32_t values[4] = {0, 0, 0, 0};
    asm_Error.error = 0;
    result.align = false;
    result.size = size;

    if (nboperande == 0)
    {
        if (createbin)
            result.bincode = createbin(values);
        result.op = opCodeType::standard;
        return result;
    }

    SplitResult operandes(str, ",");
    if (operandes.size() == nboperande)
    {
        for (int i = 0; i < nboperande; i++)
        {
            result_parse_operande res = operandeParse(operandes.get(i), optypes[i]);
            if (res.error.error)
            {
                asm_Error.error = res.error.error;
                asm_Error.error_message = res.error.error_message;
            }
            else
            {
                if (optypes[i] == operandeType::label)
                    result.addText(string_format("%s", res.label));
                values[i] = (uint32_t)res.value;
            }
        }
        if (asm_Error.error == 0 && createbin)
            result.bincode = createbin(values);
    }
    else
    {
        asm_Error.error = 1;
        asm_Error.error_message = string_format("asm_Error:expected %d arguments got %d\n", nboperande, operandes.size());
    }
    result.op = opCodeType::standard;
    return result;
}

static int findLabel(char *s, parsedLines *asm_parsed)
{
    char *strimmed = trim(s);
    for (int i = 0; i < asm_parsed->size(); i++)
    {
        result_parse_line *it = asm_parsed->getChildAtPos(i);
        if (it->op == opCodeType::label || it->op == opCodeType::data_label ||
            it->op == opCodeType::number_label || it->op == opCodeType::external_var_label ||
            it->op == opCodeType::external_call)
        {
            if (strcmp(trim(it->getText()), strimmed) == 0)
                return i;
        }
    }
    return -1;
}

static asm_line splitOpcodeOperande(char *s)
{
    asm_line res;
    res.opcde = NULL;
    res.operandes = NULL;
    s = trim(s);
    if (strlen(s) < 2)
    {
        res.error = 1;
        return res;
    }
    res.error = 0;
    char *space = strchr(s, ' ');
    if (space)
    {
        int oplen = (int)(space - s);
        res.opcde = (char *)malloc(oplen + 1);
        memcpy(res.opcde, s, oplen);
        res.opcde[oplen] = 0;
        res.operandes = string_format("%s", space);
    }
    else
    {
        res.opcde = string_format("%s", s);
        res.operandes = string_format("%s", "");
    }
    return res;
}

// The big mnemonic dispatch table -- one branch per instruction this
// codebase's codegen (visitnode.cpp / asmInstructionsName) can emit, plus
// the pseudo-ops (labels, .bytes, callExt) the assembler itself needs.
static result_parse_line parseline(asm_line sp, parsedLines *asm_parsed)
{
    if (strchr(sp.opcde, ':') != NULL)
    {
        result_parse_line res;
        asm_Error.error = 0;
        res.op = opCodeType::label;
        res.size = 0;
        res.align = false;
        char *colon = strchr(sp.opcde, ':');
        char *name = (char *)malloc(colon - sp.opcde + 1);
        memcpy(name, sp.opcde, colon - sp.opcde);
        name[colon - sp.opcde] = 0;
        char *trimmed = trim(name);
        res.addText(string_format("%s", trimmed));
        free(name);
        if (findLabel(res.getText(), asm_parsed) != -1)
        {
            asm_Error.error = 1;
            asm_Error.error_message = string_format("label %s is already declared\n", res.getText());
        }
        if (strncmp(sp.opcde, "@_", 2) == 0)
            res.align = true;
        return res;
    }

    char *op = sp.opcde;
    if (strcmp(op, "add") == 0)
        return parseOperandes(sp.operandes, 3, op_add, 3, bin_add);
    if (strcmp(op, "sub") == 0)
        return parseOperandes(sp.operandes, 3, op_sub, 3, bin_sub);
    if (strcmp(op, "quou") == 0)
        return parseOperandes(sp.operandes, 3, op_quou, 3, bin_quou);
    if (strcmp(op, "quos") == 0)
        return parseOperandes(sp.operandes, 3, op_quou, 3, bin_quos);
    if (strcmp(op, "addi") == 0)
        return parseOperandes(sp.operandes, 3, op_addi, 3, bin_addi);
    if (strcmp(op, "and") == 0)
        return parseOperandes(sp.operandes, 3, op_and, 3, bin_and);
    if (strcmp(op, "or") == 0)
        return parseOperandes(sp.operandes, 3, op_and, 3, bin_or);

    if (strcmp(op, "bnez") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 2, op_bnez, 3, bin_bnez);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_bnez;
        return ps;
    }
    if (strcmp(op, "beqz") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 2, op_bnez, 3, bin_beqz);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_bnez;
        return ps;
    }
    if (strcmp(op, "j") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 1, op_j, 3, bin_j);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_j;
        return ps;
    }
    if (strcmp(op, "bt") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 2, op_jumpfloat, 3, bin_bt);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_blt;
        return ps;
    }
    if (strcmp(op, "bf") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 2, op_jumpfloat, 3, bin_bf);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_blt;
        return ps;
    }
    if (strcmp(op, "olt.s") == 0)
        return parseOperandes(sp.operandes, 3, op_floatco, 3, bin_olts);
    if (strcmp(op, "oeq.s") == 0)
        return parseOperandes(sp.operandes, 3, op_floatco, 3, bin_oeqs);
    if (strcmp(op, "ole.s") == 0)
        return parseOperandes(sp.operandes, 3, op_floatco, 3, bin_oles);

    if (strcmp(op, "blt") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 3, op_blt, 3, bin_blt);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_blt;
        return ps;
    }
    // visitnode.cpp's comparisonNode/whileNode codegen (the "%s" in
    // asmInstructionsName[]'s "blt%s ..."/"bge%s ..." entries) emits the
    // unsigned variant whenever either compared operand is uint32_t --
    // bin_bltu()/bin_bgeu() (asm_encoders.h) already encode them
    // correctly, they just weren't wired into the opcode dispatch here,
    // so any script comparing a uint32_t (e.g. `while (h < 700)` with
    // `uint32_t h`) failed to assemble with "Opcode bltu not found".
    // Same operand shape and jump-offset calculation as the signed forms.
    if (strcmp(op, "bltu") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 3, op_blt, 3, bin_bltu);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_blt;
        return ps;
    }
    if (strcmp(op, "blti") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 3, op_blti, 3, bin_blti);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_blt;
        return ps;
    }
    if (strcmp(op, "bgei") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 3, op_blti, 3, bin_bgei);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_blt;
        return ps;
    }
    if (strcmp(op, "bnei") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 3, op_blti, 3, bin_bnei);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_blt;
        return ps;
    }
    if (strcmp(op, "beqi") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 3, op_blti, 3, bin_beqi);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_blt;
        return ps;
    }
    if (strcmp(op, "bge") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 3, op_bge, 3, bin_bge);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_bge;
        return ps;
    }
    // See "bltu" above -- same gap, unsigned counterpart of "bge".
    if (strcmp(op, "bgeu") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 3, op_bge, 3, bin_bgeu);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_bge;
        return ps;
    }
    if (strcmp(op, "beq") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 3, op_beq, 3, bin_beq);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_beq;
        return ps;
    }
    if (strcmp(op, "bne") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 3, op_bne, 3, bin_bne);
        ps.op = opCodeType::jump;
        ps.calculateOfssetJump = jump_bne;
        return ps;
    }

    if (strcmp(op, "l32i") == 0)
        return parseOperandes(sp.operandes, 3, op_l32i, 3, bin_l32i);
    if (strcmp(op, "l32r") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 2, op_l32r, 3, bin_l32r);
        ps.op = opCodeType::jump_32aligned;
        ps.calculateOfssetJump = jump_l32r;
        return ps;
    }
    if (strcmp(op, "call8") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 1, op_call8, 3, bin_call8);
        ps.op = opCodeType::jump_32aligned;
        ps.calculateOfssetJump = jump_call8;
        return ps;
    }

    if (strcmp(op, "rsr") == 0)
        return parseOperandes(sp.operandes, 2, op_rsr, 3, bin_rsr);
    if (strcmp(op, "wsr") == 0)
        return parseOperandes(sp.operandes, 2, op_rsr, 3, bin_wsr);
    if (strcmp(op, "mov") == 0)
        return parseOperandes(sp.operandes, 2, op_mov, 3, bin_mov);
    if (strcmp(op, "movr") == 0)
        return parseOperandes(sp.operandes, 2, op_mov, 3, bin_mov);
    if (strcmp(op, "abs") == 0)
        return parseOperandes(sp.operandes, 2, op_mov, 3, bin_abs);
    if (strcmp(op, "sll") == 0)
        return parseOperandes(sp.operandes, 2, op_mov, 3, bin_sll);
    if (strcmp(op, "ssl") == 0)
        return parseOperandes(sp.operandes, 1, op_ssl, 3, bin_ssl);
    if (strcmp(op, "srl") == 0)
        return parseOperandes(sp.operandes, 2, op_mov, 3, bin_srl);
    if (strcmp(op, "mull") == 0)
        return parseOperandes(sp.operandes, 3, op_add, 3, bin_mull);
    if (strcmp(op, "neg") == 0)
        return parseOperandes(sp.operandes, 2, op_mov, 3, bin_neg);
    if (strcmp(op, "movi") == 0)
        return parseOperandes(sp.operandes, 2, op_movi, 3, bin_movi);
    if (strcmp(op, "s32i") == 0)
        return parseOperandes(sp.operandes, 3, op_l32i, 3, bin_s32i);
    if (strcmp(op, "s8i") == 0)
        return parseOperandes(sp.operandes, 3, op_l8ui, 3, bin_s8i);
    if (strcmp(op, "l8ui") == 0)
        return parseOperandes(sp.operandes, 3, op_l8ui, 3, bin_l8ui);
    if (strcmp(op, "s16i") == 0)
        return parseOperandes(sp.operandes, 3, op_l16si, 3, bin_s16i);
    if (strcmp(op, "l16si") == 0)
        return parseOperandes(sp.operandes, 3, op_l16si, 3, bin_l16si);
    if (strcmp(op, "l16ui") == 0)
        return parseOperandes(sp.operandes, 3, op_l16si, 3, bin_l16ui);
    if (strcmp(op, "remu") == 0)
        return parseOperandes(sp.operandes, 3, op_remu, 3, bin_remu);
    if (strcmp(op, "entry") == 0)
        return parseOperandes(sp.operandes, 2, op_entry, 3, bin_entry);
    if (strcmp(op, "extui") == 0)
        return parseOperandes(sp.operandes, 4, op_extui, 3, bin_extui);
    if (strcmp(op, "retw.n") == 0)
        return parseOperandes(sp.operandes, 0, NULL, 2, bin_retw_n);

    if (strcmp(op, ".bytes") == 0)
    {
        char *endptr = NULL;
        char *depart = trim(sp.operandes);
        char *space = strchr(depart, ' ');
        long value;
        char *suite = NULL; // the trailing "XX XX XX ..." hex byte list, if any
        if (space)
        {
            *space = 0;
            value = strtol(depart, &endptr, 10);
            suite = space + 1;
        }
        else
        {
            value = strtol(depart, &endptr, 10);
        }
        if (*endptr == 0)
        {
            result_parse_line ps;
            ps.op = opCodeType::data;
            result_parse_line *ps1 = getInstrAtPos(asm_parsed->size() - 1);
            if (ps1 != NULL && ps1->op == opCodeType::label)
            {
                ps1->size = 4;
                ps1->op = opCodeType::data_label;
                ps1->align = true;
                asm_Error.error = 0;
                ps.size = (uint16_t)((value / 4) * 4 + 4);
                // ".bytes N" (no trailing values) is a plain zeroed
                // reservation (e.g. the built-in _handle_/_execaddr_/
                // _sync scratch words) -- nothing to store. ".bytes N
                // XX XX XX ..." (a string literal's initial value,
                // visitnode.cpp's globalVariableNode/stringNode
                // handling) carries N space-separated hex byte values
                // that need to survive into the loaded executable's
                // data region -- ported from upstream's identical
                // ESPLiveScript asm_parser.h ".bytes" handling, which
                // this v2 port had dropped, silently discarding every
                // string literal's actual content (the assembled
                // output reserved the right *size* but never stored
                // the *bytes*, so any script string -- including any
                // printf/printfln format string -- read back as zeros
                // at runtime). addInstr()'s existing
                // `if (operande.nameref != EOF_TEXTARRAY)` branch
                // already does the rest (computing the data-region
                // address, copying these bytes into the binary's
                // temporary data-staging area, and reserving a type-3
                // relocation record for the loader to copy them into
                // the real data region) -- it was simply never reached
                // because nameref was never set.
                if (suite != NULL && *suite != 0)
                {
                    // addInstr()'s data branch later copies exactly
                    // ps.size bytes out of whatever addText() stores
                    // here (see its `memcpy(tmp_binary_data + ...,
                    // operande.getText(), operande.size)`) -- ps.size
                    // is value rounded up to a 4-byte boundary (+4),
                    // not the raw decoded byte count, so pad up to
                    // ps.size (zero-filled) rather than storing exactly
                    // `value` bytes: storing fewer than ps.size would
                    // make that later copy read past the end of this
                    // allocation.
                    SplitResult parts(suite, " ");
                    char *name = (char *)calloc(ps.size, 1);
                    int n = 0;
                    for (int i = 0; i < parts.size() && n < ps.size; i++)
                    {
                        unsigned int byteVal = 0;
                        sscanf(parts.get(i), "%x", &byteVal);
                        name[n++] = (char)byteVal;
                    }
                    ps.addText(name, ps.size);
                    free(name);
                }
                return ps;
            }
            asm_Error.error = 1;
            asm_Error.error_message = (char *)"Prior instruction is not a label";
            return ps;
        }
        result_parse_line ps;
        asm_Error.error = 1;
        asm_Error.error_message = (char *)"Not Valid size for bytes";
        return ps;
    }
    if (strcmp(op, ".json") == 0)
    {
        // Pure metadata (see visitnode.cpp's _visitjsonBindingNode): store
        // the whole "<path> @_<var> <vartype>" operand text verbatim and
        // re-split it later in createBinaryHeader(), where the referenced
        // variable's real address is actually resolvable. Deliberately
        // leaves .size/.bincode at their zero defaults so addInstr()'s
        // memcpy is a no-op and no instruction-stream address space gets
        // consumed by it.
        result_parse_line ps;
        ps.op = opCodeType::json_binding;
        // string_format() makes a fresh copy -- trim(sp.operandes) only
        // returns an offset into sp.operandes's own allocation, which
        // assembleBuffer() frees right after parseline() returns, so
        // storing that pointer directly (as opposed to e.g. the .bytes
        // handling above, which only ever reads it locally) would leave
        // asm_text holding a dangling pointer.
        ps.addText(string_format("%s", trim(sp.operandes)));
        asm_Error.error = 0;
        return ps;
    }
    if (strcmp(op, ".global") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 1, op_global, 0, NULL);
        ps.op = opCodeType::function_declaration;
        return ps;
    }
    if (strcmp(op, ".var") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 1, op_global, 0, NULL);
        ps.op = opCodeType::variable;
        return ps;
    }

    if (strcmp(op, "callExt") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 2, op_callExt, 0, bin_movExt);
        if (asm_Error.error != 0)
            return ps;

        int savbin = (int)ps.bincode;
        char *l32rArgs = string_format("a%d,%s", ps.bincode, ps.getText());
        ps = parseOperandes(l32rArgs, 2, op_l32r, 3, bin_l32r);
        free(l32rArgs);
        ps.op = opCodeType::jump_32aligned;
        ps.calculateOfssetJump = jump_l32r;
        int index = findLabel(ps.getText(), asm_parsed);
        if (index > -1)
        {
            result_parse_line *ps1 = getInstrAtPos(index);
            ps1->op = opCodeType::external_call;
            ps1->align = true;
        }
        addInstr(ps, asm_parsed);
        char *callxArgs = string_format("a%d", savbin);
        ps = parseOperandes(callxArgs, 1, op_callx8, 3, bin_callx8);
        free(callxArgs);
        return ps;
    }

    if (strcmp(op, "movExt") == 0)
    {
        result_parse_line ps = parseOperandes(sp.operandes, 2, op_l32r, 3, bin_l32r);
        ps.op = opCodeType::jump_32aligned;
        ps.calculateOfssetJump = jump_l32r;
        int index = findLabel(ps.getText(), asm_parsed);
        if (index > -1)
        {
            result_parse_line *ps1 = getInstrAtPos(index);
            ps1->op = opCodeType::external_var_label;
        }
        return ps;
    }

    /************float operator********/
    if (strcmp(op, "lsi") == 0)
        return parseOperandes(sp.operandes, 3, op_lsi, 3, bin_lsi);
    if (strcmp(op, "ssi") == 0)
        return parseOperandes(sp.operandes, 3, op_lsi, 3, bin_ssi);
    if (strcmp(op, "rfr") == 0)
        return parseOperandes(sp.operandes, 2, op_rfr, 3, bin_rfr);
    if (strcmp(op, "wfr") == 0)
        return parseOperandes(sp.operandes, 2, op_wfr, 3, bin_wfr);
    if (strcmp(op, "add.s") == 0)
        return parseOperandes(sp.operandes, 3, op_adds, 3, bin_adds);
    if (strcmp(op, "sub.s") == 0)
        return parseOperandes(sp.operandes, 3, op_adds, 3, bin_subs);
    if (strcmp(op, "float.s") == 0)
        return parseOperandes(sp.operandes, 3, op_floats, 3, bin_floats);
    if (strcmp(op, "mul.s") == 0)
        return parseOperandes(sp.operandes, 3, op_adds, 3, bin_muls);
    if (strcmp(op, "trunc.s") == 0)
        return parseOperandes(sp.operandes, 3, op_truncs, 3, bin_truncs);
    if (strcmp(op, "mov.s") == 0)
        return parseOperandes(sp.operandes, 2, op_movs, 3, bin_movs);
    if (strcmp(op, "abs.s") == 0)
        return parseOperandes(sp.operandes, 2, op_movs, 3, bin_abss);
    if (strcmp(op, "neg.s") == 0)
        return parseOperandes(sp.operandes, 2, op_negs, 3, bin_negs);
    // The rest of the Xtensa FP Option's reciprocal/division-support
    // subset -- Xtensa has no hardware FDIV, so float division compiles
    // to a call to a hand-assembled __div helper built entirely out of
    // these (see visitnode.cpp's _div[] and TokenSlash's float-divide
    // case). Their encoders/operand tables were already ported to
    // asm_encoders.h; only this dispatch was missing.
    if (strcmp(op, "div0.s") == 0)
        return parseOperandes(sp.operandes, 2, op_div0s, 3, bin_div0s);
    if (strcmp(op, "nexp01.s") == 0)
        return parseOperandes(sp.operandes, 2, op_nexp01s, 3, bin_nexp01s);
    if (strcmp(op, "maddn.s") == 0)
        return parseOperandes(sp.operandes, 3, op_maddns, 3, bin_maddns);
    if (strcmp(op, "mkdadj.s") == 0)
        return parseOperandes(sp.operandes, 2, op_mkdadjs, 3, bin_mkdadjs);
    if (strcmp(op, "addexpm.s") == 0)
        return parseOperandes(sp.operandes, 2, op_addexpms, 3, bin_addexpms);
    if (strcmp(op, "addexp.s") == 0)
        return parseOperandes(sp.operandes, 2, op_addexps, 3, bin_addexps);
    if (strcmp(op, "const.s") == 0)
        return parseOperandes(sp.operandes, 2, op_consts, 3, bin_consts);
    if (strcmp(op, "divn.s") == 0)
        return parseOperandes(sp.operandes, 3, op_divns, 3, bin_divns);

    result_parse_line res;
    asm_Error.error = 1;
    asm_Error.error_message = string_format("Opcode %s not found", op);
    return res;
}

// ---- driver ----

static void assembleBuffer(Text *text, int lineOffset, parsedLines *asm_parsed, asm_error_message_struct *main_error)
{
    int size = text->size();
    for (int i = lineOffset; i < lineOffset + size; i++)
    {
        char *front = text->front();
        if (front != NULL && strcmp(front, " ") != 0)
        {
            asm_line res = splitOpcodeOperande(front);
            if (!res.error)
            {
                result_parse_line re_sparse = parseline(res, asm_parsed);
                re_sparse.line = i + 1;
                if (asm_Error.error)
                {
                    main_error->error = 1;
                    char *msg = string_format("line:%d %s\n", i, asm_Error.error_message ? asm_Error.error_message : "");
                    main_error->error_message = main_error->error_message
                                                     ? str_concat("%s%s", main_error->error_message, msg)
                                                     : msg;
                    if (main_error->error_message != msg)
                        free(msg);
                }
                else
                {
                    addInstr(re_sparse, asm_parsed);
                }
            }
            free(res.opcde);
            free(res.operandes);
        }
        text->pop_front();
    }
}

static asm_error_message_struct parseASM(Text *_footer, Text *_header, Text *_content, parsedLines *asm_parsed)
{
    asm_error_message_struct main_error;
    main_error.error = 0;
    main_error.error_message = NULL;

    tmp_exec = NULL;
    _address_instr = 0;
    _address_data = 0;
    _tmp_data_address = 0;
    _instr_size = 0;
    _data_size = 0;
    tmp_data_size = 0;

    int nb_align_label = 0;
    int nb_not_aligned_label = 0;
    for (int pass = 0; pass < 2; pass++)
    {
        Text *t = pass == 0 ? _content : _footer;
        for (int i = 0; i < t->size(); i++)
        {
            char *str = t->getText(i);
            if (strncmp(str, "@_", 2) == 0)
                nb_align_label++;
            else if (strchr(str, ':') != NULL)
                nb_not_aligned_label++;
        }
    }

    // Every ".bytes" line in header reserves 4 bytes of instruction-stream
    // address space (the jump table addInstr's data branch consumes), which
    // isn't covered by the content/footer-only heuristic below -- count
    // them explicitly so the buffer is sized large enough.
    int header_reservations = 0;
    for (int i = 0; i < _header->size(); i++)
    {
        char *line = _header->getText(i);
        if (strncmp(line, ".bytes", 6) == 0)
        {
            header_reservations++;
            // ".bytes N XX XX XX ..." (a string literal's initial
            // value -- see the parser's own .bytes handling for the
            // full explanation) needs its N-rounded-up-to-4-plus-4
            // bytes reserved in tmp_exec's *temporary data-staging*
            // area too (tmp_binary_data, past _instr_size), not just
            // the 4-byte jump-table slot every ".bytes" line gets
            // above -- addInstr() copies that many bytes into it once
            // this line gets assembled. ".bytes N" alone (no trailing
            // values, e.g. the built-in _handle_/_execaddr_/_sync
            // scratch words) doesn't need this -- there's no payload
            // to stage. Ported from upstream ESPLiveScript's identical
            // asm_parser.h size-estimation pass, which this v2 port
            // had dropped -- without it, tmp_exec was always allocated
            // 4 bytes too small for any actual string content,
            // overflowed the moment addInstr() copied it in.
            char *rest = trim(line + 6);
            if (strchr(rest, ' ') != NULL)
            {
                long n = strtol(rest, NULL, 10);
                tmp_data_size += (uint16_t)((n / 4) * 4 + 4);
            }
        }
    }

    _instr_size = (uint16_t)((nb_align_label + 1) * ALIGN_INSTR +
                              (_content->size() + _footer->size() - nb_not_aligned_label) * 3 +
                              header_reservations * 4);
    _instr_size = (_instr_size / 8) * 8 + 8;
    tmp_instr_size = (uint16_t)(_instr_size + (tmp_data_size / 4) * 4 + 4);

    tmp_exec = (uint8_t *)malloc(tmp_instr_size);
    memset(tmp_exec, 0, tmp_instr_size);
    tmp_binary_data = tmp_exec + _instr_size;

    int tmp_size = _header->size();
    int tmp_size2 = _content->size() + _header->size();
    assembleBuffer(_header, 0, asm_parsed, &main_error);
    assembleBuffer(_content, tmp_size, asm_parsed, &main_error);
    assembleBuffer(_footer, tmp_size2, asm_parsed, &main_error);

    if (main_error.error == 1)
        free(tmp_exec);
    else
        // Every reservation (jump-table slot's backing "value" address --
        // handle/execaddr/sync scratch words, internal constants, ...)
        // lives in this range even when nothing is ever written there, so
        // the loader's data buffer needs to be at least this big.
        _data_size = (uint16_t)_address_data;

    return main_error;
}

static asm_error_message_struct calculateJump(uint8_t *exec, parsedLines *asm_parsed)
{
    asm_error_message_struct error;
    error.error = 0;
    error.error_message = NULL;
    for (int i = 0; i < asm_parsed->size(); i++)
    {
        result_parse_line *parse_line = asm_parsed->getChildAtPos(i);
        if (parse_line->op == opCodeType::jump || parse_line->op == opCodeType::jump_32aligned)
        {
            int index = findLabel(parse_line->getText(), asm_parsed);
            if (index != -1)
            {
                if (parse_line->calculateOfssetJump)
                {
                    parse_line->bincode = parse_line->calculateOfssetJump(
                        parse_line->bincode, parse_line->address, getInstrAtPos(index)->address);
                    memcpy(exec + parse_line->address, &parse_line->bincode, parse_line->size);
                }
                else
                {
                    error.error = 1;
                }
            }
            else
            {
                error.error = 1;
                error.error_message = string_format("line : %d label %s not found\n", parse_line->line, parse_line->getText());
            }
        }
    }
    return error;
}

static uint8_t *createBinaryHeader(parsedLines *asm_parsed)
{
    uint16_t nb_data = 0;
    uint16_t nb_objects = 0;
    uint8_t type;
    uint16_t text_size;
    binary_header_size = 2;

    for (int i = 0; i < asm_parsed->size(); i++)
    {
        result_parse_line *it = asm_parsed->getChildAtPos(i);
        if (it->op == opCodeType::data_label || it->op == opCodeType::number_label)
        {
            nb_objects++;
            binary_header_size += 1 + 4 + 2;
        }
        else if (it->op == opCodeType::external_var_label || it->op == opCodeType::external_call)
        {
            nb_objects++;
            binary_header_size += 1 + 2 + (uint16_t)(strlen(it->getText()) + 1) + 2;
        }
        else if (it->op == opCodeType::function_declaration)
        {
            nb_objects++;
            binary_header_size += 1 + 2 + (uint16_t)(strlen(it->getText()) + 1);
            result_parse_line *it2 = asm_parsed->getChildAtPos(i + 1);
            binary_header_size += 2 + (uint16_t)(strlen(it2->getText()) + 1);
            binary_header_size += 2 + 4 + 4;
        }
        else if (it->op == opCodeType::json_binding)
        {
            nb_objects++;
            SplitResult parts(it->getText(), " ");
            binary_header_size += 1 + 2 + (uint16_t)(strlen(parts.get(0)) + 1) + 4 + 1;
        }
        else if (it->op == opCodeType::data)
        {
            // A string literal's initial value (see the .bytes parser
            // and addInstr()'s data branch) -- type 3 (addr + running
            // tmp_data offset + size). decodeBinaryHeader's case 3
            // (asm_execute.cpp) already implements the consuming half
            // of this -- copying these bytes into the real data region
            // at load time -- ported from upstream ESPLiveScript
            // already, just never fed a type-3 record to consume,
            // since nothing ever emitted one. This is the missing
            // producer half.
            nb_objects++;
            binary_header_size += 1 + 4 + 2 + 2;
        }
    }

    uint8_t *_binary_header = (uint8_t *)malloc(binary_header_size);
    binary_header = _binary_header;
    memcpy(binary_header, &nb_objects, 2);
    binary_header += 2;

    // Running offset into the temporary data-staging area (tmp_binary_data,
    // asm_parser.cpp's addInstr()) each type-3 record's bytes came from --
    // matches decodeBinaryHeader's case-3 `exec + offset + tmp_data` source
    // address exactly, since addInstr() appended each string's bytes there
    // in the same order asm_parsed itself holds them.
    uint16_t tmp_data = 0;

    for (int i = 0; i < asm_parsed->size(); i++)
    {
        result_parse_line *it = asm_parsed->getChildAtPos(i);
        if (it->op == opCodeType::data_label || it->op == opCodeType::number_label)
        {
            type = 0;
            memcpy(binary_header, &type, 1);
            binary_header += 1;
            memcpy(binary_header, &it->bincode, 4);
            binary_header += 4;
            memcpy(binary_header, &nb_data, 2);
            binary_header += 2;
            nb_data++;
        }
        else if (it->op == opCodeType::external_var_label)
        {
            type = 1;
            memcpy(binary_header, &type, 1);
            binary_header += 1;
            text_size = (uint16_t)(strlen(it->getText()) + 1);
            memcpy(binary_header, &text_size, 2);
            binary_header += 2;
            memcpy(binary_header, it->getText(), text_size - 1);
            binary_header[text_size - 1] = 0;
            binary_header += text_size;
            memcpy(binary_header, &nb_data, 2);
            binary_header += 2;
            nb_data++;
        }
        else if (it->op == opCodeType::external_call)
        {
            type = 2;
            memcpy(binary_header, &type, 1);
            binary_header += 1;
            text_size = (uint16_t)(strlen(it->getText()) + 1);
            memcpy(binary_header, &text_size, 2);
            binary_header += 2;
            memcpy(binary_header, it->getText(), text_size - 1);
            binary_header[text_size - 1] = 0;
            binary_header += text_size;
            memcpy(binary_header, &nb_data, 2);
            binary_header += 2;
            nb_data++;
        }
        else if (it->op == opCodeType::function_declaration)
        {
            type = 4;
            memcpy(binary_header, &type, 1);
            binary_header += 1;
            text_size = (uint16_t)(strlen(it->getText()) + 1);
            memcpy(binary_header, &text_size, 2);
            binary_header += 2;
            memcpy(binary_header, it->getText(), text_size - 1);
            binary_header[text_size - 1] = 0;
            binary_header += text_size;

            result_parse_line *it2 = asm_parsed->getChildAtPos(i + 1);
            text_size = (uint16_t)(strlen(it2->getText()) + 1);
            memcpy(binary_header, &text_size, 2);
            binary_header += 2;
            memcpy(binary_header, it2->getText(), text_size - 1);
            binary_header[text_size - 1] = 0;
            binary_header += text_size;

            SplitResult args(trim(it2->getText()), " ");
            int num = 0;
            if (args.size() > 0)
                sscanf(args.get(0), "%d", &num);
            memcpy(binary_header, &num, 2);
            binary_header += 2;

            int index = findLabel(it->getText(), asm_parsed);
            uint32_t bc = index > -1 ? getInstrAtPos(index)->address : 0;
            memcpy(binary_header, &bc, 4);
            binary_header += 4;
            memcpy(binary_header, &bc, 4);
            binary_header += 4;
        }
        else if (it->op == opCodeType::json_binding)
        {
            type = 5;
            memcpy(binary_header, &type, 1);
            binary_header += 1;

            SplitResult parts(it->getText(), " ");
            text_size = (uint16_t)(strlen(parts.get(0)) + 1);
            memcpy(binary_header, &text_size, 2);
            binary_header += 2;
            memcpy(binary_header, parts.get(0), text_size - 1);
            binary_header[text_size - 1] = 0;
            binary_header += text_size;

            // The target variable's own @_name label -- declared
            // normally (see parser.cpp's json-binding branch and its
            // comment) -- resolves through the same data_label path
            // every other internal variable uses; .bincode is its
            // offset into the data region (see addInstr()'s data/number
            // branch), exactly what the loader needs to compute
            // ex->data + address.
            int index = findLabel(parts.get(1), asm_parsed);
            uint32_t bc = index > -1 ? getInstrAtPos(index)->bincode : 0;
            memcpy(binary_header, &bc, 4);
            binary_header += 4;

            uint8_t vartype = 0;
            sscanf(parts.get(2), "%hhu", &vartype);
            memcpy(binary_header, &vartype, 1);
            binary_header += 1;
        }
        else if (it->op == opCodeType::data)
        {
            type = 3;
            memcpy(binary_header, &type, 1);
            binary_header += 1;
            memcpy(binary_header, &it->address, 4);
            binary_header += 4;
            memcpy(binary_header, &tmp_data, 2);
            binary_header += 2;
            memcpy(binary_header, &it->size, 2);
            binary_header += 2;
            tmp_data += it->size;
        }
    }
    return _binary_header;
}

Binary createBinary(Text *_footer, Text *_header, Text *_content, bool display)
{
    Binary bin;
    bin.binary_data = NULL;
    bin.function_data = NULL;
    bin.error.error = 0;
    _asm_parsed.clear();

    asm_error_message_struct err = parseASM(_footer, _header, _content, &_asm_parsed);

    _header->clear();
    _content->clear();
    _footer->clear();

    if (err.error == 0)
    {
        asm_error_message_struct error = calculateJump(tmp_exec, &_asm_parsed);
        if (error.error == 1)
        {
            bin.error = error;
            _asm_parsed.clear();
            asm_text.clear();
            return bin;
        }

        uint8_t *rr = createBinaryHeader(&_asm_parsed);

        bin.binary_data = tmp_exec;
        bin.function_data = rr;
        bin.function_size = binary_header_size;
        bin.data_size = _data_size;
        bin.instruction_size = _instr_size;
        bin.tmp_instruction_size = tmp_instr_size;
    }
    else
    {
        bin.error = err;
        bin.error.error = 1;
    }
    _asm_parsed.clear();
    asm_text.clear();
    return bin;
}
