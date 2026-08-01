#pragma once
#ifndef __COMPILERERROR__
#define __COMPILERERROR__
#include "tokenize.h"
#include "string_functions.h"
#include "parser_define.h"


enum errorMessages
{
noError,
expectingASMorInt,
alreadyDeclaredVariableInScope,
expectingSemicolon,
expectingInteger,
expectingClosingBracket,
expecitngIntegerorClosingBracket,
unknownToken,
invalidStatement,
expectingCloseCurlyBracket,
functionAlreadyDeclared,
expectingOpenCurlyBracket,
impossibletoredefineexternal,
expectingClosingparenthesis,
noWhileorForFound,
issuewithReturn,
expectingEqual,
expectingOpenparenthesis,
impossibletofindvariabledeclaration,
expectingsemicolonorequal,
toomanyarguments,
expectingClosingBracketorcomma,
memberdoesnotexist,
expectingcomma,
impossibletofindtoken,
functionnotfound,
wrongnumberofarguments,
expectingsemicolonorcloseparenthesis,
expectingJsonPathString,
expectingAs,
};
struct error_message_struct
{
 
  int error;
  Token *token;
};
extern const char *error_messages[];
void display_error(error_message_struct *err);
#endif