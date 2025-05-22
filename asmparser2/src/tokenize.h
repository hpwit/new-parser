#pragma once
#ifndef __TOKENIZER__
#define __TOKENIZER__
#include "string_functions.h"
#include "parser_enum.h"
#include "stackfunctions.h"
#include "parser_define.h"

struct varType
{
    varTypeEnum _varType;
    char *varName;
    uint8_t _varSize;
    asmInstruction load[20];
    asmInstruction store[20];
    char *membersNames[20];
    uint8_t starts[20];
    uint8_t memberSize[20];
    varTypeEnum types[20];
    uint8_t sizes[20];
    uint8_t size;
    uint8_t total_size;
};

/*
typedef struct
{
    tokenType type;
    varType *_vartype;
    char *text_ref;
    char *start_line;
    uint16_t line;
    uint8_t pos;
} token;
*/
typedef struct
{
    uint16_t name;
    uint16_t content;
    // string hh;
} _define;




class Script
{
public:
    Script();
    void init();
    void addContent(char *str);
    void clear();
    char *currentCharPtr();
    char *firstCharPtr();
    char nextChar();

    char currentChar();

    char previousChar();


    void insert(char *toInsert);
    void insertAtEnd(char *toInsert);

private:
    // string * script;
    int position;

    vect<char *> script;
    char **it;
};

class Token
{

public:
    Token();

    Token(tokenType h);
    Token(tokenType _type, int __vartype, int _line);
    Token(tokenType _type, int __vartype);
    tokenType getType();
    void setType(tokenType _type);
    void clean();
    void addText(const char *t);
    void addText(char *t);
    void addText(char *t, char *e);
    char *getText();
    varType *getVarTypeObj();
     varTypeEnum getVarType();
    char *lineref;
    uint16_t line;
    uint16_t textref;
    uint8_t type;
    uint8_t _vartype;
    uint8_t pos;
};



class Tokens
{
public:
    void init();
    Tokens();

    void clear();
    int size();
    void tokenizelow(Script *script, bool update, bool increae_line, int nbToken);
    void tokenize(Script *script, bool update, bool increae_line, int nbToken);
    void push(Token t);
    void pop_back();
    Token *getTokenAtPos(int pos);
    Token *current();
    Token *next();
    Token *prev();
    Token *peek(int index);
    Token back();
    bool Match(tokenType tt);
    bool Match(tokenType tt, int index);
private:
    vect<Token> _tokens;
    Token end_token = Token(TokenEndOfFile);
    Script *_script;
#ifdef __FULL_TOKEN
    int position;
#endif
};

int tokenizer(Script *script, bool update, bool increae_line,
              int nbMaxTokenToRead,Tokens *_tks);
bool isIna_zA_Z_(unsigned char c);
int isKeyword(char *str, char *end);

bool isIna_zA_Z_0_9(unsigned char c);

bool isIn0_9(unsigned char c);

bool isIn0_9_x_b(unsigned char c);


void displaytoken(Token *t);
char *getDefine(char *deb, char *end);
Token transNumber(char *str, char *end,Tokens *tks);

int isUserDefined(char *s, char *end);
  void displayLine(Token *t);

int findStruct(char *structName);

int findMember(varType *v, char * member);
int findMember(uint8_t _v, char * member);
extern Tokens __allTokens;

extern Tokens _extra_tks ;
extern Text all_text;
extern const char * tokenNames[];
extern vect<uint16_t> userDefinedVarTypeNames; 
extern vect<varType> _userDefinedTypes;
extern varType _varTypes[];
extern const char * varTypeEnumNames[]; 
extern vect<_define> define_list;
extern int pos_in_line;
//char *line_ref = NULL;
extern bool isStructFunction;
extern bool insecond;
#endif