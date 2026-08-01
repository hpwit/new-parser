 #pragma once
 #ifndef __VISITNODE_H__
class NodeToken;

void _visittypeNode(NodeToken *nd);
void _visitnumberNode(NodeToken *nd);
void _visitbinOpNode(NodeToken *nd);
void _visitunitaryOpNode(NodeToken *nd);
void _visitoperatorNode(NodeToken *nd);
void _visitglobalVariableNode(NodeToken *nd);
void _visitlocalVariableNode(NodeToken *nd);
void _visitlocalVariableNodeAsRegister(NodeToken *nd);
void _visitblockStatementNode(NodeToken *nd);
void _visitdefFunctionNode(NodeToken *nd);
void _visitstatementNode(NodeToken *nd);
void _visitprogramNode(NodeToken *nd);
void _visitassignementNode(NodeToken *nd);
void _visitcomparatorNode(NodeToken *nd);
void _visitcallFunctionNode(NodeToken *nd);
void _visitforNode(NodeToken *nd);
void _visitargumentNode(NodeToken *nd);
void _visitextGlobalVariableNode(NodeToken *nd);
// void _visitextDefFunctionNode(NodeToken *nd);
void _visitextCallFunctionNode(NodeToken *nd);
void _visitreturnArgumentNode(NodeToken *nd);
void _visitvariableDeclarationNode(NodeToken *nd);
void _visitdefExtFunctionNode(NodeToken *nd);
void _visitinputArgumentsNode(NodeToken *nd);
void _visitdefInputArgumentsNode(NodeToken *nd);
void _visitdefExtGlobalVariableNode(NodeToken *nd);
void _visitdefGlobalVariableNode(NodeToken *nd);
void _visitdefLocalVariableNode(NodeToken *nd);
void _visitstoreLocalVariableNode(NodeToken *nd);
void _visitstoreLocalVariableNodeAsRegister(NodeToken *nd);
void _visitstoreGlobalVariableNode(NodeToken *nd);
void _visitstoreExtGlocalVariableNode(NodeToken *nd);
void _visitifNode(NodeToken *nd);
void _visitelseNode(NodeToken *nd);
void _visitwhileNode(NodeToken *nd);
void _visitreturnNode(NodeToken *nd);
void _visitdefAsmFunctionNode(NodeToken *nd);
void _visitstringNode(NodeToken *nd);
void _visitchangeTypeNode(NodeToken *nd);
void _visitimportNode(NodeToken *nd);
void _visitcontinueNode(NodeToken *nd);
void _visitbreakNode(NodeToken *nd);
void _visittestNode(NodeToken *nd);
void _visitternaryIfNode(NodeToken *nd);
void _visitcallConstructorNode(NodeToken *nd);
void _visitjsonBindingNode(NodeToken *nd);
void _visitUnknownNode(NodeToken *nd);

void _visitCallFunctionTemplate(NodeToken *nd, int regbase, bool isExtCall);
void translateType(varTypeEnum to, varTypeEnum from, int regnum);
char * _numToBytes(uint32_t __num);
extern Stack<int> register_numr ;
extern Stack<int> register_numl;
extern Text content,header,footer,*bufferText;
extern Stack<varTypeEnum> globalType;
extern int for_if_num2;
extern int local_var_num;
extern vect<int> _compare;
extern bool intest;
//extern bool safeMode;





 #endif