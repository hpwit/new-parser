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
caseOutsideSwitch,
expectingCaseConstant,
duplicateCase,
duplicateDefault,
expectingColon,
switchNeedsInteger,
structArgument,
structTooManyFields,
structRedefined,
pointerMemberType,
derefNotPointer,
addressNotPossible,
notIndexable,
};
struct error_message_struct
{
 
  int error;
  Token *token;
};
extern const char *error_messages[];
// More about the last error, when there is more to say (e.g. the
// signatures a call could have matched); empty otherwise.
extern char error_detail[240];
void display_error(error_message_struct *err);
#endif