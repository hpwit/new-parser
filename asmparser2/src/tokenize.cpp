#include "tokenize.h"
#include "vect.h"
#include "parser_define.h"
#define EOF_TEXT 0
#define EOF_TEXTARRAY 9999
#define EOF_VARTYPE 14
#define __DEPTH 5


tokenType __keywordTypes[] = {
    TokenKeywordVarType,
    TokenKeywordVarType,
    TokenKeywordVarType,
    TokenKeywordVarType,
    TokenKeywordVarType,
    TokenKeywordVarType,
    TokenKeywordVarType,
    TokenKeywordVarType,
    TokenKeywordVarType,
    TokenKeywordVarType,
    TokenKeywordVarType,
    TokenKeywordVarType,
    TokenKeywordVarType,
    TokenKeywordExternalVar,
    TokenKeywordFor,
    TokenKeywordIf,
    TokenKeywordThen,
    TokenKeywordElse,
    TokenKeywordWhile,
    TokenKeywordReturn,
    TokenKeywordImport,
    TokenKeywordFrom,
    TokenKeywordASM,
    TokenKeywordDefine,
    TokenKeywordSafeMode,
    TokenKeywordHeader,
    TokenKeywordContent,
    TokenKeywordAnd,
    TokenKeywordOr,
    TokenKeywordContinue,
    TokenKeywordBreak,
    TokenKeywordFabs,
    TokenKeywordAbs,
    TokenKeywordSaveReg,
    TokenKeywordSaveRegAbs,
    TokenKeywordStruct,
    TokenOverride

};
const char* keywordTypeNames[] = {
#ifdef __TEST_DEBUG
    "KeywordVarType",
    "KeywordVarType",
    "KeywordVarType",
    "KeywordVarType",
    "KeywordVarType",
    "KeywordVarType",
    "KeywordVarType",
    "KeywordVarType",
    "KeywordVarType",
    "KeywordVarType",
    "KeywordVarType",
    "KeywordVarType",
    "KeywordExternalVar",
    "KeywordFor",
    "KeywordIf",
    "KeywordThen",
    "KeywordElse",
    "KeywordWhile",
    "KeywordReturn",
    "KeywordImport",
    "KeywordFrom",
    "KeywordASM",
    "KeywordDefine",
    "KeywordSafeMode",
    "KeywordHeader",
    "KeywordContent",
#endif
};

const char * tokenNames[] = {
#ifdef __TEST_DEBUG
    "TokenNumber",
    "TokenAddition",
    "TokenStar",
    "TokenSubstraction",
    "TokenOpenParenthesis",
    "TokenCloseParenthesis",
    "TokenOpenBracket",
    "TokenCloseBracket",
    "TokenOpenCurlyBracket",
    "TokenCloseCurlyBracket",
    "TokenEqual",
    "TokenDoubleEqual",
    "TokenIdentifier",
    "TokenSemicolon",
    "TokenUnknown",
    "TokenSpace",
    "TokenNewline",
    "TokenEndOfFile",
    "TokenSlash",
    "TokenKeyword",
    "TokenString",
    "TokenExternal",
    "TokenComma",
    "TokenLessThan",
    "TokenPlusPlus",
    "TokenMinusMinus",
    "TokenModulo",
    "TokenLessOrEqualThan",
    "TokenMoreThan",
    "TokenMoreOrEqualThan",
    "TokenNotEqual",
    "TokenNot",
    "TokenFunction",
    "TokenUppersand",
    "TokenDiese",
    "TokenLineComment",
    "TokenStartBlockComment",
    "TokenEndBlockComment",
    "TokenNegation",
    "TokenShiftLeft",
    "TokenShiftRight",
    "TokenKeywordVarType",
    "TokenKeywordExternalVar",
    "TokenKeywordFor",
    "TokenKeywordIf",
    "TokenKeywordThen",
    "TokenKeywordElse",
    "TokenKeywordWhile",
    "TokenKeywordReturn",
    "TokenKeywordImport",
    "TokenKeywordFrom",
    "TokenKeywordASM",
    "TokenKeywordDefine",
    "TokenKeywordSafeMode",
    "TokenKeywordHeader",
    "TokenKeywordContent",
    "TokenKeywordAnd",
    "TokenKeywordOr",
    "TokenPower",
    "TokenKeywordContinue",
    "TokenKeywordBreak",
    "TokenKeywordFabs",
    "TokenKeywordAbs",
    "TokenKeywordSaveReg",
    "TokenKeywordSaveRegAbs",
    "TokenKeywordStruct",
    "TokenUserDefinedName",
    "TokenUserDefinedVariable",
    "TokenMember",
    "TokenUserDefinedVariableMember",
    "TokenUserDefinedVariableMemberFunction",
    "TokenDoubleUppersand",
    "TokenDoubleOr",
    "TokenQuestionMark",
    "TokenColon",
    "TokenPlusEqual",
    "TokenMinusEqual",
    "TokenStarEqual",
    "TokenSlashEqual",
    "TokenOverride"

#endif
};
const char * varTypeEnumNames[] = {
#ifdef __TEST_DEBUG
    "__none__",
    "__unit8_t__",
    "__unit16_t__",
    "__unit32_t__",
    "__int__",
    "__s_int__",
    "__float__",
    "__void__",
    "__CRGB__",
    "__CRGBW__",
    "__char__",
    "__Args__",
    "__bool__",
    "__userDefined__",
    "__unknown__"
#endif

};

const char * keyword_array[] =
    {"none", "uint8_t", "uint16_t", "uint32_t", "int", "s_int", "float", "void", "CRGB",
     "CRGBW", "char", "Args", "bool", "external", "for", "if", "then", "else", "while", "return",
     "import", "from", "__ASM__",
     "define", "safe_mode", "_header_", "_content_", "and", "or", "continue",
     "break", "fabs", "abs", "save_reg",
     "save_reg_abs", "struct","override"};

bool _for_display = false;

int _token_line;
int _sav_token_line = 0;
int pos_in_line = 0;
char *line_ref = NULL;
bool insecond = false;
vect<uint16_t> userDefinedVarTypeNames;
vect<varType> _userDefinedTypes;
Tokens __allTokens = Tokens();
Tokens _extra_tks = Tokens();
Text all_text;
//Tokens *_tks;
vect<_define> define_list;

varType _varTypes[] = {
    {._varType = __none__,
     .varName = (char *)"d",
     ._varSize = 0,
     .load = {},
     .store = {},
     .membersNames = {},
     .starts = {},
     .memberSize = {},
     .types = {},
     .sizes = {},
     .size = 0,
     .total_size = 0},

    {._varType = __uint8_t__,
     .varName = (char *)"d",
     ._varSize = 1,
     .load = {l8ui},
     .store = {s8i},
     .membersNames = {},
     .starts = {},
     .memberSize = {},
     .types = {},
     .sizes = {1},
     .size = 1,
     .total_size = 1},
    {
        ._varType = __uint16_t__,
        .varName = (char *)"d",
        ._varSize = 2,
        .load = {l16ui},
        .store = {s16i},
        .membersNames = {},
        .starts = {},
        .memberSize = {},
        .types = {},
        .sizes = {2},
        .size = 1,
        .total_size = 2,
    },
    {
        ._varType = __uint32_t__,
        .varName = (char *)"d",
        ._varSize = 4,
        .load = {l32i},
        .store = {s32i},
        .membersNames = {},
        .starts = {},
        .memberSize = {},
        .types = {},
        .sizes = {4},
        .size = 1,
        .total_size = 4,
    },
    {
        ._varType = __int__,
        .varName = (char *)"d",
        ._varSize = 4,
        .load = {l32i},
        .store = {s32i},
        .membersNames = {},
        .starts = {},
        .memberSize = {},
        .types = {},
        .sizes = {4},
        .size = 1,
        .total_size = 4,
    },
    {
        ._varType = __s_int__,
        .varName = (char *)"d",
        ._varSize = 2,
        .load = {l16si},
        .store = {s16i},
        .membersNames = {},
        .starts = {},
        .memberSize = {},
        .types = {},
        .sizes = {2},
        .size = 1,
        .total_size = 2,
    },
    {
        ._varType = __float__,
        .varName = (char *)"d",
        ._varSize = 4,
        .load = {lsi},
        .store = {ssi},
        .membersNames = {},
        .starts = {},
        .memberSize = {},
        .types = {},
        .sizes = {4},
        .size = 1,
        .total_size = 4,
    },
    {
        ._varType = __void__,
        .varName = (char *)"void",
        ._varSize = 0,
        .load = {},
        .store = {},
        .membersNames = {},
        .starts = {},
        .memberSize = {},
        .types = {},
        .sizes = {0},
        .size = 0,
        .total_size = 0,
    },
    {
        ._varType = __CRGB__,
        .varName = (char *)"d",
        ._varSize = 3,
        .load = {l8ui, l8ui, l8ui},
        .store = {s8i, s8i, s8i},
        .membersNames = {(char *)"red", (char *)"green", (char *)"blue"},
        .starts = {0, 1, 2},
        .memberSize = {1, 1, 1},
        .types = {__uint8_t__, __uint8_t__, __uint8_t__},
        .sizes = {1, 1, 1},
        .size = 3,
        .total_size = 3,
    },
    {
        ._varType = __CRGBW__,
        .varName = (char *)"d",
        ._varSize = 4,
        .load = {l8ui, l8ui, l8ui, l8ui},
        .store = {s8i, s8i, s8i, s8i},
        .membersNames = {(char *)"red", (char *)"green", (char *)"blue",(char *) "white"},
        .starts = {0, 1, 2, 3},
        .memberSize = {1, 1, 1, 1},
        .types = {__uint8_t__, __uint8_t__, __uint8_t__, __uint8_t__},
        .sizes = {1, 1, 1, 1},
        .size = 4,
        .total_size = 4,
    },
    {
        ._varType = __char__,
        .varName = (char *)"d",
        ._varSize = 1,
        .load = {l8ui},
        .store = {s8i},
        .membersNames = {},
        .starts = {},
        .memberSize = {},
        .types = {},
        .sizes = {1},
        .size = 1,
        .total_size = 1,
    },
    {
        ._varType = __Args__,
        .varName = (char *)"Args",
        ._varSize = 1,
        .load = {},
        .store = {},
        .membersNames = {},
        .starts = {},
        .memberSize = {},
        .types = {},
        .sizes = {1},
        .size = 1,
        .total_size = 1,

    },
    {._varType = __bool__,
     .varName = (char *)"d",
     ._varSize = 1,
     .load = {l8ui},
     .store = {s8i},
     .membersNames = {},
     .starts = {},
     .memberSize = {},
     .types = {},
     .sizes = {1},
     .size = 1,
     .total_size = 1},

};

 Script::Script()
    {

        position = -1;
    }
    void  Script::init()
    {
        it = script.begin();
        position = -1;
    }
    void Script::addContent(char *str)
    {
        script.push_back(str);
    }
    void Script::clear()
    {
        script.clear();
        script.shrink_to_fit();
        position = -1;
    }
    char *Script::currentCharPtr()
    {
        return &((*it)[position]);
    }
    char *Script::firstCharPtr()
    {
        return &(*it)[0];
    }
    char Script::nextChar()
    {

        if ((*it)[position + 1] != 0)
        {
            position++;
            return (*it)[position];
        }
        else
        {
            char **d = it;
            d++;
            if (d == script.end())
            {

                position++;
                return EOF_TEXT;
            }
            else
            {

                // it = next(it);
                it++;
                position = 0;
                return (*it)[position];
            }
        }
    }

    char Script::currentChar()
    {

        if ((*it)[position] != 0)
        {

            return (*it)[position];
        }
        else
        {

            return EOF_TEXT;
        }
    }

    char Script::previousChar()
    {

        if ((position - 1) >= 0)
        {
            position--;
            return (*it)[position];
        }
        else
        {
            if (it != script.begin())
            {

                it--;
                position = 0;
                while ((*it)[position] != 0)
                {
                    position++;
                }
                position--;
                return (*it)[position];
            }
            else
            {
                printf("jkjk\n");
                position = -1;

                return 0; // (*it)[0];
            }
        }
    }

    void Script::insert(char *toInsert)
    {

        char *_cur = &(*it)[position];
        char *_next = &(*it)[position + 1];
        it++;
        if (*_next != 0 and *_cur != 0)
            it = script.insertBefore(it, _next);
        it = script.insertBefore(it, toInsert);
        position = -1;
    }
    void Script::insertAtEnd(char *toInsert)
    {
        int i = 0;
        int res = -1;
        for (char **_it = script.begin(); _it != script.end(); _it++)
        {
            if (it == _it)
            {
                res = i;
            }
            else
            {
                i++;
            }
        }
        script.insertBefore(script.end(), toInsert);
        it = script.begin();
        while (res > 0)
        {
            it++;
            res--;
        }
    }

    Token::Token()
    {
        type = (int)TokenUnknown;
        _vartype = EOF_VARTYPE;
        textref = EOF_TEXTARRAY;
        pos = 0;
        line = 0;
        lineref = NULL;
    }

    Token::Token(tokenType h)
    {
        type = (int)h;
        _vartype = EOF_VARTYPE;
        textref = EOF_TEXTARRAY;
        pos = 0;
        line = 0;
        lineref = NULL;
    }
    Token::Token(tokenType _type, int __vartype, int _line)
    {
        type = (int)_type;
        _vartype = __vartype;
        line = _line;
        textref = EOF_TEXTARRAY;
        pos = 0;
        lineref = NULL;
    }
    Token::Token(tokenType _type, int __vartype)
    {
        type = (int)_type;
        _vartype = __vartype;

        textref = EOF_TEXTARRAY;
        pos = 0;
        line = 0;
        lineref = NULL;
    }
    tokenType Token::getType()
    {
        return (tokenType)type;
    }
    void Token::setType(tokenType _type)
    {
        type = (int)_type;
    }
    void Token::clean()
    {
        line = 0;
        type = 0;
        pos = 0;
        _vartype = EOF_VARTYPE;

        textref = EOF_TEXTARRAY;
        lineref = NULL;
    }
    void Token::addText(const char *t)
    {
        textref = all_text.addText(t);
    }
    void Token::addText(char *t)
    {
        textref = all_text.addText(t);
    }
    void Token::addText(char *t, char *e)
    {
        textref = all_text.addText(t, e);
    }
    char *Token::getText()
    {
        return all_text.getText(textref);
    }

    varTypeEnum Token::getVarType()
    {
        return (varTypeEnum)_vartype;
    }
    varType *Token::getVarTypeObj()
    {
        if (_vartype == EOF_VARTYPE)

            return NULL;

        return &_varTypes[_vartype];
    }


    void Tokens::init()
    {
#ifdef __FULL_TOKEN

        position = 0;
#endif
    }
    Tokens::Tokens()
    {
        // _tokens = &_list_of_tokens;
        clear();
        init();
    }

    void Tokens::clear()
    {
        _tokens.clear();

        _tokens.shrink_to_fit();

#ifdef __FULL_TOKEN
        position = 0;
#endif
    }
    int Tokens::size()
    {
        return _tokens.size();
    }
    void Tokens::tokenizelow(Script *script, bool update, bool increae_line, int nbToken)
    {
        _script = script;
        clear();
        tokenizer(script, true, increae_line, nbToken,this);
        // list_of_token.push_back(token());
        // Serial.printf("token read %d\n", tokenizer(script, true, increae_line, nbToken));
    }
    void Tokens::tokenize(Script *script, bool update, bool increae_line, int nbToken)
    {
        _script = script;
        clear();

        tokenizer(script, update, increae_line, nbToken,this);
        // list_of_token.push_back(token());
        // Serial.printf("token read %d\n", tokenizer(script, true, increae_line, nbToken));
    }
    void Tokens::push(Token t)
    {
        _tokens.push_back(t);
    }
    void Tokens::pop_back()
    {
        if (_tokens.size() > 0)
        {
            //	Token t = _tokens.back ();

            _tokens.pop_back();

            //	return t;
        }
        //	else
        //	  return end_token;
    }
    Token *Tokens::getTokenAtPos(int pos)
    {
        if (pos >= 0 and pos < _tokens.size())
        {
            // printf("%s at %d %d\n",_tokens[pos].line,_tokens[pos].pos);
            return _tokens.getptr(pos);
        }
        else
            return &end_token;
    }
    Token *Tokens::current()
    {
#ifdef __FULL_TOKEN
        return getTokenAtPos(position);
#else
        return getTokenAtPos(__DEPTH);
#endif
    }
    Token *Tokens::next()
    {
#ifdef __FULL_TOKEN

        // position++;
        tokenizer(_script, false, true, 1);
        position++;

        return getTokenAtPos(position);
#else

        _tokens.erase(_tokens.begin());
        _tokens.shrink_to_fit();

        tokenizer(_script, false, true, 1,this);
        return getTokenAtPos(__DEPTH);
#endif
    }
    Token *Tokens::prev()
    {
#ifdef __FULL_TOKEN
        position--;
        return getTokenAtPos(position);
#else
        _tokens.insertAfter(_tokens.begin(), Token());
        return getTokenAtPos(__DEPTH);
#endif
    }
    Token *Tokens::peek(int index)
    {

#ifdef __FULL_TOKEN
        if (index + position < _tokens.size() && index + position >= 0)
        {
            return getTokenAtPos(index + position);
        }
        else
        {
            tokenizer(_script, false, true,
                      -_tokens.size() + index + position + 1);

            return getTokenAtPos(index + position);
        }
#else
        if (index + __DEPTH < _tokens.size() && index + __DEPTH >= 0)
        {
            return getTokenAtPos(index + __DEPTH);
        }
        else
        {
            tokenizer(_script, false, true,
                      -_tokens.size() + index + __DEPTH + 1,this);

            return getTokenAtPos(index + __DEPTH);
        }
#endif
    }
    Token Tokens::back()
    {
        if (_tokens.size() > 0)
            return _tokens.back();
        else
            return Token();
    }

    bool Tokens::Match(tokenType tt)
    {
        Token *g = current();
        if (g->getType() == tt)
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    bool Tokens::Match(tokenType tt, int index)
    {
        Token *g = peek(index);
        if (g->getType() == tt)
        {
            return true;
        }
        else
        {
            return false;
        }
    }




bool isIna_zA_Z_(unsigned char c)
{
    if (c >= 97 && c <= 122)
    {
        return true;
    }
    if (c >= 65 && c <= 90)
    {
        return true;
    }
    if (c == '_')
    {
        return true;
    }
    return false;
}
int isKeyword(char *str, char *end)
{
    for (int i = 0; i < nb_keywords; i++)
    {
        if (strncmp(keyword_array[i], str, end - str + 1) == 0 and strlen(keyword_array[i])==end-str+1)
        {
            return i;
        }
    }

    return -1;
}
bool isIna_zA_Z_0_9(unsigned char c)
{
    if (c >= 97 && c <= 122)
    {
        return true;
    }
    if (c >= 65 && c <= 90)
    {
        return true;
    }
    if (c == '_')
    {
        return true;
    }
    if (c >= 48 && c <= 57)
    {
        return true;
    }
    return false;
}
bool isIn0_9(unsigned char c)
{
    if (c >= 48 && c <= 57)
    {
        return true;
    }
    return false;
}

bool isIn0_9_x_b(unsigned char c)
{
    if (c >= 48 && c <= 57)
    {
        return true;
    }
    if (c == 'b')
    {
        return true;
    }
    if (c >= 'A' && c <= 'F')
    {
        return true;
    }
    if (c >= 'a' && c <= 'f')
    {
        return true;
    }
    if (c == 'x')
    {
        return true;
    }
    if (c == '.')
    {
        return true;
    }
    return false;
}
void displaytoken(Token *t)
{
    printf("%30s\t%20s\ttype:%15d\tline:%3d\tpos:%4d\n", tokenNames[t->type], t->getText(), t->_vartype, t->line, t->pos);
}
char *getDefine(char *deb, char *end)
{
    for (_define *it = define_list.begin();
         it != define_list.end(); ++it)
    {
        if (strncmp(all_text.getText((*it).name), deb, strlen(all_text.getText((*it).name))) == 0  and strlen(all_text.getText((*it).name))==end-deb+1)
        {
            // printf("one rrent %s\n",(*it).content.c_str());
            return all_text.getText((*it).content);
        }
    }
    return NULL;
}
Token transNumber(char *str, char *end,Tokens *_tks)
{
    // t;
    // t.float_value=0;
    // t.int_value=0;
    bool todelete = false;
    if (_tks->size() > 1)
    {
        if (_tks->back().type == TokenSubstraction)
        {
            tokenType subtype = (tokenType)_tks->getTokenAtPos(_tks->size() - 2)->type;
            if (subtype == TokenComma || subtype == TokenEqual || subtype == TokenDoubleEqual || subtype == TokenLessOrEqualThan || subtype == TokenDoubleEqual || subtype == TokenMoreThan || subtype == TokenMoreOrEqualThan || subtype == TokenNotEqual || subtype == TokenStarEqual || subtype == TokenPlusEqual || subtype == TokenOpenParenthesis)
            {
                // str=strdup(str);
                char *m = (char *)malloc(end - str + 3);
                memcpy(m + 1, str, end - str + 1);
                m[end - str + 2] = 0;
                m[0] = '-';
                str = m;
                todelete = true;
                end = str + strlen(m) - 1;
                _tks->pop_back();
            }
        }
    }
    char *p = strpbrk(str, ".");
    Token t;
    if (p != NULL and p < end)
    {

        t = Token(TokenNumber, (int)__float__);
        // t.addText(str, end);
        // return t;
    }
    else
    {

        t = Token(TokenNumber, (int)__int__);
    }
    t.addText(str, end);
    if (todelete)
        free(str);
    return t;
}
int isUserDefined(char *s, char *end)
{
    for (int i = 0; i < userDefinedVarTypeNames.size(); i++)
    {
        if (strncmp(all_text.getText(userDefinedVarTypeNames[i]), s, end - s + 1) == 0)
        {
            return i;
        }
    }
    return -1;
}



int tokenizer(Script *script, bool update, bool increae_line,
              int nbMaxTokenToRead,Tokens *_tks)
{

    // list<token> list_of_token;
    // int line = 1;
    Token t;
    // int pos = 0;
    char c;
    char c2;
    _define newdef;
    // vchar.clear();
    char *vchar;
    char *endchar;

    if (update)
    {
        _tks->clear();
        for (int i = 0; i < __DEPTH; i++)
        {
            _tks->push(Token());
        }
        _token_line = 1;
        pos_in_line = 0;

        if (!insecond)
        {
            userDefinedVarTypeNames.clear();

            // all_text.clear();

            define_list.clear();
        }
        line_ref = script->firstCharPtr();
    }
    // _for_display= true;

    else
    {
        script->previousChar();
    }
    int nbReadToken = 0;
    while (script->nextChar() != EOF_TEXT and nbReadToken < nbMaxTokenToRead)
    {

        //  printf(" nb read :%c:\n",script->currentChar());
        t.clean();
        vchar = script->currentCharPtr();
        endchar = vchar;
        pos_in_line++;

        c = script->currentChar();
        // printf("line : %d pos:%d char:%c token size:%d %d\n",line,pos,c,list_of_token.size(),heap_caps_get_free_size(MALLOC_CAP_8BIT));
        if (c == '=')
        {
            c2 = script->nextChar();
            if (c2 == '=')
            {
                t = Token(TokenDoubleEqual, EOF_VARTYPE);
                // t._vartype = NULL;
                // t.type = TokenDoubleEqual;
                if (_for_display)
                    t.addText("==");
                t.line = _token_line;
                t.pos = pos_in_line;

                t.lineref = line_ref;
                pos_in_line++;
                _tks->push(t);
                // nbReadToken++;

                continue;
            }
            else
            {
                script->previousChar();
                // token t;
                //  t._vartype = NULL;
                //  t.type = TokenEqual;
                //  t.line = _token_line;
                t = Token(TokenEqual, EOF_VARTYPE, _token_line);
                t.pos = pos_in_line;
                t.lineref = line_ref;
                if (_for_display)
                    t.addText("=");
                _tks->push(t);
                // nbReadToken++;
                continue;
            }
        }
        if (c == '<')
        {
            c2 = script->nextChar();
            if (c2 == '=')
            {
                // token t;
                // t._vartype = NULL;
                // t.type = TokenLessOrEqualThan;

                t = Token(TokenLessOrEqualThan, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText("<=");
                // t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                pos_in_line++;
                _tks->push(t);
                // nbReadToken++;
                continue;
            }
            else if (c2 == '<')
            {
                // token t;
                // t._vartype = NULL;
                //  t.type = TokenShiftLeft;
                t = Token(TokenShiftLeft, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText("<<");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                pos_in_line++;
                //  _tks->push(t);
                _tks->push(t);
                nbReadToken++;
                continue;
            }
            else
            {
                script->previousChar();
                // token t;
                // t._vartype = NULL;
                // t.type = TokenLessThan;
                // t.line = _token_line;//
                // TokenLessThan
                t = Token(TokenLessThan, EOF_VARTYPE, _token_line);
                t.pos = pos_in_line;
                t.lineref = line_ref;
                if (_for_display)
                    t.addText("<");
                _tks->push(t);
                nbReadToken++;
                continue;
            }
        }
        if (c == '>')
        {
            c2 = script->nextChar();
            if (c2 == '=')
            {
                //  token t;
                //  t._vartype = NULL;
                // t.type = TokenMoreOrEqualThan;
                t = Token(TokenMoreOrEqualThan, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText(">=");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                pos_in_line++;
                //_tks->push(t);
                _tks->push(t);
                // nbReadToken++;
                continue;
            }
            else if (c2 == '>')
            {
                // token t;
                // t._vartype = NULL;
                // t.type = TokenShiftRight;
                t = Token(TokenShiftRight, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText(">>");
                // t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                // _tks->push(t);
                _tks->push(t);
                nbReadToken++;
                continue;
            }
            else
            {
                script->previousChar();
                // token t;
                // t._vartype = NULL;
                // t.type = TokenMoreThan;
                // t.line = _token_line;
                t = Token(TokenMoreThan, EOF_VARTYPE, _token_line);
                t.pos = pos_in_line;
                t.lineref = line_ref;
                if (_for_display)
                    t.addText(">");
                // _tks->push(t);
                _tks->push(t);
                // nbReadToken++;
                continue;
            }
        }
        if (c == '!')
        {
            c2 = script->nextChar();
            if (c2 == '=')
            {
                // token t;
                // t._vartype = NULL;
                // t.type = TokenNotEqual;
                t = Token(TokenNotEqual, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText("!=");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                pos_in_line++;
                //_tks->push(t);
                _tks->push(t);
                /// nbReadToken++;
                continue;
            }
            else
            {
                script->previousChar();
                //  token t;
                //  t._vartype = NULL;
                // t.type = TokenNot;
                // t.line = _token_line;
                t = Token(TokenNot, EOF_VARTYPE, _token_line);
                t.pos = pos_in_line;
                t.lineref = line_ref;
                if (_for_display)
                    t.addText("!");
                // _tks->push(t);
                _tks->push(t);
                nbReadToken++;
                continue;
            }
        }
        if (c == '+')
        {
            c2 = script->nextChar();
            if (c2 == '+')
            {
                // token t;
                // t._vartype = NULL;
                // t.type = TokenPlusPlus;
                t = Token(TokenPlusPlus, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText("++");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                pos_in_line++;
                // _tks->push(t);
                // nbReadToken++;
                _tks->push(t);
                continue;
            }
            else if (c2 == '=')
            {
                t = Token(TokenPlusEqual, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText("+=");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                pos_in_line++;
                // _tks->push(t);
                // nbReadToken++;
                _tks->push(t);
                continue;
            }
            else
            {
                script->previousChar();
                // token t;
                //  t._vartype = NULL;
                // t.type = TokenAddition;
                t = Token(TokenAddition, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText("+");
                // t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                // _tks->push(t);
                _tks->push(t);
                nbReadToken++;
                continue;
            }
        }

        if (isIna_zA_Z_(c))
        {

            int newpos = pos_in_line;

            while (isIna_zA_Z_0_9(c))
            {
                endchar++;
                newpos++;
                c = script->nextChar();
            }
            script->previousChar();
            endchar--; // on revient un caractere en arriere
            // token t;
            // Token t;
            //  t._vartype=NULL;
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            if (isKeyword(vchar, endchar) > -1)
            {
                // printf("keyword;%s\n",v.c_str());
                // t.type = TokenKeyword;
                t.type = (int)__keywordTypes[isKeyword(vchar, endchar)];
                if (isKeyword(vchar, endchar) < nb_typeVariables)
                    t._vartype = isKeyword(vchar, endchar);
                if (t.getType() == TokenKeywordExternalVar)
                {
                    t.type = (int)TokenExternal;
                    //  printf("ereeeeeeee\n");
                }
                if (t.getType() == TokenKeywordDefine)
                {
                    // printf("on est ici\n");
                    if (!_for_display)
                    {
                        if ((_tks->back()).getType() == TokenDiese)
                        {
                            // printf("on est ici");
                            _tks->pop_back();
                        }
                        else
                        {
                            t.type = TokenUnknown;
                        }
                    }
                }
                if ((t.getType() == TokenKeywordImport or t.getType() == TokenKeywordDefine) && !_for_display)
                {

                    nbReadToken--;
                }
            }
            else if (isUserDefined(vchar, endchar) > -1)
            {
                t.type = (int)TokenUserDefinedVariable;
                t._vartype = (int)__userDefined__;
            }
            else
            {
                t.type = (int)TokenIdentifier;

                if (_tks->size() >= __DEPTH)
                {

                    Token prev = _tks->back();
                    /*
                    if (prev.getType() == TokenKeywordImport && !_for_display)
                    {

                        // script->insert(import);

                        _sav_token_line = _token_line;
                        nbReadToken--;
                        if (findLibFunction(vchar,endchar) > -1)
                        {

                            _tks->pop_back();

                            all_text.pop();

                            // list_of_token.pop_back();
                            //  add_on.push_back(findLibFunction(v));
                            script->insert((char *)((*_stdlib[findLibFunction(vchar,endchar)]).c_str()));

                            // script->previousChar ();

                            continue;
                        }
                    }
                    else if (prev.getType() == TokenDiese && !_for_display)
                    {
                        nbReadToken--;
                        if (findLibFunction(vchar,endchar) > -1)
                        {

                            _tks->pop_back();
                            // printf("token %d\n",_tks->back().type);
                            all_text.pop();

                            // list_of_token.pop_back();
                            //  add_on.push_back(findLibFunction(v));
                            script->insertAtEnd((char *)((*_stdlib[findLibFunction(vchar,endcahr)]).c_str()));
                            // printf("ll%d %s\n",findLibFunction(v),(*_stdlib[findLibFunction(v)]).c_str());
                            script->nextChar();
                            // script->previousChar ();
                            continue;
                        }
                    }*/
                    if (prev.getType() == TokenKeywordDefine && !_for_display)
                    {
                        _tks->pop_back();
                        all_text.pop();
                        // nbReadToken--;

                        newdef.name = all_text.addText(vchar, endchar);
                        // newdef.content = "";

                        c2 = script->nextChar();
                        vchar = script->currentCharPtr();
                        endchar = vchar - 1;
                        // c2 = script->nextChar();
                        while (c2 != '\n' and c2 != EOF_TEXT)
                        {
                            endchar++;
                            // newdef.content = newdef.content + c2;
                            c2 = script->nextChar();
                           
                        }
                        // printf("on push |%s|\n",newdef.content.c_str());
                        newdef.content = all_text.addText(vchar, endchar);
                        define_list.push_back(newdef);
                         line_ref = script->currentCharPtr()+1;
                         pos_in_line=0;
                        if (increae_line)
                            _token_line++;
                        // script->previousChar();
                        continue;
                    }

                    else if (prev.getType() == TokenKeywordStruct && !_for_display)
                    {
                        userDefinedVarTypeNames.push_back(all_text.addText(vchar, endchar));
                        t.type = (int)TokenUserDefinedName;
                        // continue;
                    }
                }
                if (!_for_display) // on ne remplace pas lorsque l'on display
                {

                    if (getDefine(vchar, endchar) != NULL)
                    {

                        script->insert(getDefine(vchar, endchar));
                        script->nextChar();
                        // nbReadToken--;
                        continue;
                    }
                }
            }
            pos_in_line = newpos - 1;

            t.addText(vchar, endchar);
            //_tks->push(t);
            _tks->push(t);
            nbReadToken++;
            continue;
        }

        if (isIn0_9(c))
        {
            // //printf("on a %c\n",c);
            vchar = script->currentCharPtr();
            endchar = vchar;
            int newpos = pos_in_line;
            while (isIn0_9_x_b(c))
            {
                endchar++;
                c = script->nextChar();
                newpos++;
            }
            script->previousChar(); // on revient un caractere en arriere
            endchar--;
            t = transNumber(vchar, endchar,_tks);
            //  t._vartype=NULL;
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            //_tks->push(t);

            _tks->push(t);
            nbReadToken++;
            pos_in_line = newpos - 1;
            continue;
        }
        if (c == ';')
        {
            // token t;
            // t.type = TokenSemicolon;
            // t._vartype = NULL;
            t = Token(TokenSemicolon, EOF_VARTYPE, _token_line);
            if (_for_display)
                t.addText(";");
            // t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            // _tks->push(t);
            _tks->push(t);
            nbReadToken++;
            continue;
        }
        if (c == '\t')
        {
            // token t;
            // t.type = TokenSpace;
            // t._vartype = NULL;
            t = Token(TokenSpace, EOF_VARTYPE, _token_line);
            if (_for_display)
                t.addText("\t");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            if (_for_display)
            {
                //_tks->push(t);
                // nbReadToken++;
                _tks->push(t);
            }
            continue;
        }
        if (c == '&')
        {
            c2 = script->nextChar();
            if (c2 == '&')
            {
                t = Token(TokenDoubleUppersand, EOF_VARTYPE);
                // t._vartype = NULL;
                // t.type = TokenDoubleEqual;
                if (_for_display)
                    t.addText("&&");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                pos_in_line++;
                _tks->push(t);
                // nbReadToken++;

                continue;
            }
            else
            {
                script->previousChar();
                // token t;
                //  t._vartype = NULL;
                //  t.type = TokenEqual;
                t.line = _token_line;
                t = Token(TokenUppersand, EOF_VARTYPE, _token_line);
                t.pos = pos_in_line;
                t.lineref = line_ref;
                if (_for_display)
                    t.addText("&");
                _tks->push(t);
                nbReadToken++;
                continue;
            }
        }
        if (c == '#')
        {
            // token t;
            // t.type = TokenDiese;
            // t._vartype = NULL;
            t = Token(TokenDiese, EOF_VARTYPE, _token_line);
            if (_for_display)
            {
                t.addText("#");
                // _tks->push(t);
            }
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            _tks->push(t);
            // nbReadToken++;
            continue;
        }
        if (c == '(')
        {
            // token t;
            // t.type = TokenOpenParenthesis;
            // t._vartype = NULL;
            t = Token(TokenOpenParenthesis, EOF_VARTYPE, _token_line);
            if (_for_display)
                t.addText("(");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            //_tks->push(t);
            _tks->push(t);
            // nbReadToken++;
            continue;
        }
        if (c == '%')
        {
            // token t;
            //  t.type = TokenModulo;
            // t._vartype = NULL;
            t = Token(TokenModulo, EOF_VARTYPE, _token_line);
            if (_for_display)
                t.addText("%");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            //_tks->push(t);
            _tks->push(t);
            nbReadToken++;
            continue;
        }
        if (c == ')')
        {
            //  token t;
            // t.type = TokenCloseParenthesis;
            // t._vartype = NULL;
            t = Token(TokenCloseParenthesis, EOF_VARTYPE, _token_line);
            if (_for_display)
                t.addText(")");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            //_tks->push(t);
            _tks->push(t);
            nbReadToken++;
            continue;
        }
        if (c == '{')
        {
            // token t;
            // t.type = TokenOpenCurlyBracket;
            // t._vartype = NULL;
            t = Token(TokenOpenCurlyBracket, EOF_VARTYPE, _token_line);
            if (_for_display)
                t.addText("{");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            //_tks->push(t);
            _tks->push(t);
            nbReadToken++;
            continue;
        }
        if (c == '}')
        {
            // token t;
            // t.type = TokenCloseCurlyBracket;
            // t._vartype = NULL;
            t = Token(TokenCloseCurlyBracket, EOF_VARTYPE, _token_line);
            if (_for_display)
                t.addText("}");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            // _tks->push(t);
            _tks->push(t);
            nbReadToken++;
            continue;
        }
        if (c == '[')
        {
            //  token t;
            // t.type = TokenOpenBracket;
            // t._vartype = NULL;
            t = Token(TokenOpenBracket, EOF_VARTYPE, _token_line);
            if (_for_display)
                t.addText("[");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            //_tks->push(t);
            _tks->push(t);
            nbReadToken++;
            continue;
        }
        if (c == ']')
        {
            // token t;
            // t.type = TokenCloseBracket;
            // t._vartype = NULL;
            c2 = script->nextChar();
            if (c2 == '[')
            {
                t = Token(TokenComma, EOF_VARTYPE);
                // t._vartype = NULL;
                // t.type = TokenDoubleEqual;
                if (_for_display)
                    t.addText("][");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                _tks->push(t);
                nbReadToken++;

                continue;
            }
            else
            {
                script->previousChar();
                // token t;
                //  t._vartype = NULL;
                //  t.type = TokenEqual;
                t.line = _token_line;
                t = Token(TokenCloseBracket, EOF_VARTYPE, _token_line);
                t.pos = pos_in_line;
                t.lineref = line_ref;
                if (_for_display)
                    t.addText("]");
                _tks->push(t);
                nbReadToken++;
                continue;
            }
        }
        if (c == '/')
        {
            char c2 = script->nextChar();
            if (c2 == '/')
            {
                // Token t;
                t._vartype = EOF_VARTYPE;
                t.type = (int)TokenLineComment;
                vchar = script->currentCharPtr() - 1;
                endchar = vchar + 1;
                c2 = script->nextChar();
                while (c2 != '\n' and c2 != EOF_TEXT)
                {
                    endchar++; // string_format("%s%c", t.getText(), c2);
                    c2 = script->nextChar();
                    
                }
                // str = str + '\0';
                //  c2 = script->previousChar();
                if (_for_display)
                {
                    c2 = script->previousChar();
                    // endchar--;
                    t.addText(vchar, endchar);
                }
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                line_ref = script->currentCharPtr()+1;
                pos_in_line = 0;
                if (increae_line)
                    _token_line++;
                if (_for_display)
                {
                    // script->previousChar();
                    _tks->push(t);
                    nbReadToken++;
                }
                continue;
            }
            else if (c2 == '=')
            {
                t = Token(TokenSlashEqual, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText("/=");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                pos_in_line++;
                // _tks->push(t);
                // nbReadToken++;
                _tks->push(t);
                continue;
            }
            else if (c2 == '*')
            {
                // Token t;
                t._vartype = EOF_VARTYPE;
                t.type = (int)TokenLineComment;

                vchar = script->currentCharPtr() - 1;
                endchar = vchar + 2;
                c = script->nextChar();
                c2 = script->nextChar();
                while ((c != '*' or c2 != '/') and c2 != EOF_TEXT and c != EOF_TEXT) // stop when (c=* and c2=/) or c=0 or c2=0
                {
                    if (c == '\n')
                        _token_line++;

                    if (_for_display)
                        endchar++;
                    c = c2;
                    c2 = script->nextChar();
                }
                if (_for_display)
                {
                    endchar++;
                    t.addText(vchar, endchar);
                }
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                if (_for_display)
                {
                    _tks->push(t);
                    nbReadToken++;
                }
                continue;
            }
            else
            {
                script->previousChar();
                // Token t;
                t.type = (int)TokenSlash;
                t._vartype = EOF_VARTYPE;
                if (_for_display)
                    t.addText("/");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                _tks->push(t);
                nbReadToken++;
                continue;
            }
        }
        if (c == '-')
        {
            c2 = script->nextChar();
            if (c2 == '-')
            {
                // token t;
                // t._vartype = NULL;
                // t.type = TokenPlusPlus;
                t = Token(TokenMinusMinus, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText("--");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                pos_in_line++;
                // _tks->push(t);
                // nbReadToken++;
                _tks->push(t);
                continue;
            }
            if (c2 == '=')
            {
                // token t;
                // t._vartype = NULL;
                // t.type = TokenPlusPlus;
                t = Token(TokenMinusEqual, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText("-=");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                pos_in_line++;
                // _tks->push(t);
                // nbReadToken++;
                _tks->push(t);
                continue;
            }
            else
            {
                script->previousChar();
                // token t;
                //  t._vartype = NULL;
                // t.type = TokenAddition;
                t = Token(TokenSubstraction, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText("-");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                // _tks->push(t);
                _tks->push(t);
                // nbReadToken++;
                continue;
            }
            // Token t;
        }
        if (c == ' ')
        {
            // Token t;
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            vchar = script->currentCharPtr();
            endchar = vchar;
            while (c == ' ')
            {
                c = script->nextChar();
                pos_in_line++;
                endchar++;
            }
            script->previousChar(); // on revient un caractere en arriere
            endchar--;
            if (_for_display)
                t.addText(vchar, endchar);

            pos_in_line--;
            t.type = TokenSpace;
            // t.addText(" ";
            if (_for_display)
            {
                _tks->push(t);
                nbReadToken++;
            }
            continue;
        }
        if (c == '"')
        {
            vchar = script->currentCharPtr();
            // Token t;
            t._vartype = EOF_VARTYPE;
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            vchar = script->currentCharPtr();
            endchar = vchar;
            c = script->nextChar();
            pos_in_line++;
            while (c != '"' && c != EOF_TEXT)
            {
                if (!_for_display)
                {
                    char c2 = script->nextChar();
                    if (c == '\\' and c2 == 'n')
                    {
                        c = '\x0d';
                        endchar++;
                        c = '\x0a';
                        endchar++;
                        c = script->nextChar();
                    }
                    else
                    {
                        endchar++;
                        c = c2;
                    }

                    pos_in_line++;
                }
                else
                {
                    endchar++;
                    c = script->nextChar();
                    pos_in_line++;
                }
            }
            // script->previousChar(); //on revient un caractere en arriere
            // pos--;
            endchar++;
            t.type = (int)TokenString;
            t.addText(vchar, endchar);
            _tks->push(t);
            nbReadToken++;
            continue;
        }
        if (c == '\n')
        {
            // Token t;
            t.type = (int)TokenNewline;
            if (_for_display)
                t.addText("\r\n");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            line_ref = script->currentCharPtr()+1;
            if (increae_line)
                _token_line++;
            pos_in_line = 0;
            if (_for_display)
                _tks->push(t);
#ifdef PARSER_DEBUG
            if (!_for_display)
            {

                printf("line;%d\n\r", _token_line);
            }
#endif
            continue;
        }
        if (c == '?')
        {
            // Token t;
            t = Token(TokenQuestionMark, EOF_VARTYPE, _token_line);
            if (_for_display)
                t.addText("?");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            //_token_line++;
            //  pos = 0;
            // if (_for_display)
            _tks->push(t);
            continue;
        }
        if (c == '.')
        {
            // Token t;
            t.type = (int)TokenMember;
            if (_for_display)
                t.addText(".");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            //_token_line++;
            //  pos = 0;
            // if (_for_display)
            _tks->push(t);
            // nbReadToken++;
            continue;
        }
        if (c == '^')
        {
            // Token t;
            t.type = (int)TokenPower;
            if (_for_display)
                t.addText("^");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            //_token_line++;
            //  pos = 0;
            // if (_for_display)
            _tks->push(t);
            // nbReadToken++;
            continue;
        }
        if (c == '@')
        {
            // Token t;
            t.type = (int)TokenUnknown;
            if (_for_display)
                t.addText("@");
            _token_line = _sav_token_line;
            if (_for_display)
                _tks->push(t);
            continue;
        }
        if (c == '\'')
        {
            // Token t;
            t.type = (int)TokenUnknown;
            if (_for_display)
                t.addText("\'");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            //  _token_line++;
            //  pos = 0;
            if (_for_display)
                _tks->push(t);
            continue;
        }
        if (c == ':')
        {
            // Token t;
            t = Token(TokenColon, EOF_VARTYPE, _token_line);
            if (_for_display)
                t.addText(":");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            //_token_line++;
            //  pos = 0;
            // if (_for_display)
            _tks->push(t);
            continue;
        }
        if (c == '*')
        {
            c2 = script->nextChar();
            if (c2 == '=')
            {
                // token t;
                // t._vartype = NULL;
                // t.type = TokenPlusPlus;
                t = Token(TokenStarEqual, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText("*=");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                // _tks->push(t);
                // nbReadToken++;
                _tks->push(t);
                continue;
            }
            else
            {
                script->previousChar();
                // token t;
                //  t._vartype = NULL;
                // t.type = TokenAddition;
                t = Token(TokenStar, EOF_VARTYPE, _token_line);
                if (_for_display)
                    t.addText("*");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                // _tks->push(t);
                _tks->push(t);
                nbReadToken++;
                continue;
            }
            // Token t;
        }
        if (c == '|')
        {
            c2 = script->nextChar();
            if (c2 == '|')
            {
                t = Token(TokenDoubleOr, EOF_VARTYPE);
                // t._vartype = NULL;
                // t.type = TokenDoubleEqual;
                if (_for_display)
                    t.addText("||");
                t.line = _token_line;
                t.pos = pos_in_line;
                t.lineref = line_ref;
                _tks->push(t);
                nbReadToken++;

                continue;
            }
            else
            {
                script->previousChar();
                // token t;
                //  t._vartype = NULL;
                //  t.type = TokenEqual;
                t.line = _token_line;
                t = Token(TokenKeywordOr, EOF_VARTYPE, _token_line);
                t.pos = pos_in_line;
                t.lineref = line_ref;
                if (_for_display)
                    t.addText("|");
                _tks->push(t);
                nbReadToken++;
                continue;
            }
        }
        if (c == ',')
        {
            // Token t;
            t._vartype = EOF_VARTYPE;
            t.type = (int)TokenComma;
            if (_for_display)
                t.addText(",");
            t.line = _token_line;
            t.pos = pos_in_line;
            t.lineref = line_ref;
            _tks->push(t);
            // nbReadToken++;
            continue;
        }
        if (c == 27)
        {
            c2 = script->nextChar();
            continue;
        }
        if (!_for_display)
            printf("Error invalid character |%d| line :%d pos: %d\n", c, _token_line, pos_in_line);
    }

    if (script->currentChar() == EOF_TEXT)
    {
        if (_tks->back().getType() != TokenEndOfFile)
        {
            t = Token(TokenEndOfFile, EOF_VARTYPE, _token_line);

            _tks->push(t);
        }
    }
    // return list_of_token;
    return nbReadToken - 1;
}


    void displayLine(Token *t)
    {
        if (t->lineref != NULL)
        {
            printf("line:%3d position:%3d \"",t->line,t->pos);
            char *r = t->lineref;
            while (*r != '\n' and *r!=0)
            {
                printf("%c", *r);
                r++;
            }
            printf("\"\n");
            for (int i = 1; i < t->pos+23; i++)
            {
                printf(" ");
            }
            printf("^\n");
        }
    }

    int findStruct(char *structName)
{
    for (int i = 0; i < _userDefinedTypes.size(); i++)
    {
        if (strcmp(_userDefinedTypes[i].varName, structName) == 0)
        {
            return i;
        }
    }
    return -1;
}

int findMember(varType *v, char * member)
{
    //  printf("zerk %d %s\n", v->size, v->varName.c_str());
    for (int i = 0; i < v->size; i++)
    {
        //    printf("look for %s %s\n", member.c_str(), v->membersNames[i].c_str());
        if (strncmp(v->membersNames[i],member,strlen(member)) == 0)
        {
            return i;
        }
    }
    return -1;
}
int findMember(uint8_t _v, char * member)
{

    varType *v = _userDefinedTypes.getptr(_v);
    // printf("zerk dfin %d %s\n",v->size,v->varName.c_str());
   return findMember(v,member);
}
