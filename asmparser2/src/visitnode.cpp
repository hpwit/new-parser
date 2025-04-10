#include "nodetoken.h"

 Stack<int> register_numr = Stack<int>(15);
 Stack<int> register_numl = Stack<int>(15);

 int point_regnum=4;
 bool isStructFunction;
 Text content,header,footer,*bufferText;
void _visittypeNode(NodeToken *nd){
    printf("%d",nd->children_size());
}
void _visitnumberNode(NodeToken *nd){}
void _visitbinOpNode(NodeToken *nd){}
void _visitunitaryOpNode(NodeToken *nd){}
void _visitoperatorNode(NodeToken *nd){}
void _visitglobalVariableNode(NodeToken *nd){}
void _visitlocalVariableNode(NodeToken *nd){}
void _visitlocalVariableNodeAsRegister(NodeToken *nd){}
void _visitblockStatementNode(NodeToken *nd){}
//void _visitdefFunctionNode(NodeToken *nd){}
void _visitdefFunctionNode(NodeToken *nd)
{
    // printf("compiling %s\n", nd->getText());
    bufferText = &content;
    isStructFunction = false;
    if (nd->type == TokenUserDefinedVariableMemberFunction)
        isStructFunction = true;
    header.addAfter(string_format(".global @_%s", nd->getText()));
    if (!isStructFunction)
        header.addAfter(string_format(".global @__%s", nd->getText()));
    // string variables = "";
    if (!isStructFunction)
    {
        string variables = "";
        for (int i = 0; i < nd->getChildPtr(1)->children_size(); i++)
        {
            variables = string_format("%s %d", variables.c_str(), nd->getChildPtr(1)->getChildPtr(i)->getVarType()->total_size);
        }
        header.addAfter(string_format(".var %d%s", nd->getChildPtr(1)->children_size(), variables.c_str()));
    }
    if (nd->getChildPtr(1)->children_size() > -1)
    {
        header.addAfter(string_format("@_stack__%s:", nd->getText()));
        header.addAfter(string_format(".bytes %d", (nd->getChildPtr(1)->children_size() + 1) * 4));
    }
    if (!isStructFunction)
    {
        bufferText->addAfter(string_format("@__%s:", nd->getText()));

        NodeToken *variaToken = nd->getChildPtr(1);
        if (variaToken->children_size() > 0)
        {
            bufferText->addAfter(string_format("entry a1,%d", ((nd->stack_pos) / 8 + 1) * 8 + 16 + _STACK_SIZE)); // ((nd->stack_pos) / 8 + 1) * 8+20)
            bufferText->addAfter(string_format("l32r a9,@_stack__%s", nd->getText()));
        }
        for (int k = 0; k < variaToken->children_size(); k++)
        {
            // int start = variaToken->getChildPtr(k)->stack_pos;

            // printf("ee p\r\n");
            int start = variaToken->getChildPtr(k)->stack_pos;
            if (start < _STACK_SIZE)
                start = _STACK_SIZE;
            for (int j = 0; j < variaToken->getChildPtr(k)->getVarType()->size; j++)
            {
                asmInstruction asmInstr = variaToken->getChildPtr(k)->getVarType()->load[0];
                bufferText->addAfter(string_format("%s %s%d,%s%d,%d", asmInstructionsName[asmInstr].c_str(), getRegType(asmInstr, 0).c_str(), k + 10, getRegType(asmInstr, 1).c_str(), 9, start - _STACK_SIZE)); // point_regnum
                                                                                                                                                                                                                 // asmInstruction asmInstr = variaToken->getChildPtr(k)->getVarType()->store[0];
                //                   bufferText->addAfter(string_format("%s %s%d,%s9,%d", asmInstructionsName[asmInstr].c_str(), getRegType(asmInstr, 0).c_str(), k+10,getRegType(asmInstr, 1).c_str(), start));
                start += variaToken->getChildPtr(k)->getVarType()->sizes[j];
            }
            //}
        }
        if (variaToken->children_size() > 0)
        {
            bufferText->addAfter(string_format("call8 @_%s", nd->getText()));
            bufferText->addAfter(string_format("retw.n", nd->getText()));
        }
    }

    bufferText->addAfter(string_format("@_%s:", nd->getText()));
    bufferText->addAfter(string_format("entry a1,%d", ((nd->stack_pos) / 8 + 1) * 8 + 16 + _STACK_SIZE)); // ((nd->stack_pos) / 8 + 1) * 8+20)
    int sav = 9;
#if _TRIGGER == 0
    bufferText->addAfterNoDouble(string_format("l32r a%d,@_stack_%s", sav, nd->getText()));
#endif
    if (saveReg)
    {
        bufferText->addAfter("ssi f15,a1,16");
        bufferText->addAfter("ssi f14,a1,20");
        bufferText->addAfter("ssi f13,a1,24");
    }
    if (saveRegAbs)
    {
        bufferText->addAfter("s32i a15,a1,16");
        bufferText->addAfter("s32i a14,a1,20");
        bufferText->addAfter("s32i a13,a1,24");
    }
    for (int i = 1; i < nd->children_size(); i++)
    {

        nd->getChildPtr(i)->visitNode();
        // f = f + g.f;
        // h = h + g.header;
    }

    if (saveReg)
    {
        bufferText->addAfter("lsi f15,a1,16");
        bufferText->addAfter("lsi f14,a1,20");
        bufferText->addAfter("lsi f13,a1,24");
    }
    if (saveRegAbs)
    {
        bufferText->addAfter("l32i a15,a1,16");
        bufferText->addAfter("l32i a14,a1,20");
        bufferText->addAfter("l32i a13,a1,24");
    }
    bufferText->addAfter(string_format("retw.n"));

    isStructFunction = false;
    bufferText = &footer;
}
void _visitstatementNode(NodeToken *nd){}
void _visitprogramNode(NodeToken *nd)
{
    //
    // printf("visit program\n");
    point_regnum = 5;

    // content.clear();
    // header.clear();
    content.begin();
    header.begin();
    footer.begin();

    //  header.addAfter("@_stack:");
    // header.addAfter(".bytes 60");
    header.addBefore("@__handle_:");
    header.addBefore(".bytes 4");
    header.addBefore("@__execaddr_:");
    header.addBefore(".bytes 4");
    header.addBefore("@__sync:");
    header.addBefore(".bytes 4");
    // header.addAfter("@_stackr:");
    // header.addAfter(".bytes 32");

    footer.addBefore(" ");
    // footer.addAfter("@__footer:");
    // footer.addAfter("entry a1,144");

    // header.addAfter("__basetime:");
    // header.addAfter(".bytes 4");
    register_numr.clear();
    register_numl.clear();
    register_numl.push(15);
    register_numr.push(15);
    bufferText = &footer;
    for (int i = 0; i < nd->children_size(); i++)
    {

#ifndef __MEM_PARSER
        if (nd->getChildPtr(i)->_nodetype != defFunctionNode && nd->getChildPtr(i)->_nodetype != defAsmFunctionNode)
        {
#endif
            nd->getChildPtr(i)->visitNode();
#ifndef __MEM_PARSER
        } // NEW
#endif
    }

    // footer.addAfter("retw.n");

    if (footer.size() > 1)
    {
        header.addAfter(".global @__footer");
        footer.addAfter("retw.n");
        footer.begin();

        footer.addBefore("entry a1,144");
        footer.begin();
        footer.addBefore("@__footer:");
    }
/*
    if (addfloatdivision)
    {
        header.addAfter(" .global @___div(d|d)");
        header.addAfter("@_stack___div(d|d):");
        header.addAfter(".bytes 12");
        content.end();
        for (int i = 0; i < _div_size; i++)
        {
            content.addAfter(string(_div[i]));
        }
    }
*/
}

void _visitassignementNode(NodeToken *nd){}
void _visitcomparatorNode(NodeToken *nd){}
void _visitcallFunctionNode(NodeToken *nd){}
void _visitforNode(NodeToken *nd){}
void _visitargumentNode(NodeToken *nd){}
void _visitextGlobalVariableNode(NodeToken *nd){}
// void _visitextDefFunctionNode(NodeToken *nd){}
void _visitextCallFunctionNode(NodeToken *nd){}
void _visitreturnArgumentNode(NodeToken *nd){}
void _visitvariableDeclarationNode(NodeToken *nd){}
void _visitdefExtFunctionNode(NodeToken *nd){}
void _visitinputArgumentsNode(NodeToken *nd){}
void _visitdefInputArgumentsNode(NodeToken *nd){}
void _visitdefExtGlobalVariableNode(NodeToken *nd){}
void _visitdefGlobalVariableNode(NodeToken *nd){}
void _visitdefLocalVariableNode(NodeToken *nd){}
void _visitstoreLocalVariableNode(NodeToken *nd){}
void _visitstoreLocalVariableNodeAsRegister(NodeToken *nd){}
void _visitstoreGlobalVariableNode(NodeToken *nd){}
void _visitstoreExtGlocalVariableNode(NodeToken *nd){}
void _visitifNode(NodeToken *nd){}
void _visitelseNode(NodeToken *nd){}
void _visitwhileNode(NodeToken *nd){}
void _visitreturnNode(NodeToken *nd){}
void _visitdefAsmFunctionNode(NodeToken *nd){}
void _visitstringNode(NodeToken *nd){}
void _visitchangeTypeNode(NodeToken *nd){}
void _visitimportNode(NodeToken *nd){}
void _visitcontinueNode(NodeToken *nd){}
void _visitbreakNode(NodeToken *nd){}
void _visittestNode(NodeToken *nd){}
void _visitternaryIfNode(NodeToken *nd){}
void _visitcallConstructorNode(NodeToken *nd){}
void _visitUnknownNode(NodeToken *nd){}