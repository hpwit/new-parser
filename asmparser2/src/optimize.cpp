#include "optimize.h"
#include "string_functions.h"
#include "vect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// RAII wrapper around a space/comma split of an assembly-line fragment.
// str_split() mallocs a fresh copy of every token; wrapping it like this
// means we never have to remember to free the pieces by hand at every one
// of the many call sites below.
struct SplitResult
{
    vect<char *> parts;
    SplitResult(char *str, const char *delim)
    {
        str_split(&parts, str, (char *)delim);
    }
    ~SplitResult()
    {
        parts.empty();
    }
    int size() { return parts.size(); }
    char *get(int i) { return parts.get(i); }
};

static char *ownedCopy(const char *s)
{
    return string_format("%s", s);
}

// ---------------------------------------------------------------------
// Lightweight liveness analysis, used by Pass 3 below to replace an ad
// hoc, single-register special case with a real correctness check.
//
// The bug this replaces: Pass 3's fold ("<op> aX,...; mov aY,aX" ->
// "<op> aY,...", applied whenever aX looked "otherwise dead") had no way
// to see that a2 -- this function's own implicit return-value register,
// which retw.n reads with no textual operand naming it -- might still be
// needed downstream. On optimize()'s second pass (parser.cpp calls it
// twice: once, then again after removeBlankLines() has made lines
// adjacent that weren't the first time this fold ran), it folded a
// return value's own computation straight into an outgoing
// call-argument register, which an intervening call8/callExt then
// clobbered before retw.n ever ran -- a real, QEMU-confirmed silent-
// wrong-return-value bug (gen_saveload.cpp's script -- computes c,
// passes it to report(c), then returns c -- came back 0 instead of c).
//
// This is a standard backward "live variable" dataflow over the
// existing instruction stream, treated as its own tiny CFG: labels are
// join points, branches/jumps are edges, call8/callExt are ordinary
// fall-through instructions that clobber a8..a15 (the callee's own new
// register window) and conservatively read a10..a15 (this compiler
// never passes more than 6 arguments), and retw.n is an exit node that
// implicitly *uses* a2 -- closing exactly the gap above. Scoped to
// a0..a15 only (float registers/b0 are never fold targets in this file)
// and represented as one 16-bit bitmask per line for simplicity and
// speed: "lightweight" in the literal sense -- no separate IR, no SSA,
// just one scan of the existing text lines plus a bounded fixed-point
// iteration, matching every other pass in this file's own
// re-scan-the-text style.
//
// Deliberately conservative wherever real per-call argument counts
// aren't tracked here (e.g. treating every call8/callExt as reading
// a10..a15 even when the callee takes fewer): over-approximating *use*
// only ever costs a missed optimization, never a wrong one. The same
// applies to `ok` below -- if a branch/jump target can't be resolved
// (should never happen against this compiler's own generated labels,
// but this is a correctness-critical query, not a hint), every query
// answers "live" rather than trust a partial analysis.
struct Liveness
{
    int n = 0;
    uint16_t *liveOut = NULL; // liveOut[i]: a0..a15 bitmask, live *after* line i
    bool ok = false;

    ~Liveness() { free(liveOut); }

    bool isLiveAfter(int i, int reg) const
    {
        if (!ok || reg < 0 || reg > 15 || i < 0 || i >= n)
            return true; // unknown -- assume live, never optimize on a guess
        return (liveOut[i] & (uint16_t)(1u << reg)) != 0;
    }
};

// Returns 0..15 if `token` is exactly "aN" with N in that range, else -1.
static int livenessParseReg(const char *token)
{
    if (token == NULL || token[0] != 'a' || token[1] == 0)
        return -1;
    for (const char *p = token + 1; *p; p++)
        if (*p < '0' || *p > '9')
            return -1;
    int n = atoi(token + 1);
    return (n >= 0 && n <= 15) ? n : -1;
}

static Liveness *computeLiveness(Text *text)
{
    Liveness *lv = new Liveness();
    int n = text->size();
    lv->n = n;
    lv->liveOut = (uint16_t *)calloc(n > 0 ? n : 1, sizeof(uint16_t));
    if (n == 0)
    {
        lv->ok = true;
        return lv;
    }

    uint16_t *defMask = (uint16_t *)calloc(n, sizeof(uint16_t));
    uint16_t *useMask = (uint16_t *)calloc(n, sizeof(uint16_t));
    int *succ1 = (int *)malloc(n * sizeof(int));
    int *succ2 = (int *)malloc(n * sizeof(int));
    for (int i = 0; i < n; i++)
    {
        succ1[i] = -1;
        succ2[i] = -1;
    }

    // Label name (colon stripped) -> line index, for resolving
    // branch/jump targets.
    vect<char *> labelNames;
    vect<int> labelLines;
    for (int i = 0; i < n; i++)
    {
        char *tmp = *text->getChildAtPos(i);
        if (tmp == NULL || strcmp(tmp, " ") == 0 || strcmp(tmp, "") == 0)
            continue;
        if (strchr(tmp, ':') == NULL)
            continue;
        char *nm = ownedCopy(tmp);
        size_t len = strlen(nm);
        if (len > 0 && nm[len - 1] == ':')
            nm[len - 1] = 0;
        labelNames.push_back(nm);
        labelLines.push_back(i);
    }

    auto resolveLabel = [&](const char *name) -> int
    {
        if (name == NULL)
            return -1;
        for (int k = 0; k < labelNames.size(); k++)
            if (strcmp(labelNames.get(k), name) == 0)
                return labelLines.get(k);
        return -1;
    };

    auto nextLine = [&](int from) -> int
    {
        for (int k = from; k < n; k++)
        {
            char *t = *text->getChildAtPos(k);
            if (t != NULL && strcmp(t, " ") != 0 && strcmp(t, "") != 0)
                return k;
        }
        return -1;
    };

    bool ok = true;
    for (int i = 0; i < n; i++)
    {
        char *tmp = *text->getChildAtPos(i);
        if (tmp == NULL || strcmp(tmp, " ") == 0 || strcmp(tmp, "") == 0)
            continue;

        if (strchr(tmp, ':') != NULL)
        {
            succ1[i] = nextLine(i + 1);
            continue;
        }

        SplitResult d(tmp, " ");
        if (d.size() == 0)
        {
            succ1[i] = nextLine(i + 1);
            continue;
        }
        char *op = d.get(0);
        char *operands = d.size() > 1 ? d.get(1) : NULL;

        if (strcmp(op, "retw.n") == 0)
        {
            useMask[i] |= (uint16_t)(1u << 2); // implicit ABI read of a2
            // no successor: function exit
        }
        else if (operands == NULL)
        {
            // Every other opcode this file dispatches on below always
            // carries operands when correctly formed (verified against
            // parser_enum.cpp's asmInstructionsName table plus every
            // hand-built "op %s"/"movExt"/"callExt" string in
            // visitnode.cpp) -- str_split() would dereference a NULL
            // pointer if handed one, so treat an operand-less line here
            // as unparseable (SplitResult's whole reason to exist below
            // is precisely to never be handed one).
            ok = false;
            succ1[i] = nextLine(i + 1);
        }
        else if (strcmp(op, "j") == 0)
        {
            SplitResult p(operands, ",");
            int t = p.size() > 0 ? resolveLabel(p.get(p.size() - 1)) : -1;
            if (t < 0)
                ok = false;
            succ1[i] = t;
        }
        else if (strcmp(op, "call8") == 0 || strcmp(op, "callExt") == 0)
        {
            // Windowed call: a8..a15 become the callee's own new
            // register window and are fully clobbered; a10..a15 are
            // conservatively treated as outgoing arguments. a0..a7 --
            // including a2, the return-value slot -- are hardware-
            // preserved by the window rotation, so they're neither
            // defined nor used here.
            for (int r = 8; r <= 15; r++)
                defMask[i] |= (uint16_t)(1u << r);
            for (int r = 10; r <= 15; r++)
                useMask[i] |= (uint16_t)(1u << r);
            if (strcmp(op, "callExt") == 0 && operands != NULL)
            {
                SplitResult ops(operands, ",");
                int r = ops.size() > 0 ? livenessParseReg(ops.get(0)) : -1;
                if (r >= 0)
                    useMask[i] |= (uint16_t)(1u << r);
            }
            succ1[i] = nextLine(i + 1);
        }
        else if (strcmp(op, "blt") == 0 || strcmp(op, "bltu") == 0 ||
                 strcmp(op, "bge") == 0 || strcmp(op, "bgeu") == 0 ||
                 strcmp(op, "beq") == 0 || strcmp(op, "bne") == 0)
        {
            SplitResult p(operands, ",");
            if (p.size() >= 3)
            {
                int r0 = livenessParseReg(p.get(0));
                int r1 = livenessParseReg(p.get(1));
                if (r0 >= 0)
                    useMask[i] |= (uint16_t)(1u << r0);
                if (r1 >= 0)
                    useMask[i] |= (uint16_t)(1u << r1);
            }
            int t = p.size() > 0 ? resolveLabel(p.get(p.size() - 1)) : -1;
            if (t < 0)
                ok = false;
            succ1[i] = t;
            succ2[i] = nextLine(i + 1);
        }
        else if (strcmp(op, "blti") == 0 || strcmp(op, "beqi") == 0 ||
                 strcmp(op, "bgei") == 0 || strcmp(op, "bnei") == 0 ||
                 strcmp(op, "bnez") == 0 || strcmp(op, "beqz") == 0 ||
                 strcmp(op, "bt") == 0 || strcmp(op, "bf") == 0)
        {
            // blt*i/bnez/beqz: one general register (b0 for bt/bf isn't
            // tracked, so no use added there).
            SplitResult p(operands, ",");
            if (p.size() >= 2 && strcmp(op, "bt") != 0 && strcmp(op, "bf") != 0)
            {
                int r0 = livenessParseReg(p.get(0));
                if (r0 >= 0)
                    useMask[i] |= (uint16_t)(1u << r0);
            }
            int t = p.size() > 0 ? resolveLabel(p.get(p.size() - 1)) : -1;
            if (t < 0)
                ok = false;
            succ1[i] = t;
            succ2[i] = nextLine(i + 1);
        }
        else
        {
            SplitResult p(operands, ",");
            auto reg = [&](int idx) -> int
            { return idx < p.size() ? livenessParseReg(p.get(idx)) : -1; };

            if (strcmp(op, "movi") == 0 || strcmp(op, "l32r") == 0 || strcmp(op, "movExt") == 0)
            {
                int rd = reg(0);
                if (rd >= 0)
                    defMask[i] |= (uint16_t)(1u << rd);
            }
            else if (strcmp(op, "mov") == 0 || strcmp(op, "movr") == 0 ||
                     strcmp(op, "neg") == 0 || strcmp(op, "abs") == 0 ||
                     strcmp(op, "sll") == 0 || strcmp(op, "srl") == 0 ||
                     strcmp(op, "addi") == 0)
            {
                int rd = reg(0), rs = reg(1);
                if (rd >= 0)
                    defMask[i] |= (uint16_t)(1u << rd);
                if (rs >= 0)
                    useMask[i] |= (uint16_t)(1u << rs);
            }
            else if (strcmp(op, "add") == 0 || strcmp(op, "sub") == 0 ||
                     strcmp(op, "mull") == 0 || strcmp(op, "quou") == 0 ||
                     strcmp(op, "quos") == 0 || strcmp(op, "remu") == 0 ||
                     strcmp(op, "and") == 0 || strcmp(op, "or") == 0 ||
                     strcmp(op, "addx2") == 0 || strcmp(op, "addx4") == 0 ||
                     strcmp(op, "addx8") == 0 || strcmp(op, "subx2") == 0 ||
                     strcmp(op, "subx4") == 0 || strcmp(op, "subx8") == 0)
            {
                int rd = reg(0), rs1 = reg(1), rs2 = reg(2);
                if (rd >= 0)
                    defMask[i] |= (uint16_t)(1u << rd);
                if (rs1 >= 0)
                    useMask[i] |= (uint16_t)(1u << rs1);
                if (rs2 >= 0)
                    useMask[i] |= (uint16_t)(1u << rs2);
            }
            else if (strcmp(op, "l32i") == 0 || strcmp(op, "l16i") == 0 ||
                     strcmp(op, "l16ui") == 0 || strcmp(op, "l16si") == 0 ||
                     strcmp(op, "l8ui") == 0 || strcmp(op, "extui") == 0)
            {
                int rd = reg(0), rs = reg(1);
                if (rd >= 0)
                    defMask[i] |= (uint16_t)(1u << rd);
                if (rs >= 0)
                    useMask[i] |= (uint16_t)(1u << rs);
            }
            else if (strcmp(op, "rfr") == 0 || strcmp(op, "trunc.s") == 0)
            {
                // Destination is an a-register; source is a float
                // register (fN), not tracked here.
                int rd = reg(0);
                if (rd >= 0)
                    defMask[i] |= (uint16_t)(1u << rd);
            }
            else if (strcmp(op, "s32i") == 0 || strcmp(op, "s16i") == 0 ||
                     strcmp(op, "s8i") == 0 || strcmp(op, "ssi") == 0 ||
                     strcmp(op, "lsi") == 0 || strcmp(op, "wfr") == 0 ||
                     strcmp(op, "float.s") == 0)
            {
                // Store-family (source, base, offset) and the int-facing
                // operand of the two float-boundary ops (wfr's/
                // float.s's *integer* operand is a use, not a def; lsi's
                // float destination isn't tracked, only its base
                // register is).
                int r0 = reg(0), r1 = reg(1);
                if (r0 >= 0)
                    useMask[i] |= (uint16_t)(1u << r0);
                if (r1 >= 0)
                    useMask[i] |= (uint16_t)(1u << r1);
            }
            else if (strcmp(op, "ssl") == 0 || strcmp(op, "wsr") == 0)
            {
                int r0 = reg(0);
                if (r0 >= 0)
                    useMask[i] |= (uint16_t)(1u << r0);
            }
            // else: no a-register operands this file ever emits for this
            // op (entry, .bytes, .global, and float-only ops like
            // add.s/mov.s/neg.s/abs.s/olt.s/oeq.s/ole.s) -- correctly
            // contribute no def/use.
            succ1[i] = nextLine(i + 1);
        }
    }

    // Backward fixed-point iteration -- bounded, not a worklist: this
    // file's functions are small enough (hundreds of lines at most) that
    // a plain sweep-to-convergence is simple and fast enough.
    uint16_t *liveIn = (uint16_t *)calloc(n, sizeof(uint16_t));
    bool changed = true;
    int guard = 0;
    while (changed && guard++ < n + 16)
    {
        changed = false;
        for (int i = n - 1; i >= 0; i--)
        {
            uint16_t out = 0;
            if (succ1[i] >= 0)
                out |= liveIn[succ1[i]];
            if (succ2[i] >= 0)
                out |= liveIn[succ2[i]];
            uint16_t in = useMask[i] | (uint16_t)(out & ~defMask[i]);
            if (out != lv->liveOut[i] || in != liveIn[i])
            {
                lv->liveOut[i] = out;
                liveIn[i] = in;
                changed = true;
            }
        }
    }

    for (int k = 0; k < labelNames.size(); k++)
        free(labelNames.get(k));
    free(liveIn);
    free(defMask);
    free(useMask);
    free(succ1);
    free(succ2);
    lv->ok = ok;
    return lv;
}

// from/to/index track live "aY came from aX at line index" facts (as set up
// by a `movr` instruction) so a later use of aY can be rewritten to use aX
// directly and the movr dropped. from/to entries are owned copies (see
// ownedCopy) independent of the Text buffer's own strings.
static void trackReset(vect<char *> &from, vect<char *> &to, vect<int> &index)
{
    from.empty();
    to.empty();
    index.clear();
}

static void trackRemoveAt(vect<char *> &from, vect<char *> &to, vect<int> &index, int i)
{
    free(from.get(i));
    free(to.get(i));
    from.erase(i);
    to.erase(i);
    index.erase(i);
}

static void trackAdd(vect<char *> &from, vect<char *> &to, vect<int> &index, char *f, char *t, int idx)
{
    from.push_back(ownedCopy(f));
    to.push_back(ownedCopy(t));
    index.push_back(idx);
}

void optimize(Text *text)
{
    // Pass 1: for each register a3..a10, walk the buffer and blank out an
    // instruction that reloads a register with the exact same source
    // instruction as the last one seen for it, provided nothing since
    // (a label, a call, or an unrelated write to that register) could have
    // invalidated it.
    for (int regnum = 3; regnum < 11; regnum++)
    {
        char *registername = string_format("a%d", regnum);
        char *str = ownedCopy("__");

        for (int i = 0; i < text->size(); i++)
        {
            char *tmp = *text->getChildAtPos(i);
            if (tmp != NULL && strlen(tmp) > 0)
            {
                if (strncmp(tmp, "@_", 2) == 0 || strchr(tmp, ':') != NULL)
                {
                    free(str);
                    str = ownedCopy("__");
                }
                else
                {
                    SplitResult d(tmp, " ");
                    if (d.size() > 0 && strcmp(d.get(0), "call8") == 0 && regnum >= 8)
                    {
                        free(str);
                        str = ownedCopy("_");
                    }
                    else if (d.size() > 1)
                    {
                        // NOTE: this is a prefix match inherited from the
                        // original algorithm, so "a1" also matches "a10",
                        // "a11", etc. Preserved as-is rather than changed
                        // to an exact-register match.
                        if (strncmp(d.get(1), registername, strlen(registername)) == 0)
                        {
                            if (strcmp(str, tmp) == 0)
                            {
                                text->replaceText(i, " ");
                            }
                            else
                            {
                                char *op = d.get(0);
                                free(str);
                                if (strcmp(op, "movi") == 0 || strcmp(op, "l32i") == 0 ||
                                    strcmp(op, "l16i") == 0 || strcmp(op, "l16ui") == 0 ||
                                    strcmp(op, "l8ui") == 0 || strcmp(op, "movExt") == 0)
                                    str = ownedCopy(tmp);
                                else
                                    str = ownedCopy("__");
                            }
                        }
                    }
                }
            }
        }

        free(str);
        free(registername);
    }

    // Pass 2: a float.s reload is dead if either an identical float.s line
    // already ran with nothing since touching its destination register, or
    // the register a float.s loaded into gets overwritten before its first
    // use.
    char *before = ownedCopy(" ");
    int indexbefore = 0;
    char *torep = ownedCopy(" ");

    for (int i = 0; i < text->size(); i++)
    {
        char *tmp = *text->getChildAtPos(i);
        if (tmp == NULL)
        {
            printf("************************** %d \n\r", i);
            continue;
        }
        if (strcmp(tmp, " ") == 0 || strcmp(tmp, "") == 0)
            continue;

        SplitResult d(tmp, " ");
        if (d.size() > 0 && strcmp(d.get(0), "float.s") == 0)
        {
            if (strcmp(tmp, before) == 0)
            {
                text->replaceText(indexbefore, " ");
                free(before);
                before = ownedCopy(" ");
            }
            else
            {
                free(before);
                before = ownedCopy(tmp);
                indexbefore = i;
                SplitResult d2(d.get(1), ",");
                free(torep);
                torep = ownedCopy(d2.size() > 1 ? d2.get(1) : " ");
            }
        }
        else if (strchr(d.get(0), ':') != NULL || strcmp(d.get(0), "call8") == 0 || strcmp(d.get(0), "callExt") == 0)
        {
            free(before);
            before = ownedCopy(" ");
            free(torep);
            torep = ownedCopy(" ");
        }
        else if (d.size() > 1)
        {
            SplitResult d2(d.get(1), ",");
            if (d2.size() > 0 && strcmp(d2.get(0), torep) == 0)
            {
                free(before);
                before = ownedCopy(" ");
                free(torep);
                torep = ownedCopy(" ");
            }
        }
    }

    // Pass 3: fold "<op> aX,...; mov aY,aX" into "<op> aY,..." when aX is
    // otherwise dead right after the mov -- "otherwise dead" now answered
    // by a real liveness query (see computeLiveness()'s own comment)
    // instead of an ad hoc guess, so this covers every register with an
    // implicit later use invisible to this pass' own text scan (a2 before
    // a retw.n being the one that actually bit -- see gen_saveload.cpp),
    // not just that one hardcoded case.
    free(before);
    before = ownedCopy(" ");
    indexbefore = 0;

    Liveness *liveness = computeLiveness(text);

    for (int i = 0; i < text->size(); i++)
    {
        char *tmp = *text->getChildAtPos(i);
        if (tmp == NULL)
            continue;
        if (strcmp(tmp, " ") == 0 || strcmp(tmp, "") == 0)
            continue;

        SplitResult d2(tmp, " ");
        if (d2.size() > 0 && strcmp(d2.get(0), "mov") == 0)
        {
            SplitResult d(before, " ");
            char *op = d.size() > 0 ? d.get(0) : (char *)"";
            if (d.size() > 1 &&
                (strcmp(op, "mull") == 0 || strcmp(op, "quou") == 0 || strcmp(op, "quos") == 0 ||
                 strcmp(op, "sub") == 0 || strcmp(op, "add") == 0 || strcmp(op, "mov") == 0 ||
                 strcmp(op, "movi") == 0 || strcmp(op, "l32i") == 0 || strcmp(op, "l32r") == 0 ||
                 strcmp(op, "l16i") == 0 || strcmp(op, "l16ui") == 0 || strcmp(op, "l8ui") == 0 ||
                 strcmp(op, "addi") == 0))
            {
                SplitResult p(d.get(1), ",");
                SplitResult p2(d2.get(1), ",");
                // Folding eliminates the write into p.get(0) (before's own
                // destination) at `indexbefore`, relying entirely on the
                // mov's destination (p2.get(0)) to carry the value from
                // here on -- only sound when nothing downstream still
                // needs p.get(0) itself, i.e. it's genuinely dead
                // immediately after this mov.
                bool sourceStillLiveAfterFold =
                    liveness->isLiveAfter(i, livenessParseReg(p.get(0)));
                if (p.size() > 0 && p2.size() > 1 && strcmp(p.get(0), p2.get(1)) == 0 &&
                    !sourceStillLiveAfterFold)
                {
                    text->replaceText(indexbefore, " ");
                    char *newstr = string_format("%s %s", d.get(0), p2.get(0));
                    for (int k = 1; k < p.size(); k++)
                        newstr = str_concat("%s,%s", newstr, p.get(k));
                    text->replaceText(i, newstr);
                    // replaceText() just freed the old content at i (which
                    // is what tmp still points to) and stored the
                    // replacement -- possibly a pooled duplicate rather
                    // than newstr itself. Re-read it so "before = tmp"
                    // below reflects what's actually there now.
                    tmp = *text->getChildAtPos(i);
                }
            }
        }

        free(before);
        before = ownedCopy(tmp);
        indexbefore = i;
    }
    free(before);
    delete liveness;

    // Pass 4: register-copy propagation. A `movr aY,aX` records "aY is
    // currently just a copy of aX"; later instructions that read aY get
    // rewritten to read aX directly and the movr's source slot is blanked,
    // until a label/call or a write to either register invalidates the
    // fact. Plain `mov aY,aX` gets the same treatment, but registered
    // separately, further down in this same pass's op-dispatch (see the
    // neg/abs/mov/sll/srl branch's own comment) -- `mov` already has to
    // run through that dispatch regardless (to consume/rewrite against
    // *older* facts, e.g. blanking a stale movr whose value this mov
    // itself is just forwarding), so folding trackAdd() into the tail of
    // that existing branch avoids re-processing the same line twice with
    // conflicting effects: doing it here too, as a second, independent
    // trigger, would have this pass's own dest-invalidation check (a few
    // lines into that same dispatch branch) immediately undo the fact
    // this trigger just added, since by the time dispatch runs, `from`
    // already contains this line's own destination register -- confirmed
    // the hard way, via a `movr a15,a2 / mov a2,a15` return-value
    // round-trip (previously fully eliminated) surviving intact once
    // both triggers ran independently on the same "mov a2,a15" line.
    vect<char *> from, to;
    vect<int> index;

    for (int i = 0; i < text->size(); i++)
    {
        char *tmp = *text->getChildAtPos(i);
        if (tmp == NULL)
            continue;
        if (strcmp(tmp, " ") == 0 || strcmp(tmp, "") == 0)
            continue;

        SplitResult d(tmp, " ");
        if (d.size() == 0)
            continue;

        if (strcmp(d.get(0), "movr") == 0 && d.size() > 1)
        {
            SplitResult d1(d.get(1), ",");
            if (d1.size() > 1)
            {
                bool found = false;
                int ind = -1;
                for (int mp = 0; mp < index.size(); mp++)
                    if (strcmp(d1.get(1), to.get(mp)) == 0)
                    {
                        found = true;
                        ind = mp;
                    }
                if (found)
                    trackRemoveAt(from, to, index, ind);

                found = false;
                for (int mp = 0; mp < index.size(); mp++)
                    if (strcmp(d1.get(0), from.get(mp)) == 0)
                    {
                        found = true;
                        ind = mp;
                    }
                if (found)
                    trackRemoveAt(from, to, index, ind);

                trackAdd(from, to, index, d1.get(0), d1.get(1), i);
            }
        }

        if (strncmp(d.get(0), "b", 1) == 0 && d.size() > 1)
        {
            SplitResult d2(d.get(1), ",");
            bool found = false;
            int ind = -1;
            for (int mp = 0; mp < index.size(); mp++)
                if (d2.size() > 0 && strcmp(d2.get(0), to.get(mp)) == 0)
                {
                    found = true;
                    ind = mp;
                }
            if (found)
                trackRemoveAt(from, to, index, ind);
        }

        // "mov" is the one op-dispatch branch below that can *establish*
        // a tracked fact (see its own comment), not just consume/
        // invalidate an existing one -- so, unlike every other branch
        // here, it still needs to run even when nothing is tracked yet
        // (e.g. fib()'s very first `mov a15,a10`, with no prior movr/mov
        // anywhere to have populated `index`).
        if (index.size() == 0 && strcmp(d.get(0), "mov") != 0)
            continue;

        char *op = d.get(0);
        if ((strcmp(op, "bnez") == 0 || strcmp(op, "beqz") == 0) && d.size() > 1)
        {
            SplitResult d2(d.get(1), ",");
            char *newstr = string_format("%s ", op);
            bool found = false;
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(0), from.get(mp)) == 0)
                {
                    newstr = str_concat("%s%s,", newstr, to.get(mp));
                    text->replaceText(index.get(mp), " ");
                    found = true;
                }
            if (!found)
                newstr = str_concat("%s%s,", newstr, d2.get(0));
            newstr = str_concat("%s%s", newstr, d2.get(1));
            text->replaceText(i, newstr);
            // The original algorithm does not drop the tracked entry here
            // even when it was consumed above -- preserved as-is.
        }
        else if ((strcmp(op, "bge") == 0 || strcmp(op, "blt") == 0 || strcmp(op, "beq") == 0 ||
                  strcmp(op, "bne") == 0 || strcmp(op, "blti") == 0 || strcmp(op, "bgei") == 0 ||
                  strcmp(op, "beqi") == 0 || strcmp(op, "bnei") == 0) &&
                 d.size() > 1)
        {
            SplitResult d2(d.get(1), ",");
            char *newstr = string_format("%s ", op);
            bool found = false;
            int ind = -1;
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(0), from.get(mp)) == 0)
                {
                    newstr = str_concat("%s%s,", newstr, to.get(mp));
                    text->replaceText(index.get(mp), " ");
                    found = true;
                    ind = mp;
                }
            if (!found)
                newstr = str_concat("%s%s,", newstr, d2.get(0));
            if (found)
                trackRemoveAt(from, to, index, ind);

            found = false;
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(1), from.get(mp)) == 0)
                {
                    newstr = str_concat("%s%s,", newstr, to.get(mp));
                    text->replaceText(index.get(mp), " ");
                    found = true;
                    ind = mp;
                }
            if (!found)
                newstr = str_concat("%s%s,", newstr, d2.get(1));

            newstr = str_concat("%s%s", newstr, d2.get(2));
            text->replaceText(i, newstr);

            if (found)
                trackRemoveAt(from, to, index, ind);
        }
        else if ((strcmp(op, "movi") == 0 || strcmp(op, "rsr") == 0 || strcmp(op, "wsr") == 0 ||
                  strcmp(op, "ssl") == 0 || strcmp(op, "l32r") == 0 || strcmp(op, "movExt") == 0 ||
                  strcmp(op, "extui") == 0 || strcmp(op, "rfr") == 0) &&
                 d.size() > 1)
        {
            // l32r/movExt/extui/rfr all write a fresh, unrelated value
            // into their first operand -- same as movi -- but were
            // missing from this branch entirely, so nothing ever
            // invalidated a stale "aY is a copy of aX" fact once aX got
            // overwritten this way: a later read of aY could still be
            // rewritten to aX, silently picking up whatever aX held
            // *before* this line instead of after. Real, QEMU-confirmed
            // bug: `movr a15,a2 / mov a11,a15` (correctly folds to `mov
            // a11,a2`, but the "a15 == a2" fact isn't cleared) followed
            // by `l32r a15,@_some_string` then `mov a10,a15` rewrote the
            // second mov to `mov a10,a2` -- printfln()'s format-string
            // argument silently became an unrelated int value instead of
            // the string's address.
            SplitResult d2(d.get(1), ",");
            bool found = false;
            int ind = -1;
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(0), from.get(mp)) == 0)
                {
                    found = true;
                    ind = mp;
                }
            if (found)
                trackRemoveAt(from, to, index, ind);
        }
        else if ((strcmp(op, "neg") == 0 || strcmp(op, "abs") == 0 || strcmp(op, "mov") == 0 ||
                  strcmp(op, "sll") == 0 || strcmp(op, "srl") == 0) &&
                 d.size() > 1)
        {
            SplitResult d2(d.get(1), ",");
            char *newstr = string_format("%s %s,", op, d2.get(0));
            bool foundSrc = false;
            int indSrc = -1;
            char *finalSrc = d2.get(1);
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(1), from.get(mp)) == 0)
                {
                    // Copy, not alias: to.get(mp)'s storage is about to
                    // be free()'d by trackRemoveAt() below, but finalSrc
                    // still needs to be valid for this whole branch's
                    // remaining uses -- most notably the trackAdd() at
                    // its tail, past that removal.
                    finalSrc = ownedCopy(to.get(mp));
                    newstr = str_concat("%s%s", newstr, to.get(mp));
                    text->replaceText(index.get(mp), " ");
                    foundSrc = true;
                    indSrc = mp;
                }
            if (!foundSrc)
                newstr = str_concat("%s%s", newstr, d2.get(1));
            text->replaceText(i, newstr);
            // The fact just consumed above (its own movr/mov line already
            // blanked) has to be dropped from tracking now too, not just
            // textually blanked -- otherwise it lingers as a stale, no-
            // longer-true "aY is a copy of aX" claim that nothing but a
            // write *Pass 4 itself recognizes* would ever clear (see the
            // movi/l32r/movExt/extui/rfr branch's own comment for the
            // real bug this caused once combined with l32r's own missing
            // invalidation).
            if (foundSrc)
                trackRemoveAt(from, to, index, indSrc);

            bool foundDst = false;
            int indDst = -1;
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(0), from.get(mp)) == 0)
                {
                    foundDst = true;
                    indDst = mp;
                }
            if (foundDst)
                trackRemoveAt(from, to, index, indDst);

            // A plain "mov aY,aX" (unlike neg/abs/sll/srl, which compute
            // something rather than copy verbatim) is itself a fresh,
            // trackable "aY is a copy of aX" fact -- e.g.
            // _visitcallFunctionNode() (visitnode.cpp) unconditionally
            // copies a just-returned call result out of a10 into a
            // scratch register this way, even when that register's only
            // use is the very next instruction (fib()'s `mov a14,a10`
            // right before `add a2,a15,a14`, which could just read
            // `add a2,a15,a10` instead). Register it here, using
            // whatever source this rewrite just settled on (finalSrc),
            // so a later line reading aY can still be rewritten straight
            // to aX -- skipped when finalSrc == the destination (a
            // rewritten-down-to-self-move, already handled by Pass 5).
            //
            // Deliberately done here, at the tail of this branch,
            // instead of as a second independent trigger alongside
            // "movr"'s above: a `mov` line unavoidably reaches this
            // branch regardless (to consume/rewrite against *older*
            // facts, same as this comment's example needs), so adding a
            // second, separate trigger for the same line would have this
            // branch's own dest-invalidation check just above
            // immediately undo the fact that other trigger had only just
            // added -- confirmed the hard way, via a
            // `movr a15,a2 / mov a2,a15` return-value round-trip
            // (previously fully eliminated by this exact branch) surviving
            // intact once both triggers ran independently on the same line.
            if (strcmp(op, "mov") == 0 && strcmp(d2.get(0), finalSrc) != 0)
                trackAdd(from, to, index, d2.get(0), finalSrc, i);
        }
        else if ((strcmp(op, "round.s") == 0 || strcmp(op, "floor.s") == 0 || strcmp(op, "float.s") == 0 ||
                  strcmp(op, "l32i") == 0 || strcmp(op, "s32i") == 0 || strcmp(op, "l16i") == 0 ||
                  strcmp(op, "l16si") == 0 || strcmp(op, "l16ui") == 0 || strcmp(op, "l8ui") == 0 ||
                  strcmp(op, "s8i") == 0 || strcmp(op, "s16i") == 0 || strcmp(op, "addi") == 0 ||
                  strcmp(op, "trunc.s") == 0) &&
                 d.size() > 1)
        {
            SplitResult d2(d.get(1), ",");
            char *newstr = string_format("%s %s,", op, d2.get(0));
            bool found = false;
            int ind = -1;
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(1), from.get(mp)) == 0)
                {
                    newstr = str_concat("%s%s,", newstr, to.get(mp));
                    text->replaceText(index.get(mp), " ");
                    found = true;
                    ind = mp;
                }
            if (!found)
                newstr = str_concat("%s%s,", newstr, d2.get(1));
            newstr = str_concat("%s%s", newstr, d2.get(2));
            text->replaceText(i, newstr);

            found = false;
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(0), from.get(mp)) == 0)
                {
                    found = true;
                    ind = mp;
                }
            if (found)
                trackRemoveAt(from, to, index, ind);
        }
        else if ((strcmp(op, "remu") == 0 || strcmp(op, "or") == 0 || strcmp(op, "and") == 0 ||
                  strcmp(op, "mull") == 0 || strcmp(op, "sub") == 0 || strcmp(op, "add") == 0 ||
                  strcmp(op, "quou") == 0 || strcmp(op, "quos") == 0 ||
                  strcmp(op, "addx2") == 0 || strcmp(op, "addx4") == 0 || strcmp(op, "addx8") == 0 ||
                  strcmp(op, "subx2") == 0 || strcmp(op, "subx4") == 0 || strcmp(op, "subx8") == 0) &&
                 d.size() > 1)
        {
            SplitResult d2(d.get(1), ",");
            char *newstr = string_format("%s %s,", op, d2.get(0));
            bool found = false;
            int ind = -1;
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(1), from.get(mp)) == 0)
                {
                    newstr = str_concat("%s%s,", newstr, to.get(mp));
                    text->replaceText(index.get(mp), " ");
                    found = true;
                    ind = mp;
                }
            if (!found)
                newstr = str_concat("%s%s,", newstr, d2.get(1));

            found = false;
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(2), from.get(mp)) == 0)
                {
                    newstr = str_concat("%s%s", newstr, to.get(mp));
                    found = true;
                    ind = mp;
                    text->replaceText(index.get(mp), " ");
                }
            if (!found)
                newstr = str_concat("%s%s", newstr, d2.get(2));

            found = false;
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(0), from.get(mp)) == 0)
                {
                    found = true;
                    ind = mp;
                }
            if (found)
                trackRemoveAt(from, to, index, ind);

            text->replaceText(i, newstr);
        }
        else if (strchr(op, ':') != NULL || strcmp(op, "call8") == 0 || strcmp(op, "callExt") == 0)
        {
            trackReset(from, to, index);
        }
    }
    trackReset(from, to, index);

    // Pass 5: "mov aX,aX" is always a no-op.
    for (int i = 0; i < text->size(); i++)
    {
        char *tmp = *text->getChildAtPos(i);
        if (tmp == NULL || strcmp(tmp, " ") == 0 || strcmp(tmp, "") == 0)
            continue;

        SplitResult d(tmp, " ");
        if (d.size() > 1 && strcmp(d.get(0), "mov") == 0)
        {
            SplitResult d2(d.get(1), ",");
            if (d2.size() > 1 && strcmp(d2.get(0), d2.get(1)) == 0)
                text->replaceText(i, " ");
        }
    }

    // Pass 6: a load immediately following a store to the same address is
    // redundant -- the register the store just came from already holds the
    // value the load would fetch.
    for (int i = 1; i < text->size(); i++)
    {
        char *tmp = *text->getChildAtPos(i);
        if (tmp == NULL || strcmp(tmp, " ") == 0 || strcmp(tmp, "") == 0)
            continue;

        SplitResult d(tmp, " ");
        if (d.size() < 2)
            continue;

        const char *loadOp = NULL;
        const char *storeOp = NULL;
        if (strcmp(d.get(0), "l32i") == 0)
        {
            loadOp = "l32i";
            storeOp = "s32i";
        }
        else if (strcmp(d.get(0), "l8ui") == 0)
        {
            loadOp = "l8ui";
            storeOp = "s8i";
        }
        else if (strcmp(d.get(0), "lsi") == 0)
        {
            loadOp = "lsi";
            storeOp = "ssi";
        }
        if (loadOp == NULL)
            continue;

        char *prev = *text->getChildAtPos(i - 1);
        if (prev == NULL || strcmp(prev, " ") == 0 || strcmp(prev, "") == 0)
            continue;

        SplitResult d2(prev, " ");
        if (d2.size() > 1 && strcmp(d2.get(0), storeOp) == 0 && strcmp(d2.get(1), d.get(1)) == 0)
            text->replaceText(i, " ");
    }
}

// See optimize.h's doc comment for why this is a separate pass from
// optimize()'s own Pass 1 rather than just widening that loop's bound:
// Pass 1's call8-only invalidation (`regnum >= 8`) is safe for a3..a10
// because only call8 (a windowed call, shifting the register file by 8)
// ever runs between two of its tracked reloads in practice at that
// range -- but a11..a15 is also exactly where callExt (an *unwindowed*
// call into arbitrary host C code, e.g. atan2/hypot/hsv in
// examples/MultiEffectController.ino-style scripts) passes outgoing
// arguments and receives its return value, so a callExt in between two
// otherwise-identical reloads has to invalidate them too, not just
// call8 -- confirmed load-bearing against a real script (verbatim from
// a user session): `movi a14,96` reloaded a few lines after an
// identical one, with a `callExt a8,@_hypot(num|num)` sitting in
// between, must NOT be treated as redundant, while the same reload
// pattern with no call in between (the common case: two array stores
// sharing one index sub-expression, e.g. rMapAngle[i]=...;
// rMapRadius[i]=...; from the same script) safely is.
void optimizeSpeed(Text *text)
{
    for (int regnum = 11; regnum < 16; regnum++)
    {
        char *registername = string_format("a%d", regnum);
        char *str = ownedCopy("__");

        for (int i = 0; i < text->size(); i++)
        {
            char *tmp = *text->getChildAtPos(i);
            if (tmp != NULL && strlen(tmp) > 0)
            {
                if (strncmp(tmp, "@_", 2) == 0 || strchr(tmp, ':') != NULL)
                {
                    free(str);
                    str = ownedCopy("__");
                }
                else
                {
                    SplitResult d(tmp, " ");
                    if (d.size() > 0 && (strcmp(d.get(0), "call8") == 0 || strcmp(d.get(0), "callExt") == 0))
                    {
                        free(str);
                        str = ownedCopy("_");
                    }
                    else if (d.size() > 1)
                    {
                        // Same prefix-match caveat as Pass 1: "a1" also
                        // matches "a10".."a15". Not an issue here since
                        // registername is always exactly 3 characters
                        // ("a11".."a15"), so it can only prefix-match
                        // itself.
                        if (strncmp(d.get(1), registername, strlen(registername)) == 0)
                        {
                            if (strcmp(str, tmp) == 0)
                            {
                                text->replaceText(i, " ");
                            }
                            else
                            {
                                char *op = d.get(0);
                                free(str);
                                // l32r is included here (unlike Pass 1)
                                // because its operand is always a label --
                                // a compile-time-constant address -- so a
                                // repeated `l32r aY,@_same_label` is always
                                // safe to treat as a redundant reload, not
                                // just the register-indirect loads Pass 1
                                // already trusts.
                                if (strcmp(op, "movi") == 0 || strcmp(op, "l32i") == 0 ||
                                    strcmp(op, "l16i") == 0 || strcmp(op, "l16ui") == 0 ||
                                    strcmp(op, "l8ui") == 0 || strcmp(op, "movExt") == 0 ||
                                    strcmp(op, "l32r") == 0)
                                    str = ownedCopy(tmp);
                                else
                                    str = ownedCopy("__");
                            }
                        }
                    }
                }
            }
        }

        free(str);
        free(registername);
    }

    // Pass 2: a `retw.n` immediately following another `retw.n` (nothing
    // but blanked-out lines in between -- no label, meaning nothing can
    // jump into it) is unreachable dead code. _visitdefFunctionNode()
    // (visitnode.cpp) always appends its own closing retw.n after
    // visiting a function's body, regardless of whether the body's last
    // statement was already a `return` (whose own retw.n comes from
    // _visitreturnNode) -- so *every* function ending in `return`, not
    // just this one, compiles with this exact dead instruction. Confirmed
    // against examples/FibonacciTiming.ino's fib(): its compiled
    // `@_fib(num)` ends in "add a2,a15,a14 / retw.n / retw.n", the second
    // one dead. A label resets `sawRetw` (not just the immediately-prior
    // line) since a blanked-out line in between must not hide an
    // intervening label this second retw.n could actually be a real
    // jump target for.
    bool sawRetw = false;
    for (int i = 0; i < text->size(); i++)
    {
        char *tmp = *text->getChildAtPos(i);
        if (tmp == NULL || strcmp(tmp, " ") == 0 || strcmp(tmp, "") == 0)
            continue;

        if (strchr(tmp, ':') != NULL)
        {
            sawRetw = false;
            continue;
        }

        if (strcmp(tmp, "retw.n") == 0)
        {
            if (sawRetw)
                text->replaceText(i, " ");
            else
                sawRetw = true;
        }
        else
        {
            sawRetw = false;
        }
    }
}

// See optimize.h's own comment: physically deletes every line optimize()/
// optimizeSpeed() left blanked out (" " or "") instead of erasing in
// place, now that no pass still needs those slots to keep their original
// position. Walks backwards so each erase() (a memmove, see vect.h)
// never disturbs the indices of entries not yet visited.
void removeBlankLines(Text *text)
{
    for (int i = text->size() - 1; i >= 0; i--)
    {
        char *tmp = *text->getChildAtPos(i);
        if (tmp == NULL || strcmp(tmp, " ") == 0 || strcmp(tmp, "") == 0)
            text->_texts.erase(i);
    }
}
