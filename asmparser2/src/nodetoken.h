#pragma once
#ifndef __NodeToken__
#define __NodeToken__
#include "parser_define.h"
#include "assert.h"
#include "stackfunctions.h"
#include "string_functions.h"
#include "parser_enum.h"
#include "vect.h"
#include "tokenize.h"


enum nodeType

{
	typeNode,
	numberNode,
	binOpNode,
	unitaryOpNode,
	operatorNode,
	globalVariableNode,
	localVariableNode,
	localVariableNodeAsRegister,
	blockStatementNode,
	defFunctionNode,
	statementNode,
	programNode,
	assignementNode,
	comparatorNode,
	callFunctionNode,
	forNode,
	argumentNode,
	extGlobalVariableNode,
	extDefFunctionNode,
	extCallFunctionNode,
	returnArgumentNode,
	variableDeclarationNode,
	defExtFunctionNode,
	inputArgumentsNode,
	defExtGlobalVariableNode,
	defGlobalVariableNode,
	defLocalVariableNode,
	defLocalVariableNodeAsRegister,
	storeLocalVariableNode,
	storeLocalVariableNodeAsRegister,
	storeGlobalVariableNode,
	storeExtGlocalVariableNode,
	ifNode,
	elseNode,
	whileNode,
	returnNode,
	defAsmFunctionNode,
	stringNode,
	changeTypeNode,
	importNode,
	continueNode,
	breakNode,
	testNode,
	ternaryIfNode,
	callConstructorNode,
	defInputArgumentsNode,
	declarationFunctionNode,
	UnknownNode

};

class NodeToken
{
public:
	NodeToken();
	NodeToken(nodeType tt);
	NodeToken(Token *t, nodeType tt);
	NodeToken(Token *t);
	NodeToken(Token t, nodeType tt);
	NodeToken(NodeToken *nd);
	NodeToken(char * _target, nodeType tt);
	NodeToken(NodeToken nd, nodeType tt,uint16_t _target);

	void clear();

	NodeToken *getChildPtr(int i);

	NodeToken *children_backptr();
	NodeToken children_back();

	NodeToken *children_frontptr();
	NodeToken children_front();
	NodeToken *addChild(NodeToken nd);
		NodeToken *addChild(NodeToken *nd);
	NodeToken *addChildFront(NodeToken nd);
	NodeToken *operator[](int i);
	NodeToken(NodeToken nd, nodeType tt);
	NodeToken(NodeToken *nd, nodeType tt);
	varType *getVarTypeObj();
	NodeToken children_pop();
	varTypeEnum getVarType();
	void setText(char * str);
	void addTargetText(char *t);
void addTargetText(const char *t);
	void erase(NodeToken *asset);
 nodeType getNodeTokenType();
	void erase(int k);
	int children_size();

	char *getText();
	char *getTargetText();
	NodeToken *children= nullptr;
	NodeToken *parent= nullptr;
	uint16_t _total_size = 1;
	uint16_t target = EOF_TEXTARRAY;
	uint16_t textref = EOF_TEXTARRAY;
	uint16_t stack_pos = 0;
	uint16_t _c_size;
	bool isPointer = 0;
	bool asPointer = 0;
	uint8_t _nodetype = 0;

	uint8_t type = 0;
	uint8_t _vartype = 0;
};
extern const char *nodeTypeNames[];

extern NodeToken *search_result;
extern int search_result_index;
extern Stack<int> _for_depth_reg;
extern Stack<bool> _is_variable_as_register;
extern NodeToken *lasttype;
extern vect<NodeToken *> sav_token;
extern vect<NodeToken *> change_type;
extern int stack_size;
extern int point_regnum;
bool findCandidate(NodeToken *nd, char *str);
void findFunction(NodeToken *nd, char * st);
void findVariable(NodeToken *nd, char *, bool forCreation);
void findVariable(NodeToken *nd, Token *t, bool forCreation);
void prettyPrint(NodeToken *nd, int iden);
void copyPrty(NodeToken *from, NodeToken *to);
void testChange(vect<NodeToken *> *is, NodeToken *from,NodeToken *to);
uint16_t stringToInt(char *str);
#endif