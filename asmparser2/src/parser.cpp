
#include "parser.h"
#include "nodetoken.h"
#include "compiler_error.h"
#include "string_constants.h"
#include "parser_define.h"
#include "binding.h"
#include "optimize.h"
#include "runtime_functions.h"
//NodeToken *current_node;
NodeToken program = NodeToken(programNode);
NodeToken  extra_parser;
NodeToken main_context,ext_function_cntx,*sav_current_cntx;
NodeToken functions;
Stack<uint16_t> targetList;
vect<uint16_t> sigs;
vect<int> nb_sav_args;
vect<int> nb_args;
int nb_argument = 0;
char *struct_name;
bool isExternal = false;
int __sav_arg = 0;
bool isPointer = true;
//bool isStructFunction = false;
bool sav_b = false;
bool isASM = false;
bool safeMode = false;
bool saveReg = false;
bool saveRegAbs = false;

bool _asPointer = false;
int for_if_num = 0;
int block_statement_num = 0;
char *signature;
bool isExtra;
vect<NodeToken> nodeTokenList;
vect<Token> sav_t;
vect<NodeToken> _node_token_stack;

error_message_struct Error;

Parser::Parser() {}

int Parser::size()
{
    return _tks->size();
}
Token *Parser::getTokenAtPos(int pos)
{
    return _tks->getTokenAtPos(pos);
}
Token *Parser::current()
{
    return _tks->current();
}
Token *Parser::next()
{
    return _tks->next();
}
Token *Parser::prev()
{
    return _tks->prev();
}
Token *Parser::peek(int index)
{
    return _tks->peek(index);
}
bool Parser::Match(tokenType tt)
{
    return _tks->Match(tt);
}
bool Parser::Match(tokenType tt, int index)
{
    return _tks->Match(tt, index);
}

void Parser::parse(Script *main_script, Tokens *__tks)
{
    // Always-available built-ins (printf/printfln -- see
    // runtime_functions.h) need to be registered before parseProgram()
    // below has any chance of resolving a call to one of them.
    registerBuiltinRuntimeFunctions();

    _tks = __tks;
    _tks->tokenize(main_script, true, true, 1);
    parseProgram();
    if(Error.error)
    return;
 PARSER_LOG("build parents")
    buildParents(&program);
    PARSER_LOG("visit parents")
    program.visitNode();
    PARSER_LOG("optimize")
    optimize(&content);
}

void Parser::parseProgram()
{

    int memberpos = 0;
    int _start = 0;
    int _pos = 0;
    isExtra=false;
    // functions.clear();
    // main_context.clear();
    struct_name = NULL;
    //    int _totalsize = 0;

    current_cntx = &main_context;
    function_cntx.parent=&main_context;
    ext_function_cntx.parent=&main_context;
    current_node = &program;
    Error.error = 0;
    while (Match(TokenEndOfFile) == false)
    {
        isStructFunction = false;
        current_cntx=&main_context;
        if (Match(TokenUnknown))
            RETURN_ERROR(unknownToken)

        if (Match(TokenKeywordStruct))
        {
            function_cntx.clear();   
            current_cntx =&function_cntx; //current_cntx->addChild(NodeToken());
            current_cntx->type = TokenKeywordStruct;
            next();
            if (Match(TokenUserDefinedName))
            {
                varType usded;
                usded._varType = __userDefined__;
                memberpos = 0;
                _start = 0;
                _pos = 0;

                usded.varName = current()->getText();
                struct_name = usded.varName;
                next(); //{

                next(); // int
                while (!Match(TokenCloseCurlyBracket) and !Match(TokenEndOfFile))
                {

                    if (Match(TokenUserDefinedVariable) and Match(TokenOpenParenthesis, 1))
                    {
                        isStructFunction = true;
                        NodeToken _nd = NodeToken(UnknownNode);
                        _nd._nodetype = typeNode;
                        _nd.type = TokenKeywordVarType;
                        _nd._vartype = __void__;
                        _nd.textref = EOF_TEXT;

                        nodeTokenList.push_back(_nd);

                        current()->addText(string_format(_s_dot_underscore_arobase_s, usded.varName, current()->getText()));
                        parseDefFunction();

                        if (Error.error)
                        {
                            return;
                        }
                        isStructFunction = false;
                    }
                    else if (Match(TokenKeywordVarType) and Match(TokenIdentifier, 1) and Match(TokenOpenParenthesis, 2))
                    {
                        isStructFunction = true;
                        parseType();
                        if (Error.error)
                        {

                            return;
                        }

                        current()->addText(string_format(_s_dot_s_, usded.varName, current()->getText()));
                        parseDefFunction();

                        if (Error.error)
                        {
                            return;
                        }
                        isStructFunction = false;
                    }
                    else if (Match(TokenKeywordVarType) and Match(TokenIdentifier, 1) and !Match(TokenOpenParenthesis, 2))
                    {
                        usded.starts[memberpos] = _start;

                        varType *__v = current()->getVarTypeObj();
                        usded.types[memberpos] = __v->_varType;
                        usded.memberSize[memberpos] = __v->size;
                        _start += __v->total_size;
                        for (int _var = 0; _var < __v->size; _var++)
                        {
                            usded.load[_pos] = __v->load[_var];
                            usded.store[_pos] = __v->store[_var];
                            usded.sizes[_pos] = __v->sizes[_var];
                            _pos++;
                        }
                        next(); // name
                        NodeToken nd = NodeToken(current(), defLocalVariableNode);
                        nd.type = TokenUserDefinedVariableMember;
                        nd._vartype = __v->_varType;
                        nd.stack_pos = 1000 * (_start - __v->total_size) + _STACK_SIZE;
                        nd._total_size = __v->size;
                        nd.asPointer = true;
                        nd.isPointer = true;
                        current_cntx->addChild(nd);

                        usded.membersNames[memberpos] = current()->getText();
                        // printf(" addinr %s.%s\r\n",usded.varName.c_str(),current()->text.c_str());
                        next(); // ;
                        while (Match(TokenComma))
                        {
                            // next();
                            memberpos++;
                            usded.starts[memberpos] = _start;

                            usded.types[memberpos] = __v->_varType;
                            usded.memberSize[memberpos] = __v->size;
                            _start += __v->total_size;
                            for (int _var = 0; _var < __v->size; _var++)
                            {
                                usded.load[_pos] = __v->load[_var];
                                usded.store[_pos] = __v->store[_var];
                                usded.sizes[_pos] = __v->sizes[_var];
                                _pos++;
                            }
                            next(); // name
                            NodeToken nd = NodeToken(current(), defLocalVariableNode);
                            nd.type = TokenUserDefinedVariableMember;
                            nd._vartype = __v->_varType;
                            nd.stack_pos = 1000 * (_start - __v->total_size) + _STACK_SIZE;
                            nd._total_size = __v->size;
                            nd.asPointer = true;
                            nd.isPointer = true;
                            current_cntx->addChild(nd);

                            usded.membersNames[memberpos] = current()->getText();
                            // printf(" addinr %s.%s\r\n",usded.varName.c_str(),current()->text.c_str());
                            next(); // ;
                        }
                        if (!Match(TokenSemicolon))
                            RETURN_ERROR(expectingSemicolon)

                        next();
                        memberpos++;
                    }
                    else
                    {
                        Error.error = 1;
                        Error.token = current();
                        return;
                    }
                }
                usded.size = _pos;
                usded.total_size = _start;
                usded._varSize = _start;
                _userDefinedTypes.push_back(usded);
                next();
            }
            // if (!oneFunction)
            //{

            current_cntx = current_cntx->parent;

            isStructFunction = false;
            // struct_name is set above (~line 136) the moment a struct
            // definition starts, so every method call resolved afterward
            // -- for the rest of the *entire* script, not just while
            // still inside this struct's own body -- can find the
            // current struct's methods by qualified name. Nothing reset
            // it back to NULL once this struct's definition ends, so it
            // stayed set to the last-parsed struct's name for everything
            // parsed afterward: every call site of the form
            // `identifier.method(...)` anywhere later in the script,
            // regardless of which struct `identifier` actually is or
            // whether it's local/global/an array element, went through
            // parseArguments()'s `struct_name != NULL` branch instead of
            // its `struct_name == NULL` branch -- hardcoding the self-
            // pointer argument as a local variable at a fixed stack
            // offset (_STACK_SIZE) instead of reusing the self node
            // parseUserDefinedVariableMember() (~line 2830) already
            // built and correctly tagged as local/global based on how
            // `identifier` was actually declared. Confirmed via a direct
            // codegen comparison: `counter c; ... c.increment();` at
            // global scope compiled to `l32i a10,a1,56` (garbage -- reads
            // whatever another function happened to leave on the stack)
            // instead of `l32r a5,@_c` for the exact same script's very
            // next reference to `c.count`, which does go through the
            // correct path. v1 (upstream ESPLiveScript) never has this
            // problem in the first place -- its own struct_name
            // (NodeToken.h) is declared once and never assigned anywhere
            // else, so the equivalent branch there is permanently dead.
            struct_name = NULL;
            // }
        }

        else if (Match(TokenKeywordJson))
        {
            // `json "path.to.value" as int myVar;` -- ported from
            // upstream ESPLiveScript's ESPLiveScript.h. Only records the
            // path/name/type as a jsonBindingNode here; the actual
            // variable declaration (real storage + a @_name label) comes
            // from the ordinary "TYPE IDENTIFIER ;" path at the bottom of
            // this loop running again on the *same* "int myVar;" tokens
            // next iteration -- see the prev() calls below, the same
            // rewind trick upstream's parser uses for this.
            next(); // consume "json"
            if (!Match(TokenString))
                RETURN_ERROR(expectingJsonPathString)
            // getText() on a string token includes its enclosing quotes
            // (tokenize.cpp captures the whole "..." span) -- strip them,
            // since this is used as a literal path/key, not displayed
            // script text.
            char *rawJsonPath = current()->getText();
            int rawLen = (int)strlen(rawJsonPath);
            char *jsonPath;
            if (rawLen >= 2 && rawJsonPath[0] == '"' && rawJsonPath[rawLen - 1] == '"')
                jsonPath = strndup(rawJsonPath + 1, rawLen - 2);
            else
                jsonPath = strdup(rawJsonPath);
            next(); // consume the path string
            if (!Match(TokenKeywordAs))
            {
                free(jsonPath);
                RETURN_ERROR(expectingAs)
            }
            next(); // consume "as"

            parseType();
            if (Error.error)
            {
                free(jsonPath);
                return;
            }
            NodeToken typeNd = nodeTokenList.pop_back();

            if (!Match(TokenIdentifier))
            {
                free(jsonPath);
                RETURN_ERROR(expectingASMorInt)
            }

            NodeToken jnd = NodeToken(jsonBindingNode);
            jnd.setText(jsonPath); // ownership transferred to the Text pool
            jnd.addTargetText(current()->getText());
            jnd._vartype = typeNd._vartype;
            program.addChild(jnd);

            prev(); // undo parseType()'s next() past the type name
            if (typeNd.isPointer)
                prev(); // ...and past the '*' too, if there was one
        }
        else if (Match(TokenKeywordSafeMode))
        {
            safeMode = true;
            next();
        }
        else if (Match(TokenKeywordSaveReg))
        {
            saveReg = true;
            next();
        }
        else if (Match(TokenKeywordSaveRegAbs))
        {
            saveRegAbs = true;
            next();
        }
        else
        {
            parseType();
            RETURN_IF_ERROR
            if (Match(TokenOpenParenthesis, 1))
            {

                parseDefFunction();

                if (Error.error)
                {
                    return;
                }
            }
            else
            {
                nodeTokenList.push_back(nodeTokenList.back());
                parseVariableForCreation();
                if (Error.error)
                {

                    return;
                }
                NodeToken nd = nodeTokenList.pop_back();
              
               // PARSER_LOG("gert %s",nd.getText());
                NodeToken _t = nodeTokenList.pop_back();
                if (isExternal)
                {
                    nd._nodetype = (int)defExtGlobalVariableNode;

                    isExternal = false;
                }
                else
                {
                    nd._nodetype = (int)defGlobalVariableNode;
                }
               
               //PARSER_LOG("gert %s %s",nd.getText(),_t.getText());
                copyPrty(&_t, &nd);
                _t.clear();
             // PARSER_LOG("gert %s %s",nd.getText(),_t.getText());

                current_node = program.addChild(&nd);
               // PARSER_LOG("gert %s %s",current_node->getText(),_t.getText());
                tmp_sav = current_node;
                current_cntx->addChildClear(nd);
               // PARSER_LOG("gert %s %s",nd.getText(),_t.getText());
                if (Match(TokenComma))
                {
                    while (Match(TokenComma))
                    {
                        next();
                        nodeTokenList.push_back(nodeTokenList.back());
                        parseVariableForCreation();
                        if (Error.error)
                        {

                            return;
                        }
                        NodeToken nd = nodeTokenList.pop_back();
                        NodeToken _t = nodeTokenList.pop_back();
                        if (isExternal)
                        {
                            nd._nodetype = (int)defExtGlobalVariableNode;

                            isExternal = false;
                        }
                        else
                        {
                            nd._nodetype = (int)defGlobalVariableNode;
                        }
                        copyPrty(&_t, &nd);

                        current_node = program.addChild(nd);
                        tmp_sav = current_node;
                        current_cntx->addChildClear(nd);
                    }
                    if (!Match(TokenSemicolon))
                        RETURN_ERROR(expectingSemicolon)

                    else
                    {
                        next();
                       // nodeTokenList.backptr()->clear();
                        nodeTokenList.pop_back();
                        current_node = current_node->parent;
                    }
                }
                else if (Match(TokenSemicolon))
                {
                    //nodeTokenList.backptr()->clear();
                    nodeTokenList.pop_back();

                    if (current_node->type == TokenUserDefinedVariable)
                    {
                        current()->addText(string_format(_s_dot_underscore_arobase_s_openp_closep, current_node->getVarTypeObj()->varName, current_node->getVarTypeObj()->varName));
                        findFunction(&functions, current()->getText());

                        if (search_result != NULL)
                        {
                            NodeToken d = NodeToken(*current_node);
                            d._nodetype = callConstructorNode;
                            current_node = current_node->parent;
                            current_node = current_node->addChild(d);
                        }
                    }

                    current_node = current_node->parent;
                    next();
                }
                else if (Match(TokenEqual) && Match(TokenString, 1))
                {
                   // nodeTokenList.backptr()->clear();
                   
                    nodeTokenList.pop_back();
                    next();
                    current_node->addChild(NodeToken(current(), stringNode));
                    next();
                    if (!Match(TokenSemicolon))
                        RETURN_ERROR(expectingSemicolon)

                    next();
                    Error.error = 0;

                    current_node = current_node->parent;
                }

                else if (Match(TokenEqual) && Match(TokenOpenCurlyBracket, 1))
                {
                    //nodeTokenList.backptr()->clear();
                  PARSER_LOG("size:%s",nodeTokenList.backptr()->getText());
                   //nodeTokenList.pop_back();
                   
                    next();
                    next();
                    parseFactor();
                    if (Error.error)
                    {
                        return;
                    }
                    while (Match(TokenComma))
                    {
                        next();
                        parseFactor();
                        if (Error.error)
                        {
                            return;
                        }
                    }
                    if (!Match(TokenCloseCurlyBracket))
                        RETURN_ERROR(expectingClosingBracket)
                    next();
                    if (!Match(TokenSemicolon))
                        RETURN_ERROR(expectingSemicolon)
                    next();
                    Error.error = 0;
                    // __sav_pos = _tks->position;

                    // _tks->position = __sav_pos;
                    current_node = current_node->parent;
                    
                }

                else if (Match(TokenEqual) and Match(TokenUserDefinedVariable, 1) and Match(TokenOpenParenthesis, 2) and current_node->type == TokenUserDefinedVariable)
                {

                   // nodeTokenList.backptr()->clear();
                    nodeTokenList.pop_back();
                    next();
                    current()->addText(string_format(_s_dot_underscore_arobase_s, current_node->getVarTypeObj()->varName, current()->getText()));
                    NodeToken nd = NodeToken(*current_node);
                    if (current_node->_nodetype == defGlobalVariableNode)
                        nd._nodetype = globalVariableNode;
                    else
                        nd._nodetype = localVariableNode; // globalVariableNode;
                    nd.type = TokenUserDefinedVariableMemberFunction;
                    nd.isPointer = true;
                    nd._total_size = current_node->getVarTypeObj()->total_size;

                    nodeTokenList.push_back(nd);
                    isStructFunction = true;
                    current_node = current_node->parent;
                    parseFunctionCall();

                    if (Error.error)
                    {
                        return;
                    }
                    if (!Match(TokenSemicolon))
                        RETURN_ERROR(expectingSemicolon)

                    next();
                    //       current_node->getChildAtPos(current_node->children_size() - 1)->getChildAtPos(2)->getChildAtPos(0)->copyChildren(par);
                    isStructFunction = false;
                    Error.error = 0;
                    // current_node = current_node->parent;
                    // return;
                }
                else if (Match(TokenEqual))
                {
                   // nodeTokenList.backptr()->clear();
                    nodeTokenList.pop_back();
                    next();
                    NodeToken nd = NodeToken(*current_node);
                    nd._nodetype = storeGlobalVariableNode;
                    current_node = current_node->parent;
                    current_node = current_node->addChild(NodeToken(assignementNode));
                    current_node->addChild(nd);

                    parseExpr();
                    if (Error.error)
                    {
                        return;
                    }
                    if (!Match(TokenSemicolon))
                        RETURN_ERROR(expectingSemicolon)

                    next();
                    current_node = current_node->parent;
                    //  current_node=current_node->parent;
                }

                else if (!Match(TokenSemicolon))
                    RETURN_ERROR(expectingSemicolon)
            }
        }
    }
}

void Parser::parseType()
{
#ifdef PARSER_DEBUG
    updateMem();
#endif

    NodeToken _nd = NodeToken(UnknownNode);
    if (Match(TokenExternal))
    {
        isExternal = true;
        next();
    }
    else if (Match(TokenKeywordASM))
    {
        isASM = true;
        next();
    }

    if (Match(TokenKeywordVarType) or Match(TokenUserDefinedVariable))
    {

        _nd._nodetype = typeNode;
        _nd.type = current()->type;
        _nd._vartype = current()->_vartype;
        _nd.textref = current()->textref;
        int k = findStruct(current()->getText());
        if (k > -1)
        {
            _nd._vartype = k;
            _nd._total_size = _userDefinedTypes[k]._varSize;
        }

        next();
        if (Match(TokenStar))
        {
            _nd.isPointer = true;
            next();
        }
        else
        {
            _nd.isPointer = false;
        }
    }
    else if (Match(TokenSemicolon))
    {
        next();
    }
    else
        RETURN_ERROR(expectingASMorInt)
    Error.error = 0;
    nodeTokenList.push_back(_nd);
    return;
}

void Parser::parseVariableForCreation()
{
#ifdef PARSER_DEBUG
    updateMem();
#endif
    findVariable(current_cntx, current(), true);
    if (search_result != NULL)
        RETURN_ERROR(alreadyDeclaredVariableInScope);

    if (Match(TokenOpenBracket, 1))
    {

        // we are in the case led[];
        char *sizestr = NULL;
        int j = 0;
        NodeToken var = NodeToken(current());
        next();
        next();
        var._total_size = 1;
        if (Match(TokenNumber))
        {
            j++;
            if (current()->getVarType() == __int__)
            {
                var._total_size *= stringToInt(current()->getText());
                sizestr = str_concat(_s_space_s_, sizestr, current()->getText());

                next();
            }
            else
                RETURN_ERROR(expectingInteger)

            while (Match(TokenComma))
            {
                next();
                // displaytoken(current());

                if (current()->getVarType() == __int__)
                {
                    j++;
                    var._total_size *= stringToInt(current()->getText());
                    sizestr = str_concat(_s_space_s_, sizestr, current()->getText());
                    next();
                }
                else
                    RETURN_ERROR(expectingInteger)
            }
            if (Match(TokenCloseBracket))
            {
                var.isPointer = true;
                var._nodetype = defGlobalVariableNode; // we can't have arrays in the stack
                                                       // var._total_size = stringToInt(num.getText());
                if (sizestr == NULL)
                    sizestr = string_format(_arobase_d, j);
                else
                {
                    char *tmp = string_format(_arobase_d_s, j, sizestr);
                    free(sizestr);
                    sizestr = tmp;
                }

                var.addTargetText(sizestr);
                next();
                // resParse result;
                Error.error = 0;
                nodeTokenList.push_back(var);
                return;
            }
            else
                RETURN_ERROR(expectingClosingBracket)
        }
        else if (Match(TokenCloseBracket))
        {
            var.isPointer = true;
            var._nodetype = defGlobalVariableNode; // we can't have arrays in the stack
                                                   // var._total_size = stringToInt(num->text);
            next();

            Error.error = 0;
            nodeTokenList.push_back(var);
            return;
        }
        else
            RETURN_ERROR(expecitngIntegerorClosingBracket)
    }
    else
    {
        NodeToken nd = NodeToken(current());
        Error.error = 0;
        nodeTokenList.push_back(nd);
        next();
        return;
    }
}

void Parser::parseFactor()
{
#ifdef PARSER_DEBUG
    updateMem();
#endif

    Error.error = 0;

    if (Match(TokenStar) && Match(TokenIdentifier, 1))
    {
        _asPointer = true;

        next();
        // return;
    }
    if (Match(TokenUppersand) && Match(TokenIdentifier, 1))
    {
        isPointer = true;
        // printf("qsldkqsld\n");
        next();
        // return;
    }
    if (current()->getType() == TokenEndOfFile)
    {

        next();
        return;
    }

    else if (Match(TokenNumber))
    {

        // NodeNumber g = NodeNumber(current());
        current_node->addChild(NodeToken(current(), numberNode));
        if (change_type.size() > 0)
        {
            if (change_type.back()->_vartype != __float__)
            {
                // A bare number literal is tokenized as __float__ (has a
                // '.') or __int__ (tokenize.cpp) -- __uint32_t__ was
                // never actually reachable here, so plain integer
                // literals (e.g. a call argument like `fib(10)`) never
                // propagated their type into change_type, leaving it at
                // its __none__ placeholder. That was invisible as long
                // as __none__'s signature marker happened to be the same
                // string as every numeric type's (both "d"); now that
                // they're distinct ("void" vs "num", matching upstream
                // ESPLiveScript's own convention), the mismatch between a
                // declaration's signature and such a call site's
                // surfaced as a real "function not found" error. Fixed
                // by also propagating __int__.
                if (current()->_vartype == __float__ || current()->_vartype == __uint32_t__ || current()->_vartype == __int__)
                {
                    change_type.back()->_vartype = current()->_vartype;
                }
            }
        }
        next();

        Error.error = 0;
        // result._nd = g;
        // printf("exit factor\n");

        return;
    }

    else if (Match(TokenNot) || Match(TokenAddition) || Match(TokenSubstraction) || Match(TokenUppersand) || Match(TokenKeywordFabs) || Match(TokenKeywordAbs))
    {

        current_node = current_node->addChild(NodeToken(unitaryOpNode));
        sav_t.push_back(*current());

        next();

        parseFactor();
        RETURN_IF_ERROR
        current_node->type = sav_t.back().type;
        sav_t.pop_back();
        current_node = current_node->parent;
        Error.error = 0;
        return;
    }

    else if (Match(TokenOpenParenthesis) && Match(TokenKeywordVarType, 1) && Match(TokenCloseParenthesis, 2) && Match(TokenOpenParenthesis, 3))
    {

        next();

        current_node = current_node->addChild(NodeToken(current(), changeTypeNode));

        next(); //)
        next(); //(
        next();

        NodeToken nd = NodeToken(changeTypeNode);
        nd._nodetype = changeTypeNode;
        nd.type = TokenKeywordVarType;
        nd._vartype = current_node->_vartype;

        current_node = current_node->addChild(nd);
        change_type.push_back(current_node);

        parseExpr();
        RETURN_IF_ERROR
        if (Match(TokenCloseParenthesis))
        {
            next();
            Error.error = 0;
            // current_node = current_node->parent;

            // Same gap as parseFactor()'s TokenNumber case above (see its
            // comment): this cast's own result type never propagated
            // into the *enclosing* change_type context (e.g. a call
            // argument like `setPixel((int)(2 * xc - i), j, 1)`'s first
            // argument), so an argument that was only a cast expression
            // -- never a bare literal/identifier for the outer branches
            // to have already caught -- left the enclosing signature's
            // type at __none__.
            uint8_t _castType = current_node->_vartype;
            current_node = current_node->parent;
            current_node = current_node->parent;
            change_type.pop_back();
            if (change_type.size() > 0 && change_type.back()->_vartype != __float__)
            {
                if (_castType == __float__ || _castType == __uint32_t__ || _castType == __int__ ||
                    _castType == __uint8_t__ || _castType == __uint16_t__ || _castType == __s_int__ ||
                    _castType == __bool__)
                {
                    change_type.back()->_vartype = _castType;
                }
            }

            // current_node=current_node->parent;
            return;
        }
        else
            RETURN_ERROR(expectingClosingparenthesis)
    }
    else if (Match(TokenOpenParenthesis))
    {
        next();

        parseExprAndOr();

        RETURN_IF_ERROR

        if (Match(TokenCloseParenthesis))
        {
            next();
            Error.error = 0;
            return;
        }
        else
            RETURN_ERROR(expectingClosingparenthesis)
    }

    else if (Match(TokenIdentifier) && !Match(TokenOpenParenthesis, 1))
    {
        getVariable(false);
        if (Error.error)
        {
            return;
        }

        if (change_type.size() > 0)
        {

            if (tmp_sav->children_size() == 0 && !(tmp_sav->asPointer))
                change_type.back()->isPointer = tmp_sav->isPointer;
            if (change_type.back()->_vartype != __float__)
            {
                // Same gap as parseFactor()'s TokenNumber case above (see
                // its comment): only float/uint32_t propagated, so a
                // variable declared e.g. `int`/`uint8_t`/`bool` used as a
                // call argument (like `report(c)` with `int c;`) never
                // updated change_type's inferred type either.
                if (tmp_sav->_vartype == __float__ || tmp_sav->_vartype == __uint32_t__ ||
                    tmp_sav->_vartype == __int__ || tmp_sav->_vartype == __uint8_t__ ||
                    tmp_sav->_vartype == __uint16_t__ || tmp_sav->_vartype == __s_int__ ||
                    tmp_sav->_vartype == __bool__)
                {
                    change_type.back()->_vartype = tmp_sav->_vartype;
                }
            }
        }

        RETURN_IF_ERROR

        return;
    }

    else if (Match(TokenUserDefinedVariable) && Match(TokenOpenParenthesis, 1))
    {
        sav_b = isStructFunction;
        isStructFunction = true;
        NodeToken d = NodeToken(current_node->parent->getChildPtr(0));

        _node_token_stack.push_back(current_node->parent->getChildPtr(0));

        current_node = current_node->parent;

        if (d._nodetype == storeGlobalVariableNode)
            d._nodetype = globalVariableNode;
        else if (d._nodetype == storeLocalVariableNodeAsRegister)
            d._nodetype = localVariableNodeAsRegister;
        else
            d._nodetype = localVariableNode;
        d.type = TokenUserDefinedVariableMemberFunction;
        current_node->children[0]->children_pop();
        current_node->children[0]->children_pop();
        nodeTokenList.push_back(d);

        // current_node->children->clear();
        current()->addText(string_format(_s_dot_underscore_arobase_s, current()->getText(), current()->getText()));
        parseFunctionCall();
        if (Error.error)
        {
            return;
        }
        // current_node->getChildAtPos(current_node->children_size() - 1)->getChildAtPos(2)->getChildAtPos(0)->copyChildren(_node_token_stack.back());
        // 16/03
        nodeTokenList.backptr()->clear();
        _node_token_stack.pop_back();

        isStructFunction = sav_b;
        return;
    }
    else if (Match(TokenIdentifier) && Match(TokenOpenParenthesis, 1))
    {
        sav_b = isStructFunction;
        isStructFunction = false;
        parseFunctionCall();
        if (Error.error)
        {
            return;
        }

        isStructFunction = sav_b;
        return;
    }

    else if (Match(TokenKeywordVarType) && Match(TokenOpenParenthesis, 1))
    {
        // on tente CRGB()
        // token *typeVar = current();
        sav_t.push_back(*current());
        // NodeNumber num = NodeNumber( current());
        current_node = current_node->addChild(NodeToken(current(), numberNode));
        next();
        if (Match(TokenOpenParenthesis))
        {
            for (int i = 0; i < sav_t.back().getVarTypeObj()->size; i++)
            {
                next();
                parseExpr();
                if (Error.error)
                {
                    next();
                    return;
                }
                // num.addChild(res._nd);
                if (i == sav_t.back().getVarTypeObj()->size - 1)
                {
                    if (!Match(TokenCloseParenthesis))
                        RETURN_ERROR(expectingClosingparenthesis)
                }
                else
                {
                    if (!Match(TokenComma))
                        RETURN_ERROR(expectingcomma)
                }
            }
            next();
            current_node = current_node->parent;
            current_node->_vartype = __CRGB__;
            // resParse result;
            Error.error = 0;

            // result._nd=num;
            sav_t.pop_back();
            return;
        }
        else
            RETURN_ERROR(expectingOpenparenthesis)
    }
    else if (Match(TokenString))
    {
        // printf("in a stirn\n");
        //  NodeToken nd; //=NodeToken();
        NodeToken nd = NodeToken(defGlobalVariableNode);
        nd._nodetype = defGlobalVariableNode;
        nd.type = TokenKeywordVarType;

        nd._vartype = __char__;
        nd.isPointer = true;
        nd.textref = all_text.addText(string_format(_local_string_d, for_if_num));
        for_if_num++;

        current_cntx->addChild(nd);
        program.addChildFront(nd)->addChild(NodeToken(current(), stringNode));

        nd._nodetype = globalVariableNode;

        if (current_node->_nodetype == changeTypeNode)
        {
            current_node->_vartype = __char__;
            current_node->isPointer = true;
        }
        current_node->addChild(nd);
        next();
        // printf("in a stirn end\n");
        return;
    }
    RETURN_ERROR(impossibletofindtoken)
    return;
}
void Parser::parseFunctionCall()
{
#ifdef PARSER_DEBUG
    pushToConsole(string_format("functions:%s", __FUNCTION__));
    updateMem();
#endif
    // printf("calling  function yves %s\r\n", current()->getText());

    sav_t.push_back(*current());
    next();
    next();

    findCandidate(&functions, sav_t.back().getText());
    if (!findCandidate(&functions, sav_t.back().getText()))
    {
        if (struct_name != NULL)
        {
           
            char *v_tmp = string_format(_s_dot_s_, struct_name, sav_t.back().getText());
            if (findCandidate(&functions, v_tmp))
                isStructFunction = true;
               free(v_tmp);
        }
    }
    // printf("calling  function suite\r\n");
    parseArguments();
    //       printf("calling  function suite\r\n");
    if (Error.error)
    {
        return;
    }

    _node_token_stack.push_back(current_node->children_backptr());
    // NodeToken d = current_node->children->back();
    current_node->children_pop();

    sav_t.backptr()->addText(string_format(_s_s_, sav_t.back().getText(), all_text.getText(sigs.back())));
    sigs.pop_back();
    findFunction(&functions, sav_t.back().getText());
    // NodeToken *t =search_result;
    if (search_result == NULL)
    {

        if (struct_name != NULL)
        {
            sav_t.push_back(sav_t.back());
            sav_t.backptr()->addText(string_format(_s_dot_s_, struct_name, sav_t.back().getText()));
            findFunction(&functions, sav_t.back().getText());
            isStructFunction = true;
            sav_t.pop_back();
            // sav_t.pop_back();
        }
        if (search_result == NULL)
        {
            
            bool found=false;
            int savestacksize=0;
            for (int i = 0; i < binded_assets.size(); i++)
            {
                // printf("comparing %s ,%s \n\r", external_links[i].signature.c_str(), external_links[i].signature.c_str());
                //   bool
                found = false;

                if (strstr(extern_text.getText( binded_assets[i].sign_ref), "Args") != NULL)
                {
                    int l = strstr(extern_text.getText( binded_assets[i].sign_ref), "Args") - extern_text.getText( binded_assets[i].sign_ref);
                    if (l > 0)
                        l--;
                    if (strncmp(extern_text.getText( binded_assets[i].sign_ref), sav_t.back().getText(), l) == 0)
                    {
                        found = true;
                    }
                }
                //else if (external_links[i].signature.compare(string(sav_t.back().getText())) == 0)
                else if (strcmp(extern_text.getText( binded_assets[i].sign_ref),sav_t.back().getText()) == 0)
                {
                    found = true;
                }
                if (found)
                {
                    //   printf("her\n\r");
                    savestacksize = stack_size;
                   // sav_token.push_back(current_node);     
                    sav_current_node=current_node;
                    sav_current_cntx=current_cntx;
                    current_cntx=&ext_function_cntx;
                    isExtra=true;
                    // Auto-declared (bindFunction()-only, no `external` line
                    // in the script) function declarations used to be
                    // parsed into extra_parser -- a scratch NodeToken that
                    // is never a descendant of `program` -- so the
                    // synthesized defExtFunctionNode was registered in the
                    // `functions` lookup table (findFunction() below
                    // succeeds) but program.visitNode() never walked it,
                    // and its jump-table header reservation never got
                    // emitted. Parsing straight into `program` instead
                    // makes it a real part of the tree that gets visited
                    // like any other external declaration.
                    current_node = &program;
                    // string toinsert = external_links[i].name; //"external " + external_links[i].out + " " + external_links[i].name + "("+external_links[i].in + ");";
                   
                    //  main_script.previousChar();
                    Script extra_script;
                    extra_script.clear();

                    extra_script.addContent(  extern_text.getText( binded_assets[i].name_ref));
                    extra_script.init();
                    _tks = &_extra_tks;
                    // extra_script.nextChar();

                    _tks->clear();

                    // next();
                    // prev();
                    pos_in_line = 0;
                    insecond = true;
                    _tks->tokenizelow(&extra_script, true, true, 20);
                    insecond = false;
                    //  printf("%s \n\r",next()->getText());
                    // _for_display=false;

                    // printf("%s \n\r",next()->getText());
                    // printf("%s \n\r",next()->getText());

                    // prev();
                    sav_b = isStructFunction;
                    isStructFunction = false;
                    parseType();

                    if (Error.error)
                    {
                        //         printf("ice\n\r");
                        return;
                    }

                    parseDefFunction();
                    extra_script.clear();
                    _extra_tks.clear();
                    isExtra=false;
                    if (Error.error)
                    {
                        //           printf("cold\n\r");
                        return;
                    }
                   // current_node = sav_token.pop_back();
                   // _node_token_stack.pop_back();
                   current_node=sav_current_node;
                    isStructFunction = sav_b;
                    current_cntx=sav_current_cntx;
                    break;
                    // return;

                }
                
            }
            _tks = &__allTokens;
            
           extra_parser.clear();
            stack_size = savestacksize;
            
            findFunction(&functions, sav_t.back().getText());
            if (search_result == NULL)
            {
                RETURN_ERROR(functionnotfound)
            }
            // current()->type=TokenSemicolon;
            
           
        }
    }

    // NodeToken *res=search_result;

    NodeToken _nd = NodeToken(*search_result);
    if (_nd._nodetype == (int)defExtFunctionNode)
    {

        _nd._nodetype = extCallFunctionNode;
    }
    else // if (_nd._nodetype == (int)defFunctionNode)
    {
        _nd._nodetype = callFunctionNode;
    }

    // NodeExtCallFunction function = NodeExtCallFunction(t);
    _nd.target = search_result_index;
    current_node = current_node->addChild(_nd);
    // current_node->copyChildren(search_result);
    // current_node->addChild(NodeToken(search_result->getChildAtPos(0)));

    // if (search_result->getChildAtPos(0)->_vartype == __float__ and change_type.size() > 0)
    //   change_type.back()->_vartype = __float__;

    if (change_type.size() > 0)
    {
        if (search_result->getChildPtr(0)->children_size() == 0 && !search_result->getChildPtr(0)->asPointer)
            change_type.back()->isPointer = search_result->getChildPtr(0)->isPointer; // n,ew modif here
        if (change_type.back()->_vartype != __float__)
        {
            // Same gap as parseFactor()'s TokenNumber case (see its
            // comment): only float/uint32_t propagated, so a function
            // call returning e.g. int/uint8_t/bool used within another
            // expression never updated change_type's inferred type.
            if (search_result->getChildPtr(0)->_vartype == __float__ || search_result->getChildPtr(0)->_vartype == __uint32_t__ ||
                search_result->getChildPtr(0)->_vartype == __int__ || search_result->getChildPtr(0)->_vartype == __uint8_t__ ||
                search_result->getChildPtr(0)->_vartype == __uint16_t__ || search_result->getChildPtr(0)->_vartype == __s_int__ ||
                search_result->getChildPtr(0)->_vartype == __bool__)
            {
                change_type.back()->_vartype = search_result->getChildPtr(0)->_vartype;
            }
        }
    }
    //  NodeToken *o = current_node->addChild(NodeToken(search_result->getChildAtPos(1)));
    //  o->copyChildren(search_result->getChildAtPos(1));

    // sav_nb_arg = function._link->getChildAtPos(1)->children_size();

    nb_sav_args.push_back(search_result->getChildPtr(1)->children_size());
    if (isStructFunction)
    {
        // nb_sav_args.push_back( nb_sav_args.back()-1);
        isStructFunction = false;
    }
    for (int i = 0; i < search_result->getChildPtr(1)->children_size(); i++)
    {
        if (search_result->getChildPtr(1)->getChildPtr(i)->_vartype == __Args__)
        {

            nb_sav_args.pop_back();
            nb_sav_args.push_back(999);
        }
    }

    current_node->_vartype = search_result->getChildPtr(0)->_vartype;

    current_node->addChildClear(_node_token_stack.back());
    _node_token_stack.pop_back();

    if (nb_sav_args.back() != nb_args.back() and nb_sav_args.back() != 999) // if (sav_nb_arg != nb_args.back())
        RETURN_ERROR(wrongnumberofarguments)
    nb_args.pop_back();
    nb_sav_args.pop_back();
    sav_t.pop_back();
    Error.error = 0;
    current_node = current_node->parent;

    return;
}

void Parser::parseStatement()
{

#ifdef PARSER_DEBUG
    updateMem();
#endif

    isPointer = false;
    sav_token.clear();
    Error.error = 0;
    current_node->addChild(NodeToken(statementNode));
    //  updateMem();
    if (Match(TokenString))
    {
        current_node->addChild(NodeToken(current(), stringNode));
        next();
        return;
    }
    else if (Match(TokenKeywordBreak))
    {
        char *c = findForWhile();
        if (c == NULL)
            RETURN_ERROR(noWhileorForFound)
        next();
        if (Match(TokenSemicolon))
        {
            current_node->addChild(NodeToken(c, breakNode));
            next();
            return;
        }
        else
            RETURN_ERROR(expectingSemicolon)
    }
    else if (Match(TokenKeywordContinue))
    {
        char *c = findForWhile();
        if (c == NULL)
            RETURN_ERROR(noWhileorForFound)
        next();
        if (Match(TokenSemicolon))
        {
            current_node->addChild(NodeToken(c, continueNode));
            next();
            return;
        }
        else
            RETURN_ERROR(expectingSemicolon)
    }
    else if (Match(TokenKeywordReturn))
    {
        next();
        if (Match(TokenSemicolon))
        {
            current_node->addChild(NodeToken(returnNode));
            next();
            return;
        }
        else
        {
            current_node = current_node->addChild(NodeToken(returnNode));

            NodeToken nd = NodeToken(changeTypeNode);
            nd._nodetype = changeTypeNode;
            nd.type = TokenKeywordVarType;

            nd._vartype = __none__;
            if (lasttype != NULL)
            {
                nd._vartype = lasttype->_vartype;
            }
            else
                RETURN_ERROR(issuewithReturn)

            current_node = current_node->addChild(nd);
            change_type.push_back(current_node);
            parseExpr();
            if (Error.error)
            {
                return;
            }
            if (Match(TokenSemicolon))
            {
                Error.error = 0;
                current_node = current_node->parent;
                // res._nd = var;

                current_node = current_node->parent;
                change_type.pop_back();
                next();
                return;
            }
            else
                RETURN_ERROR(expectingSemicolon)
        }
    }

    else if (Match(TokenIdentifier) && Match(TokenOpenParenthesis, 1))
    {
        sav_b = isStructFunction;
        isStructFunction = false;
        parseFunctionCall();

        isStructFunction = sav_b;
        if (Error.error)
        {
            return;
        }
        else
        {
            if (Match(TokenSemicolon))
            {
                Error.error = 0;

                next();
                return;
            }
            else
                RETURN_ERROR(expectingSemicolon)
        }
    }
    if (Match(TokenIdentifier) && (Match(TokenPlusPlus, 1) or Match(TokenMinusMinus, 1)))
    {

        current_node = current_node->addChild(NodeToken(assignementNode));
        getVariable(true);
        if (Error.error)
        {
            return;
        }

        current_node = current_node->addChild(NodeToken(unitaryOpNode));
        prev();
        getVariable(false);
        if (Error.error)
        {
            return;
        }
        current_node->type = current()->type;

        next();
        current_node = current_node->parent;
        current_node = current_node->parent;
        if (!Match(TokenSemicolon) && !Match(TokenCloseParenthesis))
            RETURN_ERROR(expectingsemicolonorcloseparenthesis)
        Error.error = 0;
        next();
        return;
    }
    else if (Match(TokenStar) && Match(TokenIdentifier, 1))
    {
        _asPointer = true;
        next();

        Error.error = 0;
        // return;
    }
    else if (Match(TokenIdentifier))
    {
        // NodeAssignement nd;

        current_node = current_node->addChild(NodeToken(assignementNode));
        getVariable(true);
        if (Error.error)
        {
            return;
        }
        NodeToken d = NodeToken(*current_node->getChildPtr(0));

        nodeTokenList.push_back(d);
        _asPointer = false;
        isPointer = false;
        if (Error.error)
        {
            return;
        }

        if (Match(TokenSemicolon))
        {
            Error.error = 0;

            current_node = current_node->parent;
            next();
           // nodeTokenList.backptr()->clear();
            nodeTokenList.pop_back();
            return;
        }
        if (Match(TokenEqual))
        {
            
            nodeTokenList.pop_back();

            NodeToken nd = NodeToken(changeTypeNode);
            nd._nodetype = changeTypeNode;
            nd.type = TokenKeywordVarType;
            // nd.addTargetText("yves");
           // PARSER_LOG(" ici  esual %d",tmp_sav->_vartype);
             nd._vartype = tmp_sav->_vartype;

            current_node = current_node->addChild(nd);
            change_type.push_back(current_node);
            next();
            parseExpr();
            if (Error.error)
            {
                return;
            }

            if (!Match(TokenSemicolon) && !Match(TokenCloseParenthesis))
                RETURN_ERROR(expectingSemicolon)

            Error.error = 0;
            // result._nd = nd;
            current_node = current_node->parent;
            current_node = current_node->parent; //  expr

            change_type.pop_back();
            next();
            return;
        }
        else if (Match(TokenPlusEqual) || Match(TokenMinusEqual) || Match(TokenStarEqual) || Match(TokenSlashEqual))
        {
            sav_t.push_back(*current());

            NodeToken nd = NodeToken(changeTypeNode);
            nd._nodetype = changeTypeNode;
            nd.type = TokenKeywordVarType;

            nd._vartype = tmp_sav->_vartype;

            current_node = current_node->addChild(nd);
            change_type.push_back(current_node);
            next();
            current_node = current_node->addChild(NodeToken(binOpNode));
            NodeToken *_d = current_node->addChildClear(nodeTokenList.pop_back());
            switch (_d->_nodetype)
            {
            case storeExtGlocalVariableNode:
                _d->_nodetype = extGlobalVariableNode;
                break;
            case storeGlobalVariableNode:
                _d->_nodetype = globalVariableNode;
                break;
            case storeLocalVariableNode:
                _d->_nodetype = localVariableNode;
                break;
            case storeLocalVariableNodeAsRegister:
                _d->_nodetype = localVariableNodeAsRegister;
                break;
            }
            Token __t = sav_t.back();
            switch (__t.type)
            {
            case TokenPlusEqual:
                __t.type = TokenAddition;
                break;
            case TokenMinusEqual:
                __t.type = TokenSubstraction;
                break;
            case TokenStarEqual:
                __t.type = TokenStar;
                break;
            case TokenSlashEqual:
                __t.type = TokenSlash;
                break;
            }

            current_node->type = __t.type;
            sav_t.pop_back();

            parseExpr();
            if (Error.error)
            {
                return;
            }
            if (Match(TokenSemicolon))
            {
                Error.error = 0;

                current_node = current_node->parent;
                current_node = current_node->parent;
                current_node = current_node->parent;
                next();

                return;
            }
            else
                RETURN_ERROR(expectingSemicolon)
        }

        else
            RETURN_ERROR(expectingEqual)
    }

    else if (Match(TokenKeywordElse))
    {

        current_cntx = current_cntx->addChild(NodeToken());
        targetList.push(all_text.addText(string_format(_label_underscore_d, for_if_num)));
        // targetList.push(string_format("label_%d", for_if_num));

        for_if_num++;

        current_node = current_node->addChild(NodeToken(current(), elseNode, targetList.pop()));
        next();

        if (Match(TokenOpenCurlyBracket))
        {
            parseBlockStatement();
            RETURN_IF_ERROR
        }
        else
        {
            // next();
            parseStatement();
            RETURN_IF_ERROR
        }

        Error.error = 0;

        current_cntx = current_cntx->parent;
        current_node = current_node->parent;

        return;
    }
    else if (Match(TokenKeywordWhile))
    {
        // on tente le for(){}
        sav_t.push_back(*current());

        current_cntx = current_cntx->addChild(NodeToken());
        targetList.push(all_text.addText(string_format(_label_underscore_d, for_if_num)));
        // targetList.push(string_format("label_%d", for_if_num));

        for_if_num++;
        next();
        if (Match(TokenOpenParenthesis))
        {

            current_node = current_node->addChild(NodeToken(sav_t.backptr(), whileNode, targetList.get()));
            next();

            parseComparaison();

            RETURN_IF_ERROR

            if (Match(TokenOpenCurlyBracket))
            {
                parseBlockStatement();
                RETURN_IF_ERROR
            }
            else
            {

                parseStatement();
                RETURN_IF_ERROR
            }

            Error.error = 0;

            current_cntx = current_cntx->parent;
            current_node = current_node->parent;
            sav_t.pop_back();

            return;
        }
        else
            RETURN_ERROR(expectingOpenparenthesis)
    }
    else if (Match(TokenKeywordIf))
    {

        current_cntx = current_cntx->addChild(NodeToken());

        targetList.push(all_text.addText(string_format(_label_underscore_d, for_if_num)));
        // targetList.push(string_format("label_%d", for_if_num));

        for_if_num++;

        if (Match(TokenOpenParenthesis, 1))
        {

            current_node = current_node->addChild(NodeToken(current(), ifNode, targetList.get()));
            next();
            next();

            parseComparaison();
            if (Error.error)
            {
                return;
            }

            if (Match(TokenOpenCurlyBracket))
            {
                parseBlockStatement();
                if (Error.error)
                {
                    return;
                }
            }
            else
            {

                parseStatement();
                if (Error.error)
                {
                    return;
                }
            }

            Error.error = 0;

            current_cntx = current_cntx->parent;
            current_node = current_node->parent;

            return;
        }
        else
            RETURN_ERROR(expectingOpenparenthesis)
    }
    else if (Match(TokenKeywordFor))
    {

        current_cntx = current_cntx->addChild(NodeToken());

        targetList.push(all_text.addText(string_format(_label_underscore_d, for_if_num)));
        // targetList.push(string_format("label_%d", for_if_num));;

        for_if_num++;

        if (Match(TokenOpenParenthesis, 1))
        {

            next();

            current_node = current_node->addChild(NodeToken(current(), forNode, targetList.get()));
            next();
            current_node = current_node->addChild(NodeToken(statementNode));

            _is_variable_as_register.set(false);

            if (_for_depth_reg.get() <= _MAX_FOR_DEPTH_REG)
            {
                _is_variable_as_register.set(true);
            }

            parseStatement();

            _is_variable_as_register.set(false);

            if (Error.error)
            {
                return;
            }
            current_node = current_node->parent;
            // printf(" *************** on parse comp/n");
            parseComparaison();
            if (Error.error)
            {
                return;
            }

            current_node = current_node->addChild(NodeToken(statementNode));
            parseStatement();

            if (Error.error)
            {
                return;
            }
            current_node = current_node->parent;
            if (Match(TokenOpenCurlyBracket))
            {
                parseBlockStatement();
                if (Error.error)
                {
                    return;
                }
            }
            else
            {

                parseStatement();
                if (Error.error)
                {
                    return;
                }
            }

            Error.error = 0;

            current_cntx = current_cntx->parent;
            current_node = current_node->parent;

            _for_depth_reg.decrease();
            return;
        }
        else
            RETURN_ERROR(expectingOpenparenthesis)
    }

    else if (Match(TokenKeywordVarType) or Match(TokenUserDefinedVariable))
    {

        parseType();
        varTypeEnum d = nodeTokenList.back().getVarTypeObj()->_varType;
        if (Error.error)
        {

            return;
        }
        nodeTokenList.push_back(nodeTokenList.back());
        parseVariableForCreation();
        if (Error.error)
        {

            return;
        }
        if (_for_depth_reg.get() <= _MAX_FOR_DEPTH_REG_2 and (d == __float__ or d == __int__ or d == __s_int__ or d == __uint8_t__ or d == __uint32_t__ or d == __uint16_t__))
        {
            _is_variable_as_register.set(true);
        }

        nodeTokenList.push_back(createNodeLocalVariableForCreation(nodeTokenList.pop_back(), nodeTokenList.pop_back()));

        _is_variable_as_register.set(false);

        current_cntx->addChild(nodeTokenList.back()); // 27/05
        if (Match(TokenComma))
        {
            while (Match(TokenComma))
            {
                next();
                if (nodeTokenList.back()._nodetype == defLocalVariableNodeAsRegister)
                {
                    nodeTokenList.backptr()->_nodetype=defLocalVariableNode;
                }
                nodeTokenList.push_back(nodeTokenList.back());
                parseVariableForCreation();
                if (Error.error)
                {

                    return;
                }
                nodeTokenList.push_back(createNodeLocalVariableForCreation(nodeTokenList.pop_back(), nodeTokenList.pop_back()));
                current_cntx->addChildClear(nodeTokenList.back());
            }
            if (!Match(TokenSemicolon))
                RETURN_ERROR(expectingSemicolon)
            else
            {
                next();
                nodeTokenList.pop_back();
                return;
            }
        }

        if (Match(TokenSemicolon))
        {
            Error.error = 0;

            if (nodeTokenList.back().type == TokenUserDefinedVariable)
            {

                current()->addText(string_format(_s_dot_underscore_arobase_s_openp_closep, nodeTokenList.back().getVarTypeObj()->varName, nodeTokenList.back().getVarTypeObj()->varName));
                findFunction(&functions, current()->getText());

                if (search_result != NULL)
                {

                    NodeToken nd = nodeTokenList.back();
                    nd._nodetype = callConstructorNode;
                    current_node->addChild(nodeTokenList.pop_back());
                    current_node->addChildClear(nd);
                    nodeTokenList.backptr()->clear();
                    nodeTokenList.pop_back();
                    next();
                    return;
                }
                else
                {
                    current_node->addChildClear(nodeTokenList.pop_back());
                    nodeTokenList.pop_back();
                    next();
                    return;
                }
            }
            current_node->addChildClear(nodeTokenList.pop_back());
            nodeTokenList.pop_back();

            next();
            return;
        }

        if (Match(TokenEqual))
        {

            tmp_sav = current_node->addChildClear(nodeTokenList.back());
            if (nodeTokenList.back().type == TokenUserDefinedVariable)
            {

                findVariable(current_cntx, nodeTokenList.back().getText(), false);
                if (search_result == NULL)
                    RETURN_ERROR(impossibletofindvariabledeclaration)

                next();
                current()->addText(string_format(_s_dot_underscore_arobase_s, search_result->getVarTypeObj()->varName, current()->getText()));
                NodeToken nd = NodeToken(*search_result);

                if (search_result->_nodetype == defGlobalVariableNode)
                    nd._nodetype = globalVariableNode;
                else
                {
                    if (search_result->_nodetype == defLocalVariableNodeAsRegister)
                    {
                        nd._nodetype = localVariableNodeAsRegister;
                        nd.target = search_result->target;
                    }
                    else
                        nd._nodetype = localVariableNode; // globalVariableNode;
                }
                nd.type = TokenUserDefinedVariableMemberFunction;
                nd.isPointer = true;
                nd._total_size = search_result->getVarTypeObj()->total_size;

                nodeTokenList.push_back(nd);
                isStructFunction = true;

                parseFunctionCall();

                if (Error.error)
                {
                    return;
                }
                if (!Match(TokenSemicolon))
                    RETURN_ERROR(expectingSemicolon)
                next();

                isStructFunction = false;
                Error.error = 0;

                return;
            }
            //PARSER_LOG("chagfe tyuêr type %d",tmp_sav->_vartype);
            current_node = current_node->addChild(NodeToken(assignementNode));
           // PARSER_LOG("chagfe tyuêr type %d",tmp_sav->_vartype);
            next();

            NodeToken _uniquesave = nodeTokenList.pop_back();
            if (_uniquesave.getNodeTokenType() == defLocalVariableNode)
            {
                _uniquesave._nodetype = (int)storeLocalVariableNode;
            }
            else if (_uniquesave.getNodeTokenType() == defLocalVariableNodeAsRegister)
            {
                _uniquesave._nodetype = (int)storeLocalVariableNodeAsRegister;
            }
            else
            {
                _uniquesave._nodetype = (int)storeGlobalVariableNode;
            }

            current_node->addChildClear(_uniquesave);

            NodeToken nd = NodeToken(changeTypeNode);
            nd._nodetype = changeTypeNode;
            nd.type = TokenKeywordVarType;
            nd._vartype = tmp_sav->_vartype;

            current_node = current_node->addChild(nd);
            change_type.push_back(current_node);
            parseExpr();

            if (Error.error)
            {

                return;
            }

            if (!Match(TokenSemicolon))
                RETURN_ERROR(expectingSemicolon)

            Error.error = 0;

            current_node = current_node->parent;

            current_node = current_node->parent; //  expr
            change_type.pop_back();
            next();
            return;
        }

        else
            RETURN_ERROR(expectingsemicolonorequal)

        return;
    }
    else
    {
        parseExpr();

        RETURN_IF_ERROR

        if (!Match(TokenSemicolon) && !Match(TokenCloseParenthesis))
            RETURN_ERROR(expectingSemicolon)
        current_node = current_node->parent; //  expr

        change_type.pop_back();
        next();
        return;
    }
}

void Parser::parseBlockStatement()
{
#ifdef PARSER_DEBUG
    updateMem();
#endif

    // updateMem();
    current_cntx = current_cntx->addChild(NodeToken());
    block_statement_num++;

    current_node = current_node->addChild(NodeToken(current(), blockStatementNode));
    next();
    while (!Match(TokenCloseCurlyBracket) && !Match(TokenEndOfFile))
    {

        parseStatement();

        if (Error.error)
        {
            return;
        }
    }
    if (Match(TokenEndOfFile))
        RETURN_ERROR(expectingCloseCurlyBracket)

    Error.error = 0;
    current_cntx = current_cntx->parent;
    current_node = current_node->parent;
    next();
    return;
}

void Parser::parseDefFunction()
{

#ifdef PARSER_DEBUG
    updateMem();
#endif
if (!isStructFunction and !isExtra)
{
    function_cntx.clear();
    current_cntx=&function_cntx;
}


    _for_depth_reg.push(2);
    _is_variable_as_register.push(false);
    if (isStructFunction)
        _for_depth_reg.increase();
    Error.error = 0;
    bool ext_function = false;
    bool is_asm = false;

    if (isExternal)
    {
        ext_function = true;
        isExternal = false;
    }
    if (isASM)
    {
        isASM = false;
        is_asm = true;
    };
    NodeToken nd;

    if (ext_function)
    {

        nd = NodeToken(current(), defExtFunctionNode);
       // PARSER_LOG("heres %s",nodeTypeNames[ current_node->_nodetype]);
        current_node = current_node->addChild(nd);
       // PARSER_LOG("heres %s",nodeTypeNames[ current_node->_nodetype]);
        lasttype = current_node->addChildClear(nodeTokenList.pop_back());
       // PARSER_LOG("heres %s",nodeTypeNames[ current_node->_nodetype]);
    }
    else if (is_asm)
    {
        nd = NodeToken(current(), defAsmFunctionNode);

        current_node = current_node->addChild(nd);
        lasttype = current_node->addChildClear(nodeTokenList.pop_back());
    }
    else
    {
        nd = NodeToken(current(), defFunctionNode);

        current_node = current_node->addChild(nd);
        lasttype = current_node->addChildClear(nodeTokenList.pop_back());
    }
    // on ajoute un nouveau contexte

    current_cntx = current_cntx->addChild(NodeToken());
    // current_cntx = k;
    if (isStructFunction)
    {
        stack_size = _STACK_SIZE + 4;
        current_node->type = TokenUserDefinedVariableMemberFunction;
    }
    else
    {
        stack_size = _STACK_SIZE;
    }
    block_statement_num = 0;
    next();
    next();
    parseCreateArguments();
    if (Error.error)
    {
        return;
    }

    current_node->setText(string_format(_s_s_, current_node->getText(), signature));
    free(signature);
    signature=NULL;
    findFunction(&functions, current_node->getText());
    bool isdeclaration = false;
    if (search_result != NULL) // if (current_cntx->findFunction(current()) != NULL)
    {

        if (search_result->_nodetype != declarationFunctionNode)
            RETURN_ERROR(functionAlreadyDeclared)

        else
        {
            isdeclaration = true;
        }
    }
    if (!isdeclaration)
    {
        functions.addChild(*current_node);
    }

    if (!Match(TokenCloseParenthesis))
        RETURN_ERROR(expectingClosingparenthesis)
    next();
    if (ext_function)
    {
        if (Match(TokenSemicolon))
        {

            Error.error = 0;
            current_cntx = current_cntx->parent;
            current_node->clear();
            current_node = current_node->parent;
            //current_node->children_pop();
            next();
            _is_variable_as_register.pop();
            _for_depth_reg.pop();
            return;
        }
        else
            RETURN_ERROR(impossibletoredefineexternal)
    }
    else if (Match(TokenSemicolon))
    {
        functions.children_backptr()->_nodetype = declarationFunctionNode;
        Error.error = 0;
        current_cntx = current_cntx->parent;
        current_node = current_node->parent;
        current_node->children_pop();
        next();
        _is_variable_as_register.pop();
        _for_depth_reg.pop();
        return;
    }
    else
    {
        if (Match(TokenOpenCurlyBracket))
        {

            parseBlockStatement();
            if (Error.error)
            {
                return;
            }

            // current_node->addChild(blocsmt._nd);
            // current_node = current_node->parent;
            current_node->stack_pos = stack_size;
            // result._nd = function;
            Error.error = 0;

           

#ifndef __MEM_PARSER
            buildParents(current_node);

            current_node->visitNode();
            current_node->clear();
            current_cntx->clear();
            _node_token_stack.clear();
            sav_token.clear();
            change_type.clear();
            // printf("after clean function %s\n",current_node->getTokenText());
           // updateMem();
#endif

            /*
            #ifndef __MEM_PARSER
                           printf("on compile %s\r\n",current_node->text.c_str());
                            __sav_pos = _tks->position;
                            buildParents(current_node);

                            current_node->visitNode(current_node);
                            clearContext(tobedeted);
                            _tks->position = __sav_pos;
            #endif
            */
            // printf("on a visité\r\n");
            current_cntx = current_cntx->parent;
            current_node = current_node->parent;
            _is_variable_as_register.pop();
            _for_depth_reg.pop();
            return;
        }
        else
            RETURN_ERROR(expectingOpenCurlyBracket)
    }
    _is_variable_as_register.pop();
    _for_depth_reg.pop();
    return;
}

void Parser::parseExprAndOr()
{
#ifdef PARSER_DEBUG
    updateMem();
#endif

    sav_token.push_back(current_node);

    parseExprConditionnal();

    RETURN_IF_ERROR
    while (Match(TokenDoubleUppersand) || Match(TokenDoubleOr))
    {

        sav_t.push_back(*current());
        next();

        _node_token_stack.push_back(*current_node->children_backptr());
       // //current_node->children_backptr()->clear();
        current_node->children_pop();

        current_node = current_node->addChild(NodeToken(binOpNode));
        current_node->addChildClear(_node_token_stack.back());
        _node_token_stack.pop_back();

        if ((sav_t.backptr())->type == TokenDoubleUppersand)
            (sav_t.backptr())->type = TokenKeywordAnd;
        else
            (sav_t.backptr())->type = TokenKeywordOr;
        current_node->type = sav_t.back().type;
        sav_t.pop_back();
        parseExprConditionnal();
        RETURN_IF_ERROR
        current_node = current_node->parent;
    }
    // next();
    current_node = sav_token.back();
    sav_token.pop_back();

    Error.error = 0;
    return;
}
void Parser::parseExprConditionnal()
{
#ifdef PARSER_DEBUG
    updateMem();
#endif
    sav_token.push_back(current_node);
    parseExpr();
    RETURN_IF_ERROR
    // TokenNotEqual ("!=") was missing here even though visitnode.cpp's
    // codegen has always fully handled it (see its TokenDoubleEqual/
    // TokenNotEqual/TokenMoreThan switch cases) -- meaning "!=" has never
    // actually worked in any script: without this, "!=" is left
    // unconsumed after parseExpr() returns, desyncing the token cursor
    // for everything that follows and eventually crashing deep inside an
    // unrelated statement (an assert(_size > 0) in vect::pop_back(),
    // change_type popped once too often) rather than failing cleanly at
    // the actual "!=" site.
    while (Match(TokenDoubleEqual) || Match(TokenNotEqual) || Match(TokenLessOrEqualThan) || Match(TokenLessThan) || Match(TokenMoreOrEqualThan) || Match(TokenMoreThan))
    {

        // token *op = current();
        targetList.push(all_text.addText(string_format(_label_underscore_d, for_if_num)));
        // targetList.push(string_format("label_%d", for_if_num));

        //=target;
        for_if_num++;
        sav_t.push_back(*current());
        next();
        _node_token_stack.push_back(current_node->children_back());
       // //current_node->children_backptr()->clear();
        current_node->children_pop();
        current_node = current_node->addChild(NodeToken(testNode));
        // current_node->addChild(NodeToken(&sav_t.back(), operatorNode));
        current_node->type = sav_t.back().type;
        current_node->target = targetList.pop();
        // NodeToken nd;
        NodeToken nd = NodeToken(changeTypeNode);
        // nd._nodetype = changeTypeNode;
        nd.type = TokenKeywordVarType;
        nd._vartype = findfloat(_node_token_stack.backptr());
        if (nd._vartype != __float__)
        {
            nd._vartype = finduint32_t(_node_token_stack.backptr());
        }
        current_node = current_node->addChild(nd);
        change_type.push_back(current_node);
        current_node->addChildClear(_node_token_stack.back());
        _node_token_stack.pop_back();
        current_node = current_node->parent;
        // Was missing this pop_back() to match the push_back() above --
        // change_type was left with an orphaned entry (from this comparator
        // node, which nothing further keeps alive) after every comparison
        // operator, until eventually the node it pointed to got freed
        // elsewhere while the stale entry was still sitting there. Found via
        // a use-after-free parser.cpp's own change_type.back() read
        // (parseFactor()'s TokenNumber case) would intermittently hit,
        // 100% reproducible under AddressSanitizer once isolated to this
        // function -- see "float type + ternary"'s test script for the
        // simplest repro (any comparison works, e.g. `a < 2.0`).
        change_type.pop_back();
        NodeToken nd2 = NodeToken(changeTypeNode);
        // nd._nodetype = changeTypeNode;
        nd2.type = TokenKeywordVarType;
        nd2._vartype = __none__;
        current_node = current_node->addChild(nd);
        // current_node->type=sav_t.back().type;
        change_type.push_back(current_node);
        sav_t.pop_back();
        parseExpr();
        RETURN_IF_ERROR

        current_node = current_node->parent;
        current_node = current_node->parent;
        change_type.pop_back();
    }

    current_node = sav_token.back();
    sav_token.pop_back();

    Error.error = 0;
    return;
}
void Parser::parseExpr()
{
#ifdef PARSER_DEBUG
    updateMem();
#endif
    sav_token.push_back(current_node);

    parseTerm();
    RETURN_IF_ERROR
    while (Match(TokenAddition) || Match(TokenSubstraction) || Match(TokenShiftLeft) || Match(TokenShiftRight))
    {

       
      //  sav_t.push_back(*current());
       
        _node_token_stack.push_back(current_node->children_back());
     // //current_node->children_backptr()->clear();
        current_node->children_pop();
        current_node = current_node->addChild(NodeToken(binOpNode,current()->getType()));
        current_node->addChildClear(_node_token_stack.pop_back());
        //_node_token_stack.pop_back();
        // current_node->addChild(NodeToken(&sav_t.back(), operatorNode));
       // current_node->type = sav_t.back().type;
        //sav_t.pop_back();
        next();
        parseTerm();
        RETURN_IF_ERROR
        current_node = current_node->parent;
    }

    current_node = sav_token.back();
    sav_token.pop_back();

    Error.error = 0;
    return;
}

void Parser::parseTerm()
{
    
#ifdef PARSER_DEBUG
    updateMem();
#endif
    sav_token.push_back(current_node);
    parseFactor();
    RETURN_IF_ERROR
    if (Match(TokenQuestionMark))
    {
        next();
        _node_token_stack.push_back(current_node->children_back());
       // //current_node->children_backptr()->clear();
        current_node->children_pop();
        current_node = current_node->addChild(NodeToken(ternaryIfNode));
        current_node->addChildClear(_node_token_stack.back());
        _node_token_stack.pop_back();
        current_node->addTargetText(string_format(_label_tern_d, for_if_num));
        for_if_num++;
        parseExpr();
        if (Error.error)
        {
            return;
        }
        if (Match(TokenColon))
        {
            next();

            parseExpr();
            if (Error.error)
            {
                return;
            }
        }
        else
            RETURN_ERROR(expectingSemicolon)

        current_node = current_node->parent;
    }

    while (Match(TokenStar) || Match(TokenSlash) || Match(TokenModulo) || Match(TokenKeywordOr) || Match(TokenKeywordAnd) || Match(TokenPower))
    {
        sav_t.push_back(*current());
        next();
        _node_token_stack.push_back(current_node->children_back());
        current_node->children_pop();
        current_node = current_node->addChild(NodeToken(binOpNode));
        current_node->addChildClear(_node_token_stack.back());
        _node_token_stack.pop_back();
        current_node->type = sav_t.back().type;
        sav_t.pop_back();
        parseFactor();
        
        RETURN_IF_ERROR
        current_node = current_node->parent;
    }

    current_node = sav_token.back();
    sav_token.pop_back();
   
    return;
}
void Parser::parseCreateArguments()
{
      
#ifdef PARSER_DEBUG
    updateMem();
#endif
    Error.error = 0;
    // printf("heres\n");
    signature = NULL;
    signature = str_concat(_s_s_, signature, _openparenthesis_);

    current_node = current_node->addChild(NodeToken(defInputArgumentsNode));
    if (isStructFunction)
    {
        NodeToken nd = NodeToken();
        nd.addTargetText(_pointer_);
        nd.isPointer = true;
        nd.type = TokenUserDefinedVariable;
        nd._nodetype = defLocalVariableNode;
        nd.stack_pos = _STACK_SIZE;
        current_node->addChild(nd);
        current_cntx->addChild(nd);
    }
    if (Match(TokenCloseParenthesis))
    {
        // resParse result;
        Error.error = 0;
        // result._nd = arg;
        current_node = current_node->parent;

        signature = str_concat(_s_s_, signature, _closeparenthesis_);

        return;
    }
    parseType();
    if (Error.error)
    {
        return;
    }

    signature = str_concat(_s_s_, signature, nodeTokenList.back().getVarTypeObj()->varName);

    if (nodeTokenList.back().isPointer)
        signature = str_concat(_s_s_, signature, _star_);

    parseVariableForCreation();
    if (Error.error)
    {
        return;
    }
    NodeToken _nd = nodeTokenList.pop_back();
    NodeToken _t = nodeTokenList.pop_back();

    copyPrty(&_t, &_nd);

    _is_variable_as_register.set(false);

    if (_for_depth_reg.get() <= _MAX_FOR_DEPTH_REG_2)
    {
        _is_variable_as_register.set(true);
    }
    if (_is_variable_as_register.get())
    {
        _nd = NodeToken(_nd, defLocalVariableNodeAsRegister);
        _nd.target = _for_depth_reg.get();
        _for_depth_reg.increase();
    }
    else
        _nd = NodeToken(_nd, defLocalVariableNode);

    _is_variable_as_register.set(false);
    current_node->addChild(_nd);
    current_cntx->addChildClear(_nd);
    while (Match(TokenComma))
    {
        next();
        parseType();
        if (Error.error)
        {
            return;
        }
        signature = str_concat(_s_s_s_, signature, "|", nodeTokenList.back().getVarTypeObj()->varName);
        if (nodeTokenList.back().isPointer)
            signature = str_concat(_s_s_, signature, _star_);
        parseVariableForCreation();
        if (Error.error)
        {
            return;
        }
        NodeToken _nd = nodeTokenList.pop_back();

        NodeToken _t = nodeTokenList.pop_back();

        copyPrty(&_t, &_nd);
        if (_is_variable_as_register.get())
        {
            _nd = NodeToken(_nd, defLocalVariableNodeAsRegister);
            _nd.target = _for_depth_reg.get();
        }
        else
            _nd = NodeToken(_nd, defLocalVariableNode);

        current_node->addChild(_nd);
        current_cntx->addChild(_nd);
    }

    Error.error = 0;
    signature = str_concat(_s_s_, signature, _closeparenthesis_);
    current_node = current_node->parent;
       
    return;
}

char *findForWhile()
{

    NodeToken *p = current_node;
    while (p->_nodetype != forNode and p->_nodetype != whileNode)
    {
        p = p->parent;
        if (p == NULL)
            break;
    }
    if (p != NULL)
    {
        // //printf("jkd\n");
        return p->getTargetText();
    }
    return NULL;
}

void Parser::getVariable(bool isStore)
{

    //PARSER_LOG("trtyioen dto find %s",current()->getText());
    findVariable(current_cntx, current(), false); // false
    // NodeToken *nd = search_result;
    // printf("found %s\n",search_result->getTargetText());
    if (search_result == NULL)
    {

        bool found = false;
        
        for (int i = 0; i < binded_assets.size(); i++)
        {
            if (strcmp(current()->getText(),  extern_text.getText( binded_assets[i].name_ref)) == 0)
            {
                sav_t.push_back(*current());
                found = true;
              //  _node_token_stack.push_back(current_node);
                sav_current_node=current_node;
                extra_parser.clear();
                current_node = &extra_parser;
               // current_node = &program;
                // string toinsert = external_links[i].name;
                Script extra_script;
                extra_script.clear();
                _extra_tks.clear();
                // printf("on iserset %s\n", external_links[i].signature.c_str());
                extra_script.addContent(  extern_text.getText( binded_assets[i].sign_ref));
                extra_script.init();
               // __isBlockComment = false;
                _tks = &_extra_tks;
                
                //07/04
                /*
                for (int i = 0; i < 20; i++)
                {
                    _tks->push(Token());
                }
                */
                insecond = true;
                _tks->tokenizelow(&extra_script, true, true, 20);
                insecond = false;

                parseType();
                if (Error.error)
                {

                    return;
                }
                parseVariableForCreation();
                if (Error.error)
                {

                    return;
                }
               
                NodeToken nd = nodeTokenList.pop_back();
               
                NodeToken _t = nodeTokenList.pop_back();
                if (isExternal)
                {
                    nd._nodetype = (int)defExtGlobalVariableNode;

                    isExternal = false;
                }
                else
                {
                    nd._nodetype = (int)defGlobalVariableNode;
                }
                copyPrty(&_t, &nd);

                // Same gap as parseFunctionCall()'s auto-declare path above
                // (bindVariable()-only, no `external` line in the script):
                // main_context.addChild(nd) alone registers the synthesized
                // declaration for name lookup, but program.visitNode()
                // never sees it unless it's also attached to `program`, so
                // its jump-table header reservation never got emitted.
                program.addChild(nd);

                main_context.addChild(nd);
               // current_node = _node_token_stack.back();
               
                current_node=sav_current_node;
               // _node_token_stack.pop_back();

                _tks = &__allTokens;
                extra_parser.clear();
                extra_script.clear();
                _extra_tks.clear();
              // current_cntx->findVariable(&sav_t.back(), false);
            //PARSER_LOG("looking for %s",sav_t.backptr()->getText());
                findVariable(current_cntx,sav_t.backptr(), false);
                if(search_result==NULL)
                RETURN_ERROR(impossibletofindvariabledeclaration)
                sav_t.pop_back();
                
                break;
            }
        }
        
        if (!found)
            RETURN_ERROR(impossibletofindvariabledeclaration)
        
    }
//PARSER_LOG("hjhh %s",current()->getText());
    // token *vartoken = current();
    // auto var =
    // current_node = current_node->addChild(
    createNodeVariable(current(), isStore);
// PARSER_LOG("hjhh %s",current()->getText());
    next();
    if (Match(TokenOpenBracket))
    {
        // on parse
        next();

        // NodeToken nd;
        NodeToken nd = NodeToken(changeTypeNode);
        nd._nodetype = changeTypeNode;
        nd.type = TokenKeywordVarType;
        nd._vartype = __none__;
        current_node = current_node->addChild(nd);
        change_type.push_back(current_node);
        parseExpr();
        if (Error.error)
        {
            next();
            return;
        }
        current_node = current_node->parent;
        change_type.pop_back();
        if (Match(TokenCloseBracket))
        {

            // Error.error = 0;
            // current_node = current_node->parent;

            next();
            // return;
        }
        else if (Match(TokenComma))
        {

            while (Match(TokenComma))
            {

                next();

                // nb_argument++;
                // NodeToken nd;
                NodeToken nd = NodeToken(changeTypeNode);
                nd._nodetype = changeTypeNode;
                nd.type = TokenKeywordVarType;
                nd._vartype = __none__;
                current_node = current_node->addChild(nd);
                change_type.push_back(current_node);
                parseExpr();
                if (Error.error)
                {
                    return;
                }
                current_node = current_node->parent;
                change_type.pop_back();
                // arg.addChild(res._nd);
            }
            if (Match(TokenCloseBracket))
            {

                next();
                vect<char *> tile;
                int _s = current_node->children_size();
                int nb = 0;
                char *sd = current_node->getTargetText();

                if (strncmp(sd, (char *)"@", 1) == 0)
                {
                    str_split(&tile, sd, (char *)" ");
                    sscanf(tile.get(0), _arobase_d, &nb);
                }
                tile.empty();
                tile.clear();
                if (nb < _s)
                    RETURN_ERROR(toomanyarguments)
            }
            else
                RETURN_ERROR(expectingClosingBracketorcomma)
        }
        else
            RETURN_ERROR(expectingClosingBracketorcomma)
    }
    // else
    // {

    if (Match(TokenMember) && Match(TokenIdentifier, 1) && !Match(TokenOpenParenthesis, 2))
    {
        next();
        int i = 0;
        varType *v = NULL;

        if (current_node->_vartype == __CRGB__ or current_node->_vartype == __CRGBW__)
        {
            i = findMember(current_node->getVarType(), current()->getText());
            v = current_node->getVarTypeObj();
            RETURN_ERROR(memberdoesnotexist)
        }
        else
        {
            i = findMember(current_node->_vartype, current()->getText());
            v = _userDefinedTypes.getptr(current_node->_vartype); //  &_userDefinedTypes[current_node->_vartype];

            if (i < 0)
                RETURN_ERROR(memberdoesnotexist)
        }
        // next();
        // current_node->addTargetText(string(current()->getText()));
        current_node->type = TokenUserDefinedVariableMember;
        current_node->_vartype = v->types[i];
        if (!_asPointer)
            current_node->stack_pos = current_node->stack_pos + v->starts[i];
        else
            current_node->stack_pos = current_node->stack_pos + 1000 * v->starts[i];
        if (current_node->isPointer)
        {
            // v1 leaves _total_size untouched here (its equivalent line,
            // ESPLiveScript.h:584, is dead/commented-out code) -- it stays
            // whatever the preceding array-index step already set it to
            // (the struct's real per-element byte size). The 1000x-encoded
            // form this used to compute instead collided with the *other*,
            // legitimate use of that same "1000 * X + Y" packing scheme
            // (stack_pos's offset encoding, decoded via "- (n/1000)*1000"
            // elsewhere in this file and in visitnode.cpp) but had no
            // corresponding decode step of its own: visitnode.cpp's
            // indexed-member codegen (~line 678/2246) uses _total_size
            // directly as a `movi`/`mull` stride, so for e.g. a 7-field
            // (28-byte) struct this produced 1000*28+4=28004 -- not only
            // wrong (multiplying the index by a garbage stride computes a
            // wrong address, silent memory corruption at runtime) but also
            // outside `movi`'s 12-bit (-2048..2047) immediate range, which
            // is what actually surfaced this as an assembler error.
        }
        else
        {
            current_node->_total_size = v->sizes[i];
        }
        next();
    }
    else if (Match(TokenMember, 0) && Match(TokenIdentifier, 1) && Match(TokenOpenParenthesis, 2))
    {

        findVariable(current_cntx, current_node->getText(), false);
        if (search_result == NULL)
            RETURN_ERROR(impossibletofindvariabledeclaration)
        // next();
        next();

        current()->addText(string_format(_s_dot_s_, search_result->getVarTypeObj()->varName, current()->getText()));
        // nd = *search_result; //30/12
        NodeToken nd = NodeToken(*search_result);
        // nd.copyChildren(search_result);
        if (search_result->_nodetype == defGlobalVariableNode)
            nd._nodetype = globalVariableNode;
        else if (search_result->_nodetype == defLocalVariableNodeAsRegister)
        {
            nd._nodetype = localVariableNodeAsRegister;
            nd.target = search_result->target;
        }
        else
            nd._nodetype = localVariableNode;

        nd.type = TokenUserDefinedVariableMemberFunction;
        nd.isPointer = true;
        nd._total_size = search_result->getVarTypeObj()->total_size;
        // nd is built fresh from search_result (the bare declaration --
        // e.g. `arr`'s declaration, always element 0), which drops
        // whatever index expression current_node already accumulated
        // while parsing `arr[i]` above (added as current_node's own
        // child by the `[` handling ~60 lines up). Without this, every
        // `arr[i].method()` call's self-pointer silently resolves to
        // arr[0] regardless of i -- both _visitglobalVariableNode's and
        // _visitlocalVariableNode's `isPointer && children_size() > 0`
        // branches (visitnode.cpp) already have the correct index-scaled
        // addressing codegen for TokenUserDefinedVariableMemberFunction
        // specifically, but neither is ever reached without this, since
        // nd.children_size() is otherwise always 0. This exact loop used
        // to be here (see git history), just commented out -- restored,
        // minus its neighboring `current_node->parent->children->
        // pop_back(); current_node = par;` lines, which would double up
        // with the (already-active) UnknownNode/_node_token_stack
        // handling directly below that already detaches current_node
        // from further processing.
        for (int i = 0; i < current_node->children_size(); i++)
        {
            nd.addChild(*current_node->getChildPtr(i));
        }
        current_node->_nodetype = UnknownNode;
        // NodeToken *par = current_node;
        _node_token_stack.push_back(current_node);
        current_node = current_node->parent;

        nodeTokenList.push_back(nd);
        isStructFunction = true;
        // printf("her\n");
        parseFunctionCall();

        if (Error.error)
        {
            return;
        }
        // current_node->getChildAtPos(current_node->children_size() - 1)->getChildAtPos(0)->getChildAtPos(0)->copyChildren(_node_token_stack.back());
        // to put again 16/03
       // _node_token_stack.backptr()->clear();
        _node_token_stack.pop_back();
        isStructFunction = false;
        Error.error = 0;
        return;
    }
    Error.error = 0;
    tmp_sav = current_node;
    current_node = current_node->parent;

    return;

    //}
}

void Parser::parseComparaison()
{

#ifdef PARSER_DEBUG
    updateMem();
#endif

    Error.error = 0;

    current_node = current_node->addChild(NodeToken(current(), comparatorNode));

    current_node->target = targetList.pop();
    parseExprAndOr();
    //  parseExprConditionnal();
    if (Error.error)
    {
        return;
    }

    next();
    Error.error = 0;
    current_node = current_node->parent;

    return;
}

NodeToken createNodeLocalVariableForCreation(NodeToken var, NodeToken nd)

{
    switch (var._nodetype)
    {
    case defGlobalVariableNode:
    {
        NodeToken v = NodeToken(var, defGlobalVariableNode);
        copyPrty(&nd, &v);
        return v;
    }
    break;
    case defLocalVariableNode:
    {

        NodeToken v = NodeToken(var, defLocalVariableNode);
        if (_is_variable_as_register.get())
        {
            v._nodetype = defLocalVariableNodeAsRegister;
            v.target = _for_depth_reg.get();
            _for_depth_reg.increase();
            _is_variable_as_register.set(false);
        }
        copyPrty(&nd, &v);
        return v;
    }
    break;
        break;
    default:
    {

        copyPrty(&nd, &var);
        NodeToken v = NodeToken(var, defLocalVariableNode);
        if (_is_variable_as_register.get())
        {
            v._nodetype = defLocalVariableNodeAsRegister;
            v.target = _for_depth_reg.get();
            _for_depth_reg.increase();
            _is_variable_as_register.set(false);
        }
        // copyPrty(&nd, &v);
        //  NodeDefLocalVariable v = NodeDefLocalVariable(var);

        return v;
    }
    break;
    }
}

void createNodeVariable(Token *_var, bool isStore)

{
    // //printf("***************create cariavbla %d %s\n", isStore ,nd->_token->text.c_str());
    // NodeToken var = NodeToken(_var);
    // //printf("%s %d\n",_var->text.c_str(),_asPointer);

    NodeToken v = NodeToken(_var);
    switch (search_result->getNodeTokenType())
    {
    case extGlobalVariableNode:
    {
        if (isStore)
        {
            // NodeStoreExtGlobalVariable v = NodeStoreExtGlobalVariable(var);
            // NodeStoreExtGlobalVariable v = NodeStoreExtGlobalVariable(_var);
            v._nodetype = (int)storeExtGlocalVariableNode;
            // current_node->asPointer=asPointer;
            //  return;
        }
        else
        {
            // NodeExtGlobalVariable v = NodeExtGlobalVariable(_var);
            v._nodetype = (int)extGlobalVariableNode;

            // current_node->asPointer=asPointer;
            // return;
        }
    }
    break;
    case defExtGlobalVariableNode:
    {
        if (isStore)
        {
            // NodeStoreExtGlobalVariable v = NodeStoreExtGlobalVariable(_var);
            v._nodetype = (int)storeExtGlocalVariableNode;
            // current_node->asPointer=asPointer;
            // return;
        }
        else
        {
            // NodeExtGlobalVariable v = NodeExtGlobalVariable(_var);
            v._nodetype = (int)extGlobalVariableNode;
            // current_node->asPointer=asPointer;
            // return;
        }
    }
    break;
    case defLocalVariableNodeAsRegister:
    {
        if (isStore == true)
        {
            // NodeStoreLocalVariable v = NodeStoreLocalVariable(_var);
            v._nodetype = storeLocalVariableNodeAsRegister;
            v.target = search_result->target;
            //  asPointer=false;
            //  return;
        }
        else
        {
            // NodeLocalVariable v = NodeLocalVariable(_var);
            v._nodetype = (int)localVariableNodeAsRegister;
            v.target = search_result->target;
            // current_node->asPointer=asPointer;
            //  return;
        }
    }
    break;
    case localVariableNodeAsRegister:
    {
        if (isStore == true)
        {
            // NodeStoreLocalVariable v = NodeStoreLocalVariable(_var);
            v._nodetype = storeLocalVariableNodeAsRegister;
            v.target = search_result->target;
            // current_node->asPointer=asPointer;
            //  asPointer=false;
            //  return;
        }
        else
        {
            // NodeLocalVariable v = NodeLocalVariable(_var);
            v._nodetype = (int)localVariableNodeAsRegister;
            v.target = search_result->target;
            // current_node->asPointer=asPointer;
            //  return;
        }
    }
    break;
    case defLocalVariableNode:
    {
        if (isStore == true)
        {
            // NodeStoreLocalVariable v = NodeStoreLocalVariable(_var);
            v._nodetype = (int)storeLocalVariableNode;
            // current_node->asPointer=asPointer;
            //  asPointer=false;
            //  return;
        }
        else
        {
            // NodeLocalVariable v = NodeLocalVariable(_var);
            v._nodetype = localVariableNode;
            // current_node->asPointer=asPointer;
            //  return;
        }
    }
    break;
    case localVariableNode:
    {
        if (isStore == true)
        {
            //  NodeStoreLocalVariable v = NodeStoreLocalVariable(_var);

            v._nodetype = storeLocalVariableNode;
            // current_node->asPointer=asPointer;
            //            return;
        }
        else
        {
            // NodeLocalVariable v = NodeLocalVariable(_var);
            v._nodetype = (int)localVariableNode;
            // current_node->asPointer=asPointer;
            // §            return;
        }
    }
    break;
    case defGlobalVariableNode:
    {

        if (isStore)
        {
            // NodeStoreGlobalVariable v = NodeStoreGlobalVariable(_var);
            v._nodetype = (int)storeGlobalVariableNode;
            // v.addTargetText(search_result->getTargetText());
            // current_node->asPointer=asPointer;
            //  return;
        }
        else
        {
            // NodeGlobalVariable v = NodeGlobalVariable(_var);
            v._nodetype = (int)globalVariableNode;
            // current_node->asPointer=asPointer;
            //            return;
        }
    }
    break;
    case globalVariableNode:
    {
        if (isStore)
        {
            // NodeStoreGlobalVariable v = NodeStoreGlobalVariable(_var);
            v._nodetype = (int)storeGlobalVariableNode;
            // v.addTargetText(search_result->getTargetText());
            //  current_node->asPointer=asPointer;
            //   return;
        }
        else
        {
            // NodeGlobalVariable v = NodeGlobalVariable(_var);
            v._nodetype = (int)globalVariableNode;
            // current_node->asPointer=asPointer;
            //            return;
        }
    }
    break;
    default:
    {
        if (isStore == true)
        {
            //  NodeStoreLocalVariable v = NodeStoreLocalVariable(_var);
            v._nodetype = storeLocalVariableNode;
            // current_node->asPointer=asPointer;
            //            return;
        }
        else
        {
            // NodeLocalVariable v = NodeLocalVariable(_var);
            v._nodetype = localVariableNode;
            // current_node->asPointer=asPointer;
            // §            return;
        }
    }
    break;
    }
    if (search_result->getTargetText()[0] == '@')
    {

        v.target = search_result->target;
    }
    copyPrty(search_result, &v);
    v.asPointer = _asPointer;
    if (search_result->asPointer)
    {
        v.asPointer = true;
    }
    if (isPointer)
        v.isPointer = isPointer;
    current_node = current_node->addChild(v);
}

varTypeEnum findfloat(NodeToken *nd)
{
    if (nd->_vartype == __float__)
    {
        return __float__;
    }
    else if (nd->_nodetype == globalVariableNode)
    {
        return __none__;
    }
    else
    {
        if (nd->children_size() > 0)
        {
            for (int i = 0; i < nd->children_size(); i++)
            {
                NodeToken *child = nd->getChildPtr(i);
                if (findfloat(child) == __float__)
                {
                    return __float__;
                }
            }
            return __none__;
        }
        else
        {
            return __none__;
        }
    }
}
varTypeEnum finduint32_t(NodeToken *nd)
{
    if (nd->_vartype == __uint32_t__)
    {
        return __uint32_t__;
    }
    else
    {
        if (nd->children_size() > 0)
        {
            for (int i = 0; i < nd->children_size(); i++)
            {
                NodeToken *child = nd->getChildPtr(i);
                if (findfloat(child) == __uint32_t__)
                {
                    return __uint32_t__;
                }
            }
            return __none__;
        }
        else
        {
            return __none__;
        }
    }
}

void Parser::parseArguments()
{
#ifdef PARSER_DEBUG
    updateMem();
#endif
    // resParse result;
    // signature="(";
    sigs.push_back(all_text.addText(_openparenthesis_));
    nb_argument = 0;
    nb_args.push_back(0);
    // NodeInputArguments arg;
    current_node = current_node->addChild(NodeToken(inputArgumentsNode));
    if (isStructFunction)
    {

        if (struct_name == NULL)
        {
            current_node->addChildClear(nodeTokenList.pop_back());
            nb_args.pop_back();
            nb_args.push_back(1);
        }
        else
        {
            NodeToken nd = NodeToken();
            nd.addTargetText(_pointer_);
            nd.isPointer = true;
            nd.type = TokenUserDefinedVariable;
            nd._nodetype = localVariableNode;
            nd.stack_pos = _STACK_SIZE;
            current_node->addChild(nd);
            nb_args.pop_back();
            nb_args.push_back(1);
        }
    }
    if (Match(TokenCloseParenthesis))
    {
        // resParse result;
        Error.error = 0;
        // result._nd = arg;
        // printf("on retourne with argh ide\n");
        current_node = current_node->parent;
        char *_signature = string_format(_s_s_, all_text.getText(sigs.back()), _closeparenthesis_);
        // string _signature = sigs.back() + ")";
        sigs.pop_back();
        sigs.push_back(all_text.addText(_signature));
        next();
        return;
    }
    nb_args.pop_back();
    if (isStructFunction)
    {
        nb_args.push_back(2);
    }
    else
    {
        nb_args.push_back(1);
    }
    // nb_argument = 1;
    // Serial.printf("lkklqdqsdksmdkqsd\r\n");
    // NodeToken nd;
    NodeToken nd = NodeToken(changeTypeNode);
    nd._nodetype = changeTypeNode;
    nd.type = TokenKeywordVarType;
    nd._vartype = __none__;
    current_node = current_node->addChild(nd);
    change_type.push_back(current_node);
    // printf("lkklqdqsdksm excut dkqsd\r\n");
    parseExpr();
    // printf("lkklqdqsdksm excut dkqsd\r\n");
    if (Error.error)
    {
        return;
    }
    // string _signature = sigs.back() + current_node->getVarType()->varName;
    char *_signature = string_format(_s_s_, all_text.getText(sigs.back()), current_node->getVarTypeObj()->varName);
    sigs.pop_back();
    sigs.push_back(all_text.addText(_signature));
    if (current_node->isPointer)
    {

        // string _signature = sigs.back() + "*";
        char *_signature = string_format(_s_s_, all_text.getText(sigs.back()), _star_);
        sigs.pop_back();
        sigs.push_back(all_text.addText(_signature));
    }
    current_node = current_node->parent;
    change_type.pop_back();
    // arg.addChild(res._nd);
    while (Match(TokenComma))
    {
        next();
        __sav_arg = nb_args.back();
        nb_args.pop_back();
        nb_args.push_back(__sav_arg + 1);
        // nb_argument++;
        // NodeToken nd;
        NodeToken nd = NodeToken(changeTypeNode);
        nd._nodetype = changeTypeNode;
        nd.type = TokenKeywordVarType;
        nd._vartype = __none__;
        current_node = current_node->addChild(nd);
        change_type.push_back(current_node);
        parseExpr();
        if (Error.error)
        {
            return;
        }

        // string _signature = sigs.back() + "|" + current_node->getVarType()->varName;
        char *_signature = string_format(_s_s_s_, all_text.getText(sigs.back()), _separ_, current_node->getVarTypeObj()->varName);
        sigs.pop_back();
        sigs.push_back(all_text.addText(_signature));
        if (current_node->isPointer)
        {

            char *_signature = string_format(_s_s_, all_text.getText(sigs.back()), _star_);
            sigs.pop_back();
            sigs.push_back(all_text.addText(_signature));
        }
        current_node = current_node->parent;
        change_type.pop_back();
        // arg.addChild(res._nd);
    }
    if (!Match(TokenCloseParenthesis))
        RETURN_ERROR(expectingClosingparenthesis)

    next();
    Error.error = 0;
    // result._nd = arg;
    _signature = string_format(_s_s_, all_text.getText(sigs.back()), _closeparenthesis_);
    // string _signature = sigs.back() + ")";
    sigs.pop_back();
    sigs.push_back(all_text.addText(_signature));
    current_node = current_node->parent;
    return;
}

void Parser::clean()
{
   // printf("1\n");
    //all_text.clear();
   // printf("2\n");
    program.clear();
   // printf("3\n");
    main_context.clear();

    function_cntx.clear();
   // printf("4\n");
    functions.clear();
   // printf("5\n");
    targetList.clear();
  // printf("6\n");
    sigs.clear();
   // printf("7\n");
    nb_sav_args.clear();
   // printf("8\n");
    nb_args.clear();
   // printf("9\n");
    sav_t.clear();
   // printf("10\n");
   for(int i=0;i<nodeTokenList.size();i++)
   {
    nodeTokenList[i].clear();
   }
    nodeTokenList.clear();
    ext_function_cntx.clear();
    function_cntx.clear();
   // printf("11\n");
    sav_token.clear();
   //printf("12\n");
    change_type.clear();
   //printf("13\n");
    __allTokens.clear();
   // printf("14\n");
    userDefinedVarTypeNames.clear();
   // printf("15\n");
    _userDefinedTypes.clear();
   // printf("16\n");
   define_list.clear();
    //printf("13\n");
    _for_depth_reg.clear();
   // printf("17\n");
 _is_variable_as_register.clear();
 //printf("18\n");
 for(int i=0;i<_node_token_stack.size();i++)
 {
    _node_token_stack[i].clear();
 }
 _node_token_stack.clear();
 //printf("19\n");
 all_text.clear();
  //printf("20\n");
  
}