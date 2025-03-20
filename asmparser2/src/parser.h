#pragma once
#ifndef __PARSER__
#define __PARSER__
#include "parser_define.h"
#include "nodetoken.h"
#include "compiler_error.h"

#define _START_2 32
#define _STACK_SIZE (_START_2 + 6 * 4)
#define _MAX_FOR_DEPTH_REG 4
#define _MAX_FOR_DEPTH_REG_2 2
#define RETURN_ERROR(x) { printf("line:%d\n",__LINE__);Error.error=x;Error.token=current();return;}
#define RETURN_IF_ERROR if(Error.error!=noError) return;
class Parser
{
public:
    NodeToken root;
    Parser();

    int size();
    Token *getTokenAtPos(int pos);
    Token *current();
    Token *next();
    Token *prev();
    Token *peek(int index);
    bool Match(tokenType tt);
    bool Match(tokenType tt, int index);
    void parse(Script *main_script, Tokens *__tks);
    void parseProgram();
    void parseType();
    void parseVariableForCreation();
    void parseFactor();
    void parseFunctionCall();
    void parseDefFunction();
    void parseExpr();
    void parseExprConditionnal();
    void parseExprAndOr();
    void parseTerm();
    void parseBlockStatement();
    void parseCreateArguments();
    void parseStatement();
    void getVariable(bool isStore);
    void parseComparaison();
    void parseArguments();
    void clean();
    Tokens *_tks;
};

char * findForWhile();
NodeToken createNodeLocalVariableForCreation(NodeToken var, NodeToken nd);
void createNodeVariable(Token *_var, bool isStore);
varTypeEnum finduint32_t(NodeToken *nd);
varTypeEnum findfloat(NodeToken *nd);
extern NodeToken program, *current_node;
extern NodeToken main_context, *current_cntx;
extern NodeToken functions;


extern Stack<uint16_t> targetList;
extern error_message_struct Error;
#endif