#include "nodetoken.h"
#include "assert.h"
#include "stackfunctions.h"
#include "string_functions.h"
#include "vect.h"
#include "parser_define.h"
#include "string_constants.h"
#include "visitnode.h"
#include "compiler_error.h"

// Declared (not just extern'd from parser.h, which nodetoken.h is included
// by and can't itself include back) so copyPrty() below can report a
// clean parse error instead of segfaulting -- see its comment.
extern error_message_struct Error;

NodeToken *search_result;
int search_result_index;
Stack<int> _for_depth_reg;
Stack<bool> _is_variable_as_register;
NodeToken *lasttype;
NodeToken *current_node,*current_cntx;
NodeToken function_cntx;
NodeToken *sav_current_node;
vect<NodeToken *> sav_token;
vect<NodeToken *> change_type;
NodeToken *tmp_sav;

int stack_size;
//int point_regnum;
const char *nodeTypeNames[] =
	{

#ifdef __TEST_DEBUG
		"typeNode",
		"numberNode",
		"binOpNode",
		"unitaryOpNode",
		"operatorNode",
		"globalVariableNode",
		"localVariableNode",
		"localVariableNodeAsRegister",
		"blockStatementNode",
		"defFunctionNode",
		"statementNode",
		"programNode",
		"assignementNode",
		"comparatorNode",
		"callFunctionNode",
		"forNode",
		"argumentNode",
		"extGlobalVariableNode",
		"extDefFunctionNode",
		"extCallFunctionNode",
		"returnArgumentNode",
		"variableDeclarationNode",
		"defExtFunctionNode",
		"inputArgumentsNode",
		"defExtGlobalVariableNode",
		"defGlobalVariableNode",
		"defLocalVariableNode",
		"defLocalVariableNodeAsRegister",
		"storeLocalVariableNode",
		"storeLocalVariableNodeAsRegister",
		"storeGlobalVariableNode",
		"storeExtGlocalVariableNode",
		"ifNode",
		"elseNode",
		"whileNode",
		"returnNode",
		"defAsmFunctionNode",
		"stringNode",
		"changeTypeNode",
		"importNode",
		"continueNode",
		"breakNode",
		"testNode",
		"ternaryIfNode",
		"callConstructorNode",
		"defInputArgumentsNode",
		"declarationFunctionNode",
		"jsonBindingNode",
		"UnknownNode"

#endif

};
NodeToken::~NodeToken()
{
	// clear();
}
NodeToken::NodeToken()
{
	children = NULL;
	_c_size = 0;

	parent = NULL;

	_total_size = 1;
	target = EOF_TEXTARRAY;
	textref = EOF_TEXTARRAY;
	stack_pos = 0;
	isPointer = false;
	asPointer = false;
	_nodetype = (int)UnknownNode;

	type = (int)TokenUnknown;
	_vartype = EOF_VARTYPE;
}
NodeToken::NodeToken(nodeType tt)
{
	_nodetype = tt;
	// children = NULL;
	type = (int)TokenUnknown;
	target = EOF_TEXTARRAY;
	textref = EOF_TEXTARRAY;
	children = NULL;
	_vartype = EOF_VARTYPE;
	_c_size = 0;
}
NodeToken::NodeToken(NodeToken nd, tokenType tt)
{
	type = tt;
	textref = nd.textref;
	_vartype = nd._vartype;
	isPointer = nd.isPointer;
	_total_size = nd._total_size;
	_nodetype = nd._nodetype;
	stack_pos = nd.stack_pos;
	target = nd.target;
	children = nd.children;
	_c_size = nd._c_size;
}
NodeToken::NodeToken(Token *t, nodeType tt)
{
	type = t->type;
	textref = t->textref;
	_vartype = t->_vartype;
	target = EOF_TEXTARRAY;
	_nodetype = tt;
	children = NULL;
	_c_size = 0;
}
NodeToken::NodeToken(Token *t)
{
	type = t->type;
	textref = t->textref;
	_vartype = t->_vartype;
	target = EOF_TEXTARRAY;
	children = NULL;
	_c_size = 0;
}
NodeToken::NodeToken(NodeToken *nd)
{

	type = nd->type;
	textref = nd->textref;
	_vartype = nd->_vartype;
	isPointer = nd->isPointer;
	_total_size = nd->_total_size;
	_nodetype = nd->_nodetype;
	stack_pos = nd->stack_pos;
	target = nd->target;
	children = nd->children;
	_c_size = nd->_c_size;
}
NodeToken::NodeToken(Token t, nodeType tt)
{
	type = t.type;
	textref = t.textref;
	target = EOF_TEXTARRAY;
	_vartype = t._vartype;
	_nodetype = tt;
	children = NULL;
	_c_size = 0;
}
NodeToken::NodeToken(NodeToken nd, nodeType tt, uint16_t _target)
{

	type = nd.type;
	textref = nd.textref;
	_vartype = nd._vartype;
	isPointer = nd.isPointer;
	_total_size = nd._total_size;
	_nodetype = tt;
	stack_pos = nd.stack_pos;
	target = _target;
	children = NULL;
	_c_size = 0;
}
void NodeToken::clear()
{
	// PARSER_LOG(" on efface :%s",all_text.getText(textref));
	if (children == NULL)
		return;

	for (int i = 0; i < _c_size; i++)
	{
		// Each child is its own heap allocation now (see nodetoken.h's
		// comment on `children`) -- clear its own subtree first, then
		// free the child object itself. Previously freeing the single
		// `children` block freed every child's storage at once, since
		// they were all inline values within it.
		children[i]->clear();
		free(children[i]);
	}
	free(children);
	children = NULL;
	_c_size = 0;
}

NodeToken *NodeToken::getChildPtr(int i)
{
	assert(_c_size > 0 and i >= 0 and i < _c_size);
	return children[i];
}

NodeToken *NodeToken::children_backptr()
{
	assert(_c_size > 0);
	return children[_c_size - 1];
}
NodeToken NodeToken::children_back()
{
	assert(_c_size > 0);
	return *children[_c_size - 1];
}

NodeToken *NodeToken::children_frontptr()
{
	assert(_c_size > 0);
	return children[0];
}
nodeType NodeToken::getNodeTokenType()
{
	return (nodeType)_nodetype;
}
NodeToken NodeToken::children_front()
{
	assert(_c_size > 0);
	return *children[0];
}
NodeToken NodeToken::children_pop()
{
	assert(children != NULL);
	NodeToken tmp = NodeToken(children[_c_size - 1]);
	// tmp has already shallow-copied the popped child's fields, including
	// its own `children` sub-array pointer (ownership of that subtree
	// transfers to tmp, matching the original behavior) -- so only the
	// now-unreferenced struct itself needs freeing here, not its subtree.
	free(children[_c_size - 1]);
	if (_c_size == 1)
	{
		free(children);
		children = NULL;
		_c_size = 0;
	}
	else
	{
		NodeToken **tmp2 = (NodeToken **)p_realloc(children, (_c_size - 1) * sizeof(NodeToken *));
		children = tmp2;
		_c_size--;
	}

	return tmp;
}
int NodeToken::children_size()
{
	if (children == NULL)
		return 0;
	else
		return _c_size;
}
NodeToken *NodeToken::addChildFront(NodeToken nd)
{

	nd.parent = this;
	if (children == NULL)
	{
		children = (NodeToken **)malloc(sizeof(NodeToken *));
	}
	else
	{
		NodeToken **tmp = (NodeToken **)p_realloc((void *)children, (_c_size + 1) * sizeof(NodeToken *));
		children = tmp;
	}
	if (_c_size > 0)
		memmove(children + 1, children, _c_size * sizeof(NodeToken *));
	NodeToken *new_object = (NodeToken *)malloc(sizeof(NodeToken));
	memcpy(new_object, &nd, sizeof(NodeToken));
	children[0] = new_object;
	new_object->children = NULL;
	new_object->_c_size = 0;

	for (int i = 0; i < nd._c_size; i++)
	{
		new_object->addChild(nd.getChildPtr(i));
	}

	_c_size++;
	return new_object;
}
NodeToken *NodeToken::addChild(NodeToken nd)
{
	if (_c_size == 0)
		children = NULL;
	// nd.parent = this;
	if (children == NULL)
	{
		children = (NodeToken **)malloc(sizeof(NodeToken *));
	}
	else
	{
		NodeToken **tmp = (NodeToken **)p_realloc(children, (_c_size + 1) * sizeof(NodeToken *));
		children = tmp;
	}
	NodeToken *new_object = (NodeToken *)malloc(sizeof(NodeToken));
	memcpy(new_object, &nd, sizeof(NodeToken));
	children[_c_size] = new_object;
	new_object->parent = this;
	new_object->children = NULL;
	new_object->_c_size = 0;

	for (int i = 0; i < nd._c_size; i++)
	{
		new_object->addChild(nd.getChildPtr(i));
	}

	_c_size++;

	return new_object;
}
NodeToken *NodeToken::addChild(NodeToken *nd)
{
	// nd.parent = this;
	NodeToken **tmp;
	if (children == NULL)
	{
		tmp = (NodeToken **)malloc(sizeof(NodeToken *));
	}
	else
	{
		tmp = (NodeToken **)p_realloc(children, (_c_size + 1) * sizeof(NodeToken *));
	}
	children = tmp;
	NodeToken *new_object = (NodeToken *)malloc(sizeof(NodeToken));
	memcpy(new_object, nd, sizeof(NodeToken));
	children[_c_size] = new_object;
	new_object->children = NULL;
	new_object->parent = this;
	new_object->_c_size = 0;

	for (int i = 0; i < nd->_c_size; i++)
	{
		new_object->addChild(nd->getChildPtr(i));
	}

	_c_size++;

	return new_object;
}
NodeToken *NodeToken::addChildClear(NodeToken nd)
{
	NodeToken *tmp = addChild(nd);
	nd.clear();
	return tmp;
}
NodeToken *NodeToken::operator[](int i)
{
	return getChildPtr(i);
}

void NodeToken::erase(NodeToken *asset)
{
	// children is now an array of pointers to individually-allocated
	// nodes, so `asset`'s position has to be found by value rather than
	// by pointer subtraction (which also let a real bug hide here
	// before: the old byte-range assert and the memmove below it both
	// used `_c_size - diff` as the *remaining element count* -- one too
	// many, reading one element past the end of the array. Same class of
	// off-by-one already found and fixed in vect<T>::erase() earlier
	// this project -- this hand-rolled sibling implementation just never
	// got the same fix).
	int diff = -1;
	for (int i = 0; i < _c_size; i++)
	{
		if (children[i] == asset)
		{
			diff = i;
			break;
		}
	}
	assert(diff >= 0);
	asset->clear();
	free(asset);
	memmove(children + diff, children + diff + 1, sizeof(NodeToken *) * (_c_size - diff - 1));
	children = (NodeToken **)p_realloc(children, (_c_size - 1) * sizeof(NodeToken *));
	_c_size--;
}
void NodeToken::addTargetText(char *t)
{
	target = all_text.addText(t);
}
void NodeToken::addTargetText(const char *t)
{
	target = all_text.addText((const char *)t);
}

void NodeToken::setText(char *str)
{
	if (str == NULL)
		return;
	textref = all_text.addText(str);
}
void NodeToken::erase(int k)
{
	if (k >= 0 and k < _c_size)
	{
		erase(getChildPtr(k));
	}
}
char *NodeToken::getText()
{
	return all_text.getText(textref);
}
char *NodeToken::getTargetText()
{
	return all_text.getText(target);
}

varTypeEnum NodeToken::getVarType()
{

	return (varTypeEnum)_vartype;
}
varType *NodeToken::getVarTypeObj()
{

	if (_vartype == EOF_VARTYPE)

		return NULL;
	if (_vartype == (int)__unknown__)
		_vartype = (int)__none__;

	if (type == TokenUserDefinedVariable)
	{

		if (target != EOF_TEXTARRAY)
		{

			if (strncmp(getTargetText(), "@", 1) == 0)
			{
				return _userDefinedTypes.getptr(_vartype);
			}
			int i = findMember(_vartype, getTargetText());
			if (i > -1)
			{
				return &_varTypes[(_userDefinedTypes[_vartype]).types[i]];
			}
			else
			{
				printf("member %s not foudn in %s\n", getTargetText(), _userDefinedTypes[_vartype].varName);
				return NULL;
			}
		}

		return _userDefinedTypes.getptr(_vartype);
	}
	else

		return &_varTypes[_vartype];
}

NodeToken::NodeToken(NodeToken nd, nodeType tt)
{

	type = nd.type;
	textref = nd.textref;
	_vartype = nd._vartype;
	_nodetype = tt;
	isPointer = nd.isPointer;
	_total_size = nd._total_size;
	target = nd.target;
	children = NULL;
	_c_size = 0;

	if (tt == defLocalVariableNode)
	{
		isPointer = nd.isPointer;
		_total_size = nd._total_size;
		// visitNode=visitNodeDefLocalVariable;
		int delta = 0;
		if (nd.isPointer)
		{
			if (stack_size % 4 != 0)
				delta = 4 - stack_size % 4;
		}
		if (nd.getVarTypeObj()->_varType == __uint32_t__ || nd.getVarTypeObj()->_varType == __float__ || nd.getVarTypeObj()->_varType == __CRGB__ || nd.getVarTypeObj()->_varType == __int__ || nd.getVarTypeObj()->_varType == __userDefined__) //|| nd.getVarType()->_varType == __CRGBW__)
		{
			if (stack_size % 4 != 0)
			{
				if (nd.getVarTypeObj()->_varSize % 2 == 0)
					delta = nd.getVarTypeObj()->_varSize - stack_size % 4;
				else
					delta = nd.getVarTypeObj()->_varSize - stack_size % 4 + 1;
			}
		}
		else if (nd.getVarTypeObj()->_varType == __uint16_t__ || nd.getVarTypeObj()->_varType == __s_int__)
		{
			if (stack_size % 2 != 0)
			{
				delta = 1;
			}
		}
		stack_size += delta;
		nd.stack_pos = stack_size;
		if (nd.isPointer)
		{
			stack_size += 4;
		}
		else
		{
			stack_size += nd.getVarTypeObj()->_varSize;
		}

		stack_pos = nd.stack_pos;
	}
}
NodeToken::NodeToken(NodeToken *nd, nodeType tt)
{

	type = nd->type;
	textref = nd->textref;
	_vartype = nd->_vartype;
	_nodetype = tt;
	isPointer = nd->isPointer;
	asPointer = nd->asPointer;
	_total_size = nd->_total_size;
	stack_pos = nd->stack_pos;
	target = nd->target;
	children = NULL;
	_c_size = 0;
}

NodeToken::NodeToken(char *_target, nodeType tt)
{
	_nodetype = tt;
	addTargetText(_target);
	children = NULL;
	_c_size = 0;
}
void NodeToken::visitNode()
{
	switch ((nodeType)_nodetype)
	{
	case typeNode:
		_visittypeNode(this);
		break;
	case numberNode:
		_visitnumberNode(this);
		break;

	case binOpNode:
		_visitbinOpNode(this);
		break;

	case unitaryOpNode:
		_visitunitaryOpNode(this);
		break;

	case operatorNode:
		_visitoperatorNode(this);
		break;

	case globalVariableNode:
		_visitglobalVariableNode(this);
		break;

	case localVariableNode:
		_visitlocalVariableNode(this);
		break;

	case blockStatementNode:
		_visitblockStatementNode(this);
		break;

	case defFunctionNode:
		_visitdefFunctionNode(this);
		break;

	case statementNode:
		_visitstatementNode(this);
		break;

	case programNode:
		_visitprogramNode(this);
		break;

	case assignementNode:
		_visitassignementNode(this);
		break;

	case comparatorNode:
		_visitcomparatorNode(this);
		break;

	case callFunctionNode:
		_visitcallFunctionNode(this);
		break;

	case forNode:
		_visitforNode(this);
		break;

	case argumentNode:
		_visitargumentNode(this);
		break;

	case extGlobalVariableNode:
		_visitextGlobalVariableNode(this);
		break;
		/*
				case extDefFunctionNode:
					_visitextDefFunctionNode(this);
					break;
		*/
	case extCallFunctionNode:
		_visitextCallFunctionNode(this);
		break;

	case returnArgumentNode:
		_visitreturnArgumentNode(this);
		break;

	case variableDeclarationNode:
		_visitvariableDeclarationNode(this);
		break;

	case defExtFunctionNode:
		_visitdefExtFunctionNode(this);
		break;

	case inputArgumentsNode:
		_visitinputArgumentsNode(this);
		break;

	case defInputArgumentsNode:
		_visitdefInputArgumentsNode(this);
		break;

	case defExtGlobalVariableNode:
		_visitdefExtGlobalVariableNode(this);
		break;

	case defGlobalVariableNode:
		_visitdefGlobalVariableNode(this);
		break;

	case defLocalVariableNode:
		_visitdefLocalVariableNode(this);
		break;

	case storeLocalVariableNode:
		_visitstoreLocalVariableNode(this);
		break;

	case storeGlobalVariableNode:
		_visitstoreGlobalVariableNode(this);
		break;

	case storeExtGlocalVariableNode:
		_visitstoreExtGlocalVariableNode(this);
		break;

	case ifNode:
		_visitifNode(this);
		break;
	case localVariableNodeAsRegister:
		_visitlocalVariableNodeAsRegister(this);
		break;
	case storeLocalVariableNodeAsRegister:
		_visitstoreLocalVariableNodeAsRegister(this);
		break;
	case elseNode:
		_visitelseNode(this);
		break;

	case whileNode:
		_visitwhileNode(this);
		break;

	case returnNode:
		_visitreturnNode(this);
		break;

	case defAsmFunctionNode:
		_visitdefAsmFunctionNode(this);
		break;

	case stringNode:
		_visitstringNode(this);
		break;

	case changeTypeNode:
		_visitchangeTypeNode(this);
		break;

	case importNode:
		_visitimportNode(this);
		break;

	case continueNode:
		_visitcontinueNode(this);
		break;

	case breakNode:
		_visitbreakNode(this);
		break;
	case testNode:
		_visittestNode(this);
		break;

	case ternaryIfNode:
		_visitternaryIfNode(this);
		break;
	case callConstructorNode:
		_visitcallConstructorNode(this);
		break;
	case jsonBindingNode:
		_visitjsonBindingNode(this);
		break;
	case UnknownNode:
		_visitUnknownNode(this);
		break;
	default:
		break;
	}
}

int NodeToken::findMaxArgumentSize()
{
	int cur_size = 0;
	if (_nodetype == extCallFunctionNode or _nodetype == callFunctionNode)
	{
		cur_size = getChildPtr(1)->children_size();
		for (int i = 0; i < getChildPtr(2)->children_size(); i++)
		{
			int cmp = getChildPtr(2)->getChildPtr(i)->findMaxArgumentSize();
			if (cmp > cur_size)
				cur_size = cmp;
		}
	}
	else
	{
		if (children_size() > 0)
		{
			for (int i = 0; i < children_size(); i++)
			{
				int cmp = getChildPtr(i)->findMaxArgumentSize();
				if (cmp > cur_size)
					cur_size = cmp;
			}
		}
	}
	return cur_size;
}

#ifdef __TEST_DEBUG
void NodeToken::prettyPrint(int iden)

{
	// PARSER_LOG("_nodetype %d",nd->_nodetype);

	if (iden > 0)
	{
		for (int i = 0; i < iden - 1; i++)
		{
			printf("|  ");
		}

		printf("|--");
	}

	printf("%s\tisPointer:%d\tasPointer:%d\t", nodeTypeNames[_nodetype], isPointer, asPointer); //, tokenNames[nd._token.type].c_str());

	printf("text:%s\ttokenType:%s\t", getText(), tokenNames[type]);

	if (getVarType() != __unknown__)
	{
		/*
		if (type == (int)TokenUserDefinedVariable)
			printf("var name:%s\t total size:%d\tstackpos:%d\t", _userDefinedTypes[_vartype].varName, _total_size, stack_pos);
		else
			printf("var type:%s\ttotal size:%d\tstackpos:%d\t", varTypeEnumNames[_vartype], _total_size, stack_pos);
	*/
	}

	printf("target :%s", getTargetText());
	printf("\n");
	for (int i = 0; i < children_size(); i++)
	{
		getChildPtr(i)->prettyPrint(iden + 1);
	}
}
#endif

bool findCandidate(NodeToken *nd, char *str)
{
	// char *tocmp;
	if (str == NULL)
		return false;
	if (nd->children_size() < 1)
		return false;
	for (int i = 0; i < nd->children_size(); i++)
	{
		if (nd->getChildPtr(i)->getText() != NULL)
		{
			if (strstr(nd->getChildPtr(i)->getText(), str) == nd->getChildPtr(i)->getText())
			{
				return true;
			}
		}
	}
	return false;
}
void findFunction(NodeToken *nd, char *t)
{
	search_result_index = -1;
	search_result = NULL;

	if (t == NULL)
		return;
	if (nd == NULL)
		return;
	if (nd->children_size() < 1)
		return;
	int tmp_index = 0;
	for (int i = 0; i < nd->children_size(); i++)
	{
		NodeToken *it = nd->getChildPtr(i);
		if (it->getText() != NULL)
		{
			if (strstr(it->getText(), "Args") != NULL)
			{
				int l = strstr(it->getText(), "Args") - it->getText();
				if (l > 0)
					l--;
				if (strncmp(it->getText(), t, l) == 0)
				{
					search_result = it;
					search_result_index = tmp_index;
					return;
				}
			}
			else
			{

				if (strcmp(it->getText(), t) == 0)
				{
					search_result = it;
					search_result_index = tmp_index;
					return;
				}
			}
		}
		tmp_index++;
	}

	// looking in the external

	search_result = NULL;
	return;
}

void findVariable(NodeToken *nd, char *t, bool isCreation)
{
	search_result = NULL;

	if (t == NULL)
		return;
	// //printf("lokking for variable |%s| dans %s  already %d variables defined \n", t->text.c_str(),name.c_str(),variables.size());
	// PARSER_LOG("look for %s",t);
	if (nd->children_size() > 0)
	{

		for (int i = 0; i < nd->children_size(); i++)
		{
			NodeToken *it = nd->getChildPtr(i);
			if (it->getText() != _end_text)
			{
				// PARSER_LOG("on a %s",it->getText());
				if (strcmp(it->getText(), t) == 0)
				{
					search_result = it;
					return;
				}
			}
		}
	}
	if (!isCreation)
	{
		NodeToken *c_cntx = nd->parent;
		while (c_cntx != NULL)
		{
			//printf("lokking for variable |%s| dans %d branches\n",t,c_cntx->children_size());
			if (c_cntx->children_size() > 0)
			{
				for (int i = 0; i < c_cntx->children_size(); i++)
				{
					NodeToken *it = c_cntx->getChildPtr(i);
					if(it!=NULL)
					{
					
					if (it->getText() != _end_text)
					{
						//	PARSER_LOG("on a %s",it->getText());
						//  //////printf("is equalt to |%s|\n", (*it)._token->text.c_str());
						if (strcmp(it->getText(), t) == 0)
						{
							search_result = it;
							return;
						}
					}
				}
				}
			}
			c_cntx = c_cntx->parent;
		}
	}

	search_result = NULL;
	return;
}
void findVariable(NodeToken *nd, Token *t, bool isCreation)
{
	findVariable(nd, t->getText(), isCreation);
}
void copyPrty(NodeToken *from, NodeToken *to)
{
	if (!to->isPointer)
		to->isPointer = from->isPointer;
	to->stack_pos = from->stack_pos;
	to->_vartype = from->_vartype;
	to->type = from->type;
	// to->target=from->target;
	// getVarTypeObj() returns NULL when `from`'s type never resolved to a
	// real varType (_vartype left at EOF_VARTYPE) -- reachable from
	// otherwise-plausible-looking (if invalid) input, e.g. a C-style
	// trailing ';' after a `struct Foo {...};` definition followed by a
	// global `Foo instance[N];` declaration confuses the type lookup for
	// `Foo` into never being resolved. v1 reports a clean parse error for
	// the same input; this used to segfault here instead. Reported as a
	// parse error here rather than crashing -- Parser::parse() checks
	// Error.error right after parseProgram() returns and skips codegen
	// entirely when it's set, so it's safe even though not every one of
	// copyPrty()'s other call sites re-checks Error.error immediately.
	if (Error.error == noError)
	{
		varType *vt = from->getVarTypeObj();
		if (vt == NULL)
		{
			Error.error = impossibletofindvariabledeclaration;
			Error.token = NULL;
			return;
		}
		to->_total_size = to->_total_size * vt->_varSize;
	}
}

uint16_t stringToInt(char *str)

{
	uint16_t res = 0;
	int i = 0;
	while (str[i] != 0)
	{
		res = 10 * res + (str[i] - 48);
		i++;
	}
	return res;
}

// testChange() used to live here: a manually-maintained fixup pass run
// after every children-array reallocation, patching known raw NodeToken*
// globals (current_node, sav_current_node, lasttype, tmp_sav, entries in
// sav_token/change_type) to follow the array when it moved. It never
// covered everything that could hold such a pointer (current_cntx and
// search_result were both missing, and likely other, harder-to-spot
// cases too -- see the investigation notes on the old current_cntx
// attempt, preserved in git history), which was the root cause of a
// long-standing, heap-layout-dependent intermittent crash in
// Parser::parse()/program.visitNode() on sufficiently complex scripts.
// Now that `children` is an array of pointers to individually
// heap-allocated nodes (see nodetoken.h) rather than an inline value
// array, reallocating it only ever moves the *pointers*, never the nodes
// themselves -- so no raw NodeToken* can be invalidated by it, and this
// entire fixup mechanism is unnecessary.

void buildParents(NodeToken *__nd)
{
//return;
    // return; //new
    // printf("klkkmkml %s\r\n",__nd->getTokenText());
    if (__nd->children_size() > 0)
    {
        for (int i=0;i<__nd->children_size();i++)
        {
			NodeToken *it=__nd->getChildPtr(i);
            if (it->_nodetype == (int)UnknownNode)
            {
                //__nd->children->erase(it);
                // printf("on supppirm\r\n");
            }
            else
            {
                it->parent = __nd;
                buildParents(it);
            }
        }
    }
}