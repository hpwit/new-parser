#pragma once
// A tiny interpreter for the (integer-only) subset of Xtensa assembly this
// compiler emits for straight-line arithmetic + if/while/for control flow
// in a single, non-recursive, call-free function: movi/mov/movr, add/sub/
// mull/quou/quos/remu/and/or/neg/abs, addi, l32i/s32i/l16ui/l16si/s16i/
// l8ui/s8i, ssl/sll/srl, extui, blt/bge/beq/bne, j, retw.n, entry (a no-op
// here), and bare "label:" markers.
//
// It deliberately does NOT support call8/callExt (real Xtensa register
// windowing and this compiler's external-call ABI are both out of scope
// for a test harness) or any floating point instruction. run() reports
// failure rather than guessing if it hits one, so a test can never
// mistake "couldn't interpret" for "produced the same result".
//
// This exists purely to let a test assert that optimize() doesn't change
// what a generated program computes -- it is not a general Xtensa
// simulator and makes no attempt to be one.
#include "stackfunctions.h"
#include "string_functions.h"
#include <stdint.h>
#include <string.h>

struct MiniXtensa
{
    static const int kNumRegs = 16;
    static const int kMemSize = 1024;

    int32_t regs[kNumRegs];
    uint8_t mem[kMemSize];
    int32_t sar;

    MiniXtensa()
    {
        memset(regs, 0, sizeof(regs));
        memset(mem, 0, sizeof(mem));
        sar = 0;
    }

    // Runs `lines` (a snapshot of Text's line array) from the first
    // "entry a1,..." found through the matching "retw.n". On success
    // returns true; *error is left NULL. On any instruction this
    // interpreter doesn't recognize, returns false and *error explains
    // why (caller should treat that as "not comparable", not "mismatch").
    bool run(vect<char *> &lines, const char **error)
    {
        *error = NULL;

        // Pass 1: index every "label:" line by its bare name (trailing
        // ':' stripped) so branches/jumps can resolve targets regardless
        // of the naming scheme in use (label_0, label_0_end, @_main(),
        // loop_label_3, ...).
        vect<char *> labelNames;
        vect<int> labelPos;
        int startPc = -1;
        for (int i = 0; i < lines.size(); i++)
        {
            char *line = lines.get(i);
            int len = (int)strlen(line);
            if (len > 0 && line[len - 1] == ':')
            {
                char *name = (char *)malloc(len);
                memcpy(name, line, len - 1);
                name[len - 1] = 0;
                labelNames.push_back(name);
                labelPos.push_back(i);
            }
            else if (startPc == -1 && strncmp(line, "entry ", 6) == 0)
            {
                startPc = i + 1;
            }
        }

        if (startPc == -1)
        {
            *error = "no entry instruction found";
            labelNames.empty();
            labelPos.clear();
            return false;
        }

        bool ok = true;
        int pc = startPc;
        int guard = 0;
        while (true)
        {
            if (++guard > 100000)
            {
                *error = "step limit exceeded (probable infinite loop)";
                ok = false;
                break;
            }
            if (pc < 0 || pc >= lines.size())
            {
                *error = "control flow ran off the end of the buffer";
                ok = false;
                break;
            }

            char *line = lines.get(pc);
            int len = (int)strlen(line);
            bool isBlank = (len == 0) || (len == 1 && line[0] == ' ');
            bool isLabel = (len > 0 && line[len - 1] == ':');
            if (isBlank || isLabel)
            {
                pc++;
                continue;
            }

            vect<char *> tok;
            str_split(&tok, line, (char *)" ");
            if (tok.size() == 0)
            {
                tok.empty();
                pc++;
                continue;
            }
            char *op = tok.get(0);

            if (strcmp(op, "retw.n") == 0)
            {
                tok.empty();
                break;
            }
            if (strncmp(op, "entry", 5) == 0)
            {
                tok.empty();
                pc++;
                continue;
            }

            if (strcmp(op, "j") == 0)
            {
                pc = findLabel(labelNames, labelPos, tok.get(1));
                if (pc < 0)
                {
                    *error = "unresolved jump target";
                    ok = false;
                }
                tok.empty();
                if (!ok)
                    break;
                continue;
            }

            if (tok.size() < 2)
            {
                *error = "instruction with no operands";
                ok = false;
                tok.empty();
                break;
            }

            vect<char *> args;
            str_split(&args, tok.get(1), (char *)",");

            bool handled = execOne(op, args, labelNames, labelPos, &pc, error);
            args.empty();
            tok.empty();
            if (!handled)
            {
                ok = false;
                break;
            }
        }

        labelNames.empty();
        labelPos.clear();
        return ok;
    }

    int32_t readReg(char *tok) { return regs[regNum(tok)]; }

    static int regNum(char *tok)
    {
        // tok looks like "a3"
        return atoi(tok + 1);
    }

    static int32_t toI32(char *tok) { return (int32_t)atol(tok); }

    static int findLabel(vect<char *> &names, vect<int> &pos, char *name)
    {
        for (int i = 0; i < names.size(); i++)
            if (strcmp(names.get(i), name) == 0)
                return pos.get(i);
        return -1;
    }

    int32_t load(int32_t addr, int size, bool signExtend)
    {
        if (addr < 0 || addr + size > kMemSize)
            return 0;
        int32_t v = 0;
        memcpy(&v, mem + addr, size);
        if (signExtend)
        {
            if (size == 1)
                v = (int8_t)v;
            else if (size == 2)
                v = (int16_t)v;
        }
        else
        {
            if (size == 1)
                v &= 0xFF;
            else if (size == 2)
                v &= 0xFFFF;
        }
        return v;
    }

    void store(int32_t addr, int size, int32_t v)
    {
        if (addr < 0 || addr + size > kMemSize)
            return;
        memcpy(mem + addr, &v, size);
    }

    // args is the comma-split operand list (register/immediate/label
    // tokens), already separated from the mnemonic. pc is advanced for
    // straight-line instructions by the caller's loop; branch/jump
    // instructions update *pcInOut directly and this returns before the
    // caller's default advance would apply (caller checks that by re-
    // reading *pcInOut itself -- see the dedicated pc handling below).
    bool execOne(char *op, vect<char *> &args, vect<char *> &labelNames, vect<int> &labelPos, int *pcInOut, const char **error)
    {
        int pc = *pcInOut;

        if (strcmp(op, "movi") == 0)
        {
            regs[regNum(args.get(0))] = toI32(args.get(1));
        }
        else if (strcmp(op, "mov") == 0 || strcmp(op, "movr") == 0)
        {
            regs[regNum(args.get(0))] = readReg(args.get(1));
        }
        else if (strcmp(op, "add") == 0)
            regs[regNum(args.get(0))] = readReg(args.get(1)) + readReg(args.get(2));
        else if (strcmp(op, "sub") == 0)
            regs[regNum(args.get(0))] = readReg(args.get(1)) - readReg(args.get(2));
        else if (strcmp(op, "mull") == 0)
            regs[regNum(args.get(0))] = readReg(args.get(1)) * readReg(args.get(2));
        else if (strcmp(op, "quou") == 0 || strcmp(op, "quos") == 0)
        {
            int32_t b = readReg(args.get(2));
            regs[regNum(args.get(0))] = (b == 0) ? 0 : readReg(args.get(1)) / b;
        }
        else if (strcmp(op, "remu") == 0)
        {
            int32_t b = readReg(args.get(2));
            regs[regNum(args.get(0))] = (b == 0) ? 0 : readReg(args.get(1)) % b;
        }
        else if (strcmp(op, "and") == 0)
            regs[regNum(args.get(0))] = readReg(args.get(1)) & readReg(args.get(2));
        else if (strcmp(op, "or") == 0)
            regs[regNum(args.get(0))] = readReg(args.get(1)) | readReg(args.get(2));
        else if (strcmp(op, "neg") == 0)
            regs[regNum(args.get(0))] = -readReg(args.get(1));
        else if (strcmp(op, "abs") == 0)
        {
            int32_t v = readReg(args.get(1));
            regs[regNum(args.get(0))] = v < 0 ? -v : v;
        }
        else if (strcmp(op, "addi") == 0)
            regs[regNum(args.get(0))] = readReg(args.get(1)) + toI32(args.get(2));
        else if (strcmp(op, "ssl") == 0)
            sar = readReg(args.get(0)) & 31;
        else if (strcmp(op, "sll") == 0)
            regs[regNum(args.get(0))] = readReg(args.get(1)) << sar;
        else if (strcmp(op, "srl") == 0)
            regs[regNum(args.get(0))] = (int32_t)(((uint32_t)readReg(args.get(1))) >> sar);
        else if (strcmp(op, "extui") == 0)
        {
            int shift = (int)toI32(args.get(2));
            int maskBits = (int)toI32(args.get(3));
            uint32_t mask = (maskBits >= 32) ? 0xFFFFFFFFu : ((1u << maskBits) - 1u);
            regs[regNum(args.get(0))] = (int32_t)(((uint32_t)readReg(args.get(1)) >> shift) & mask);
        }
        else if (strcmp(op, "l32i") == 0)
            regs[regNum(args.get(0))] = load(readReg(args.get(1)) + toI32(args.get(2)), 4, false);
        else if (strcmp(op, "s32i") == 0)
            store(readReg(args.get(1)) + toI32(args.get(2)), 4, readReg(args.get(0)));
        else if (strcmp(op, "l16ui") == 0)
            regs[regNum(args.get(0))] = load(readReg(args.get(1)) + toI32(args.get(2)), 2, false);
        else if (strcmp(op, "l16si") == 0)
            regs[regNum(args.get(0))] = load(readReg(args.get(1)) + toI32(args.get(2)), 2, true);
        else if (strcmp(op, "s16i") == 0)
            store(readReg(args.get(1)) + toI32(args.get(2)), 2, readReg(args.get(0)));
        else if (strcmp(op, "l8ui") == 0)
            regs[regNum(args.get(0))] = load(readReg(args.get(1)) + toI32(args.get(2)), 1, false);
        else if (strcmp(op, "s8i") == 0)
            store(readReg(args.get(1)) + toI32(args.get(2)), 1, readReg(args.get(0)));
        else if (strcmp(op, "blt") == 0 || strcmp(op, "bge") == 0 || strcmp(op, "beq") == 0 || strcmp(op, "bne") == 0)
        {
            int32_t a = readReg(args.get(0));
            int32_t b = readReg(args.get(1));
            bool taken = (strcmp(op, "blt") == 0 && a < b) ||
                         (strcmp(op, "bge") == 0 && a >= b) ||
                         (strcmp(op, "beq") == 0 && a == b) ||
                         (strcmp(op, "bne") == 0 && a != b);
            if (taken)
            {
                int target = findLabel(labelNames, labelPos, args.get(2));
                if (target < 0)
                {
                    *error = "unresolved branch target";
                    return false;
                }
                *pcInOut = target;
                return true;
            }
        }
        else if (strcmp(op, "blti") == 0 || strcmp(op, "bgei") == 0 || strcmp(op, "beqi") == 0 || strcmp(op, "bnei") == 0)
        {
            // Second operand is a *pre-encoded b4const table index*
            // (0-15), not the literal value being compared against --
            // see visitnode.cpp's isBranchImmediate()/valBranchImmediate()
            // and asm_encoders.h's op_blti for the same table.
            static const int32_t b4const[16] = {-1, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256};
            int32_t a = readReg(args.get(0));
            int idx = (int)toI32(args.get(1));
            int32_t b = (idx >= 0 && idx < 16) ? b4const[idx] : 0;
            bool taken = (strcmp(op, "blti") == 0 && a < b) ||
                         (strcmp(op, "bgei") == 0 && a >= b) ||
                         (strcmp(op, "beqi") == 0 && a == b) ||
                         (strcmp(op, "bnei") == 0 && a != b);
            if (taken)
            {
                int target = findLabel(labelNames, labelPos, args.get(2));
                if (target < 0)
                {
                    *error = "unresolved branch target";
                    return false;
                }
                *pcInOut = target;
                return true;
            }
        }
        else
        {
            *error = op; // caller's error string just names the unsupported mnemonic
            return false;
        }

        *pcInOut = pc + 1;
        return true;
    }
};
