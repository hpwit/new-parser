#pragma once
#ifndef __ARGUMENTS__
#define __ARGUMENTS__
// Ported from upstream ESPLiveScript's ESPLivescriptRuntime.h: a typed,
// ordered list of int/float values to pass to a compiled script function,
// nicer to build up than a raw int32_t[] (which can't distinguish "this
// slot is a float" from "this slot is an int" -- see asm_execute.h's
// callFunction()/runExecutableWithArgs() comment on why floats are passed
// as their raw bits reinterpreted as int32_t: Xtensa's windowed ABI here
// always uses the integer registers a2-a7/a10-a15, even for float
// parameters -- confirmed in visitnode.cpp's _visitdefInputArgumentsNode,
// which stores a float parameter with the same s32i integer-store
// instruction as an int one).
#include "vect.h"
#include "parser_enum.h"

struct _arguments
{
    varTypeEnum vartype;
    int intval;
    float floatval;

    _arguments() : vartype(__unknown__), intval(0), floatval(0) {}
    _arguments(int val) : vartype(__int__), intval(val), floatval(0) {}
    _arguments(float val) : vartype(__float__), intval(0), floatval(val) {}
};

class Arguments
{
public:
    Arguments() {}
    void add(int val) { _args.push_back(_arguments(val)); }
    void add(float val) { _args.push_back(_arguments(val)); }
    void add(_arguments a) { _args.push_back(a); }
    void clear() { _args.clear(); }
    int size() { return _args.size(); }
    vect<_arguments> _args;
};

#endif
