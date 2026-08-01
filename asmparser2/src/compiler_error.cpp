
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
"Expecting as"

};
void display_error(error_message_struct *err){
    printf("%s \n",error_messages[err->error]);
    if(err->token!=NULL)
       displayLine (err->token);
}