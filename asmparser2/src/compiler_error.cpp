
#include "compiler_error.h"
#include "tokenize.h"
#include "string_functions.h"
#include "parser_define.h"



const char *error_messages[]=
{
    "",
    "Expecting external, __ASM__  or variable type",
    "Variable already declared in current scope",
    "Expecting ;",
    "Expecting an integer",
    "Expecting ]",
    "Expecting an integer or ]",
    "Unknown token",
    "Invalid Statement",
    "Expecting }",
    "Function already declared",
    "Expecting {",
"Impossible to redifine external function expected ;",
"Expecting )",
"no For or while found for break instruction",
"Issue with return",
"Expecting =",
"Expecting (",
"Impossible to find variable declaration " ,
"Expecting ; or =",
"Too many arguments",
"expecting ]  or ,",
"Member does not exist",
"Expecting comma",
"Impossible to find token",
"Function not found",
"Wrong number of arguments",
"Expecting ; or )",
"Expecting a string literal json path after json",
"Expecting as",
"case or default outside a switch",
"Expecting an integer constant after case",
"Duplicate case value",
"More than one default in a switch",
"Expecting :",
"switch needs an integer value",
"A structure passed by value must be one of the first 6 arguments",
"Too many fields in the structure",
"Structure already defined differently (bindStruct)",
"This member can't be used through a pointer (only plain numbers and char)",
"* needs a pointer variable",
"Can't take the address of this variable"

};
void display_error(error_message_struct *err){
    printf("%s \n",error_messages[err->error]);
    if(err->token!=NULL)
       displayLine (err->token);
}