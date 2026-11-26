// Loops over byte arrays compiled to ESP32-S3 PIE code.
//
// The loops taken are these (anything else compiles as usual):
//
//   for (int i = A; i < B; i++)        A, B numbers (B may be 128 - 1)
//   {
//      uint8_t n = <expr>;             byte temporaries, any number
//      dst[row, i + k] = <expr>;       one store, to a uint8_t array
//   }
//
// where <expr> is made of numbers, the temporaries, uint8_t arrays read at
// [row, i + k] or [i + k], + - & | and the comparisons (0 or 1). A row is
// a variable, a number, or a variable plus or minus a number, and the
// array written is read nowhere else than at its own row and offset.
//
// Exact, not saturating: the S3 only has saturating adds, so the bytes are
// widened to 16-bit lanes (ee.vzip.8 with a zero register), where every
// value the expression can take is checked to fit; a uint8_t temporary is
// masked with 0xFF, a comparison's all-ones lanes with 1, and the result's
// low bytes are what is stored (ee.vunzip.8) -- the same truncations as
// the scalar code. 16 elements per pass, as two halves of 8 lanes.
//
// The loop becomes a call to a generated assembly function, appended with
// the __ASM__ functions after the optimizer (see appendAsmFunctions()):
// its arguments are the rows. The function runs the loop one element at a
// time until the store address is a multiple of 16 (ee.vst.128 needs it),
// then 16 at a time (loads at any address: ee.ld.128.usar.ip + ee.src.q),
// then one at a time again for what is left.
#include "vectorize.h"
#include "string_functions.h"
#include "visitnode.h"
#include "parser_enum.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if defined(ESP_PLATFORM) || defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include "sdkconfig.h"
#endif

#if CONFIG_IDF_TARGET_ESP32S3
static int vectorizeOn = 1;
#elif defined(ESP_PLATFORM) || defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
static int vectorizeOn = 0;
#else
static int vectorizeOn = -1; // a computer: ASM_VECTORIZE decides
#endif
static int vecCount = 0;

void setLoopVectorize(bool on)
{
    vectorizeOn = on ? 1 : 0;
}

void resetLoopVectorize()
{
    vecCount = 0;
}

enum VKind
{
    V_CONST,
    V_LOAD,
    V_TEMP,
    V_ADD,
    V_SUB,
    V_AND,
    V_OR,
    V_EQ,
    V_NE,
    V_LT,
    V_GT,
    V_LE,
    V_GE
};

struct VExpr
{
    VKind kind;
    int a, b;  // operands (indexes into the plan's expressions)
    int value; // V_CONST
    int ref;   // V_LOAD: which array/row
    int off;   // V_LOAD: k in [i + k]
    int temp;  // V_TEMP
    long lo, hi;
};

struct VRef
{
    const char *array;
    NodeToken *row; // NULL for a one-dimension array
    int width;      // a row's size
};

#define VMAX_EXPR 64
#define VMAX_REF 4
#define VMAX_TEMP 4
#define VMAX_STMT 8
#define VMAX_CONST 6

struct VPlan
{
    const char *iName;
    int from, to;
    VExpr e[VMAX_EXPR];
    int ne;
    VRef ref[VMAX_REF];
    int nref;
    const char *tempName[VMAX_TEMP];
    int ntemp;
    int stmtTemp[VMAX_STMT]; // the temporary assigned, or -1: the store
    int stmtExpr[VMAX_STMT];
    int nstmt;
    int storeRef, storeOff;
    bool usesCompare, usesTemp;
};

static NodeToken *strip(NodeToken *n)
{
    while (n != NULL && n->_nodetype == changeTypeNode && n->children_size() == 1 && strlen(n->getText()) == 0)
        n = n->getChildPtr(0);
    return n;
}

static bool isLocal(NodeToken *n)
{
    return n->_nodetype == localVariableNode || n->_nodetype == localVariableNodeAsRegister;
}

static bool constantOf(NodeToken *n, long *v)
{
    n = strip(n);
    if (n == NULL)
        return false;
    if (n->_nodetype == numberNode && n->children_size() == 0)
    {
        const char *t = n->getText();
        if (t == NULL || strchr(t, '.') != NULL || strchr(t, 'x') != NULL || strchr(t, 'X') != NULL)
            return false;
        *v = strtol(t, NULL, 10);
        return true;
    }
    if (n->_nodetype == binOpNode && n->children_size() == 2)
    {
        long a, b;
        if (!constantOf(n->getChildPtr(0), &a) || !constantOf(n->getChildPtr(1), &b))
            return false;
        if (n->type == TokenAddition)
            *v = a + b;
        else if (n->type == TokenSubstraction)
            *v = a - b;
        else if (n->type == TokenStar)
            *v = a * b;
        else
            return false;
        return true;
    }
    return false;
}

static bool sameTree(NodeToken *a, NodeToken *b)
{
    a = strip(a);
    b = strip(b);
    if (a == NULL || b == NULL)
        return a == b;
    if (a->_nodetype != b->_nodetype || a->type != b->type || a->children_size() != b->children_size())
        return false;
    const char *ta = a->getText(), *tb = b->getText();
    if ((ta == NULL) != (tb == NULL) || (ta != NULL && strcmp(ta, tb) != 0))
        return false;
    for (int i = 0; i < a->children_size(); i++)
        if (!sameTree(a->getChildPtr(i), b->getChildPtr(i)))
            return false;
    return true;
}

// A row: a variable (not i, not a temporary), a number, or one of them
// plus or minus a number -- something the caller computes in one or two
// registers.
static bool validRow(VPlan *p, NodeToken *n, int depth)
{
    n = strip(n);
    if (n == NULL || depth > 1)
        return false;
    if (n->_nodetype == numberNode)
    {
        long v;
        return constantOf(n, &v);
    }
    if (isLocal(n))
    {
        if (strcmp(n->getText(), p->iName) == 0)
            return false;
        for (int t = 0; t < p->ntemp; t++)
            if (strcmp(n->getText(), p->tempName[t]) == 0)
                return false;
        return n->children_size() == 0;
    }
    if (n->_nodetype == binOpNode && n->children_size() == 2 &&
        (n->type == TokenAddition || n->type == TokenSubstraction))
    {
        long v;
        return validRow(p, n->getChildPtr(0), depth + 1) && constantOf(n->getChildPtr(1), &v);
    }
    return false;
}

// [i + k], [i - k], [k + i], [i]
static bool indexOffset(VPlan *p, NodeToken *n, int *off)
{
    n = strip(n);
    if (n == NULL)
        return false;
    if (isLocal(n) && strcmp(n->getText(), p->iName) == 0)
    {
        *off = 0;
        return true;
    }
    if (n->_nodetype != binOpNode || n->children_size() != 2)
        return false;
    NodeToken *l = strip(n->getChildPtr(0)), *r = strip(n->getChildPtr(1));
    long v;
    if (n->type == TokenAddition)
    {
        if (isLocal(l) && strcmp(l->getText(), p->iName) == 0 && constantOf(r, &v))
            *off = (int)v;
        else if (isLocal(r) && strcmp(r->getText(), p->iName) == 0 && constantOf(l, &v))
            *off = (int)v;
        else
            return false;
    }
    else if (n->type == TokenSubstraction && isLocal(l) && strcmp(l->getText(), p->iName) == 0 &&
             constantOf(r, &v))
        *off = (int)-v;
    else
        return false;
    return *off >= -64 && *off <= 64;
}

// A uint8_t array element at [row, i + k] or [i + k]: its ref and k.
static bool arrayAccess(VPlan *p, NodeToken *n, int *ref, int *off)
{
    if (!n->isPointer || n->asPointer || n->getVarTypeObj() == NULL || n->getVarTypeObj()->_varType != __uint8_t__)
        return false;
    const char *t = n->getTargetText();
    if (t == NULL || t[0] != '@' || strstr(t, " $") != NULL || strstr(t, " *") != NULL)
        return false;
    int dims = atoi(t + 1);
    NodeToken *row = NULL;
    int width = 0;
    if (dims == 1 && n->children_size() == 1)
    {
        if (!indexOffset(p, n->getChildPtr(0), off))
            return false;
    }
    else if (dims == 2 && n->children_size() == 2)
    {
        int rows = 0;
        if (sscanf(t, "@2 %d %d", &rows, &width) != 2 || width <= 0)
            return false;
        row = n->getChildPtr(0);
        if (!validRow(p, row, 0) || !indexOffset(p, n->getChildPtr(1), off))
            return false;
    }
    else
        return false;
    for (int r = 0; r < p->nref; r++)
        if (strcmp(p->ref[r].array, n->getText()) == 0 && (row == NULL) == (p->ref[r].row == NULL) &&
            (row == NULL || sameTree(row, p->ref[r].row)))
        {
            *ref = r;
            return true;
        }
    if (p->nref == VMAX_REF)
        return false;
    p->ref[p->nref].array = n->getText();
    p->ref[p->nref].row = row;
    p->ref[p->nref].width = width;
    *ref = p->nref++;
    return true;
}

static long clampBits(long hi)
{
    long m = 1;
    while (m <= hi)
        m <<= 1;
    return m - 1;
}

// The expression tree of n, or -1. Every value it can take is kept in
// [lo, hi]; outside 16 bits, the loop is not vectorized.
static int buildExpr(VPlan *p, NodeToken *n)
{
    n = strip(n);
    if (n == NULL || p->ne == VMAX_EXPR)
        return -1;
    VExpr x;
    memset(&x, 0, sizeof(x));
    long v;
    if (constantOf(n, &v))
    {
        if (v < -2048 || v > 2047)
            return -1;
        x.kind = V_CONST;
        x.value = (int)v;
        x.lo = x.hi = v;
    }
    else if (n->_nodetype == globalVariableNode)
    {
        x.kind = V_LOAD;
        if (!arrayAccess(p, n, &x.ref, &x.off))
            return -1;
        x.lo = 0;
        x.hi = 255;
    }
    else if (isLocal(n) && n->children_size() == 0)
    {
        int t = -1;
        for (int k = 0; k < p->ntemp; k++)
            if (strcmp(n->getText(), p->tempName[k]) == 0)
                t = k;
        if (t < 0)
            return -1; // i itself, or a variable of the function: not handled
        x.kind = V_TEMP;
        x.temp = t;
        x.lo = 0;
        x.hi = 255;
    }
    else if ((n->_nodetype == binOpNode || n->_nodetype == testNode) && n->children_size() == 2)
    {
        switch (n->type)
        {
        case TokenAddition: x.kind = V_ADD; break;
        case TokenSubstraction: x.kind = V_SUB; break;
        case TokenKeywordAnd: x.kind = V_AND; break;
        case TokenKeywordOr: x.kind = V_OR; break;
        case TokenDoubleEqual: x.kind = V_EQ; break;
        case TokenNotEqual: x.kind = V_NE; break;
        case TokenLessThan: x.kind = V_LT; break;
        case TokenMoreThan: x.kind = V_GT; break;
        case TokenLessOrEqualThan: x.kind = V_LE; break;
        case TokenMoreOrEqualThan: x.kind = V_GE; break;
        default: return -1;
        }
        if ((n->_nodetype == testNode) != (x.kind >= V_EQ))
            return -1;
        x.a = buildExpr(p, n->getChildPtr(0));
        if (x.a < 0)
            return -1;
        x.b = buildExpr(p, n->getChildPtr(1));
        if (x.b < 0)
            return -1;
        VExpr &l = p->e[x.a], &r = p->e[x.b];
        switch (x.kind)
        {
        case V_ADD: x.lo = l.lo + r.lo; x.hi = l.hi + r.hi; break;
        case V_SUB: x.lo = l.lo - r.hi; x.hi = l.hi - r.lo; break;
        case V_AND:
        case V_OR:
            if (l.lo < 0 || r.lo < 0)
            {
                x.lo = -32768;
                x.hi = 32767;
            }
            else
            {
                x.lo = 0;
                x.hi = x.kind == V_AND ? (l.hi < r.hi ? l.hi : r.hi) : clampBits(l.hi > r.hi ? l.hi : r.hi);
            }
            break;
        default:
            x.lo = 0;
            x.hi = 1;
            p->usesCompare = true;
            break;
        }
    }
    else
        return -1;
    if (x.lo < -32768 || x.hi > 32767)
        return -1;
    p->e[p->ne] = x;
    return p->ne++;
}

// for (int i = A; i < B; i++) with A and B numbers: i's name, A, B.
static bool loopHeader(VPlan *p, NodeToken *nd)
{
    if (nd->children_size() != 4)
        return false;
    // init: a declaration of i, then i = A
    NodeToken *init = nd->getChildPtr(0);
    NodeToken *def = NULL, *assign = NULL;
    for (int k = 0; k < init->children_size(); k++)
    {
        NodeToken *c = init->getChildPtr(k);
        if (c->_nodetype == defLocalVariableNode || c->_nodetype == defLocalVariableNodeAsRegister)
            def = c;
        else if (c->_nodetype == assignementNode)
            assign = c;
        else if (!(c->_nodetype == statementNode && c->children_size() == 0))
            return false;
    }
    if (def == NULL || assign == NULL || assign->children_size() != 2)
        return false;
    p->iName = def->getText();
    NodeToken *store = assign->getChildPtr(0);
    long from, to;
    if (strcmp(store->getText(), p->iName) != 0 || !constantOf(assign->getChildPtr(1), &from))
        return false;
    // condition: i < B
    NodeToken *cmp = nd->getChildPtr(1);
    if (cmp->_nodetype != comparatorNode || cmp->children_size() != 1)
        return false;
    NodeToken *test = cmp->getChildPtr(0);
    if (test->_nodetype != testNode || test->type != TokenLessThan || test->children_size() != 2)
        return false;
    NodeToken *iv = strip(test->getChildPtr(0));
    if (!isLocal(iv) || strcmp(iv->getText(), p->iName) != 0 || !constantOf(test->getChildPtr(1), &to))
        return false;
    // step: i++
    NodeToken *inc = nd->getChildPtr(2);
    NodeToken *incAssign = NULL;
    for (int k = 0; k < inc->children_size(); k++)
    {
        NodeToken *c = inc->getChildPtr(k);
        if (c->_nodetype == assignementNode)
        {
            if (incAssign != NULL)
                return false;
            incAssign = c;
        }
        else if (!(c->_nodetype == statementNode && c->children_size() == 0))
            return false;
    }
    if (incAssign == NULL || incAssign->children_size() != 2 ||
        strcmp(incAssign->getChildPtr(0)->getText(), p->iName) != 0)
        return false;
    NodeToken *pp = incAssign->getChildPtr(1);
    if (pp->_nodetype != unitaryOpNode || pp->type != TokenPlusPlus || pp->children_size() != 1 ||
        strcmp(strip(pp->getChildPtr(0))->getText(), p->iName) != 0)
        return false;
    if (from < -2048 || from > 2047 || to < -2048 || to > 2047)
        return false;
    p->from = (int)from;
    p->to = (int)to;
    return true;
}

static bool loopBody(VPlan *p, NodeToken *body)
{
    p->nstmt = 0;
    p->storeRef = -1;
    vect<NodeToken *> stmts;
    if (body->_nodetype == blockStatementNode)
        for (int k = 0; k < body->children_size(); k++)
            stmts.push_back(body->getChildPtr(k));
    else
        stmts.push_back(body);
    // a statementNode can hold a declaration and its assignment
    vect<NodeToken *> flat;
    for (int k = 0; k < stmts.size(); k++)
    {
        NodeToken *s = stmts.get(k);
        if (s->_nodetype == statementNode)
        {
            for (int m = 0; m < s->children_size(); m++)
                flat.push_back(s->getChildPtr(m));
        }
        else
            flat.push_back(s);
    }
    for (int k = 0; k < flat.size(); k++)
    {
        NodeToken *s = flat.get(k);
        if (s->_nodetype == statementNode && s->children_size() == 0)
            continue;
        if (s->_nodetype == defLocalVariableNode)
        {
            if (s->getVarTypeObj() == NULL || s->getVarTypeObj()->_varType != __uint8_t__ || s->isPointer ||
                p->ntemp == VMAX_TEMP)
                return false;
            p->tempName[p->ntemp++] = s->getText();
            continue;
        }
        if (s->_nodetype != assignementNode || s->children_size() != 2 || p->nstmt == VMAX_STMT)
            return false;
        NodeToken *target = s->getChildPtr(0);
        if (target->_nodetype == storeLocalVariableNode)
        {
            int t = -1;
            for (int m = 0; m < p->ntemp; m++)
                if (strcmp(target->getText(), p->tempName[m]) == 0)
                    t = m;
            if (t < 0 || p->storeRef >= 0) // a temporary, before the store
                return false;
            int e = buildExpr(p, s->getChildPtr(1));
            if (e < 0)
                return false;
            p->stmtTemp[p->nstmt] = t;
            p->stmtExpr[p->nstmt++] = e;
            p->usesTemp = true;
            continue;
        }
        if (target->_nodetype != storeGlobalVariableNode || p->storeRef >= 0)
            return false;
        int ref, off;
        if (!arrayAccess(p, target, &ref, &off))
            return false;
        int e = buildExpr(p, s->getChildPtr(1));
        if (e < 0)
            return false;
        p->storeRef = ref;
        p->storeOff = off;
        p->stmtTemp[p->nstmt] = -1;
        p->stmtExpr[p->nstmt++] = e;
    }
    if (p->storeRef < 0 || p->stmtTemp[p->nstmt - 1] != -1)
        return false;
    // a temporary is assigned once, before it is read
    for (int t = 0; t < p->ntemp; t++)
    {
        int n = 0;
        for (int s = 0; s < p->nstmt; s++)
            n += p->stmtTemp[s] == t;
        if (n != 1)
            return false;
    }
    // the array written is read only where it is written (same row, same k)
    for (int e = 0; e < p->ne; e++)
        if (p->e[e].kind == V_LOAD)
        {
            const VRef &r = p->ref[p->e[e].ref], &w = p->ref[p->storeRef];
            if (strcmp(r.array, w.array) == 0 && (p->e[e].ref != p->storeRef || p->e[e].off != p->storeOff))
                return false;
        }
    // a temporary read before it is assigned
    for (int s = 0; s < p->nstmt; s++)
        for (int e = 0; e < p->ne; e++)
            if (p->e[e].kind == V_TEMP)
            {
                int assigned = -1;
                for (int m = 0; m < p->nstmt; m++)
                    if (p->stmtTemp[m] == p->e[e].temp)
                        assigned = m;
                if (assigned < 0)
                    return false;
            }
    return true;
}

// ---- code generation ----

struct VGen
{
    vect<char *> lines;
    bool failed;
    int label;
    int fn;
    // scalar: a10..a15
    uint32_t aFree;
    int tempReg[VMAX_TEMP];
    // vector: q0..q7
    uint32_t qFree;
    // the constants (1 for the comparisons, 255 for the temporaries
    // included), in a table in the stack frame at a7 + 16 * c; those
    // with a register are loaded into it once, the others at each use
    int constVal[VMAX_CONST];
    int constUses[VMAX_CONST];
    int constReg[VMAX_CONST]; // -1: loaded from the table when needed
    int nconst;
    int qTemp[VMAX_TEMP];
};

static void emit(VGen *g, char *line)
{
    g->lines.push_back(line);
}

static char *newLabel(VGen *g)
{
    return string_format("___vec%d_%d", g->fn, g->label++);
}

static int allocA(VGen *g)
{
    for (int r = 10; r <= 15; r++)
        if (g->aFree & (1u << r))
        {
            g->aFree &= ~(1u << r);
            return r;
        }
    g->failed = true;
    return 10;
}

static void freeA(VGen *g, int r)
{
    g->aFree |= 1u << r;
}

// the address of ref's element i + k in `reg` (a8 is i)
static void elementAddress(VGen *g, VPlan *p, int ref, int off, int reg)
{
    emit(g, string_format("add a%d,a%d,a8", reg, 2 + ref));
    if (off != 0)
        emit(g, string_format("addi a%d,a%d,%d", reg, reg, off));
}

static bool isTempRegA(VGen *g, VPlan *p, int r)
{
    for (int t = 0; t < p->ntemp; t++)
        if (g->tempReg[t] == r)
            return true;
    return false;
}

static int scalarExpr(VGen *g, VPlan *p, int e)
{
    VExpr &x = p->e[e];
    if (x.kind == V_CONST)
    {
        int r = allocA(g);
        emit(g, string_format("movi a%d,%d", r, x.value));
        return r;
    }
    if (x.kind == V_LOAD)
    {
        int r = allocA(g);
        elementAddress(g, p, x.ref, x.off, r);
        emit(g, string_format("l8ui a%d,a%d,0", r, r));
        return r;
    }
    if (x.kind == V_TEMP)
        return g->tempReg[x.temp];
    int a = scalarExpr(g, p, x.a);
    int b = scalarExpr(g, p, x.b);
    if (g->failed)
        return 10;
    int r = isTempRegA(g, p, a) ? allocA(g) : a;
    switch (x.kind)
    {
    case V_ADD: emit(g, string_format("add a%d,a%d,a%d", r, a, b)); break;
    case V_SUB: emit(g, string_format("sub a%d,a%d,a%d", r, a, b)); break;
    case V_AND: emit(g, string_format("and a%d,a%d,a%d", r, a, b)); break;
    case V_OR: emit(g, string_format("or a%d,a%d,a%d", r, a, b)); break;
    default:
    {
        // r = 1, then 0 unless the comparison holds; r must differ from a and b
        int c = allocA(g);
        char *yes = newLabel(g);
        const char *br;
        int x1 = a, x2 = b;
        switch (x.kind)
        {
        case V_EQ: br = "beq"; break;
        case V_NE: br = "bne"; break;
        case V_LT: br = "blt"; break;
        case V_GE: br = "bge"; break;
        case V_GT: br = "blt"; x1 = b; x2 = a; break;
        default: br = "bge"; x1 = b; x2 = a; break; // V_LE
        }
        emit(g, string_format("movi a%d,1", c));
        emit(g, string_format("%s a%d,a%d,%s", br, x1, x2, yes));
        emit(g, string_format("movi a%d,0", c));
        emit(g, string_format("%s:", yes));
        free(yes);
        if (!isTempRegA(g, p, a))
            freeA(g, a);
        if (!isTempRegA(g, p, b))
            freeA(g, b);
        return c;
    }
    }
    if (!isTempRegA(g, p, b))
        freeA(g, b);
    return r;
}

// one element, i in a8
static void scalarBody(VGen *g, VPlan *p)
{
    for (int t = 0; t < p->ntemp; t++)
        g->tempReg[t] = -1;
    for (int s = 0; s < p->nstmt; s++)
    {
        int r = scalarExpr(g, p, p->stmtExpr[s]);
        if (g->failed)
            return;
        if (p->stmtTemp[s] >= 0)
        {
            if (isTempRegA(g, p, r)) // n = m: a register of its own
            {
                int c = allocA(g);
                emit(g, string_format("mov a%d,a%d", c, r));
                r = c;
            }
            emit(g, string_format("extui a%d,a%d,0,8", r, r));
            g->tempReg[p->stmtTemp[s]] = r;
        }
        else
        {
            int ad = allocA(g);
            elementAddress(g, p, p->storeRef, p->storeOff, ad);
            emit(g, string_format("s8i a%d,a%d,0", r, ad));
            freeA(g, ad);
            if (!isTempRegA(g, p, r))
                freeA(g, r);
        }
    }
    for (int t = 0; t < p->ntemp; t++)
        if (g->tempReg[t] >= 0)
            freeA(g, g->tempReg[t]);
}

static int allocQ(VGen *g)
{
    for (int q = 0; q < 8; q++)
        if (g->qFree & (1u << q))
        {
            g->qFree &= ~(1u << q);
            return q;
        }
    g->failed = true;
    return 0;
}

static void freeQ(VGen *g, int q)
{
    g->qFree |= 1u << q;
}

// a q register the expression code owns (not a kept constant, not a temporary)
static bool ownedQ(VGen *g, VPlan *p, int q)
{
    for (int c = 0; c < g->nconst; c++)
        if (g->constReg[c] == q)
            return false;
    for (int t = 0; t < p->ntemp; t++)
        if (g->qTemp[t] == q)
            return false;
    return true;
}

static int constIndex(VGen *g, int value)
{
    for (int c = 0; c < g->nconst; c++)
        if (g->constVal[c] == value)
            return c;
    return -1;
}

static bool constKept(VGen *g, int value)
{
    int c = constIndex(g, value);
    return c >= 0 && g->constReg[c] >= 0;
}

// value in every lane: its register, or a new one loaded from the table
static int constQ(VGen *g, int value)
{
    int c = constIndex(g, value);
    if (c < 0)
    {
        g->failed = true;
        return 0;
    }
    if (g->constReg[c] >= 0)
        return g->constReg[c];
    int q = allocQ(g);
    emit(g, string_format("addi a10,a7,%d", 16 * c));
    emit(g, string_format("ee.vld.128.ip q%d,a10,0", q));
    return q;
}

// The q registers evaluating e takes at its peak: a load takes three
// (ee.src.q's destination must differ from its sources), a constant not
// kept in a register one.
static int qNeed(VGen *g, VPlan *p, int e)
{
    VExpr &x = p->e[e];
    if (x.kind == V_TEMP)
        return 0;
    if (x.kind == V_CONST)
        return constKept(g, x.value) ? 0 : 1;
    if (x.kind == V_LOAD)
        return 3;
    int a = qNeed(g, p, x.a), b = qNeed(g, p, x.b);
    int first = a > b ? a : b, second = a > b ? b : a;
    int need = first > second + 1 ? first : second + 1;
    if (need < 1)
        need = 1;
    if (x.kind >= V_EQ && !constKept(g, 1) && need < 2) // the result and the 1
        need = 2;
    return need;
}

// the registers a half takes at its peak: the temporaries, plus the
// worst statement (a store needs its value and one more for ee.vunzip.8)
static int halfNeed(VGen *g, VPlan *p)
{
    int worst = 0;
    for (int s = 0; s < p->nstmt; s++)
    {
        int n = qNeed(g, p, p->stmtExpr[s]);
        if (n < 2)
            n = 2;
        if (n > worst)
            worst = n;
    }
    return p->ntemp + worst;
}

// 8 lanes of 16 bits: half 0 is elements i..i+7, half 1 i+8..i+15
static int vectorExpr(VGen *g, VPlan *p, int e, int half)
{
    VExpr &x = p->e[e];
    if (x.kind == V_CONST)
        return constQ(g, x.value);
    if (x.kind == V_TEMP)
        return g->qTemp[x.temp];
    if (x.kind == V_LOAD)
    {
        // 16 bytes at any address, then widened: w gets elements 0..7,
        // d 8..15, each a 16-bit lane
        int d = allocQ(g), z = allocQ(g), w = allocQ(g);
        elementAddress(g, p, x.ref, x.off, 10);
        emit(g, string_format("ee.ld.128.usar.ip q%d,a10,16", d));
        emit(g, string_format("ee.vld.128.ip q%d,a10,0", z));
        emit(g, string_format("ee.src.q q%d,q%d,q%d", w, d, z));
        emit(g, string_format("ee.zero.q q%d", d));
        emit(g, string_format("ee.vzip.8 q%d,q%d", w, d));
        freeQ(g, z);
        if (half == 0)
        {
            freeQ(g, d);
            return w;
        }
        freeQ(g, w);
        return d;
    }
    // the operand that needs more registers first (no side effects)
    int a, b;
    if (qNeed(g, p, x.b) > qNeed(g, p, x.a))
    {
        b = vectorExpr(g, p, x.b, half);
        a = vectorExpr(g, p, x.a, half);
    }
    else
    {
        a = vectorExpr(g, p, x.a, half);
        b = vectorExpr(g, p, x.b, half);
    }
    if (g->failed)
        return 0;
    int d = ownedQ(g, p, a) ? a : (ownedQ(g, p, b) ? b : allocQ(g));
    switch (x.kind)
    {
    case V_ADD: emit(g, string_format("ee.vadds.s16 q%d,q%d,q%d", d, a, b)); break;
    case V_SUB: emit(g, string_format("ee.vsubs.s16 q%d,q%d,q%d", d, a, b)); break;
    case V_AND: emit(g, string_format("ee.andq q%d,q%d,q%d", d, a, b)); break;
    case V_OR: emit(g, string_format("ee.orq q%d,q%d,q%d", d, a, b)); break;
    case V_EQ:
    case V_NE: emit(g, string_format("ee.vcmp.eq.s16 q%d,q%d,q%d", d, a, b)); break;
    case V_LT:
    case V_GE: emit(g, string_format("ee.vcmp.lt.s16 q%d,q%d,q%d", d, a, b)); break;
    default: emit(g, string_format("ee.vcmp.gt.s16 q%d,q%d,q%d", d, a, b)); break; // GT, LE
    }
    if (x.kind == V_NE || x.kind == V_GE || x.kind == V_LE)
        emit(g, string_format("ee.notq q%d,q%d", d, d));
    if (x.kind >= V_EQ)
    {
        int one = constQ(g, 1);
        emit(g, string_format("ee.andq q%d,q%d,q%d", d, d, one));
        if (ownedQ(g, p, one) && one != d)
            freeQ(g, one);
    }
    if (a != d && ownedQ(g, p, a))
        freeQ(g, a);
    if (b != d && ownedQ(g, p, b))
        freeQ(g, b);
    return d;
}

// the statements for one half; the stored value's register
static int vectorHalf(VGen *g, VPlan *p, int half)
{
    for (int t = 0; t < p->ntemp; t++)
        g->qTemp[t] = -1;
    int result = -1;
    for (int s = 0; s < p->nstmt; s++)
    {
        int r = vectorExpr(g, p, p->stmtExpr[s], half);
        if (g->failed)
            return 0;
        if (!ownedQ(g, p, r)) // a constant or a temporary: copied
        {
            int c = allocQ(g);
            emit(g, string_format("ee.orq q%d,q%d,q%d", c, r, r));
            r = c;
        }
        if (p->stmtTemp[s] >= 0)
        {
            int ff = constQ(g, 255);
            emit(g, string_format("ee.andq q%d,q%d,q%d", r, r, ff));
            if (ownedQ(g, p, ff) && ff != r)
                freeQ(g, ff);
            g->qTemp[p->stmtTemp[s]] = r;
        }
        else
        {
            // the low bytes of the 8 lanes, then 8 bytes stored (the
            // address is a multiple of 16, the second half's of 8)
            int junk = allocQ(g);
            if (g->failed)
                return 0;
            emit(g, string_format("ee.vunzip.8 q%d,q%d", r, junk));
            elementAddress(g, p, p->storeRef, p->storeOff + 8 * half, 10);
            emit(g, string_format("ee.vst.l.64.ip q%d,a10,0", r));
            freeQ(g, junk);
            freeQ(g, r);
            result = r;
        }
    }
    for (int t = 0; t < p->ntemp; t++)
        if (g->qTemp[t] >= 0)
        {
            int q = g->qTemp[t];
            g->qTemp[t] = -1;
            freeQ(g, q);
        }
    return result;
}

// `if (a8 >= a9) goto target`, with a reach of a jump
static void jumpIfDone(VGen *g, const char *cmp, int x, int y, const char *target)
{
    char *skip = newLabel(g);
    emit(g, string_format("%s a%d,a%d,%s", cmp, x, y, skip));
    emit(g, string_format("j %s", target));
    emit(g, string_format("%s:", skip));
    free(skip);
}

// The constants and which of them get a register: the most used first,
// as long as they and a half's peak fit in q0..q7.
static bool chooseConstants(VGen *g, VPlan *p)
{
    g->nconst = 0;
    int extra[2] = {p->usesCompare ? 1 : -1, p->usesTemp ? 255 : -1};
    for (int e = -2; e < p->ne; e++)
    {
        int v;
        if (e < 0)
        {
            if (extra[e + 2] < 0)
                continue;
            v = extra[e + 2];
        }
        else if (p->e[e].kind == V_CONST)
            v = p->e[e].value;
        else
            continue;
        int c = constIndex(g, v);
        if (c < 0)
        {
            if (g->nconst == VMAX_CONST)
                return false;
            c = g->nconst++;
            g->constVal[c] = v;
            g->constUses[c] = 0;
            g->constReg[c] = -1;
        }
        g->constUses[c] += e < 0 ? 4 : 1; // 1 and 255: once per comparison or temporary
    }
    int kept = 0;
    for (int round = 0; round < g->nconst; round++)
    {
        int best = -1;
        for (int c = 0; c < g->nconst; c++)
            if (g->constReg[c] == -1 && (best < 0 || g->constUses[c] > g->constUses[best]))
                best = c;
        if (best < 0)
            break;
        g->constReg[best] = 99; // tried as kept
        if (kept + 1 + halfNeed(g, p) <= 8)
            kept++;
        else
            g->constReg[best] = -2; // in the table only
    }
    for (int c = 0; c < g->nconst; c++)
        if (g->constReg[c] == -2)
            g->constReg[c] = -1;
    return kept + halfNeed(g, p) <= 8;
}

static bool generate(VGen *g, VPlan *p, const char *name)
{
    g->failed = false;
    g->label = 0;
    if (!chooseConstants(g, p))
        return false;
    // a2..: the rows (arguments) become the rows' addresses
    emit(g, string_format("%s:", name));
    emit(g, string_format("entry a1,%d", 48 + 16 * g->nconst));
    // the constants' table, at a7: a multiple of 16 (ee.vld.128 ignores the
    // address's low 4 bits), whatever a1's alignment
    emit(g, string_format("addi a7,a1,31"));
    emit(g, string_format("movi a10,-16"));
    emit(g, string_format("and a7,a7,a10"));
    for (int c = 0; c < g->nconst; c++)
    {
        emit(g, string_format("movi a10,%d", g->constVal[c]));
        emit(g, string_format("extui a10,a10,0,16"));
        emit(g, string_format("slli a11,a10,16"));
        emit(g, string_format("or a10,a10,a11"));
        for (int w = 0; w < 4; w++)
            emit(g, string_format("s32i a10,a7,%d", 16 * c + 4 * w));
    }
    // the last first: ref r's row is argument argOf[r] <= r, which an
    // address written for a later ref would otherwise overwrite
    int argOf[VMAX_REF], arg = 0;
    for (int r = 0; r < p->nref; r++)
        argOf[r] = p->ref[r].row != NULL ? arg++ : -1;
    for (int r = p->nref - 1; r >= 0; r--)
    {
        int reg = 2 + r;
        if (p->ref[r].row != NULL)
        {
            // a2+argOf[r] holds the row: a2+r = array + row * width
            emit(g, string_format("movi a10,%d", p->ref[r].width));
            emit(g, string_format("mull a10,a%d,a10", 2 + argOf[r]));
            emit(g, string_format("l32r a%d,@_%s", reg, p->ref[r].array));
            emit(g, string_format("add a%d,a%d,a10", reg, reg));
        }
        else
            emit(g, string_format("l32r a%d,@_%s", reg, p->ref[r].array));
    }
    char *head = newLabel(g), *vec = newLabel(g), *vloop = newLabel(g), *tail = newLabel(g), *done = newLabel(g);
    g->aFree = 0xFC00; // a10..a15
    emit(g, string_format("movi a8,%d", p->from));
    emit(g, string_format("movi a9,%d", p->to));
    // one at a time until the store address is a multiple of 16
    emit(g, string_format("%s:", head));
    jumpIfDone(g, "blt", 8, 9, done);
    elementAddress(g, p, p->storeRef, p->storeOff, 10);
    emit(g, string_format("extui a10,a10,0,4"));
    emit(g, string_format("beqz a10,%s", vec));
    scalarBody(g, p);
    emit(g, string_format("addi a8,a8,1"));
    emit(g, string_format("j %s", head));
    // 16 at a time
    emit(g, string_format("%s:", vec));
    g->qFree = 0xFF;
    for (int c = 0; c < g->nconst; c++)
        if (g->constReg[c] >= 0)
        {
            int q = allocQ(g);
            g->constReg[c] = q;
            emit(g, string_format("addi a10,a7,%d", 16 * c));
            emit(g, string_format("ee.vld.128.ip q%d,a10,0", q));
        }
    emit(g, string_format("%s:", vloop));
    emit(g, string_format("addi a10,a8,16"));
    jumpIfDone(g, "bge", 9, 10, tail);
    vectorHalf(g, p, 0);
    vectorHalf(g, p, 1);
    emit(g, string_format("addi a8,a8,16"));
    emit(g, string_format("j %s", vloop));
    // one at a time for the rest
    emit(g, string_format("%s:", tail));
    jumpIfDone(g, "blt", 8, 9, done);
    g->aFree = 0xFC00;
    scalarBody(g, p);
    emit(g, string_format("addi a8,a8,1"));
    emit(g, string_format("j %s", tail));
    emit(g, string_format("%s:", done));
    emit(g, string_format("retw.n"));
    free(head);
    free(vec);
    free(vloop);
    free(tail);
    free(done);
    return !g->failed;
}

static bool enabled()
{
    if (vectorizeOn < 0)
        vectorizeOn = getenv("ASM_VECTORIZE") != NULL ? 1 : 0;
    return vectorizeOn == 1;
}

bool vectorizeLoop(NodeToken *nd)
{
    if (!enabled())
        return false;
    VPlan *p = (VPlan *)calloc(1, sizeof(VPlan));
    if (p == NULL)
        return false;
    bool h = loopHeader(p, nd);
    bool b = h && loopBody(p, nd->getChildPtr(3));
    bool ok = h && b && p->from < p->to;
    int nargs = 0;
    for (int r = 0; ok && r < p->nref; r++)
        nargs += p->ref[r].row != NULL;
    // the arguments go in a10..a13, the rows computed in the registers above
    ok = ok && nargs <= 4 && register_numl.get() >= 15;
    VGen *g = NULL;
    char *name = NULL;
    if (ok)
    {
        g = new VGen();
        g->fn = vecCount;
        char sig[40] = "";
        for (int a = 0; a < nargs; a++)
            strcat(sig, a == 0 ? "num" : "|num");
        name = string_format("@____vec%d(%s)", vecCount, sig);
        ok = g != NULL && generate(g, p, name);
    }
    if (ok)
    {
        vecCount++;
        // the call: each row into a10, a11...
        int arg = 0;
        for (int r = 0; r < p->nref; r++)
            if (p->ref[r].row != NULL)
            {
                register_numl.duplicate();
                p->ref[r].row->visitNode();
                register_numl.pop();
                bufferText->addAfter(string_format("mov a%d,a%d", 10 + arg, register_numl.get()));
                arg++;
            }
        bufferText->addAfter(string_format("call8 %s", name));
        asmFunctions.end();
        for (int k = 0; k < g->lines.size(); k++)
            asmFunctions.addAfter((const char *)g->lines.get(k));
    }
    if (g != NULL)
    {
        for (int k = 0; k < g->lines.size(); k++)
            free(g->lines.get(k));
        g->lines.clear();
        delete g;
    }
    free(name);
    free(p);
    return ok;
}
