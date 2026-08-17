#include "nodetoken.h"
#include "parser_define.h"
#include "binding.h"
#include "string_constants.h"

Stack<int> register_numr = Stack<int>(15);
Stack<int> register_numl = Stack<int>(15);
Stack<varTypeEnum> globalType = Stack<varTypeEnum>(__int__);
int point_regnum = 4;
int for_if_num2 = 999;
int local_var_num = 0;
vect<int> _compare;
bool intest = false;
bool addfloatdivision = false;
// Set for the duration of a for-loop whose parse attached an onlyNode
// marker (parser.cpp) -- exactly one external array/pointer was stored
// into anywhere within it. While true, a7 already holds that array's
// resolved base address (loaded once, before the loop, by _visitforNode
// below), so _visitstoreExtGlocalVariableNode() can skip re-resolving it
// per store and just copy from a7 instead. Ported from v1's identical
// boolextern flag.
bool boolextern = false;

// Hand-assembled Xtensa FP division (there's no single hardware
// instruction for it) -- ported verbatim from upstream ESPLiveScript's
// functionlib.h ("_div"/"_div_size"). Emitted into `content` once, only
// when a script actually uses float division (see TokenSlash below and
// _visitprogramNode's use of addfloatdivision), so scripts that never
// divide floats don't pay for it.
static const char *_div[] = {
    "@___div(d|d):", "entry a1,16",
    "div0.s f3, f2", "nexp01.s f4, f2",
    "const.s f5, 1", "maddn.s f5, f4, f3",
    "mov.s f6, f3", "mov.s f7, f2",
    "nexp01.s f2, f1", "maddn.s f6, f5, f6",
    "const.s f5, 1", "const.s f0, 0",
    "neg.s f8, f2", "maddn.s f5, f4, f6",
    "maddn.s f0, f8, f3", "mkdadj.s f7, f1",
    "maddn.s f6, f5, f6", "maddn.s f8, f4, f0",
    "const.s f3, 1", "maddn.s f3, f4, f6",
    "maddn.s f0, f8, f6", "neg.s f2, f2",
    "maddn.s f6, f3, f6", "maddn.s f2, f4, f0",
    "addexpm.s f0, f7", "addexp.s f6, f7",
    "divn.s f0, f2, f6", "retw.n"};
static const int _div_size = 28;

Text content, header, footer, *bufferText;
void _visittypeNode(NodeToken *nd) {}
void _visitnumberNode(NodeToken *nd)
{
    // //printf("in number\n");

    // register_numl.duplicate();
    if (nd->children_size() > 0)
    {
        for (int i = 0; i < nd->children_size(); i++)
        {
            register_numl.duplicate();
            nd->getChildPtr(i)->visitNode();
            // register_numl.pop();
            // bufferText->sp.push(bufferText->get());
            register_numl.pop();
        }
    }
    else
    {
        if (nd->getVarTypeObj()->_varType == __float__)
        {
            float __f = 0;
            sscanf(nd->getText(), "%f", &__f);
            header.addAfter(string_format("@_%s_%d:", "local_var", local_var_num));
            // local_var_num++;
            // string val = ".bytes 4";
            // char *val=string_format(bytes,4);
            uint32_t __num = (uint32_t)(*((uint32_t *)&__f));
            /*
            // Serial.//printf(" on a float  %4x\r\n",__num);
            uint8_t c = __num & 0xff;
            val = val + " " + string_format("%02x", c);


            // val=val+'A';
            __num = __num / 256;
            c = __num & 0xff;
            val = val + " " + string_format("%02x", c);
            // val=val+'A';
            __num = __num / 256;
            c = __num & 0xff;
            val = val + " " + string_format("%02x", c);
            // val=val+'A';
            __num = __num / 256;
            c = __num & 0xff;
            val = val + " " + string_format("%02x", c);
            */
            // val=val+'A';
            header.addAfter(_numToBytes(__num));
            // point_regnum++;
            bufferText->addAfter(string_format("l32r a%d,@_%s_%d", 9, "local_var", local_var_num)); // YBA 25-02-2025
            bufferText->addAfter(string_format(lsi, register_numl.get(), 9, 0));                    // YBA 25-02-2025
            bufferText->sp.push(bufferText->get());
            // point_regnum--;
            local_var_num++;
            register_numl.decrease();
        }
        else if (strstr(nd->getText(), "x") != NULL)
        {
            unsigned int __num = 0;

            sscanf(nd->getText(), "%x", &__num);

            if (__num >= 2048)
            {
                header.addAfter(string_format("@_%s_%d:", "local_var", local_var_num));
                /*
                string val = ".bytes 4";
                uint8_t c = __num & 0xff;
                val = val + " " + string_format("%02x", c);
                // val=val+'A';
                __num = __num / 256;
                c = __num & 0xff;
                val = val + " " + string_format("%02x", c);
                // val=val+'A';
                __num = __num / 256;
                c = __num & 0xff;
                val = val + " " + string_format("%02x", c);
                // val=val+'A';
                __num = __num / 256;
                c = __num & 0xff;
                val = val + " " + string_format("%02x", c);
                // val=val+'A';
                header.addAfter(val); */
                // point_regnum++;
                header.addAfter(_numToBytes(__num));
                bufferText->addAfter(string_format("l32r a%d,@_%s_%d", 9, "local_var", local_var_num)); // YBA 25-02-2025
                bufferText->addAfter(string_format(l32i, register_numl.get(), 9, 0));                   // YBA 25-02-2025
                bufferText->sp.push(bufferText->get());
                // point_regnum--;
                local_var_num++;
                register_numl.decrease();
            }
            else
            {
                bufferText->addAfter(string_format(movi, register_numl.get(), __num)); // nd->_token->text.c_str()));
                bufferText->sp.push(bufferText->get());
                register_numl.decrease();
            }
        }
        else
        {
            int __num = 0;

            sscanf(nd->getText(), "%d", &__num);

            if (__num >= 2048 or __num <= -2047)
            {
                header.addAfter(string_format("@_%s_%d:", "local_var", local_var_num));
                /*
                string val = ".bytes 4";
                uint8_t c = __num & 0xff;
                val = val + " " + string_format("%02x", c);
                // val=val+'A';
                __num = __num / 256;
                c = __num & 0xff;
                val = val + " " + string_format("%02x", c);
                // val=val+'A';
                __num = __num / 256;
                c = __num & 0xff;
                val = val + " " + string_format("%02x", c);
                // val=val+'A';
                __num = __num / 256;
                c = __num & 0xff;
                val = val + " " + string_format("%02x", c);
                // val=val+'A';
                header.addAfter(val);
                */
                header.addAfter(_numToBytes(__num));
                // point_regnum++;
                bufferText->addAfter(string_format("l32r a%d,@_%s_%d", 9, "local_var", local_var_num)); // YBA 25-02-1975
                bufferText->addAfter(string_format(l32i, register_numl.get(), 9, 0));                   // YBA 25-02-1975
                bufferText->sp.push(bufferText->get());
                // point_regnum--;
                local_var_num++;
                register_numl.decrease();
            }
            else
            {
                // printf("Pour %s\n",bufferText->current().c_str());
                bufferText->addAfter(string_format(movi, register_numl.get(), __num)); // nd->_token->text.c_str()));
                                                                                       // printf("on a %s\n",bufferText->current().c_str());
                bufferText->sp.push(bufferText->get());
                register_numl.decrease();
            }
        }
    }
}

void _visitbinOpNode(NodeToken *nd)
{

    // printf("bin operator\n");
    // register_numl.displaystack();
    register_numl.duplicate();
    // register_numr.duplicate();
    // if (nd->getChildPtr(0)->visitNode != NULL)
    nd->getChildPtr(0)->visitNode();
    // printf("ddd %s\n",bufferText->back().c_str());
    //  register_numl.displaystack();
    register_numl.duplicate();
    // register_numr.duplicate();
    if (nd->type != TokenPower)
    {
        // if (nd->getChildPtr(2)->visitNode != NULL)
        nd->getChildPtr(1)->visitNode();
    }
    else
    {
        bufferText->addAfter(_space_);
        bufferText->sp.push(bufferText->get());
    }
    // register_numr.pop();
    register_numl.swap();
    register_numr.push(register_numl.pop());
    register_numl.swap();
    // register_numl.displaystack();
    // if (nd->getChildPtr(1)->visitNode != NULL)
    // nd->getChildPtr(1)->visitNode();
    _visitoperatorNode(nd);
    register_numl.pop();
    if (nd->type == TokenAddition || nd->type == TokenSubstraction)
    {
        register_numl.increase();
    }
    // nex
    if (nd->type == TokenSlash || nd->type == TokenStar)
    {
        register_numl.pop();
        register_numl.push(register_numr.get());
    }
    // end new
    bufferText->sp.pop();
    bufferText->sp.pop();
    bufferText->sp.push(bufferText->get());
    // register_numl.pop();
    register_numr.pop();
}

void _visitunitaryOpNode(NodeToken *nd)
{
    // register_numl.displaystack();
    register_numl.duplicate();
    // register_numr.duplicate();
    if (nd->type == TokenUppersand)
    {
        // //printf("node UNitary operator2\n");
        // nd->getChildPtr(0)->asPointer = true;
        // addTokenSup(nd);
        // nd->_token->_varType = __none__;
        nd->_vartype = (int)__none__;
        nd->isPointer = true;
        nd->getChildPtr(0)->visitNode();
        register_numl.pop();
        // bufferText->sp.pop();
        bufferText->sp.push(bufferText->get());
        return;
    }
    if (nd->type == TokenNot)
    {

        nd->getChildPtr(0)->visitNode();
        register_numl.pop();
        bufferText->addAfter(string_format(movi, 7, 0)); //"movi a7,0");
        bufferText->addAfter(string_format("bnez a%d,label_not_%d", register_numl.get(), for_if_num2));
        bufferText->addAfter(string_format(movi, 7, 1)); //"movi a7,1");
        bufferText->addAfter(string_format("label_not_%d:", for_if_num2));
        // Every other for_if_num2 label site (__test_safe_%d array-bounds
        // checks, loop_label_%d) increments the shared counter right
        // after emitting its label so the next use gets a fresh number;
        // this site never did, so every `!expr` anywhere in a script
        // reused the exact same "label_not_999" -- fine for a script with
        // only one `!`, but "label label_not_999 is already declared" for
        // any script using it twice (e.g. tetris.sc's `!checkCollision(...)`
        // in left()/right()/main()). Missing increment, not a naming
        // scheme problem -- fixed by adding it here too.
        for_if_num2++;
        bufferText->addAfter(string_format("mov a%d,a7", register_numl.get()));
        bufferText->sp.push(bufferText->get());
        register_numl.decrease();
        return;
    }
    else if (nd->type == TokenSubstraction)
    {
        nd->type = TokenNegation;

        nd->getChildPtr(0)->visitNode();
        // register_numl.displaystack();
        register_numl.pop();
        // bufferText->sp.push(bufferText->get());

        // nd->getChildPtr(1)->visitNode();
        _visitoperatorNode(nd);
        // register_numl.pop();
        register_numl.decrease();
    }
    else
    {
        // if (nd->getChildPtr(0)->visitNode != NULL)
        nd->getChildPtr(0)->visitNode();
        // register_numl.displaystack();
        register_numl.pop();
        _visitoperatorNode(nd);
        //  nd->getChildPtr(1)->visitNode();
        register_numl.decrease();
        bufferText->sp.push(bufferText->get());
    }
}

void _visitoperatorNode(NodeToken *nd)
{
    // printf("operator %s\n",tokenNames[nd->type].c_str());
    //  register_numl.pop();
    // register_numl.displaystack();
    varTypeEnum l = __none__;
    varTypeEnum r = __none__;
    // //printf("kk\n");

    if (nd->getChildPtr(0)->getVarTypeObj() != NULL)
    {
        l = nd->getChildPtr(0)->getVarTypeObj()->_varType;
    }

    // //printf("kk2 :%d\n",nd->parent->children_size());
    if (nd->children_size() >= 2)
    {
        if (nd->getChildPtr(1) == NULL)
        {
            // printf("WFT %d %s\n",nd->parent->children_size(),nodeTypeNames[nd->parent->_nodetype].c_str());
        }

        if (nd->getChildPtr(1)->getVarTypeObj() != NULL)
        {
            // //printf("kk32 %s\n",nodeTypeNames[nd->parent->_nodetype].c_str());
            r = nd->getChildPtr(1)->getVarTypeObj()->_varType;
        }
    }
    // //printf("kk3\n");
    bool ff = false;
    if (nd->getVarTypeObj() == NULL)
    {
        // addTokenSup(nd->parent);
        if (globalType.get() == __float__)
        {
            // nd->parent->_token->_varType = __float__;
            nd->_vartype = (int)__float__;
        }
        else
        {
            // nd->parent->_token->_varType = __none__;
            nd->_vartype = __none__;
        }
    }

    // //printf("kk4\n");
    if (globalType.get() == __float__)
    {
        ff = true;
        nd->_vartype = __float__;
    }
    // //printf("kk5\n");
    asmInstruction asmInstr;
    if (nd->children_size() >= 2)
        translateType(globalType.get(), r, register_numr.get());
    translateType(globalType.get(), l, register_numl.get());
    switch (nd->type)
    {
    case TokenAddition:
    {
        if (ff)
        {
            asmInstr = adds;
        }
        else
        {
            asmInstr = add;
        }
        char *new_line = string_format(asmInstr, register_numl.get(), register_numl.get(), register_numr.get());

        char *_last = bufferText->current();
        ; // bufferText->back();
        char *tocmp = string_format("movi a%d,", register_numr.get());
        // printf("to found %s\r\n",_last,tocmp);
        // if (_last.compare(0, tocmp.size(), tocmp) == 0)
        if (strncmp(_last, tocmp, strlen(tocmp)) == 0)
        {
            //   printf("to found %s\r\n",_last,tocmp);
            int a, b;
            sscanf(_last, "movi a%d,%d", &a, &b);
            if (b >= -128 and b <= 127)
            {
                // bufferText->pop();
                bufferText->blankCurrent();
                bufferText->addAfter(string_format(addi, register_numl.get(), register_numl.get(), b));
                free(new_line);
            }
            else
            {
                bufferText->addAfter(new_line);
            }
        }
        else
        {
            bufferText->addAfter(new_line);
        }
        free(tocmp);
        // return;
    }
    break;
    case TokenShiftLeft:
        // bufferText->addAfter("movi a8,32");
        // bufferText->addAfter(string_format("sub a%d,a8,a%d",register_numr.get(),register_numr.get()).c_str());
        bufferText->addAfter(string_format(ssl, register_numr.get()));
        bufferText->addAfter(string_format(sll, register_numl.get(), register_numl.get()));
        break;
    case TokenShiftRight:
        // bufferText->addAfter("movi a8,32");
        // bufferText->addAfter(string_format("sub a%d,a8,a%d",register_numr.get(),register_numr.get()).c_str());
        bufferText->addAfter(string_format(wsr, register_numr.get(), 3));
        bufferText->addAfter(string_format(srl, register_numl.get(), register_numl.get()));
        break;
    case TokenSubstraction:
    {
        if (ff)
        {
            asmInstr = subs;
        }
        else
        {
            asmInstr = sub;
        }
        char *new_line = string_format(asmInstr, register_numl.get(), register_numl.get(), register_numr.get());

        char *_last = bufferText->current();
        char *tocmp = string_format("movi a%d,", register_numr.get());
        //  printf("to found %s %s\r\n", bufferText->current().c_str(), tocmp.c_str());
        // if (_last.compare(0, tocmp.size(), tocmp) == 0)
        if (strncmp(_last, tocmp, strlen(tocmp)) == 0)
        {
            //   printf("to found %s\r\n",_last,tocmp);
            int a, b;
            sscanf(_last, "movi a%d,%d", &a, &b);
            if (b >= -128 and b <= 127)
            {
                // bufferText->pop();
                bufferText->blankCurrent();
                bufferText->addAfter(string_format(addi, register_numl.get(), register_numl.get(), 0 - b));
                free(new_line);
            }
            else
            {
                bufferText->addAfter(new_line);
            }
        }
        else
        {
            bufferText->addAfter(new_line);
        }
        // bufferText->addAfter(string_format("sub a%d,a%d,a%d", register_numl.get(), register_numl.get(), register_numr.get()));
        // return;
       
        free(tocmp);
    }
    break;
    case TokenSlash:
        // bufferText->addAfter(string_format("quou a%d,a%d,a%d", register_numl.get(), register_numl.get(), register_numr.get()));
        if (ff)
        {
            bufferText->addAfter(string_format(movs, 1, register_numl.get()));
            bufferText->addAfter(string_format(movs, 2, register_numr.get()));
            bufferText->addAfter("call8  @___div(d|d)");
            bufferText->addAfter(string_format(movs, register_numl.get(), 0));
            addfloatdivision = true;
        }
        else
        {
            // asmInstr = quou;
            if (l == __uint32_t__ || r == __uint32_t__)
                asmInstr = quou;
            else
                asmInstr = quos;
            bufferText->addAfter(string_format(asmInstr, register_numl.get(), register_numl.get(), register_numr.get()));
        }
        // return;
        break;
    case TokenStar:
        if (ff)
        {
            asmInstr = muls;
        }
        else
        {
            asmInstr = mull;
        }
        // Same "peek at the movi that just loaded the right operand"
        // trick TokenAddition/TokenSubstraction above already use to fold
        // into addi -- here folding a multiply-by-{2,3,5,9} into a single
        // add/addx2/addx4/addx8 (ar=ar+ar, ar=(ar<<1)+ar, ar=(ar<<2)+ar,
        // ar=(ar<<3)+ar) instead of movi+mull. Found by diffing this
        // compiler's output for a real LED-matrix script's `angle*4`-
        // style expressions against real xtensa-esp32-elf-gcc's -O1
        // output, which uses exactly this self-referencing addx idiom
        // (confirmed against real hardware encoding -- see asm_encoders.h).
        // 4/8 have no equally-cheap one-instruction form without a spare
        // zero register (addx4/addx8 need a real third operand, not an
        // implicit zero), so they're left as mull; 2/3/5/9 are the only
        // constants a single self-referencing instruction covers.
        if (!ff)
        {
            char *_last = bufferText->current();
            char *tocmp = string_format("movi a%d,", register_numr.get());
            if (strncmp(_last, tocmp, strlen(tocmp)) == 0)
            {
                int a, b;
                sscanf(_last, "movi a%d,%d", &a, &b);
                asmInstruction strengthReduced;
                bool found = true;
                switch (b)
                {
                case 2: strengthReduced = add; break;
                case 3: strengthReduced = addx2; break;
                case 5: strengthReduced = addx4; break;
                case 9: strengthReduced = addx8; break;
                default: found = false; break;
                }
                if (found)
                {
                    bufferText->blankCurrent();
                    bufferText->addAfter(string_format(strengthReduced, register_numl.get(), register_numl.get(), register_numl.get()));
                    free(tocmp);
                    break;
                }
            }
            free(tocmp);
        }
        bufferText->addAfter(string_format(asmInstr, register_numl.get(), register_numl.get(), register_numr.get()));
        break;
    case TokenPlusPlus:
        if (nd->getChildPtr(0)->isPointer && nd->getChildPtr(0)->children_size() == 0)
        {
            bufferText->addAfter(string_format(addi, register_numl.get(), register_numl.get(), nd->getChildPtr(0)->getVarTypeObj()->total_size));
        }
        else
        {
            bufferText->addAfter(string_format(addi, register_numl.get(), register_numl.get(), 1));
        }
        // return;
        break;
    case TokenMinusMinus:
        if (nd->getChildPtr(0)->isPointer && nd->getChildPtr(0)->children_size() == 0)
        {
            bufferText->addAfter(string_format(addi, register_numl.get(), register_numl.get(), 0 - nd->getChildPtr(0)->getVarTypeObj()->total_size));
        }
        else
        {
            bufferText->addAfter(string_format(addi, register_numl.get(), register_numl.get(), -1));
        }
        // return;
        break;
    case TokenModulo:
        bufferText->addAfter(string_format(remu, register_numl.get(), register_numl.get(), register_numr.get()));
        //  return;
        break;
    case TokenPower:
    {
        // comment supprimer ce qu'il y a avant
        int __num = 0;
        if (nd->getChildPtr(1)->_nodetype == numberNode)
        {

            sscanf(nd->getChildPtr(1)->getText(), "%d", &__num);
            if (__num > 2)
            {
                if (ff)
                {
                    bufferText->blankCurrent();
                    bufferText->addAfter(string_format(movs, 10, register_numl.get()));
                    asmInstr = muls;
                }
                else
                {
                    bufferText->addAfter(string_format(mov, 10, register_numl.get()));
                    asmInstr = mull;
                }
                for (int k = 1; k < __num; k++)
                {
                    bufferText->addAfter(string_format(asmInstr, register_numl.get(), register_numl.get(), 10));
                }
            }
            else
            {
                if (ff)
                {
                    bufferText->blankCurrent();
                    asmInstr = muls;
                }
                else
                {
                    asmInstr = mull;
                }
                bufferText->addAfter(string_format(asmInstr, register_numl.get(), register_numl.get(), register_numl.get()));
            }
        }
    }
    break;
    case TokenKeywordAnd:
        bufferText->addAfter(string_format(_and, register_numl.get(), register_numl.get(), register_numr.get()));
        break;
    case TokenKeywordOr:
        bufferText->addAfter(string_format(_or, register_numl.get(), register_numl.get(), register_numr.get()));
        break;
    case TokenKeywordFabs:

        bufferText->addAfter(string_format(abss, register_numl.get(), register_numl.get()));
        break;
    case TokenKeywordAbs:
        if (ff)
        {
            bufferText->addAfter(string_format(abss, register_numl.get(), register_numl.get()));
        }
        else
        {
            bufferText->addAfter(string_format(_abs, register_numl.get(), register_numl.get()));
        }
        break;
    case TokenNegation:
        if (ff)
        {
            asmInstr = negs;
        }
        else
        {
            asmInstr = neg;
        }
        bufferText->addAfter(string_format(asmInstr, register_numl.get(), register_numl.get()));
        bufferText->sp.pop();
        bufferText->sp.push(bufferText->get());
        // return;
    default:
        // return;
        break;
    }
}

void _visitglobalVariableNode(NodeToken *nd)
{

    // printf("comopiline glmobalvar  %s\n",nd->getTokenText());
    // int r_size = 0;
    // isInFunction = false;
    register_numl.duplicate();
    if (nd->children_size() > 0)
    {
        // int r_size = 0;
        vect<char *> tile;
        int nb = 0;
        // string sd = string(nd->getTargetText());
        // if (sd.compare(0, 1, "@") == 0)
        if (strncmp(nd->getTargetText(), (char *)"@", 1) == 0)
        {

            // tile = split(sd, " ");
            str_split(&tile, nd->getTargetText(), (char *)" ");
            sscanf(tile[0], "@%d", &nb);
            // r_size = stringToInt((char *)tile[1].c_str());
        }
        if (nb > 1)
        {
            bufferText->addAfter(string_format(movi, 10, 0));
        }

        for (int i = 0; i < nd->children_size(); i++)
        {
            //  globalType.push(__int__);
            register_numl.duplicate();
            nd->getChildPtr(i)->visitNode();
            register_numl.pop();
            translateType(__int__, nd->getChildPtr(i)->getVarTypeObj()->_varType, register_numl.get());
            if (nd->children_size() > 1)
            {
                if (i < nd->children_size() - 1)
                {

                    for (int h = 1; h < nd->children_size() - i; h++)
                    {
                        bufferText->addAfter(string_format(movi, 11,stringToInt(tile[i + 1 + h])));
                        // bufferText->addAfter(string_format("mull a11,a10,a11"));
                        bufferText->addAfter(string_format(mull, register_numl.get(), register_numl.get(),11));
                    }
                    // if(i>0)
                    bufferText->addAfter(string_format(add, 10,10,register_numl.get()));
                }
                else
                {
                    bufferText->addAfter(string_format(add, register_numl.get(), 10,register_numl.get()));
                }
            }

            // globalType.pop();
        }
        tile.empty();
        tile.clear();
    }
    varType *v = nd->getVarTypeObj();
    int start = nd->stack_pos;

    if (nd->isPointer)
    {
        // start = nd->stack_pos;
        //  regnum = point_regnum;
    }

    if (nd->isPointer && nd->children_size() > 0) // leds[g];
    {
        // f=f+number.f;
        if (nd->type == TokenUserDefinedVariableMember or nd->type == TokenUserDefinedVariableMemberFunction)
        {
            bufferText->addAfter(string_format(movi, point_regnum, nd->_total_size));
            bufferText->addAfter(string_format(mull, register_numl.get(), register_numl.get(), point_regnum));
            bufferText->addAfter(string_format("l32r a%d,@_%s", point_regnum, nd->getText()));
            // addi's third operand is a genuine immediate ("addi a%d,a%d,%d",
            // parser_enum.cpp) -- passing register_numl.get() here (a
            // register *number*, e.g. 15) rendered as a literal "...,15"
            // instead of adding that register's actual runtime value (the
            // index-scaled offset the movi/mull pair above just computed
            // into it). Every arr[i].field read / arr[i].method() call
            // this branch handles resolved to the same fixed address
            // regardless of i as a result. The sibling `v->total_size > 4`
            // branch just below already does this correctly with `add`
            // ("add a%d,a%d,a%d", all three registers) -- matching that.
            bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
        }
        else if (v->total_size == 2 || v->total_size == 3 || v->total_size == 5 || v->total_size == 7 || v->total_size == 9)
        {
            // Scale the index in place with a single self-referencing
            // add/addx2/addx4/subx8 (ar=ar+ar / ar=(ar<<1)+ar /
            // ar=(ar<<2)+ar / ar=(ar<<3)-ar -- 2x/3x/5x/7x respectively)
            // instead of movi+mull, then combine with the loaded base
            // exactly like the >4 case below -- one fewer instruction.
            // 4 and 8 have no equally-cheap self-referencing form (see
            // the size==4 case below and asm_encoders.h's addx2/addx4/
            // addx8 comment), so they're deliberately not listed here.
            asmInstruction scaleInstr;
            switch (v->total_size)
            {
            case 2: scaleInstr = add; break;
            case 3: scaleInstr = addx2; break;
            case 5: scaleInstr = addx4; break;
            case 7: scaleInstr = subx8; break;
            default: scaleInstr = addx8; break; // 9
            }
            bufferText->addAfter(string_format(scaleInstr, register_numl.get(), register_numl.get(), register_numl.get()));
            bufferText->addAfter(string_format("l32r a%d,@_%s", point_regnum, nd->getText()));
            bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
        }
        else if (v->total_size == 4)
        {
            // ar*4 via two self-doublings -- same instruction count as
            // the movi+mull path just below (4 total either way: two
            // adds + l32r + add vs movi+mull+l32r+add), but avoids the
            // full multiply. Kept as its own case for consistency with
            // the sizes above rather than falling into the general path.
            bufferText->addAfter(string_format(add, register_numl.get(), register_numl.get(), register_numl.get()));
            bufferText->addAfter(string_format(add, register_numl.get(), register_numl.get(), register_numl.get()));
            bufferText->addAfter(string_format("l32r a%d,@_%s", point_regnum, nd->getText()));
            bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
        }
        else if (v->total_size > 4)
        {
            // string tmp=content.l->back();
            // content.l->pop_back();
            bufferText->addAfter(string_format(movi, point_regnum, v->total_size));
            bufferText->addAfter(string_format(mull, register_numl.get(), register_numl.get(), point_regnum));
            bufferText->addAfter(string_format("l32r a%d,@_%s", point_regnum, nd->getText()));
            bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
        }
        else
        {
            bufferText->addAfter(string_format("l32r a%d,@_%s", point_regnum, nd->getText()));
            for (int i = 0; i < v->total_size; i++)
            {
                bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
            }
        }
    }
    else
    {
        bufferText->addAfter(string_format("l32r a%d,@_%s", point_regnum, nd->getText()));
    }
    bufferText->sp.push(bufferText->get());
    if (nd->asPointer) //(&d)
    {
        bufferText->addAfter(string_format(mov, register_numl.get(), point_regnum));
        bufferText->sp.push(bufferText->get());
    }
    else if ((nd->children_size() > 0 or !nd->isPointer) && nd->type != TokenUserDefinedVariableMemberFunction) // leds[h] or h h being global)
    {
        // if (nd->target == EOF_TEXTARRAY)
        //  {
        for (int i = 0; i < v->size; i++)
        {
            // bufferText->addAfter(string_format("%s %s%d,%s%d,%d", v->load[i].c_str(), v->reg_name.c_str(), register_numl.get(), v->reg_name.c_str(), point_regnum, start));
            asmInstruction asmInstr = v->load[i];
            // bufferText->addAfter(string_format("%s %s%d,%s%d,%d", asmInstructionsName[asmInstr], getRegType(asmInstr, 0).c_str(), register_numl.get(), getRegType(asmInstr, 1).c_str(), point_regnum, start));
            bufferText->addAfter(string_format(asmInstr, register_numl.get(), point_regnum, start));
            // register_numl--;
            start += v->sizes[i];
            bufferText->sp.push(bufferText->get());
        }
    }
    else // s(leds)
    {
        bufferText->addAfter(string_format(mov, register_numl.get(), point_regnum));
        bufferText->sp.push(bufferText->get());
    }
    // res.f = f;
    // res.header = number.header + h;
    // point_regnum++;
    register_numl.pop();
    //    res.register_numl=register_numl;
    // res.register_numr=register_numr;
    register_numl.decrease();
    /*  YBA 25-02-2025
   if (nd->asPointer or (nd->isPointer)) // && nd->children_size() == 0))
       point_regnum--;
       */
    return;
}
void _visitlocalVariableNode(NodeToken *nd)
{
    // printf("in lcoall\n");

    if (nd->asPointer)
    {
        register_numl.duplicate();
        varType *v = nd->getVarTypeObj();
        int start = nd->stack_pos;
        // //printf("kzlekmze\n");
        // bufferText->addAfter(string_format("l32r a%d,stack", point_regnum));

        // if( if (nd->isPointer && nd->children_size() > 0))

        if (nd->type == TokenUserDefinedVariableMember or nd->type == TokenUserDefinedVariableMemberFunction)
        {
            if (!nd->isPointer)
            {
                bufferText->addAfter(string_format(addi, register_numl.get(), 1, start - (int)(start / 1000) * 1000));
                bufferText->addAfter(string_format(l32i, register_numl.get(), register_numl.get(), (start / 1000)));
            }
            else
            {
                // bufferText->addAfter(string_format("l32i a%d,a1,%d", register_numl.get(),start-(int)(start/1000)*1000));
                asmInstruction asmInstr = v->load[0];
                // bufferText->addAfter(string_format("%s %s%d,%s%d,%d", asmInstructionsName[asmInstr].c_str(), getRegType(asmInstr, 0).c_str(), register_numl.get(), getRegType(asmInstr, 1).c_str(), register_numl.get(), start/1000));
                bufferText->addAfter(string_format(asmInstr, register_numl.get(), 2, start / 1000));
                translateType(globalType.get(), v->_varType, register_numl.get());
                // bufferText->addAfter(string_format("l16si a%d,a%d,%d", register_numl.get(),register_numl.get(),start/1000));
            }
        }
        else
        {
            bufferText->addAfter(string_format(addi, register_numl.get(), 1, start));
        }
        register_numl.decrease();
        if (nd->isPointer && nd->children_size() > 0)
        {
            register_numl.duplicate();
            nd->getChildPtr(0)->visitNode();
            register_numl.pop();
            for (int i = 0; i < v->total_size; i++)
            {
                bufferText->addAfter(string_format(add, register_numl.get() + 1, register_numl.get() + 1, register_numl.get()));
            }
            register_numl.increase();
        }
        bufferText->sp.push(bufferText->get());
        register_numl.pop();
        register_numl.decrease();
        // //printf("kzlekmze2\n");
        return;
    }
    register_numl.duplicate();
    // if (nd->children_size() > 0)
    //{
    //  number = nd->getChildPtr(0)->visitNode(nd->getChildPtr(0), register_numl, register_numr);
    // }

    if (nd->children_size() > 0)
    {
        globalType.push(__int__);
        register_numl.duplicate();
        nd->getChildPtr(0)->visitNode();
        register_numl.pop();
        globalType.pop();
    }
    varType *v = nd->getVarTypeObj();
    int start = nd->stack_pos;
    uint8_t regnum = 1;
    if (nd->asPointer or (nd->isPointer && nd->children_size() == 0))
        point_regnum++;
    // uint8_t save_reg;
    //  point_regnum++;
    if (nd->isPointer)
    {
        // start = nd->stack_pos;
        regnum = point_regnum;
    }
    if (nd->isPointer)
    {
        int start = nd->stack_pos;

        // bufferText->addAfter(string_format("addi a%d,a1,%d", point_regnum, start));

        if (nd->children_size() == 0)
        {
            if (nd->type == TokenUserDefinedVariableMemberFunction)
                bufferText->addAfter(string_format(addi, register_numl.get(), 1, start));
            else
                bufferText->addAfter(string_format(l32i, register_numl.get(), 1, start));
            // bufferText->addAfter(string_format("mov a%d,a%d", register_numl.get(), point_regnum));
            bufferText->sp.push(bufferText->get());
        }
        else
        {
            bufferText->addAfter(string_format(l32i, point_regnum, 1, start));
            start = 0;
            // Same strength reduction as _visitglobalVariableNode()'s
            // identical case: scale the index in place with a single
            // self-referencing add/addx2/addx4/subx8 instead of
            // v->total_size repeated adds. 4 uses two self-doublings;
            // any other size (1, 6, 8, 10+) keeps the plain repeated-add
            // loop -- no movi+mull path exists in this function to fall
            // back to for a large size.
            if (v->total_size == 2 || v->total_size == 3 || v->total_size == 5 || v->total_size == 7 || v->total_size == 9)
            {
                asmInstruction scaleInstr;
                switch (v->total_size)
                {
                case 2: scaleInstr = add; break;
                case 3: scaleInstr = addx2; break;
                case 5: scaleInstr = addx4; break;
                case 7: scaleInstr = subx8; break;
                default: scaleInstr = addx8; break; // 9
                }
                bufferText->addAfter(string_format(scaleInstr, register_numl.get(), register_numl.get(), register_numl.get()));
                bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
            }
            else if (v->total_size == 4)
            {
                bufferText->addAfter(string_format(add, register_numl.get(), register_numl.get(), register_numl.get()));
                bufferText->addAfter(string_format(add, register_numl.get(), register_numl.get(), register_numl.get()));
                bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
            }
            else
            {
                for (int i = 0; i < v->total_size; i++)
                {
                    bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
                }
            }
            bufferText->sp.push(bufferText->get());
            for (int i = 0; i < v->size; i++)
            {
                // bufferText->addAfter(string_format("%s %s%d,%s%d,%d", v->load[i].c_str(), v->reg_name.c_str(), register_numl.get(), v->reg_name.c_str(), regnum, start));
                asmInstruction asmInstr = v->load[i];
                bufferText->addAfter(string_format(asmInstr, register_numl.get(), point_regnum, start));
                translateType(globalType.get(), v->_varType, register_numl.get());
                // register_numl--;
                start += v->sizes[i];
                bufferText->sp.push(bufferText->get());
            }
        }
        if (nd->asPointer or (nd->isPointer && nd->children_size() == 0))
            point_regnum--;
        register_numl.pop();
        register_numl.decrease();
    }
    else
    {
        // printf("jj\n");
        for (int i = 0; i < v->size; i++)
        {
            // bufferText->addAfter(string_format("%s %s%d,%s%d,%d", v->load[i].c_str(), v->reg_name.c_str(), register_numl.get(), v->reg_name.c_str(), regnum, start));
            asmInstruction asmInstr = v->load[i];
            bufferText->addAfter(string_format(asmInstr, register_numl.get(), regnum, start));
            // printf("jj2 :%s\n",string_format("%s %s%d,%s%d,%d", asmInstructionsName[asmInstr].c_str(), getRegType(asmInstr, 0).c_str(), register_numl.get(), getRegType(asmInstr, 1).c_str(), regnum, start).c_str());
            translateType(globalType.get(), v->_varType, register_numl.get());
            // printf("jj3\n");
            // register_numl--;
            start += v->sizes[i];
            bufferText->sp.push(bufferText->get());
            // printf("jj4\n");
        }
        register_numl.pop();
        // printf("jj5\n");
        register_numl.decrease();
        // printf("jj6\n");

        return;
    }
}

void _visitlocalVariableNodeAsRegister(NodeToken *nd)
{
    if (nd->getVarTypeObj()->_varType == __float__)
        bufferText->addAfter(string_format(wfr, register_numl.get(), nd->target));
    else
        bufferText->addAfter(string_format(movr, register_numl.get(), nd->target));
    bufferText->sp.push(bufferText->get());
    register_numl.decrease();
}
void _visitblockStatementNode(NodeToken *nd)
{
    register_numr.clear();
    register_numl.clear();
    register_numl.push(15);
    register_numr.push(15);

    register_numl.push(15);
    register_numr.push(15);
    for (int i = 0; i < nd->children_size(); i++)
    {

        nd->getChildPtr(i)->visitNode();
        // f = f + g.f;
    }
    // nd->clear();
}
// void _visitdefFunctionNode(NodeToken *nd){}
void _visitdefFunctionNode(NodeToken *nd)
{
    // printf("compiling %s\n", nd->getText());
    bufferText = &content;
    isStructFunction = false;
    if (nd->type == TokenUserDefinedVariableMemberFunction)
        isStructFunction = true;
    header.addAfter(string_format(".global @_%s", nd->getText()));
   //remove this becuase not needed anymore
    // if (!isStructFunction)
   //     header.addAfter(string_format(".global @__%s", nd->getText()));
    // string variables = "";

    // Pure metadata, not part of the disabled wrapper mechanism below --
    // createBinaryHeader()'s function_declaration encoder (asm_parser.cpp)
    // unconditionally reads the header line right after this function's
    // own ".global @_NAME" as its args_num/variables descriptor. Without
    // this ".var" line, that encoder silently reads whatever unrelated
    // header line happens to come next instead (the next function's own
    // ".global", an external's own reservation label, ...), corrupting
    // both fields: args_num always decodes to 0 (sscanf() finds no
    // leading digit in that text and silently leaves it at its
    // zero-initialized default) and variables holds a stray copy of that
    // neighboring line's own name text. Re-enabled on its own, still
    // disabled below, since it doesn't emit any of the wrapper's actual
    // marshaling code, entry/call8/retw.n included. Restoring this made
    // every record's own `variables` field digit-led again (its real "N
    // size..." descriptor), which is exactly what asm_execute.cpp's
    // isWrapperRecord() used to read as "this is a wrapper record" --
    // see its own comment for why it's now hardcoded to always return
    // false instead, alongside this change.
    if (!isStructFunction)
    {
        char *variables = NULL;
        for (int i = 0; i < nd->getChildPtr(1)->children_size(); i++)
        {

           variables = str_concat("%s %d", variables, nd->getChildPtr(1)->getChildPtr(i)->getVarTypeObj()->total_size);
        }
        if (variables == NULL)
            header.addAfter(string_format(".var %d", nd->getChildPtr(1)->children_size()));
        else
            header.addAfter(string_format(".var %d%s", nd->getChildPtr(1)->children_size(), variables));
    if(variables!=NULL)
    free(variables);

    }

    /*
    if (nd->getChildPtr(1)->children_size() > -1)
    {
        header.addAfter(string_format("@_stack__%s:", nd->getText()));
        header.addAfter(string_format(bytes, (nd->getChildPtr(1)->children_size() + 1) * 4));
    }
    if (!isStructFunction)
    {
        bufferText->addAfter(string_format("@__%s:", nd->getText()));

        NodeToken *variaToken = nd->getChildPtr(1);
        if (variaToken->children_size() > 0)
        {
            bufferText->addAfter(string_format(entry, ((nd->stack_pos) / 8 + 1) * 8 + 16 + _STACK_SIZE)); // ((nd->stack_pos) / 8 + 1) * 8+20)
            bufferText->addAfter(string_format("l32r a9,@_stack__%s", nd->getText()));
        }
        for (int k = 0; k < variaToken->children_size(); k++)
        {
            // int start = variaToken->getChildPtr(k)->stack_pos;

            // printf("ee p\r\n");
            int start = variaToken->getChildPtr(k)->stack_pos;
            if (start < _STACK_SIZE)
                start = _STACK_SIZE;
            for (int j = 0; j < variaToken->getChildPtr(k)->getVarTypeObj()->size; j++)
            {
                asmInstruction asmInstr = variaToken->getChildPtr(k)->getVarTypeObj()->load[0];
                bufferText->addAfter(string_format(asmInstr, k + 10, 9, start - _STACK_SIZE)); // point_regnum
                                                                                               // asmInstruction asmInstr = variaToken->getChildPtr(k)->getVarTypeObj()->store[0];
                //                   bufferText->addAfter(string_format("%s %s%d,%s9,%d", asmInstructionsName[asmInstr].c_str(), getRegType(asmInstr, 0).c_str(), k+10,getRegType(asmInstr, 1).c_str(), start));
                start += variaToken->getChildPtr(k)->getVarTypeObj()->sizes[j];
            }
            //}
        }
        if (variaToken->children_size() > 0)
        {
            bufferText->addAfter(string_format(call8, nd->getText()));
            bufferText->addAfter(asmInstructionsName[retw]);
        }
    }
    */

    bufferText->addAfter(string_format(arrobase_label, nd->getText()));
    // bufferText->addAfter(string_format(entry, ((nd->stack_pos) / 8 + 1) * 8 + 16 + _STACK_SIZE)); // ((nd->stack_pos) / 8 + 1) * 8+20)
    bufferText->addAfter(string_format(entry, ((nd->stack_pos) / 8 + 1) * 8 + 16));
#if _TRIGGER == 0
    int sav = 9;
    bufferText->addAfterNoDouble(string_format("l32r a%d,@_stack_%s", sav, nd->getText()));
#endif
    if (saveReg)
    {

        bufferText->addAfter(string_format(ssi, 15, 1, 16));
        bufferText->addAfter(string_format(ssi, 15, 1, 20));
        bufferText->addAfter(string_format(ssi, 13, 1, 24));
    }
    if (saveRegAbs)
    {
        bufferText->addAfter(string_format(s32i, 15, 1, 16));
        bufferText->addAfter(string_format(s32i, 14, 1, 20));
        bufferText->addAfter(string_format(s32i, 13, 1, 24));
    }
    for (int i = 1; i < nd->children_size(); i++)
    {

        nd->getChildPtr(i)->visitNode();
        // f = f + g.f;
        // h = h + g.header;
    }

    if (saveReg)
    {
        bufferText->addAfter(string_format(lsi, 15, 1, 16));
        bufferText->addAfter(string_format(lsi, 15, 1, 20));
        bufferText->addAfter(string_format(lsi, 13, 1, 24));
    }
    if (saveRegAbs)
    {

        bufferText->addAfter(string_format(l32i, 15, 1, 16));
        bufferText->addAfter(string_format(l32i, 14, 1, 20));
        bufferText->addAfter(string_format(l32i, 13, 1, 24));
        // bufferText->addAfter("l32i a13,a1,24");
    }
    bufferText->addAfter(asmInstructionsName[retw]);

    isStructFunction = false;
    bufferText = &footer;
}
void _visitstatementNode(NodeToken *nd)
{
    point_regnum = 5;
    // printf("visit statement\n");
    register_numr.clear();
    register_numl.clear();
    register_numl.push(15);
    register_numr.push(15);

    register_numl.push(15);
    register_numr.push(15);
    for (int i = 0; i < nd->children_size(); i++)
    {

        register_numl.duplicate();
        nd->getChildPtr(i)->visitNode();
        register_numl.pop();
    }
    // nd->clear();
    // printf("end statement\n");
    //_node_token_stack.clear();
}
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

    header.addBefore(string_format(arrobase_label, _handle_));
    header.addBefore(string_format(bytes, 4));

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
        footer.addAfter(asmInstructionsName[retw]);
        footer.begin();

        footer.addBefore(string_format(entry, 144));
        footer.begin();
        footer.addBefore(string_format(arrobase_label, "_footer"));
    }
    if (addfloatdivision)
    {
        header.addAfter(" .global @___div(d|d)");
       // header.addAfter("@_stack___div(d|d):");
       // header.addAfter(".bytes 12");
        content.end();
        for (int i = 0; i < _div_size; i++)
        {
            // Use the const char* overload -- it mallocs its own copy
            // before storing it, unlike addAfter(char*), which takes
            // ownership of (and may later free()) whatever pointer it's
            // given. _div[i] points into static, non-heap storage.
            content.addAfter(_div[i]);
        }
    }
}

void _visitassignementNode(NodeToken *nd)
{
    // printf("entre assignemen\n") ;
    point_regnum = 5; // YBA 25-02-2025
    bufferText->sp.clear();
    bufferText->sp.push(bufferText->get());
    register_numl.duplicate();

    if (nd->children_size() > 1)
        globalType.push(nd->getChildPtr(0)->getVarTypeObj()->_varType);

    if (nd->children_size() > 1)
    {
        register_numl.duplicate();
        nd->getChildPtr(1)->visitNode();
        register_numl.pop();

        if (nd->getChildPtr(1)->getVarTypeObj() != NULL)
        {

            translateType(globalType.get(), nd->getChildPtr(1)->getVarTypeObj()->_varType, register_numl.get());
            // printf("retour on push\n") ;
            // }
        }

        bufferText->sp.pop();
        bufferText->sp.push(bufferText->get());

        point_regnum++;
    }
    register_numl.duplicate();
    nd->getChildPtr(0)->visitNode();
    register_numl.pop();

    register_numl.pop();
    bufferText->sp.pop();
    globalType.pop();
    // clearNodeToken(nd); // new
    // nd->clear();
    register_numl.clear();
    register_numl.push(15);
    register_numl.push(15);

    register_numr.clear();
    register_numr.push(15);
    register_numr.push(15);
}

// Ported from upstream ESPLiveScript (v1, NodeToken.h's isBranchImmediate()/
// valBranchImmediate()): Xtensa's BccI branch-immediate instructions
// (BLTI/BEQI/BGEI/BNEI) don't take an arbitrary immediate -- only 16
// specific values, the "B4CONST" table {-1,1,2,3,4,5,6,7,8,10,12,16,32,
// 64,128,256}, each identified by its table index (0-15), which is what
// actually gets encoded (see asm_encoders.h's bin_blti/bin_bgei and
// op_blti's l0_15 operand). _visitcomparatorNode() below uses these to
// fold `if (x < 5)`-style comparisons against one of these 16 values
// straight into a single branch instruction instead of a movi
// (loading the constant into a register) followed by a register-register
// branch.
//
// `inverse` supports `>`/`<=`, which Xtensa has no direct immediate form
// for -- `x > 9` becomes `x >= 10` (BGEI) and `x <= 9` becomes `x < 10`
// (BLTI) by incrementing the constant first, so eligibility has to be
// checked against `val + 1` being in the table instead of `val` itself;
// the 14 values here are exactly the regular table's values minus 1.
// (Table index 0, value -1, is never reachable through either path --
// nothing here ever calls this with val <= 0.)
static bool isBranchImmediate(int val, bool inverse)
{
    if (!inverse)
    {
        switch (val)
        {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 10:
        case 12:
        case 16:
        case 32:
        case 64:
        case 128:
        case 256:
            return true;
        default:
            return false;
        }
    }
    else
    {
        switch (val)
        {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 9:
        case 11:
        case 15:
        case 31:
        case 63:
        case 127:
        case 255:
            return true;
        default:
            return false;
        }
    }
}

// Maps a value already confirmed eligible via isBranchImmediate(val, false)
// to its B4CONST table index (what the instruction text/encoder actually
// need -- see the comment above). Caller must have already incremented
// `val` if this follows an isBranchImmediate(..., true) (inverse) check.
//
// v1's own version of this table has a real bug worth noting, not
// repeating here: its case 256 returns 14 -- the same index as 128,
// so encoding a comparison against the literal 256 would have silently
// compared against 128 instead. 256 correctly maps to index 15 below.
static int valBranchImmediate(int val)
{
    switch (val)
    {
    case 1:
        return 1;
    case 2:
        return 2;
    case 3:
        return 3;
    case 4:
        return 4;
    case 5:
        return 5;
    case 6:
        return 6;
    case 7:
        return 7;
    case 8:
        return 8;
    case 10:
        return 9;
    case 12:
        return 10;
    case 16:
        return 11;
    case 32:
        return 12;
    case 64:
        return 13;
    case 128:
        return 14;
    case 256:
        return 15;
    default:
        // Unreachable if isBranchImmediate() was checked first, as every
        // call site below does.
        return 0;
    }
}

// True if `nd` (a comparatorNode's RHS operand -- always a changeTypeNode
// wrapper, see parser.cpp's parseExprConditionnal()) is nothing more than
// a bare integer literal, e.g. `5` in `x < 5` -- not `x < y` or
// `x < (y+1)`. Only that shape can become a branch-immediate: the actual
// value has to be known at compile time, and the wrapper must have
// exactly the one child parseExprConditionnal() gives a bare literal
// (parseExpr()'s own operator loop never ran, so nothing else got added).
static bool isLiteralIntOperand(NodeToken *changeTypeWrapper)
{
    return changeTypeWrapper->children_size() == 1 &&
           changeTypeWrapper->getChildPtr(0)->_nodetype == numberNode &&
           changeTypeWrapper->getChildPtr(0)->_vartype != __float__;
}

void _visitcomparatorNode(NodeToken *nd)
{
    // printf("in comparator\n");
    int numl = register_numl.get();

    if (nd->getChildPtr(0)->_nodetype != testNode)
    {
        // on va tester si on est >0;
        nd->getChildPtr(0)->visitNode();
        if (nd->_total_size > 116)
        {
            bufferText->addAfter(string_format("bnez a%d,%s_if", numl, nd->parent->getTargetText()));
            bufferText->addAfter(string_format("j %s_end", nd->parent->getTargetText()));
            bufferText->addAfter(string_format("%s_if:", nd->parent->getTargetText()));
            register_numl.increase();
        }
        else
        {
            bufferText->addAfter(string_format("beqz a%d,%s_end", numl, nd->parent->getTargetText()));
            // bufferText->addAfter(_compare.back()+1,string_format("j %s_end", nd->target.c_str()));
            // bufferText->addAfter(_compare.back()+2,string_format("%s_if:", nd->target.c_str()));
            register_numl.increase();
        }

        return;
    }
    // printf("test node\n\r");
    //  nd->getChildPtr(0)->visitNode();
    nd = nd->getChildPtr(0);
    nd->_total_size = nd->parent->_total_size;

    nd->addTargetText(nd->parent->getTargetText());
    if (nd->getChildPtr(0)->_vartype == __float__)
        nd->getChildPtr(1)->_vartype = __float__;
    if (nd->getChildPtr(1)->_vartype == __float__)
        nd->getChildPtr(0)->_vartype = __float__;
    // register_numl.duplicate();
    char *_add = NULL;
    if (nd->getChildPtr(0)->_vartype == __uint32_t__ || nd->getChildPtr(1)->_vartype == __uint32_t__)
        _add = str_concat("%s%s", _add, "u");
    else
        _add = str_concat("%s%s", _add, "");
    nd->getChildPtr(0)->visitNode();
    // register_numl.pop();

    int leftl = register_numl.get();

    // register_numl.duplicate();
    // Real _texts index of whatever nd->getChildPtr(1)->visitNode() is
    // about to emit (its movi, for the literal-int case the immediate-
    // branch path below cares about) -- captured *before* the visit,
    // via currentPos()+1 (the position the *next* addAfter() will land
    // on). This survives _visitifNode/_visitforNode later rewinding
    // bufferText's iterator to splice the condition in before an
    // already-generated body: unlike bufferText->get() (an insert
    // counter that keeps climbing across such a rewind without tracking
    // _it's real position), currentPos() always matches _it's actual
    // location (see its comment in stackfunctions.h).
    int rhsPos = bufferText->currentPos() + 1;
    nd->getChildPtr(1)->visitNode();
    // register_numl.pop();

    //////printf("compare %s %s\n",tokenNames[nd->_token->type ].c_str(),nd->_token->text.c_str());
    asmInstruction compop;
    asmInstruction compo2;
    // to compose
    int h;

    if (nd->getChildPtr(1)->_vartype == __float__)
    {

        if (nd->_total_size > 116)
        {
            switch (nd->type)
            {
            case TokenLessThan:
                compop = olts; //"olt.s"; // greater or equal
                //                                h = numl;
                // numl = leftl;
                //  leftl = h;
                //  bufferText->addAfter( string_format("%s_end:\n",nd->target.c_str()));
                compo2 = bt; //"bt";
                break;
            case TokenDoubleEqual:
                compop = oeqs; //"oeq.s"; // not equal
                compo2 = bt;   //"bt";
                break;
            case TokenNotEqual:
                compop = oeqs; // "oeq.s"; // equal
                compo2 = bf;   //"bf";
                break;
            case TokenMoreOrEqualThan:
                compop = oles; //"ole.s"; // less then
                h = numl;
                numl = leftl;
                leftl = h;
                compo2 = bt; // "bt";

                break;
            case TokenMoreThan:
                compop = olts; //"olt.s"; // not equal
                h = numl;
                numl = leftl;
                leftl = h;
                compo2 = bt; // "bt";
                break;
            case TokenLessOrEqualThan:
                compop = oles; //"ole.s"; // not equal
                compo2 = bt;   //"bt";
                break;
            default:
                compop = oles; //"ole.s"; // not equal
                compo2 = bt;   //"bt";
                break;
            }
            bufferText->addAfter(string_format(compop, numl, leftl));
            bufferText->addAfter(string_format(compo2, nd->getTargetText(), "_if"));
            bufferText->addAfter(string_format("j %s_end", nd->getTargetText()));
            bufferText->addAfter(string_format("%s_if:", nd->getTargetText()));
            register_numl.increase();
        }
        else
        {
            switch (nd->type)
            {
            case TokenLessThan:
                compop = olts; //"olt.s"; // greater or equal
                //  bufferText->addAfter( string_format("%s_end:\n",nd->target.c_str()));
                compo2 = bf; //"bf";
                break;
            case TokenDoubleEqual:
                compop = oeqs; //"oeq.s"; // not equal
                compo2 = bf;   //"bf";
                break;
            case TokenNotEqual:
                compop = oeqs; //"oeq.s"; // equal
                compo2 = bt;   //"bt";
                break;
            case TokenMoreOrEqualThan:
                compop = oles; //"ole.s"; // less then
                h = numl;
                numl = leftl;
                leftl = h;
                compo2 = bf; //"bf";
                break;
            case TokenMoreThan:
                compop = olts; //"olt.s"; // not equal
                h = numl;
                numl = leftl;
                leftl = h;
                compo2 = bf; //"bf";
                break;
            case TokenLessOrEqualThan:
                compop = oles; //"ole.s"; // not equal
                compo2 = bf;   //"bf";

                // compo2="bt";
                break;
            default:
                compop = oles; //"ole.s"; // not equal
                compo2 = bf;   //"bf";-
                break;
            }
            bufferText->addAfter(string_format(compop, numl, leftl));
            bufferText->addAfter(string_format(compo2, nd->getTargetText(), "_end"));
            // bufferText->addAfter(string_format("%s a%d,a%d,%s_end", compop.c_str(), numl, leftl, nd->getTargetText()));

            // bufferText->addAfter(_compare.back()+1,string_format("j %s_end", nd->target.c_str()));
            // bufferText->addAfter(_compare.back()+2,string_format("%s_if:", nd->target.c_str()));
            register_numl.increase();
        }
    }
    else
    {
        // Ported from upstream ESPLiveScript's isBranchImmediate()-guarded
        // codegen (see the comment on isBranchImmediate() above): if the
        // RHS is a bare literal in Xtensa's B4CONST table, fold the
        // comparison straight into a single blti/beqi/bgei/bnei instead
        // of the movi-then-register-branch pair below. `>`/`<=` go
        // through the "inverse" (val+1) eligibility check and increment
        // `f` to their `>=`/`<` equivalent first, since Xtensa has no
        // direct immediate form for strict-greater/less-or-equal.
        //
        // Each case that qualifies sets `leftl` to whichever of
        // numl/leftl the *first* (LHS) operand's register actually ended
        // up in -- necessary since which of the two held it (and
        // whether the regular, non-immediate form below needs a swap or
        // not) differs per case/block; the immediate form always reads
        // its one register operand from `leftl`.
        bool immediate = false;
        int f = 0;
        if (isLiteralIntOperand(nd->getChildPtr(1)))
        {
            f = stringToInt(nd->getChildPtr(1)->getChildPtr(0)->getText());
        }

        if (nd->_total_size > 127)
        {
            if (isLiteralIntOperand(nd->getChildPtr(1)))
            {
                switch (nd->type)
                {
                case TokenLessThan:
                    if (isBranchImmediate(f, false))
                    {
                        compop = blti;
                        leftl = numl;
                        immediate = true;
                    }
                    break;
                case TokenDoubleEqual:
                    if (isBranchImmediate(f, false))
                    {
                        compop = bnei;
                        leftl = numl;
                        immediate = true;
                    }
                    break;
                case TokenNotEqual:
                    if (isBranchImmediate(f, false))
                    {
                        compop = beqi;
                        leftl = numl;
                        immediate = true;
                    }
                    break;
                case TokenMoreOrEqualThan:
                    if (isBranchImmediate(f, false))
                    {
                        compop = bgei;
                        leftl = numl;
                        immediate = true;
                    }
                    break;
                case TokenMoreThan:
                    if (isBranchImmediate(f, true))
                    {
                        compop = bgei;
                        leftl = numl;
                        f++;
                        immediate = true;
                    }
                    break;
                case TokenLessOrEqualThan:
                    if (isBranchImmediate(f, true))
                    {
                        compop = blti;
                        leftl = numl;
                        f++;
                        immediate = true;
                    }
                    break;
                default:
                    break;
                }
            }
            if (!immediate)
            {
                switch (nd->type)
                {
                case TokenLessThan:
                    compop = blt; //"blt"; // greater or equal
                    //  bufferText->addAfter( string_format("%s_end:\n",nd->target.c_str()));
                    break;
                case TokenDoubleEqual:
                    compop = beq; //"beq"; // not equal
                    break;
                case TokenNotEqual:
                    compop = bne; //"bne"; // equal
                    break;
                case TokenMoreOrEqualThan:
                    compop = bge; //"bge"; // less then
                    break;
                case TokenMoreThan:
                    compop = blt; //"blt"; // not equal
                    h = numl;
                    numl = leftl;
                    leftl = h;
                    break;
                case TokenLessOrEqualThan:
                    compop = bge; //"bge"; // not equal
                    h = numl;
                    numl = leftl;
                    leftl = h;
                    break;
                default:
                    compop = bge;
                    break;
                }
                bufferText->addAfter(string_format(compop, _add, numl, leftl, nd->getTargetText(), "_if"));
            }
            else
            {
                // The RHS's own codegen (nd->getChildPtr(1)->visitNode()
                // above) already emitted the movi that loaded it into a
                // register the immediate form doesn't need -- blank it
                // by its captured real position (rhsPos), not
                // blankCurrent()'s hardcoded "last element of _texts":
                // _visitifNode/_visitforNode may have already rewound
                // bufferText's iterator to splice this condition in
                // before an already-generated body, in which case the
                // true last element is somewhere inside that body, not
                // the RHS's own movi.
                bufferText->replaceText(rhsPos, " ");
                bufferText->addAfter(string_format(compop, leftl, valBranchImmediate(f), nd->getTargetText(), "_if"));
            }
            bufferText->addAfter(string_format("j %s_end", nd->getTargetText()));
            bufferText->addAfter(string_format("%s_if:", nd->getTargetText()));

            register_numl.increase();
        }
        else
        {
            if (isLiteralIntOperand(nd->getChildPtr(1)))
            {
                switch (nd->type)
                {
                case TokenLessThan:
                    if (isBranchImmediate(f, false))
                    {
                        compop = bgei;
                        h = numl;
                        numl = leftl;
                        leftl = h;
                        immediate = true;
                    }
                    break;
                case TokenDoubleEqual:
                    if (isBranchImmediate(f, false))
                    {
                        compop = bnei;
                        leftl = numl;
                        immediate = true;
                    }
                    break;
                case TokenNotEqual:
                    if (isBranchImmediate(f, false))
                    {
                        compop = beqi;
                        leftl = numl;
                        immediate = true;
                    }
                    break;
                case TokenMoreOrEqualThan:
                    if (isBranchImmediate(f, false))
                    {
                        compop = blti;
                        h = numl;
                        numl = leftl;
                        leftl = h;
                        immediate = true;
                    }
                    break;
                case TokenMoreThan:
                    if (isBranchImmediate(f, true))
                    {
                        compop = blti;
                        h = numl;
                        numl = leftl;
                        leftl = h;
                        f++;
                        immediate = true;
                    }
                    break;
                case TokenLessOrEqualThan:
                    if (isBranchImmediate(f, true))
                    {
                        compop = bgei;
                        h = numl;
                        numl = leftl;
                        leftl = h;
                        f++;
                        immediate = true;
                    }
                    break;
                default:
                    break;
                }
            }
            if (!immediate)
            {
                switch (nd->type)
                {
                case TokenLessThan:
                    compop = bge; //"bge"; // greater or equal blt
                    //  bufferText->addAfter( string_format("%s_end:\n",nd->target.c_str()));
                    break;
                case TokenDoubleEqual:
                    compop = bne; //"bne"; // not equal beq
                    break;
                case TokenNotEqual:
                    compop = beq; //"beq"; // equal
                    break;
                case TokenMoreOrEqualThan:
                    compop = blt; //"blt"; // less then
                    break;
                case TokenMoreThan:
                    compop = bge; //"bge"; // not equal
                    h = numl;
                    numl = leftl;
                    leftl = h;
                    break;
                case TokenLessOrEqualThan:
                    compop = blt; //"blt"; // not equal
                    h = numl;
                    numl = leftl;
                    leftl = h;
                    break;
                default:
                    compop = bge;
                    break;
                }
                bufferText->addAfter(string_format(compop, _add, numl, leftl, nd->getTargetText(), "_end"));
                // bufferText->addAfter(_compare.back()+1,string_format("j %s_end", nd->target.c_str()));
                // bufferText->addAfter(_compare.back()+2,string_format("%s_if:", nd->target.c_str()));
               // free(_add);
            }
            else
            {
                // See the "_if" block's identical comment above -- rhsPos
                // (not blankCurrent()) is what correctly survives a prior
                // iterator rewind.
                bufferText->replaceText(rhsPos, " ");
                bufferText->addAfter(string_format(compop, leftl, valBranchImmediate(f), nd->getTargetText(), "_end"));
            }
            register_numl.increase();
        }
    }
    // printf("out comparator\n");
    if(_add!=NULL)
    free(_add);
}

void _visitcallFunctionNode(NodeToken *nd)
{
    // printf("compiling  call function %s\n", nd->getTokenText());
    // NodeToken *t = nd; // cntx.findFunction(nd->_token);
    NodeToken *func = nd;
    if (func->getChildPtr(1)->children_size() >= _TRIGGER)
    {
        /*
        //  printf("token type %d\r\n",nd->_link->getChildAtPos(0)->_token->_vartype->_varType);
        // printf("we ar ehere\n");
        if (t == NULL)
        {
            return;
        }

        // if(t->getChildAtPos(1)->children_size()<1)
        // return;point_regnum
        int save = 9; // point_regnum;
                      // bufferText->addAfterNoDouble(string_format("l32r a%d,@_stack_%s", save,nd->getTokenText())); // point_regnum
        if (func->getChildAtPos(1)->children_size() == 0 and t->type == TokenUserDefinedVariableMemberFunction)
        {
            bufferText->addAfter(bufferText->sp.pop(), string_format("mov a10,a2"));
        }
        for (int i = 0; i < func->getChildAtPos(1)->children_size(); i++)
        {
            // printf("calll \r\n");
            // isPointer = false;
            if (func->getChildAtPos(1)->getChildAtPos(i)->isPointer)
            {
                //  printf("calll p\r\n");
                // isPointer = true;
                register_numl.duplicate();
                nd->getChildAtPos(0)->getChildAtPos(i)->visitNode();
                register_numl.pop();
                int start = func->getChildAtPos(1)->getChildAtPos(i)->stack_pos - _STACK_SIZE;

                if (t->type == TokenUserDefinedVariableMemberFunction and i == 0)
                {
                    // printf("*******onr en ocoi******\r\n");
                    if (!isStructFunction)
                    {
                        bufferText->addAfter(bufferText->sp.pop(), string_format("mov a10,a%d", register_numl.get()));
                    }
                    else
                    {
                        // bufferText->pop();
                        bufferText->addAfter(bufferText->sp.pop(), string_format("mov a10,a2"));
                    }
                }
                else
                {
                    bufferText->addAfter(bufferText->sp.pop(), string_format("s32i a%d,a%d,%d", register_numl.get(), save, start)); // point_regnum
                    bufferText->addBefore(string_format("l32r a%d,@_stack_%s", save, nd->getTokenText()));                          // point_regnum
                } // isPointer=false;
            }
            else
            {

                globalType.push(func->getChildAtPos(1)->getChildAtPos(i)->getVarType()->_varType);
                register_numl.duplicate();
                // printf("in callf:");
                nd->getChildAtPos(0)->getChildAtPos(i)->visitNode();
                // printf("\n");
                register_numl.pop();
                int start = func->getChildAtPos(1)->getChildAtPos(i)->stack_pos - _STACK_SIZE + func->getChildAtPos(1)->getChildAtPos(i)->getVarType()->total_size;
                int tot = func->getChildAtPos(1)->getChildAtPos(i)->getVarType()->size - 1;
                for (int j = 0; j < func->getChildAtPos(1)->getChildAtPos(i)->getVarType()->size; j++)
                {

                    if (nd->getChildAtPos(0)->getChildAtPos(i)->getVarType() != NULL)
                    {
                        translateType(globalType.get(), nd->getChildAtPos(0)->getChildAtPos(i)->getVarType()->_varType, register_numl.get());
                        bufferText->sp.pop();
                        bufferText->sp.push(bufferText->get());
                    }
                    else
                    {
                        //   translateType(globalType.get(), nd->getChildAtPos(0)->getChildAtPos(i)->_token->_varType, register_numl.get());
                    }

                    start -= func->getChildAtPos(1)->getChildAtPos(i)->getVarType()->sizes[tot - j];
                    asmInstruction asmInstr = func->getChildAtPos(1)->getChildAtPos(i)->getVarType()->store[tot - j];
                    // bufferText->addAfter(bufferText->sp.pop(), string_format("%s %s%d,%s%d,%d", asmInstructionsName[asmInstr].c_str(), getRegType(asmInstr, 0).c_str(), register_numl.get(), getRegType(asmInstr, 1).c_str(), point_regnum, start));
                    int sav;
                    // if (j == t->getChildAtPos(1)->getChildAtPos(i)->getVarType()->size - 1)
                    if (!intest)
                    {
                        sav = bufferText->get();
                        bufferText->addAfter(bufferText->sp.pop(), string_format("%s %s%d,%s%d,%d", asmInstructionsName[asmInstr].c_str(), getRegType(asmInstr, 0).c_str(), register_numl.get(), getRegType(asmInstr, 1).c_str(), save, start));
                        if (j == func->getChildAtPos(1)->getChildAtPos(i)->getVarType()->size - 1)
                        {
                            // bufferText->sp.push(bufferText->get());

                            bufferText->addAfter(sav, string_format("l32r a%d,@_stack_%s", save, nd->getTokenText())); // point_regnum
                                                                                                                       // bufferText->addAfter(sav, string_format("l32r a%d,@_stack",save));
                        }
                    }
                    else
                    {
                        // sav = bufferText->get();
                        bufferText->addAfter(string_format("%s %s%d,%s%d,%d", asmInstructionsName[asmInstr].c_str(), getRegType(asmInstr, 0).c_str(), register_numl.get(), getRegType(asmInstr, 1).c_str(), save, start));
                        if (j == func->getChildAtPos(1)->getChildAtPos(i)->getVarType()->size - 1)
                        {
                            // bufferText->sp.push(bufferText->get());

                            bufferText->addBefore(string_format("l32r a%d,@_stack_%s", save, nd->getTokenText())); // point_regnum
                                                                                                                   // bufferText->putIteratorAtPos(bufferText->get()+1);                                                                                          // bufferText->addAfter(sav, string_format("l32r a%d,@_stack",save));
                        }
                    }
                    // start+=t->getChildAtPos(1)->getChildAtPos(i)->_token->_vartype->sizes[j];
                }
                globalType.pop();
            }
            // bufferText->addAfter(string_format("addi a%d,a1,%d",11+i,t->getChildAtPos(1)->getChildAtPos(i)->stack_pos));
        }
        // bufferText->addAfter(string_format("mov a10,a2")); // neded to find the external variables !!!!!!
        bufferText->addAfter(string_format("call8 @_%s", nd->getTokenText()));
        */
    }
    else
    {
        _visitCallFunctionTemplate(nd, 10, false);
    }
    int start = nd->stack_pos;
    // printf("ini\r\n");
    varType *v = func->getChildPtr(0)->getVarTypeObj();
    // printf("ini\r\n");
    if (v == NULL)
    {
        printf("nodeToken %d\r\n", func->getChildPtr(0)->type);
        printf("NULL\r\n");
    }

    if (v->size > 1)
    {
        //   printf("ini size\r\n");
        bufferText->addAfter(string_format("l32r a%d,@_stackr", 8)); // point_regnum
        for (int i = 0; i < v->size; i++)
        {
            // bufferText->addAfter(string_format("mov a15,a10"));
            // bufferText->addAfter(string_format("%s %s%d,%s%d,%d", v->load[i].c_str(), v->reg_name.c_str(), register_numl.get(), v->reg_name.c_str(), point_regnum, start));
            asmInstruction asmInstr = v->load[i];
            // printf("tryin to get %d %d\r\n",i,asmInstr);
            bufferText->addAfter(string_format(asmInstr, register_numl.get(), 8, start)); // point_regnum
            // register_numl--;
            start += v->sizes[i];
            // bufferText->sp.push(bufferText->get());
        }
        register_numl.decrease();
    }
    else if (v->size > 0)
    {
        if (v->_varType == __float__)
        {
            bufferText->addAfter(string_format(movs, register_numl.get(), 2));
        }
        else
        {
            bufferText->addAfter(string_format(mov, register_numl.get(), 10));
        }
        // bufferText->sp.push(bufferText->get());
        bufferText->sp.push(bufferText->get());
        register_numl.decrease();
    }
}

// Emits a bottom-tested for-loop's own condition check: branches
// *directly back to the loop body* when the condition still holds --
// one instruction per iteration in the common case -- instead of
// _visitcomparatorNode()'s top-tested "skip the body if false" shape
// (kept unchanged, and still used as-is by _visitifNode/_visitwhileNode
// and by _visitforNode()'s own fallback for anything this doesn't
// handle). Mirrors _visitcomparatorNode()'s testNode branch
// register-capture order and mnemonic-selection tables exactly (see the
// two switches below), just with the near/far roles swapped: there, the
// body is what's near (immediately following) and the loop end is
// potentially far; here, the loop end is what's immediately adjacent
// (the caller emits "%s_end:" right after this returns) and the body
// -- now the branch-back target -- is what's potentially far away.
// `test` is the testNode itself (a comparatorNode's child0, already
// confirmed non-float and one of the 6 relational token types by
// _visitforNode() *before* calling this, using only parse-time-resolved
// fields -- this function's own child visits below have real,
// one-shot side effects, same as _visitcomparatorNode()'s). `distance`
// is the already-measured byte gap from the loop body's own start back
// to here, analogous to _visitcomparatorNode()'s nd->_total_size but
// measured in the opposite direction, since this condition sits *after*
// the body in program order instead of before it.
void _visitforConditionRotated(NodeToken *test, char *bodyLabel, int distance)
{
    int numl = register_numl.get();

    if (test->getChildPtr(0)->_vartype == __float__)
        test->getChildPtr(1)->_vartype = __float__;
    if (test->getChildPtr(1)->_vartype == __float__)
        test->getChildPtr(0)->_vartype = __float__;
    char *_add = NULL;
    if (test->getChildPtr(0)->_vartype == __uint32_t__ || test->getChildPtr(1)->_vartype == __uint32_t__)
        _add = str_concat("%s%s", _add, "u");
    else
        _add = str_concat("%s%s", _add, "");

    test->getChildPtr(0)->visitNode();
    int leftl = register_numl.get();
    int rhsPos = bufferText->currentPos() + 1;
    test->getChildPtr(1)->visitNode();

    asmInstruction compop;
    int h;
    bool immediate = false;
    int f = 0;
    if (isLiteralIntOperand(test->getChildPtr(1)))
        f = stringToInt(test->getChildPtr(1)->getChildPtr(0)->getText());

    if (distance <= 127)
    {
        // Body is within short-branch range: natural sense (matches
        // _visitcomparatorNode()'s own >127/"_if" block's mnemonic
        // choices exactly), straight back to it, one instruction.
        if (isLiteralIntOperand(test->getChildPtr(1)))
        {
            switch (test->type)
            {
            case TokenLessThan:
                if (isBranchImmediate(f, false)) { compop = blti; leftl = numl; immediate = true; }
                break;
            case TokenDoubleEqual:
                if (isBranchImmediate(f, false)) { compop = bnei; leftl = numl; immediate = true; }
                break;
            case TokenNotEqual:
                if (isBranchImmediate(f, false)) { compop = beqi; leftl = numl; immediate = true; }
                break;
            case TokenMoreOrEqualThan:
                if (isBranchImmediate(f, false)) { compop = bgei; leftl = numl; immediate = true; }
                break;
            case TokenMoreThan:
                if (isBranchImmediate(f, true)) { compop = bgei; leftl = numl; f++; immediate = true; }
                break;
            case TokenLessOrEqualThan:
                if (isBranchImmediate(f, true)) { compop = blti; leftl = numl; f++; immediate = true; }
                break;
            default:
                break;
            }
        }
        if (!immediate)
        {
            switch (test->type)
            {
            case TokenLessThan:
                compop = blt;
                break;
            case TokenDoubleEqual:
                compop = beq;
                break;
            case TokenNotEqual:
                compop = bne;
                break;
            case TokenMoreOrEqualThan:
                compop = bge;
                break;
            case TokenMoreThan:
                compop = blt;
                h = numl; numl = leftl; leftl = h;
                break;
            case TokenLessOrEqualThan:
                compop = bge;
                h = numl; numl = leftl; leftl = h;
                break;
            default:
                compop = bge;
                break;
            }
            bufferText->addAfter(string_format(compop, _add, numl, leftl, bodyLabel, ""));
        }
        else
        {
            bufferText->replaceText(rhsPos, " ");
            bufferText->addAfter(string_format(compop, leftl, valBranchImmediate(f), bodyLabel, ""));
        }
    }
    else
    {
        // Body is too far for a direct short branch: inverted sense
        // (matches _visitcomparatorNode()'s own <=127/"_end" block's
        // mnemonic choices exactly) short-branches to a label right
        // here -- always in range -- when the condition is false,
        // falling through to a long-range unconditional jump back to
        // the body when it's still true.
        if (isLiteralIntOperand(test->getChildPtr(1)))
        {
            switch (test->type)
            {
            case TokenLessThan:
                if (isBranchImmediate(f, false)) { compop = bgei; h = numl; numl = leftl; leftl = h; immediate = true; }
                break;
            case TokenDoubleEqual:
                if (isBranchImmediate(f, false)) { compop = bnei; leftl = numl; immediate = true; }
                break;
            case TokenNotEqual:
                if (isBranchImmediate(f, false)) { compop = beqi; leftl = numl; immediate = true; }
                break;
            case TokenMoreOrEqualThan:
                if (isBranchImmediate(f, false)) { compop = blti; h = numl; numl = leftl; leftl = h; immediate = true; }
                break;
            case TokenMoreThan:
                if (isBranchImmediate(f, true)) { compop = blti; h = numl; numl = leftl; leftl = h; f++; immediate = true; }
                break;
            case TokenLessOrEqualThan:
                if (isBranchImmediate(f, true)) { compop = bgei; h = numl; numl = leftl; leftl = h; f++; immediate = true; }
                break;
            default:
                break;
            }
        }
        if (!immediate)
        {
            switch (test->type)
            {
            case TokenLessThan:
                compop = bge;
                break;
            case TokenDoubleEqual:
                compop = bne;
                break;
            case TokenNotEqual:
                compop = beq;
                break;
            case TokenMoreOrEqualThan:
                compop = blt;
                break;
            case TokenMoreThan:
                compop = bge;
                h = numl; numl = leftl; leftl = h;
                break;
            case TokenLessOrEqualThan:
                compop = blt;
                h = numl; numl = leftl; leftl = h;
                break;
            default:
                compop = bge;
                break;
            }
            bufferText->addAfter(string_format(compop, _add, numl, leftl, bodyLabel, "_skip"));
        }
        else
        {
            bufferText->replaceText(rhsPos, " ");
            bufferText->addAfter(string_format(compop, leftl, valBranchImmediate(f), bodyLabel, "_skip"));
        }
        bufferText->addAfter(string_format("j %s", bodyLabel));
        bufferText->addAfter(string_format("%s_skip:", bodyLabel));
    }

    if (_add != NULL)
        free(_add);
}

void _visitforNode(NodeToken *nd)
{
    // printf("ente for\n") ;
    point_regnum = 5;

    // Ported from v1 (NodeToken.h's _visitforNode): if parser.cpp's
    // for-loop parsing attached an onlyNode marker to this node (exactly
    // one distinct external array/pointer got stored into anywhere
    // within this -- possibly nested -- for-loop), resolve that array's
    // base address once, here, before the loop, instead of letting
    // _visitstoreExtGlocalVariableNode() re-resolve it on every single
    // store. onlyNode is always the *last* child when present (appended
    // after every real child parser.cpp gives a for-node), so checking
    // its type there -- not children_size() alone -- is what
    // distinguishes it from a real, already-existing 5th child (a
    // for-loop with more than one comma-separated increment expression,
    // e.g. `for(...;...;i++,j++)`) rather than colliding with that
    // unrelated, pre-existing use of a variable child count.
    int realChildren = nd->children_size();
    bool hasOnlyExternal = realChildren > 0 &&
                            nd->getChildPtr(realChildren - 1)->_nodetype == (int)onlyNode;
    if (hasOnlyExternal)
        realChildren--;

    register_numl.duplicate();
    nd->getChildPtr(0)->visitNode();
    register_numl.pop();

    if (hasOnlyExternal)
    {
        // v2's movExt (unlike v1's) already resolves straight to the
        // external variable's final value -- no separate dereference
        // needed, see _visitstoreExtGlocalVariableNode()'s own existing,
        // un-hoisted movExt emission for the same thing.
        bufferText->addAfter(string_format("movExt a7,@_ext_%s", nd->getChildPtr(realChildren)->getTargetText()));
        boolextern = true;
    }

    // Bottom-tested (loop-rotated) fast path, ported from v1: one branch
    // per iteration instead of a compare-and-fall-through plus a
    // separate unconditional jump back. Only takes it for a plain,
    // non-float, testNode-wrapped relational condition (i<96-style) --
    // by far the common shape for a for-loop -- checked here using only
    // already parse-time-resolved fields (node type, vartype), *before*
    // emitting anything, since _visitforConditionRotated()'s own child
    // visits have real, one-shot side effects there's no clean way to
    // undo if eligibility were checked mid-emission instead. Anything
    // else (float bound, or a non-relational/generic-boolean condition)
    // falls through to the original top-tested shape below, unchanged.
    bool rotated = false;
    NodeToken *test = NULL;
    if (nd->getChildPtr(1)->getChildPtr(0)->_nodetype == (int)testNode)
    {
        test = nd->getChildPtr(1)->getChildPtr(0);
        if (test->getChildPtr(0)->_vartype != __float__ && test->getChildPtr(1)->_vartype != __float__)
        {
            switch (test->type)
            {
            case TokenLessThan:
            case TokenMoreOrEqualThan:
            case TokenDoubleEqual:
            case TokenNotEqual:
            case TokenMoreThan:
            case TokenLessOrEqualThan:
                rotated = true;
                break;
            default:
                break;
            }
        }
    }

    if (rotated)
    {
        bufferText->addAfter(string_format("j test_%s", nd->getTargetText()));
        bufferText->addAfter(string_format("%s:", nd->getTargetText()));
        int bodyStart = bufferText->get();

        register_numl.duplicate();
        if (realChildren > 4)
        {
            nd->getChildPtr(3)->visitNode();
            nd->getChildPtr(4)->visitNode();
        }
        else
        {
            nd->getChildPtr(3)->visitNode();
        }
        register_numl.pop();

        bufferText->addAfter(string_format("%s_continue:", nd->getTargetText()));

        register_numl.duplicate();
        nd->getChildPtr(2)->visitNode();
        register_numl.pop();

        bufferText->addAfter(string_format("test_%s:", nd->getTargetText()));

        int distance = (bufferText->get() - bodyStart) * 3;
        register_numl.duplicate();
        _visitforConditionRotated(test, nd->getTargetText(), distance);
        register_numl.pop();

        bufferText->addAfter(string_format("%s_end:", nd->getTargetText()));
    }
    else
    {
        bufferText->addAfter(string_format("%s:", nd->getTargetText()));
        _compare.push_back(bufferText->get());

        register_numl.duplicate();
        if (realChildren > 4)
        {
            nd->getChildPtr(3)->visitNode();
            nd->getChildPtr(4)->visitNode();
        }
        else
        {
            nd->getChildPtr(3)->visitNode();
        }
        register_numl.pop();

        bufferText->addAfter(string_format("%s_continue:", nd->getTargetText()));

        register_numl.duplicate();
        nd->getChildPtr(2)->visitNode();
        register_numl.pop();

        int jumpsize = (bufferText->get() - _compare.back()) * 3;
        nd->getChildPtr(1)->_total_size = jumpsize;
        bufferText->putIteratorAtPos(_compare.back());

        register_numl.duplicate();
        nd->getChildPtr(1)->visitNode();
        register_numl.pop();

        _compare.pop_back();
        bufferText->putIteratorAtPos(bufferText->get());
        bufferText->addAfter(string_format("j %s", nd->getTargetText()));
        bufferText->addAfter(string_format("%s_end:", nd->getTargetText()));
    }

    if (hasOnlyExternal)
        boolextern = false;
    // clearNodeToken(nd);
    return;
}

void _visitargumentNode(NodeToken *nd) {}
void _visitextGlobalVariableNode(NodeToken *nd)
{
    register_numl.duplicate();
    if (nd->children_size() > 0)
    {
        register_numl.duplicate();
        nd->getChildPtr(0)->visitNode();
        register_numl.pop();
    }
    varType *v = nd->getVarTypeObj();
    int start = nd->stack_pos;
    // uint8_t regnum = 1;
    if (nd->isPointer)
    {
        // start = nd->stack_pos;
        //  regnum = point_regnum;
    }
    // string body = "";
    // register_numl++;

    if (!nd->isPointer)
    {
        bufferText->addAfter(string_format("movExt a%d,@_ext_%s", point_regnum, nd->getText()));
    }
    else
    {
        if (nd->children_size() > 0)
        {

            if (nd->children_size() > 1)
            {

                vect<char *> tile;
                int nb = 0;
                // string sd = string(nd->getTargetText());
                // if (sd.compare(0, 1, "@") == 0)
                if (strncmp(nd->getTargetText(), (char *)"@", 1) == 0)
                {

                    // tile = split(sd, " ");
                    str_split(&tile, nd->getTargetText(), (char *)" ");
                    sscanf(tile[0], "@%d", &nb);
                    // r_size = stringToInt((char *)tile[1].c_str());
                }
                if (nb > 1)
                {
                    bufferText->addAfter(string_format(movi, 10, 0));
                }

                for (int par = 0; par < nd->children_size(); par++)
                {
                    // globalType.push(__int__);
                    if (par > 0)
                    {
                        register_numl.duplicate();
                        nd->getChildPtr(par)->visitNode();
                        register_numl.pop();
                    }
                    if (nd->getChildPtr(par)->getVarTypeObj() != NULL)
                    {
                        translateType(__int__, nd->getChildPtr(par)->getVarTypeObj()->_varType, register_numl.get());
                    }
                    if (nb > 1)
                    {
                        if (par < nd->children_size() - 1)
                        {

                            for (int h = 1; h < nd->children_size() - par; h++)
                            {
                                bufferText->addAfter(string_format(movi, 11, stringToInt((char *)tile[par + 1 + h])));
                                // bufferText->addAfter(string_format("mull a11,a10,a11"));
                                bufferText->addAfter(string_format(mull, register_numl.get(), register_numl.get(), 11));
                            }
                            bufferText->addAfter(string_format(add, 10, 10, register_numl.get()));
                        }
                        else
                        {
                            bufferText->addAfter(string_format(add, register_numl.get(), 10, register_numl.get()));
                        }
                    }

                    // globalType.pop();
                }
                tile.empty();
                tile.clear();
            }
            else
            {
                translateType(__int__, nd->getChildPtr(0)->getVarTypeObj()->_varType, register_numl.get());
            }

            bufferText->addAfter(string_format("movExt a%d,@_ext_%s", point_regnum, nd->getText()));
            // f=f+number.f;
            // Same strength reduction as _visitglobalVariableNode()'s
            // identical case: scale the index in place with a single
            // self-referencing add/addx2/addx4/subx8 instead of
            // v->total_size repeated adds. 4 uses two self-doublings;
            // any other size (1, 6, 8, 10+) keeps the plain repeated-add
            // loop -- there's no movi+mull path in this function to fall
            // back to for a large size, unlike the global-variable case.
            if (v->total_size == 2 || v->total_size == 3 || v->total_size == 5 || v->total_size == 7 || v->total_size == 9)
            {
                asmInstruction scaleInstr;
                switch (v->total_size)
                {
                case 2: scaleInstr = add; break;
                case 3: scaleInstr = addx2; break;
                case 5: scaleInstr = addx4; break;
                case 7: scaleInstr = subx8; break;
                default: scaleInstr = addx8; break; // 9
                }
                bufferText->addAfter(string_format(scaleInstr, register_numl.get(), register_numl.get(), register_numl.get()));
                bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
            }
            else if (v->total_size == 4)
            {
                bufferText->addAfter(string_format(add, register_numl.get(), register_numl.get(), register_numl.get()));
                bufferText->addAfter(string_format(add, register_numl.get(), register_numl.get(), register_numl.get()));
                bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
            }
            else
            {
                for (int i = 0; i < v->total_size; i++)
                {
                    bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
                }
            }
        }
        else
        {
            bufferText->addAfter(string_format("movExt a%d,@_ext_%s", register_numl.get(), nd->getText()));
            bufferText->sp.push(bufferText->get());
            bufferText->sp.push(bufferText->get());
            register_numl.pop();
            register_numl.decrease();
            return;
        }
    }
    bufferText->sp.push(bufferText->get());
    for (int i = 0; i < v->size; i++)
    {
        // bufferText->addAfter(string_format("%s %s%d,%s%d,%d", v->load[i].c_str(), v->reg_name.c_str(), register_numl.get(), v->reg_name.c_str(), point_regnum, start));
        asmInstruction asmInstr = v->load[i];
        bufferText->addAfter(string_format(asmInstr, register_numl.get(), point_regnum, start));
        // register_numl--;
        start += v->sizes[i];
        bufferText->sp.push(bufferText->get());
    }
    // res.f = f;
    // res.header = number.header + h;
    // point_regnum++;
    register_numl.pop();
    register_numl.decrease();
    //    res.register_numl=register_numl;
    // res.register_numr=register_numr;
    return;
}

// void _visitextDefFunctionNode(NodeToken *nd){}

void _visitextCallFunctionNode(NodeToken *nd)
{

    // NodeToken *t = nd;
    _visitCallFunctionTemplate(nd, 10, true);
    int start = nd->stack_pos;
    varType *v = nd->getChildPtr(0)->getVarTypeObj();
    if (v->_varType == __float__)
    {
        bufferText->addAfter(string_format(wfr, register_numl.get(), 10));

        bufferText->sp.push(bufferText->get());
        // globalType.push(__float__);
    }
    else
    {
        if (v->size > 1)
        {
            for (int i = 0; i < v->size; i++)
            {
                // bufferText->addAfter(string_format("mov a15,a10"));
                bufferText->addAfter(string_format(extui, register_numl.get(), 10, start * 8, v->sizes[i] * 8));
                // register_numl--;
                start += v->sizes[i];
                bufferText->sp.push(bufferText->get());
            }
        }
        else
        {
            if (v->_varType != __void__)
            {
                bufferText->addAfter(string_format(mov, register_numl.get(), 10));
            }
            bufferText->sp.push(bufferText->get());
        }
    }
    register_numl.decrease();
}

void _visitreturnArgumentNode(NodeToken *nd) {}
void _visitvariableDeclarationNode(NodeToken *nd) {}
void _visitdefExtFunctionNode(NodeToken *nd)
{
    header.addAfter(string_format("@_%s:", nd->getText()));
    header.addAfter(".bytes 4");
}

void _visitinputArgumentsNode(NodeToken *nd) {}

void _visitdefInputArgumentsNode(NodeToken *nd)
{

    if (nd->children_size() < 1)
        return;
    int sav = 9;
    if (nd->children_size() >= _TRIGGER)
    { // point_regnum;
      // 17/01 // bufferText->addAfterNoDouble(string_format("l32r a%d,@_stack_%s", sav, nd->parent->getTokenText())); // point_regnum
      // bufferText->addAfterNoDouble(string_format("l32r a%d,@_stack", sav));
        for (int i = 0; i < nd->children_size(); i++)
        {
            // printf("ee\r\n");
            int start = nd->getChildPtr(i)->stack_pos;
            if (nd->getChildPtr(i)->isPointer)
            {
                // printf("ee p\r\n");
                int start = nd->getChildPtr(i)->stack_pos;
                bufferText->addAfter(string_format(l32i, 15, sav, start - _STACK_SIZE)); // point reg_bnum
                bufferText->addAfter(string_format(s32i, 15, 1, start));
            }
            else
            {
                // printf("ee j\r\n");
                for (int j = 0; j < nd->getChildPtr(i)->getVarTypeObj()->size; j++)
                {
                    asmInstruction asmInstr = nd->getChildPtr(i)->getVarTypeObj()->load[0];
                    bufferText->addAfter(string_format(asmInstr, 15, sav, start - _STACK_SIZE)); // point_regnum
                    asmInstr = nd->getChildPtr(i)->getVarTypeObj()->store[0];
                    bufferText->addAfter(string_format(asmInstr, 15, 1, start));
                    start += nd->getChildPtr(i)->getVarTypeObj()->sizes[j];
                }
            }
        }
    }
    else
    {
        int reg_num = 2;
       // int stek = ((nd->parent->stack_pos) / 8 + 1) * 8 + 16 + _STACK_SIZE;
int stek = ((nd->parent->stack_pos) / 8 + 1) * 8 + 16;
        for (int i = 0; i < nd->children_size(); i++)
        {
            int start = nd->getChildPtr(i)->stack_pos;
            if (start >= _STACK_SIZE)
            {
                // printf("ee\r\n");
                if (reg_num <= 7)
                {

                    // if (nd->getChildPtr(i)->isPointer)
                    //{
                    // printf("ee p\r\n");
                    //  int start = nd->getChildPtr(i)->stack_pos;
                    // bufferText->addAfter(string_format("l32i a15,a%d,%d", sav, start - _STACK_SIZE)); // point reg_bnum
                    //
                    if (nd->getChildPtr(i)->isPointer)
                    {
                        bufferText->addAfter(string_format("s32i a%d,a1,%d", reg_num, start));
                    }
                    else if (nd->getChildPtr(i)->getVarTypeObj()->_varType == __float__)
                    {
                        bufferText->addAfter(string_format("s32i a%d,a1,%d", reg_num, start));
                        // bufferText->addAfter(string_format("%s %s15,%s1,%d", asmInstructionsName[asmInstr].c_str(), getRegType(asmInstr, 0).c_str(), getRegType(asmInstr, 1).c_str(), start));
                    }
                    else
                    {
                        asmInstruction asmInstr = nd->getChildPtr(i)->getVarTypeObj()->store[0];
                        bufferText->addAfter(string_format(asmInstr, reg_num, 1, start));
                    }
                    // reg_num++;
                }
                else
                {
                    asmInstruction asmInstr = nd->getChildPtr(i)->getVarTypeObj()->load[0];
                    bufferText->addAfter(string_format(asmInstr, 15, 1, stek + start - nd->getChildPtr(6)->stack_pos));
                    asmInstr = nd->getChildPtr(i)->getVarTypeObj()->store[0];
                    bufferText->addAfter(string_format(asmInstr, 15, 1, start));
                }
            }
            else if (nd->getChildPtr(i)->getVarTypeObj()->_varType == __float__)
            {
            }
            reg_num++;
        }
    }
}

void _visitdefExtGlobalVariableNode(NodeToken *nd)
{
    if (safeMode)
    {
        if (nd->isPointer)
        {
            header.addAfter(string_format("@_size_%s:", nd->getText()));
            header.addAfter(_numToBytes(nd->_total_size / nd->getVarTypeObj()->total_size));
        }
    }
    header.addAfter(string_format("@_ext_%s:", nd->getText()));
    header.addAfter(".bytes 4");
}

// Emits a pseudo-instruction the assembler (asm_parser.cpp's parseline()/
// createBinaryHeader()) recognizes specially, matching the .bytes/.global
// pattern already used for external declarations: the json path, a
// reference to the target variable's own label (declared normally --
// parser.cpp's json-binding branch only records this metadata, then
// rewinds so the ordinary variable-declaration path runs too, giving the
// variable real storage and a @_name label the same as any other global),
// and its type. The assembler resolves that label to a real address once
// everything is laid out and emits a type-5 relocation header entry,
// which the loader (asm_execute.cpp, __JSON_OPTION__-guarded) uses to
// populate the variable from a JSON document at execution time.
void _visitjsonBindingNode(NodeToken *nd)
{
    header.addAfter(string_format(".json %s @_%s %d", nd->getText(), nd->getTargetText(), nd->_vartype));
}

void _visitdefGlobalVariableNode(NodeToken *nd)
{
    if (strcmp(nd->getText(), _handle_) == 0)
        return;
    if (safeMode)
    {
        if (nd->isPointer)
        {
            header.addAfter(string_format("@_size_%s:", nd->getText()));
            header.addAfter(_numToBytes(nd->_total_size / nd->getVarTypeObj()->total_size));
        }
    }
    header.addAfter(string_format("@_%s:", nd->getText()));
    if (nd->children_size() == 0)
    {
        header.addAfter(string_format(bytes, nd->_total_size));
    }
    else
    {
        char *_data_sav = NULL;
        if (nd->getVarTypeObj()->_varType == __CRGB__ or nd->getVarTypeObj()->_varType == __CRGBW__)
        {
            if (nd->children_size() > 0)
            {
                // for (NodeToken *ndt : *nd->children)
                for (int ndindex = 0; ndindex < nd->children_size(); ndindex++)
                {
                    NodeToken *ndt = nd->getChildPtr(ndindex);
                    if (ndt->children_size())
                    {
                        // for (NodeToken *ndtc : *ndt->children)
                        for (int ndtindex = 0; ndtindex < ndt->children_size(); ndtindex++)
                        {
                            NodeToken *ndtc = ndt->getChildPtr(ndtindex);
                            int __num = 0;
                            sscanf(ndtc->getText(), "%d", &__num);
                            _data_sav = str_concat("%s %02x", _data_sav, __num);
                        }
                    }
                }
            }
        }
        else if (nd->getVarTypeObj()->_varType == __char__)
        {
            char *str = nd->getChildPtr(0)->getText();
            nd->_total_size = strlen(str) - 1; // we remove the size taken by " " adn we add 0 fo \0
            for (int i = 1; i < strlen(str) - 1; i++)
            {
                _data_sav = str_concat("%s %02x", _data_sav, str[i]);
            }
            _data_sav = str_concat("%s 00", _data_sav);
        }
        else
        {
            uint32_t __num;
            __num = 0;
            uint8_t c;
            if (nd->children_size() > 0)
            {
                // for (NodeToken *ndt : *nd->children)
                // {
                for (int ndindex = 0; ndindex < nd->children_size(); ndindex++)
                {
                    NodeToken *ndt = nd->getChildPtr(ndindex);
                    if (nd->getVarTypeObj()->_varType == __float__)
                    {
                        float __f = 0;
                        sscanf(ndt->getText(), "%f", &__f);
                        __num = (uint32_t)(*((uint32_t *)&__f));
                    }
                    else
                    {
                        __num = 0;
                        sscanf(ndt->getText(), "%u", &__num);
                    }
                    for (int i = 0; i < nd->getVarTypeObj()->total_size; i++)
                    {
                        c = __num & 0xff;
                        _data_sav = str_concat("%s %02x", _data_sav, c);
                        //_data_sav = _data_sav + " " + string_format("%02x", c);
                        // val=val+'A';
                        __num = __num / 256;
                    }
                }
            }
        }
        if (_data_sav != NULL)
        {
            header.addAfter(string_format(".bytes %d%s", nd->_total_size, _data_sav));
            free(_data_sav);
        }
        else
            header.addAfter(string_format(".bytes %d", nd->_total_size));
    }
}
void _visitstoreLocalVariableNode(NodeToken *nd)
{
    // //printf("jjjkkj\n");
    varType *v = nd->getVarTypeObj();
    // globalType.push(__float__);//v->_varType);
    // //printf("ona stocké:%d: %s\n",globalType.get(),varTypeEnumNames[globalType.get()].c_str());
    uint8_t regnum = 1;
    if (nd->asPointer or (nd->isPointer && nd->children_size() == 0))
        point_regnum++;
    if (nd->isPointer)
    {
        // start = nd->stack_pos;
        regnum = point_regnum;
    }
    int start = nd->stack_pos;
    if (nd->isPointer)
    {
        start = 0;
    }
    int numl = register_numl.get();
    for (int h = 0; h < v->size - 1; h++)
    {
        start += v->sizes[h];
    }
    if (nd->children_size() > 0 or !nd->isPointer or nd->asPointer)
    {
        for (int i = v->size - 1; i >= 0; i--)
        {
            // //printf("jjjkkj: %d\n",i);
            // list<string>::iterator l=bufferText->sp.pop();
            // bufferText->addAfter(  bufferText->sp.pop(),  string_format("%s %s%d,%s%d,%d", v->store[i].c_str(), v->reg_name.c_str(), numl, v->reg_name.c_str(), regnum, start));
            bufferText->addAfter(bufferText->sp.pop(), string_format(v->store[i], numl, regnum, start));
            // numl++;
            start -= v->sizes[i];
        }
    }
    else
    {
        bufferText->addAfter(bufferText->sp.pop(), string_format(s32i, numl, regnum, 0)); // start
        // bufferText->addAfter(bufferText->sp.pop(), string_format("s32i a%d,a%d,%d", numl, 1,nd->stack_pos));
    }
    if (nd->isPointer)
    {
        register_numl.duplicate();
        bufferText->sp.push(bufferText->get());
        bufferText->sp.swap();
        bufferText->putIteratorAtPos(bufferText->sp.get());
        // bufferText->sp.displaystack("PILE");
        if (nd->children_size() > 0)
        {
            globalType.push(__int__);
            register_numl.duplicate();
            nd->getChildPtr(0)->visitNode();
            register_numl.pop();
            globalType.pop();
        }
        if (nd->children_size() > 0)
        {
            bufferText->addAfter(string_format(l32i, point_regnum, 1, nd->stack_pos));
            // Same strength reduction as _visitglobalVariableNode()'s
            // identical case: scale the index in place with a single
            // self-referencing add/addx2/addx4/subx8 instead of
            // v->total_size repeated adds. 4 uses two self-doublings;
            // any other size (1, 6, 8, 10+) keeps the plain repeated-add
            // loop -- no movi+mull path exists in this function to fall
            // back to for a large size.
            if (v->total_size == 2 || v->total_size == 3 || v->total_size == 5 || v->total_size == 7 || v->total_size == 9)
            {
                asmInstruction scaleInstr;
                switch (v->total_size)
                {
                case 2: scaleInstr = add; break;
                case 3: scaleInstr = addx2; break;
                case 5: scaleInstr = addx4; break;
                case 7: scaleInstr = subx8; break;
                default: scaleInstr = addx8; break; // 9
                }
                bufferText->addAfter(string_format(scaleInstr, register_numl.get(), register_numl.get(), register_numl.get()));
                bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
            }
            else if (v->total_size == 4)
            {
                bufferText->addAfter(string_format(add, register_numl.get(), register_numl.get(), register_numl.get()));
                bufferText->addAfter(string_format(add, register_numl.get(), register_numl.get(), register_numl.get()));
                bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
            }
            else
            {
                for (int i = 0; i < v->total_size; i++)
                {
                    bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
                }
            }
        }
        else if (nd->asPointer)
        {
            if (nd->type == TokenUserDefinedVariableMember or nd->type == TokenUserDefinedVariableMemberFunction)
            {

                if (isStructFunction)
                {
                    bufferText->addAfter(string_format(addi, point_regnum, 2, (int)(nd->stack_pos / 1000)));
                }
                else
                {
                    bufferText->addAfter(string_format(l32i, point_regnum, 1, nd->stack_pos - (int)(nd->stack_pos / 1000) * 1000));
                    bufferText->addAfter(string_format(addi, point_regnum, point_regnum, (int)(nd->stack_pos / 1000)));
                }
            }
            else

                bufferText->addAfter(string_format(l32i, point_regnum, 1, nd->stack_pos));
        }
        else
        {
            // a remettre
            bufferText->addAfter(string_format(addi, point_regnum, 1, nd->stack_pos));
        }
        bufferText->end();
        bufferText->sp.pop();
        // point_regnum++;
        register_numl.pop();
        // point_regnum--;
        if (nd->asPointer or (nd->isPointer && nd->children_size() == 0))
            point_regnum--;
    }
}

void _visitstoreLocalVariableNodeAsRegister(NodeToken *nd)
{
    if (nd->getVarTypeObj()->_varType == __float__)
    {
        bufferText->addAfter(bufferText->sp.pop(), string_format(rfr, nd->target, register_numl.get()));
    }
    else
        bufferText->addAfter(bufferText->sp.pop(), string_format(mov, nd->target, register_numl.get()));
}

void _visitdefLocalVariableNode(NodeToken *nd) {}
void _visitstoreGlobalVariableNode(NodeToken *nd)
{

    register_numl.duplicate();
    varType *v = nd->getVarTypeObj();
    int start = nd->stack_pos;
    // uint8_t regnum = 1;
    // int savreg_num=point_regnum;
    if (nd->asPointer or (nd->isPointer)) // && nd->children_size() == 0))
        point_regnum++;
    int savreg_num = point_regnum;
    point_regnum = 6; // YBA 25-02-20252
    if (nd->isPointer)
    {
        // start = nd->stack_pos;
        // regnum = point_regnum;
    }
    // string body = "";
    //  register_numl++;
    for (int h = 0; h < v->size - 1; h++)
    {
        start += v->sizes[h];
    }
    int j = bufferText->sp.get();
    if ((nd->children_size() > 0 or !nd->isPointer))
    {
        //  if (nd->target == EOF_TEXTARRAY)
        // {

        for (int i = v->size - 1; i >= 0; i--)
        {
            bufferText->addAfter(bufferText->sp.pop(), string_format(v->store[i], register_numl.get(), point_regnum, start));

            start -= v->sizes[i - 1];
        }
    }

    else
    {
        bufferText->addAfter(bufferText->sp.pop(), string_format(s32i, register_numl.get(), point_regnum, start));
    }

    bufferText->sp.push(bufferText->get());
    bufferText->sp.swap();
    if (v->size > 0)
        bufferText->putIteratorAtPos(bufferText->sp.get());
    else
        bufferText->putIteratorAtPos(j);
    if (nd->children_size() > 0)
    {
        vect<char *> tile;
        int nb = 0;
        // string sd = string(nd->getTargetText());
        // if (sd.compare(0, 1, "@") == 0)
        if (strncmp(nd->getTargetText(), (char *)"@", 1) == 0)
        {
            str_split(&tile, nd->getTargetText(), (char *)" ");
            sscanf(tile[0], "@%d", &nb);
        }
        if (nb > 1)
        {
            bufferText->addAfter(string_format(movi, 10, 0));
        }
        for (int i = 0; i < nd->children_size(); i++)
        {
            globalType.push(__int__);
            register_numl.duplicate();
            nd->getChildPtr(i)->visitNode();
            register_numl.pop();
            translateType(__int__, nd->getChildPtr(i)->getVarTypeObj()->_varType, register_numl.get());
            if (nb > 1)
            {
                if (i < nd->children_size() - 1)
                {

                    for (int h = 1; h < nd->children_size() - i; h++)
                    {
                        // bufferText->addAfter(string_format("movi a11,%d", stringToInt((char *)tile[i + 1 + h].c_str())));
                        bufferText->addAfter(string_format(movi, 11, stringToInt(tile[i + 1 + h])));

                        // bufferText->addAfter(string_format("mull a%d,a%d,a11", register_numl.get(), register_numl.get()));
                        bufferText->addAfter(string_format(mull, register_numl.get(), register_numl.get(), 11));
                    }
                    // bufferText->addAfter(string_format("add a10,a10,a%d", register_numl.get()));
                    bufferText->addAfter(string_format(add, 10, 10, register_numl.get()));
                }
                else
                {
                    // bufferText->addAfter(string_format("add a%d,a10,a%d", register_numl.get(), register_numl.get()));
                    bufferText->addAfter(string_format(add, register_numl.get(), 10, register_numl.get()));
                }
            }

            globalType.pop();
        }
        tile.empty();
        tile.clear();
    }

    if (safeMode && nd->isPointer)
    {
        bufferText->addAfter(string_format("l32r a%d,@_size_%s", point_regnum, nd->getText()));
        bufferText->addAfter(string_format(l32i, point_regnum, point_regnum, 0));
        bufferText->addAfter(string_format("bge a%d,a%d,__test_safe_%d", point_regnum, register_numl.get(), for_if_num2));
        bufferText->addAfter(string_format(movi, 10, 0));
        bufferText->addAfter(string_format(mov, 11, point_regnum));
        bufferText->addAfter(string_format(mov, 12, register_numl.get()));
        bufferText->addAfter("callExt a8,error");
        bufferText->addAfter("retw.n");
        bufferText->addAfter(string_format("__test_safe_%d:", for_if_num2));
        for_if_num2++;
    }

    if (nd->isPointer && nd->children_size() > 0)
    {

        if (nd->type == TokenUserDefinedVariableMember or nd->type == TokenUserDefinedVariableMemberFunction)
        {
            bufferText->addAfter(string_format(movi, point_regnum, nd->_total_size));
            bufferText->addAfter(string_format(mull, register_numl.get(), register_numl.get(), point_regnum));
            bufferText->addAfter(string_format("l32r a%d,@_%s", point_regnum, nd->getText()));
            bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
        }
        else if (v->total_size == 2 || v->total_size == 3 || v->total_size == 5 || v->total_size == 7 || v->total_size == 9)
        {
            // See _visitglobalVariableNode()'s identical case for the
            // rationale -- one fewer instruction than movi+mull, via a
            // single self-referencing add/addx2/addx4/subx8.
            asmInstruction scaleInstr;
            switch (v->total_size)
            {
            case 2: scaleInstr = add; break;
            case 3: scaleInstr = addx2; break;
            case 5: scaleInstr = addx4; break;
            case 7: scaleInstr = subx8; break;
            default: scaleInstr = addx8; break; // 9
            }
            bufferText->addAfter(string_format(scaleInstr, register_numl.get(), register_numl.get(), register_numl.get()));
            bufferText->addAfter(string_format("l32r a%d,@_%s", point_regnum, nd->getText()));
            bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
        }
        else if (v->total_size == 4)
        {
            // ar*4 via two self-doublings -- see
            // _visitglobalVariableNode()'s identical case.
            bufferText->addAfter(string_format(add, register_numl.get(), register_numl.get(), register_numl.get()));
            bufferText->addAfter(string_format(add, register_numl.get(), register_numl.get(), register_numl.get()));
            bufferText->addAfter(string_format("l32r a%d,@_%s", point_regnum, nd->getText()));
            bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
        }
        else if (v->total_size > 4)
        {

            // string tmp=content.l->back();
            // content.l->pop_back();
            bufferText->addAfter(string_format(movi, point_regnum, v->total_size));
            bufferText->addAfter(string_format(mull, register_numl.get(), register_numl.get(), point_regnum));
            bufferText->addAfter(string_format("l32r a%d,@_%s", point_regnum, nd->getText()));
            bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
        }
        else
        {
            bufferText->addAfter(string_format("l32r a%d,@_%s", point_regnum, nd->getText()));
            for (int i = 0; i < v->total_size; i++)
            {
                bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
            }
        }
    }
    else
    {
        bufferText->addAfter(string_format("l32r a%d,@_%s", point_regnum, nd->getText()));
    }
    bufferText->end();
    if (isStructFunction)
    {
        bufferText->addAfter(string_format(s32i, 2, 1, _STACK_SIZE));
    }
    bufferText->sp.pop();

    register_numl.pop();
    point_regnum = savreg_num;
    if (nd->asPointer or (nd->isPointer))
        point_regnum--;

    return;
}

void _visitstoreExtGlocalVariableNode(NodeToken *nd)
{

    register_numl.duplicate();
    varType *v = nd->getVarTypeObj();
    int start = nd->stack_pos;
    uint8_t regnum = 1;
    if (nd->isPointer)
    {
        // start = nd->stack_pos;
        regnum = point_regnum;
    }
    // string body = "";
    int savreg_num = point_regnum;
    point_regnum = 6; // YBA 25-02-2025
    // register_numl++;
    for (int h = 0; h < v->size - 1; h++)
    {
        start += v->sizes[h];
    }
    if (nd->children_size() > 0 or !nd->isPointer or nd->asPointer)
    {
        for (int i = v->size - 1; i >= 0; i--)
        {
            bufferText->addAfter(bufferText->sp.pop(), string_format(v->store[i], register_numl.get(), point_regnum, start));
            // register_numl--;
            start -= v->sizes[i];
            // bufferText->sp.push(bufferText->get());
        }
    }
    else
    {
        bufferText->addAfter(bufferText->sp.pop(), string_format(s32i, register_numl.get(), point_regnum, start));
    }
    // res.f = f;
    // res.header = number.header + h;
    // bufferText->sp.push(bufferText->get());
    // bufferText->sp.swap();
    // bufferText->putIteratorAtPos(bufferText->sp.get());
    // bufferText->sp.displaystack("PILE");
    // bufferText->addAfter(bufferText->sp.l->front(),"");
    bufferText->putIteratorAtPos(bufferText->sp.front());
    if (nd->children_size() == 1)
    {
        register_numl.duplicate();
        nd->getChildPtr(0)->visitNode();
        register_numl.pop();
        if (nd->getChildPtr(0)->getVarTypeObj() != NULL)
        {
            translateType(__int__, nd->getChildPtr(0)->getVarTypeObj()->_varType, register_numl.get());
        }
        else
        {
            // translateType(__int__, nd->getChildPtr(0)->_token->_varType, register_numl.get());
        }
    }
    else if (nd->children_size() > 1)
    {

        vect<char *> tile;
        int nb = 0;
        // string sd = string(nd->getTargetText());
        // if (sd.compare(0, 1, "@") == 0)
        if (strncmp(nd->getTargetText(), (char *)"@", 1) == 0)
        {

            // tile = split(sd, " ");
            str_split(&tile, nd->getTargetText(), (char *)" ");
            sscanf(tile[0], "@%d", &nb);
            // r_size = stringToInt((char *)tile[1].c_str());
        }
        if (nb > 1)
        {
            bufferText->addAfter(string_format(movi, 10, 0));
        }
        for (int par = 0; par < nd->children_size(); par++)
        {
            // globalType.push(__int__);
            register_numl.duplicate();
            nd->getChildPtr(par)->visitNode();
            register_numl.pop();
            translateType(__int__, nd->getChildPtr(par)->getVarTypeObj()->_varType, register_numl.get());
            if (nb > 1)
            {
                if (par < nd->children_size() - 1)
                {

                    for (int h = 1; h < nd->children_size() - par; h++)
                    {
                        bufferText->addAfter(string_format(movi, 11, stringToInt((char *)tile[par + 1 + h])));
                        // bufferText->addAfter(string_format("mull a11,a10,a11"));
                        bufferText->addAfter(string_format(mull, register_numl.get(), register_numl.get(), 11));
                    }
                    bufferText->addAfter(string_format(add, 10, 10, register_numl.get()));
                }
                else
                {
                    bufferText->addAfter(string_format(add, register_numl.get(), 10, register_numl.get()));
                }
            }

            // globalType.pop();
        }
        tile.empty();
        tile.clear();
    }

    if (safeMode && nd->isPointer)
    {
        bufferText->addAfter(string_format("l32r a%d,@_size_%s", regnum, nd->getText()));
        bufferText->addAfter(string_format(l32i, regnum, regnum, 0));
        bufferText->addAfter(string_format("bge a%d,a%d,__test_safe_%d", regnum, register_numl.get(), for_if_num2));
        bufferText->addAfter(string_format(movi, 10, 0)); // nd->_token->line
        bufferText->addAfter(string_format(mov, 11, regnum));
        bufferText->addAfter(string_format("mov a12,a%d", register_numl.get()));
        bufferText->addAfter("callExt a8,error");
        bufferText->addAfter("retw.n");
        bufferText->addAfter(string_format("__test_safe_%d:", for_if_num2));
        for_if_num2++;
    }
    // boolextern (see _visitforNode()): if the enclosing for-loop already
    // resolved this exact array's base address into a7 once, before the
    // loop, just copy it -- nbofextern's parse-time count already
    // guarantees a store reached while boolextern is true can only ever
    // be to that one hoisted array, no name check needed here either
    // (matching v1's identical, equally unconditional `if (!boolextern)`).
    if (!boolextern)
    {
        bufferText->addAfter(string_format("movExt a%d,@_ext_%s",
                                           point_regnum, nd->getText()));
    }
    else
    {
        bufferText->addAfter(string_format(mov, point_regnum, 7));
    }
    if (nd->isPointer && nd->children_size() > 0)
    {
        // f=f+number.f;
        // Same strength reduction as _visitglobalVariableNode()/
        // _visitextGlobalVariableNode()'s identical case: scale the
        // index in place with a single self-referencing add/addx2/
        // addx4/subx8 instead of v->total_size repeated adds. 4 uses
        // two self-doublings; any other size (1, 6, 8, 10+) keeps the
        // plain repeated-add loop -- no movi+mull path exists in this
        // function to fall back to for a large size.
        if (v->total_size == 2 || v->total_size == 3 || v->total_size == 5 || v->total_size == 7 || v->total_size == 9)
        {
            asmInstruction scaleInstr;
            switch (v->total_size)
            {
            case 2: scaleInstr = add; break;
            case 3: scaleInstr = addx2; break;
            case 5: scaleInstr = addx4; break;
            case 7: scaleInstr = subx8; break;
            default: scaleInstr = addx8; break; // 9
            }
            bufferText->addAfter(string_format(scaleInstr, register_numl.get(), register_numl.get(), register_numl.get()));
            bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
        }
        else if (v->total_size == 4)
        {
            bufferText->addAfter(string_format(add, register_numl.get(), register_numl.get(), register_numl.get()));
            bufferText->addAfter(string_format(add, register_numl.get(), register_numl.get(), register_numl.get()));
            bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
        }
        else
        {
            for (int i = 0; i < v->total_size; i++)
            {
                bufferText->addAfter(string_format(add, point_regnum, point_regnum, register_numl.get()));
            }
        }
    }
    bufferText->end();
    bufferText->sp.pop();
    //;
    register_numl.pop();
    point_regnum = savreg_num;
    // point_regnum--;
    //    res.register_numl=register_numl;
    // res.register_numr=register_numr;
    return;
}

void _visitifNode(NodeToken *nd)
{
    // printf("oo\n");
    point_regnum = 5;
    _compare.push_back(bufferText->get());
    // printf("oo1\n");
    //  bufferText->addAfter(  string_format("%s:\n",nd->target.c_str()));

    register_numl.duplicate();
    // printf("oo2bis\n");

    if (nd->children_size() > 2)
    {
        nd->getChildPtr(1)->visitNode();
        nd->getChildPtr(2)->visitNode();
    }
    else
    {
        nd->getChildPtr(1)->visitNode();
    }
    register_numl.pop();
    // printf("oo2\n");

    nd->getChildPtr(0)->_total_size = (bufferText->get() - _compare.back()) * 3; // on estime que toutes les instructions ont la meme taille
    bufferText->addAfter(string_format("%s_end:", nd->getTargetText()));
    // printf("oo3\n");
    bufferText->putIteratorAtPos(_compare.back());
    // bufferText->addAfter("");
    // bufferText->sp.push(bufferText->get());
    //  printf("oo4\n");
    register_numl.duplicate();
    intest = true;
    nd->getChildPtr(0)->visitNode();
    intest = false;
    register_numl.pop();
    // printf("oo5n");
    bufferText->putIteratorAtPos(bufferText->get());
    _compare.pop_back();
}

void _visitelseNode(NodeToken *nd)
{
    point_regnum = 5;
    bufferText->addBefore(string_format("j %s", nd->getTargetText()));
    // register_numl.duplicate();

    register_numl.duplicate();
    if (nd->children_size() > 1)
    {
        nd->getChildPtr(1)->visitNode();
    }
    else
    {
        nd->getChildPtr(0)->visitNode();
    }
    register_numl.pop();

    bufferText->addAfter(string_format("%s:", nd->getTargetText()));
}

void _visitwhileNode(NodeToken *nd)
{
    point_regnum = 5;
    bufferText->addAfter(string_format("%s_while:", nd->getTargetText()));
    bufferText->addAfter(string_format("%s_continue:", nd->getTargetText()));
    _compare.push_back(bufferText->get());

    // bufferText->addAfter(  string_format("%s:\n",nd->target.c_str()));

    register_numl.duplicate();
    if (nd->children_size() > 2)
    {
        nd->getChildPtr(1)->visitNode();
        nd->getChildPtr(2)->visitNode();
    }
    else
    {
        nd->getChildPtr(1)->visitNode();
    }
    register_numl.pop();

    bufferText->addAfter(string_format("j %s_while", nd->getTargetText()));
    nd->getChildPtr(0)->_total_size = (bufferText->get() - _compare.back()) * 3; // on estime que toutes les instructions ont la meme taille

    bufferText->addAfter(string_format("%s_end:", nd->getTargetText()));
    if (nd->getChildPtr(0)->getChildPtr(0)->_nodetype != numberNode)
    {
        bufferText->putIteratorAtPos(_compare.back());
        intest = true;
        nd->getChildPtr(0)->visitNode();
        intest = false;
        register_numl.pop();
        bufferText->putIteratorAtPos(bufferText->get());
    }
    _compare.pop_back();
}

void _visitreturnNode(NodeToken *nd)
{
    NodeToken *t = nd;
    while (t->_nodetype != defFunctionNode and t->parent != NULL)
    {
        t = t->parent;
    }
    t = t->getChildPtr(0);
    if (t->getVarTypeObj()->size > 1)
    {
        bufferText->addAfter(string_format("l32r a%d,@_stackr", 9)); // point_regnum
        for (int i = 0; i < nd->children_size(); i++)
        {
            globalType.push(t->getVarTypeObj()->_varType);
            register_numl.duplicate();
            nd->getChildPtr(i)->visitNode();
            register_numl.pop();
            int start = nd->stack_pos + t->getVarTypeObj()->total_size;
            int tot = t->getVarTypeObj()->size - 1;
            // bufferText->addBefore(bufferText->sp.get(),string_format("l32r a%d,@_stackr", 8));
            for (int j = 0; j < t->getVarTypeObj()->size; j++)
            {
                start -= t->getVarTypeObj()->sizes[tot - j];
                asmInstruction asmInstr = t->getVarTypeObj()->store[tot - j];
                bufferText->addAfter(bufferText->sp.pop(), string_format(asmInstr, register_numl.get(), 9, start)); // point_regnum
            }
            globalType.pop();
        }
    }
    else
    {
        for (int i = 0; i < nd->children_size(); i++)
        {
            globalType.push(t->getVarTypeObj()->_varType);
            register_numl.duplicate();
            nd->getChildPtr(i)->visitNode();
            register_numl.pop();
            translateType(t->getVarTypeObj()->_varType, nd->getChildPtr(i)->getVarTypeObj()->_varType, register_numl.get());
            // int start = nd->stack_pos + t->getVarTypeObj()->total_size;
            // int tot = t->getVarTypeObj()->size - 1;
            if (t->getVarTypeObj()->_varType == __float__)
            {
                // bufferText->addAfter(bufferText->sp.pop(), string_format("mov.s f2,f%d", register_numl.get()));
                bufferText->addAfter(string_format(movs, 2, register_numl.get()));
            }
            else
            {
                // bufferText->addAfter(bufferText->sp.pop(), string_format("mov a2,a%d", register_numl.get()));
                bufferText->addAfter(string_format(mov, 2, register_numl.get()));
            }

            globalType.pop();
        }
    }
    if (saveReg)
    {
        bufferText->addAfter(string_format(lsi, 15, 1, 16));
        bufferText->addAfter(string_format(lsi, 15, 1, 20));
        bufferText->addAfter(string_format(lsi, 13, 1, 24));
    }
    if (saveRegAbs)
    {
        bufferText->addAfter(string_format(l32i, 15, 1, 16));
        bufferText->addAfter(string_format(l32i, 14, 1, 20));
        bufferText->addAfter(string_format(l32i, 13, 1, 24));
    }
    bufferText->addAfter("retw.n");
}

void _visitdefAsmFunctionNode(NodeToken *nd)
{
    bufferText = &content;
    header.addAfter(string_format(".global @_%s", nd->getText()));
    bufferText->addAfter(string_format("@_%s:", nd->getText()));
    header.addAfter(string_format("@_stack_%s:", nd->getText()));
    header.addAfter(string_format(".bytes %d", (nd->getChildPtr(1)->children_size() + 1) * 4));

    // bufferText->addAfter(string_format("entry a1,%d", 80)); // ((nd->stack_pos) / 8 + 1) * 8)
    if (nd->children_size() >= 3)
    {
        nd->getChildPtr(2)->visitNode();
    } // f = f + g.f;
      // h = h + g.header;
    bufferText = &footer;
}

void _visitstringNode(NodeToken *nd)
{
    // Strips the surrounding quotes into a *new* buffer rather than
    // mutating nd->getText() in place: all_text's Text::addText() (see
    // stackfunctions.cpp) interns/deduplicates identical string literals,
    // so two distinct string-literal nodes with the same text (extremely
    // common inside __ASM__ function bodies -- e.g. "retw.n" or
    // "entry a1,32" repeated verbatim across several __ASM__ functions in
    // the same script) share the exact same underlying char*. The old
    // in-place memmove()/truncate corrupted that shared buffer a little
    // more on every subsequent visit -- the 2nd occurrence of a repeated
    // line lost its first char, the 3rd lost two, etc., producing
    // "Opcode ntry not found" style assembler errors for scripts with
    // more than one __ASM__ function sharing any instruction text (e.g.
    // squaresani.sc's setTime()/millis()/elapseMillis(), which all start
    // with "entry a1,32" and end with "retw.n").
    char *target = nd->getText();
    int size = strlen(target) - 2;
    bufferText->addAfter(string_format("%.*s", size, target + 1));
}

void _visitchangeTypeNode(NodeToken *nd)
{

    globalType.push(nd->getVarTypeObj()->_varType);
    for (int i = 0; i < nd->children_size(); i++)
    {

        register_numl.duplicate();
        nd->getChildPtr(i)->visitNode();

        register_numl.pop();
        // f = f + g.f;
        // h = h + g.header;

        if (nd->getChildPtr(i)->getVarTypeObj() != NULL)
        {
            // register_numl.pop();
            if (strlen(nd->getChildPtr(i)->getText()) > 0)
                translateType(globalType.get(), nd->getChildPtr(i)->getVarTypeObj()->_varType, register_numl.get());
            else
                translateType(globalType.get(), nd->getChildPtr(i)->getVarTypeObj()->_varType, register_numl.get());
        }
        else
        {
            // translateType(globalType.get(), nd->getChildPtr(i)->_token->_varType, register_numl.get());
        }
        register_numl.decrease();
    }
    // varTypeEnum s = globalType.pop();
    globalType.pop();
}

void _visitimportNode(NodeToken *nd) {}
void _visitcontinueNode(NodeToken *nd)
{
    bufferText->addAfter(string_format("j %s_continue", nd->getTargetText()));
}
void _visitbreakNode(NodeToken *nd)
{

    bufferText->addAfter(string_format("j %s_end", nd->getTargetText()));
}
void _visittestNode(NodeToken *nd)
{

    int numl = register_numl.get();

    if (nd->getChildPtr(0)->_vartype == __float__)
        nd->getChildPtr(1)->_vartype = __float__;
    if (nd->getChildPtr(1)->_vartype == __float__)
        nd->getChildPtr(0)->_vartype = __float__;
    char *_add = NULL;
    if (nd->getChildPtr(0)->_vartype == __uint32_t__ || nd->getChildPtr(1)->_vartype == __uint32_t__)
        _add = str_concat("%s%s", _add, "u");
    else
        _add = str_concat("%s%s", _add, "");
    // register_numl.duplicate();
    nd->getChildPtr(0)->visitNode();
    // register_numl.pop();

    int leftl = register_numl.get();

    // register_numl.duplicate();
    nd->getChildPtr(1)->visitNode();
    // register_numl.pop();

    //////printf("compare %s %s\n",tokenNames[nd->_token->type ].c_str(),nd->_token->text.c_str());
    asmInstruction compop;
    asmInstruction compo2;
    // to compose
    int h = 999;

    if (nd->getChildPtr(1)->_vartype == __float__)
    {
        switch (nd->type)
        {
        case TokenLessThan:
            h = numl;
            compop = olts; //"olt.s"; // greater or equal
            //  bufferText->addAfter( string_format("%s_end:\n",nd->target.c_str()));
            compo2 = bf; // "bf";
            break;
        case TokenDoubleEqual:
            h = numl;
            compop = oeqs; //"oeq.s"; // not equal
            compo2 = bf;   //"bf";
            break;
        case TokenNotEqual:
            h = numl;
            compop = oeqs; //"oeq.s"; // equal
            compo2 = bt;   //"bt";
            break;
        case TokenMoreOrEqualThan:
            compop = oles; //"ole.s"; // less then
            h = numl;
            numl = leftl;
            leftl = h;
            compo2 = bf; //"bf";
            break;
        case TokenMoreThan:
            compop = olts; //"olt.s"; // not equal
            h = numl;
            numl = leftl;
            leftl = h;
            compo2 = bf; //"bf";
            break;
        case TokenLessOrEqualThan:
            h = numl;
            compop = oles; //"ole.s"; // not equal
            compo2 = bf;   //"bf";

            // compo2="bt";
            break;
        default:
            compop = oles; //"ole.s"; // not equal
            compo2 = bf;   //"bf";

            break;
        }
        // bufferText->addAfter(string_format("%s b0,f%d,f%d", compop.c_str(), numl, leftl));
        // bufferText->addAfter(string_format("%s b0,%s_end", compo2.c_str(), nd->getTargetText()));
        bufferText->addAfter(string_format(compop, numl, leftl));
        bufferText->addAfter(string_format(compo2, nd->getTargetText(), "_end"));
        bufferText->addAfter(string_format(movi, h, 1)); //"movi a%d,1", h));
        bufferText->addAfter(string_format("j %s_end_", nd->getTargetText()));
        bufferText->addAfter(string_format("%s_end:", nd->getTargetText()));
        bufferText->addAfter(string_format(movi, h, 0)); //"movi a%d,0", h));
        bufferText->addAfter(string_format("%s_end_:", nd->getTargetText()));
        register_numl.increase();
    }
    else
    {

        switch (nd->type)
        {
        case TokenLessThan:
            h = numl;
            compop = bge; //"bge"; // greater or equal blt
            //  bufferText->addAfter( string_format("%s_end:\n",nd->target.c_str()));
            break;
        case TokenDoubleEqual:
            h = numl;
            compop = bne; //"bne"; // not equal beq
            break;
        case TokenNotEqual:
            h = numl;
            compop = beq; //"beq"; // equal
            break;
        case TokenMoreOrEqualThan:
            h = numl;
            compop = blt; //"blt"; // less then
            break;
        case TokenMoreThan:
            compop = bge; //"bge"; // not equal
            h = numl;
            numl = leftl;
            leftl = h;
            break;
        case TokenLessOrEqualThan:
            compop = blt; //"blt"; // not equal
            h = numl;
            numl = leftl;
            leftl = h;
            break;
        default:
            compop = blt; //"blt"; // not equal
            break;
        }

        // bufferText->addAfter(string_format("%s%s a%d,a%d,%s_end", compop.c_str(), _add.c_str(), numl, leftl, nd->getTargetText()));
        bufferText->addAfter(string_format(compop, _add, numl, leftl, nd->getTargetText(), "_end"));
        bufferText->addAfter(string_format(movi, h, 1)); //"movi a%d,1", h));
        bufferText->addAfter(string_format("j %s_end_", nd->getTargetText()));
        bufferText->addAfter(string_format("%s_end:", nd->getTargetText()));
        bufferText->addAfter(string_format(movi, h, 0)); //"movi a%d,0", h));
        bufferText->addAfter(string_format("%s_end_:", nd->getTargetText()));
       
        register_numl.increase();
    }
    if(_add!=NULL)
    free(_add);
    // f = f + g.f;
}

void _visitternaryIfNode(NodeToken *nd)
{

    register_numl.duplicate();
    nd->getChildPtr(0)->visitNode();
    register_numl.pop();
    bufferText->addAfter(string_format("beqz a%d,%s", register_numl.get(), nd->getTargetText()));

    register_numl.duplicate();
    nd->getChildPtr(1)->visitNode();
    register_numl.pop();
    bufferText->addAfter(string_format("j %s_end", nd->getTargetText()));
    bufferText->addAfter(string_format("%s:", nd->getTargetText()));

    register_numl.duplicate();
    nd->getChildPtr(2)->visitNode();
    register_numl.pop();
    bufferText->addAfter(string_format("%s_end:", nd->getTargetText()));
}

void _visitcallConstructorNode(NodeToken *nd)
{
    // getVarTypeObj()->total_size (the struct's own per-instance byte
    // size, shared by the type -- NOT specific to this array
    // declaration) is 0 for a struct with no data members (a
    // constructor-only struct, e.g. one that only calls printfln()/does
    // other side effects). nd->_total_size (this array's total byte
    // footprint = element_count * that per-instance size) is then *also*
    // unavoidably 0, since count*0==0 regardless of count -- so this used
    // to be a genuine 0/0 integer division, undefined behavior in C++.
    // On host (ARM64) that silently evaluates to 0, masking it; compiled
    // for Xtensa/ESP32 (no hardware integer divide, so this routes
    // through a software libgcc division routine at actual script-
    // compile time, since Parser::parse() itself runs on-device),
    // divide-by-zero behavior is implementation-defined and was
    // confirmed to produce a real, reproduced-on-hardware crash (Guru
    // Meditation Error: InstrFetchProhibited, PC=0x00000000) for exactly
    // this case. Guarding the divisor to at least 1 makes this
    // deterministic instead: size still comes out 0 (the array's own
    // element count is unrecoverable once multiplied by a 0-byte
    // per-instance size -- a separate, pre-existing limitation, not
    // fixed here), so only the first element's constructor still runs,
    // but safely, with no UB.
    int size = nd->_total_size / (nd->getVarTypeObj()->total_size > 0 ? nd->getVarTypeObj()->total_size : 1);

    if (nd->stack_pos > 0)
    {
        bufferText->addAfter(string_format(addi, 5, 1, nd->stack_pos));
    }
    else
    {
        bufferText->addAfter(string_format("l32r a5,@_%s", nd->getText()));
    }
    if (size > 1)
    {
        bufferText->addAfter(string_format(movi, 6, 0)); //"movi a6,0");
        bufferText->addAfter(string_format("loop_label_%d:", for_if_num2));
    }
    bufferText->addAfter(string_format(mov, 10, 5)); //"mov a10,a5");
    // bufferText->addAfter("mov a10,a2");
    bufferText->addAfter(string_format("call8 @_%s._@%s()", nd->getVarTypeObj()->varName, nd->getVarTypeObj()->varName));
    if (size > 1)
    {
        bufferText->addAfter(string_format(addi, 5, 5, nd->getVarTypeObj()->total_size));
        bufferText->addAfter(string_format(addi, 6, 6, 1)); //"addi a6,a6,1");
        bufferText->addAfter(string_format(movi, 7, size));

        bufferText->addAfter(string_format("bne a7,a6,loop_label_%d", for_if_num2));
        for_if_num2++;
    }
}
void _visitUnknownNode(NodeToken *nd) {}

void translateType(varTypeEnum to, varTypeEnum from, int regnum)

{
    // printf("to:%s from: %s\n",varTypeEnumNames[to].c_str(),varTypeEnumNames[from].c_str());
    // if(to==__none__)
    //       to=__int__;
    if (to == __none__ or from == to)
        return;
    switch (to)
    {
    case __float__:
        // bufferText->sp.pop();
        bufferText->addAfterNoDouble(string_format(floats, regnum, regnum));
        // bufferText->sp.push(bufferText->get());
        break;
    case __int__:
        switch (from)
        {
        case __float__:
            //  bufferText->sp.pop();
            bufferText->addAfterNoDouble(string_format(truncs, regnum, regnum));
            // bufferText->sp.push(bufferText->get());
            break;
        default:
            break;
        }
        break;
    case __uint8_t__:
        switch (from)
        {
        case __float__:
            //  bufferText->sp.pop();
            bufferText->addAfterNoDouble(string_format(truncs, regnum, regnum));
            // bufferText->sp.push(bufferText->get());
            break;
        default:
            break;
        }
        break;

    default:
        switch (from)
        {
        case __float__:
            //  bufferText->sp.pop();
            bufferText->addAfterNoDouble(string_format(truncs, regnum, regnum));
            // bufferText->sp.push(bufferText->get());
            break;
        default:
            break;
        }
        break;
        break;
    }
}

char *_numToBytes(uint32_t __num)
{
    char *val = string_format(bytes, 4);
    uint8_t c = __num & 0xff;
    val = str_concat("%s %02x", val, c);

    // val=val+'A';
    __num = __num / 256;
    c = __num & 0xff;
    val = str_concat("%s %02x", val, c);
    //  val = val + " " + string_format("%02x", c);
    // val=val+'A';
    __num = __num / 256;
    c = __num & 0xff;
    val = str_concat("%s %02x", val, c);
    // val = val + " " + string_format("%02x", c);
    //  val=val+'A';
    __num = __num / 256;
    c = __num & 0xff;
    val = str_concat("%s %02x", val, c);
    // val = val + " " + string_format("%02x", c);
    return val;
}

void _visitCallFunctionTemplate(NodeToken *nd, int regbase, bool isExtCall)
{

    int staack_offset = (nd->getChildPtr(2)->children_size() - 7) * 4;
    bool convert = true;
    bool isArg = false;
    // int nbfloat = 0;
    if (nd == NULL)
    {
        return;
    }
    bool saveinstack[20];
    for (int i = 0; i < 20; i++)
    {
        if (isExtCall)
            saveinstack[i] = false;
        else
            saveinstack[i] = false;
        if (i >= 4)
            saveinstack[i] = true;
    }
    NodeToken *func = nd; //  _functions[nd->target];

    NodeToken *t = nd; // cntx.findFunction(nd->_token);
    if (t == NULL)
    {
        // globalType.pop();
        return;
    }
    // printf(" %s %d %d\n",nd->_token->text.c_str(),  t->children_size(),t->getChildPtr(1)->children_size());
    // for (int i = 0; i < t->getChildPtr(1)->children_size(); i++)

    // printf("number of arg %s %d\r\n", nd->getTokenText(), nd->findMaxArgumentSize());
    for (int i = t->getChildPtr(2)->children_size() - 1; i >= 0; i--)
    {
        // printf("***number of arg %d %d\r\n", i, nd->getChildPtr(2)->getChildPtr(i)->findMaxArgumentSize());
        bool save_in_stack = false;
        for (int j = 0; j < i; j++)
        {
            if (nd->getChildPtr(2)->getChildPtr(j)->findMaxArgumentSize() - 1 >= i)
            {
                save_in_stack = true;
            }
        }
        if (isExtCall)
        {
            if (i == 0)
                save_in_stack = false;
        }
        else
        {
            if (i < 2)
                save_in_stack = false;
        }
        saveinstack[i] = save_in_stack;
        if (i >= 4)
        {
            save_in_stack = true;
            saveinstack[i] = true;
        }
        if (i >= 6)
        {
            save_in_stack = false;
            saveinstack[i] = false;
        }
        if (t->getChildPtr(2)->getChildPtr(i)->isPointer)
        {
            register_numl.duplicate();
            nd->getChildPtr(2)->getChildPtr(i)->visitNode();
            register_numl.pop();
            if (save_in_stack == true)
            {
                bufferText->addAfter(string_format("s32i a%d,a1,%d", register_numl.get(), i * 4 + _START_2));
            }
            else
            {
                bufferText->addAfter(string_format("mov a%d,a%d", regbase + i, register_numl.get()));
            }
        }
        else

        {
            if (i < func->getChildPtr(1)->children_size())
            {
                if (func->getChildPtr(1)->getChildPtr(i)->getVarTypeObj()->_varType == __Args__)
                    convert = false;

                register_numl.duplicate();
                globalType.push(func->getChildPtr(1)->getChildPtr(i)->getVarTypeObj()->_varType);
                nd->getChildPtr(2)->getChildPtr(i)->visitNode();
                register_numl.pop();
            }
            else
            {
                register_numl.duplicate();
                // globalType.push(t->getChildPtr(2)->getChildPtr(i)->getVarTypeObj()->_varType);
                nd->getChildPtr(2)->getChildPtr(i)->visitNode();
                register_numl.pop();
            }
            if (nd->getChildPtr(2)->getChildPtr(i)->getVarTypeObj() != NULL and convert)
                translateType(globalType.get(), nd->getChildPtr(2)->getChildPtr(i)->getVarTypeObj()->_varType, register_numl.get());
            varTypeEnum _vartype;
            NodeToken *l;
            if (i < func->getChildPtr(1)->children_size())
            {
                l = func->getChildPtr(1);
                _vartype = func->getChildPtr(1)->getChildPtr(i)->getVarTypeObj()->_varType;
                if (_vartype == __Args__)
                {
                    _vartype = t->getChildPtr(2)->getChildPtr(i)->getVarTypeObj()->_varType;
                    isArg = true;
                    /*
                    bufferText->pop();
                    bufferText->addAfter(string_format("l32i a12,a8"));
                     bufferText->addAfter(string_format("mov a12,a8"));
                     bufferText->addAfter(string_format("addi a8,a8,4"));
                     bufferText->addAfter(string_format("mov a13,a8"));
                     */
                }
            }
            else
            {
                l = t->getChildPtr(2);
                _vartype = t->getChildPtr(2)->getChildPtr(i)->getVarTypeObj()->_varType;
            }

            if (_vartype == __float__)
            {

                if (isArg)
                {
                }
                else
                {
                    if (save_in_stack == true)
                    {
                        bufferText->addAfter(string_format("ssi f%d,a1,%d", register_numl.get(), i * 4 + _START_2));
                    }
                    else
                    {
                        bufferText->addAfter(string_format("rfr a%d,f%d", regbase + i, register_numl.get()));
                    }
                }
            }
            else if (l->getChildPtr(i)->getVarTypeObj()->_varType == __CRGB__ or l->getChildPtr(i)->getVarTypeObj()->_varType == __CRGBW__)
            {
                // bufferText->addAfter( bufferText->sp.pop(),string_format("mov a%d,a%d", 10 + i, register_numl.get()));
                if (t->getChildPtr(2)->getChildPtr(i)->getChildPtr(0)->_nodetype == numberNode)
                {

                    if (save_in_stack == true)
                    {
                        for (int k = 0; k < 3; k++)
                        {
                            bufferText->addAfter(bufferText->sp.pop(), string_format("s8i a%d,a1,%d", register_numl.get(), i * 4 + _START_2 + k));
                        }
                        // bufferText->addAfter(string_format("l32i a%d,a1,%d",10+i,i * 4 + _START_2));
                    }

                    else
                    {
                        for (int k = 2; k >= 0; k--)
                        {
                            bufferText->addAfter(bufferText->sp.pop(), string_format("s8i a%d,a1,%d", register_numl.get(), i * 4 + _START_2 + k));
                        }
                        bufferText->addAfter(string_format("l32i a%d,a1,%d", regbase + i, i * 4 + _START_2));
                    }
                }
                else
                {

                    for (int k = 0; k < t->getChildPtr(2)->getChildPtr(i)->getChildPtr(0)->getVarTypeObj()->size; k++)
                    {
                        // bufferText->addAfter(string_format("mov a15,a10"));
                        // bufferText->addAfter(bufferText->sp.pop(),string_format("slli a%d,a%d,%d", 10+i,register_numl.get(),  k* 8));
                        // register_numl--;
                        bufferText->pop();
                    }
                    if (t->getChildPtr(2)->getChildPtr(i)->getChildPtr(0)->_nodetype == callFunctionNode)
                    {
                        if (save_in_stack == true)
                        {
                            bufferText->addAfter(bufferText->sp.pop(), string_format("l32i a%d,a8,0", regbase + i));
                            bufferText->addAfter(string_format("s32i a%d,a1,%d", regbase + i, i * 4 + _START_2));
                        }
                        else
                        {
                            bufferText->addAfter(bufferText->sp.pop(), string_format("l32i a%d,a8,0", 10 + i));
                        }
                    }
                    if (t->getChildPtr(2)->getChildPtr(i)->getChildPtr(0)->_nodetype == extCallFunctionNode)
                    {
                        if (save_in_stack == true)
                        {
                            // bufferText->addAfter(bufferText->sp.pop(), string_format("mov a%d,a10", 10 + i));
                            bufferText->addAfter(string_format("s32i a10,a1,%d", i * 4 + _START_2));
                        }
                        else
                        {
                            bufferText->addAfter(bufferText->sp.pop(), string_format("mov a%d,a10", regbase + i));
                        }
                    }
                    else if (t->getChildPtr(2)->getChildPtr(i)->getChildPtr(0)->_nodetype == localVariableNode)
                    {
                        if (save_in_stack == true)
                        {
                            bufferText->addAfter(bufferText->sp.pop(), string_format("l32i a%d,a1,%d", regbase + i, t->getChildPtr(0)->getChildPtr(i)->getChildPtr(0)->stack_pos));
                            bufferText->addAfter(string_format("s32i a%d,a1,%d", regbase + i, i * 4 + _START_2));
                        }
                        else
                        {
                            bufferText->addAfter(bufferText->sp.pop(), string_format("l32i a%d,a1,%d", regbase + i, t->getChildPtr(0)->getChildPtr(i)->getChildPtr(0)->stack_pos));
                        }
                    }
                    else if (t->getChildPtr(2)->getChildPtr(i)->getChildPtr(0)->_nodetype == globalVariableNode)
                    {
                        // tobe done
                        if (save_in_stack == true)
                        {
                            bufferText->addAfter(bufferText->sp.pop(), string_format("l32i a%d,a5,0", regbase + i));
                            bufferText->addAfter(string_format("s32i a%d,a1,%d", regbase + i, i * 4 + _START_2));
                        }
                        else
                        {
                            bufferText->addAfter(bufferText->sp.pop(), string_format("l32i a%d,a5,0", regbase + i));
                        }
                    }
                }
            }

            else
            {
                if (save_in_stack == true)
                {
                    bufferText->addAfter(string_format("s32i a%d,a1,%d", register_numl.get(), i * 4 + _START_2));
                }
                else
                {
                    // to change
                    if (i >= 6)
                    {
                        bufferText->addAfter(string_format("s32i a%d,a1,%d", register_numl.get(), staack_offset));
                        staack_offset -= 4;
                    }
                    else
                    {
                        bufferText->addAfter(string_format("mov a%d,a%d", regbase + i, register_numl.get()));
                    }
                }
            }

            globalType.pop();
        }
    }

    // bufferText->end();
    for (int i = 0; i < nd->getChildPtr(2)->children_size(); i++)
    {
        if (i < 7)
        {
            if (saveinstack[i] == true)
            {
                varType *v;
                if (i < func->getChildPtr(1)->children_size())
                    v = func->getChildPtr(1)->getChildPtr(i)->getVarTypeObj();
                else
                    v = nd->getChildPtr(2)->getChildPtr(i)->getVarTypeObj();
                if (v->_varType == __none__)
                {
                    v = &_varTypes[__uint32_t__];
                }

                if (v->_varType == __float__)
                {
                    bufferText->addAfter(string_format(l32i, regbase + i, 1,i * 4 + _START_2));
                }
                else
                {
                    bufferText->addAfter(string_format(v->load[0], regbase + i,1, i * 4 + _START_2));
                }
            }
        }
    }

    if (isExtCall)
    {
        bufferText->addAfter(string_format("callExt a8,@_%s", nd->getText()));
    }
    else
    {
        // bufferText->addAfter(string_format("mov a10,a2"));
        bufferText->addAfter(string_format("call8 @_%s", nd->getText()));
    }
    bufferText->sp.push(bufferText->get());

    return;
}
