#include "optimize.h"
#include "parser_define.h"
#include "string_functions.h"
#include "vect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// Base register of a register-indirect load ("l8ui a14,a5,0" -> "a5"),
// written into `base` (empty if the line isn't one). Returns false only
// for a load that overwrites its own base -- "l32i a5,a5,0" -- which must
// not be tracked at all: an identical repeat is a second dereference,
// never a redundant reload. Pass 1 and
// optimizeSpeed() use it to forget a tracked load as soon as its base is
// rewritten: two textually identical loads only read the same address if
// the base still holds the same value. Confirmed load-bearing against
// examples/GameOfLife: `line1[i + 1] + line1[i]` compiles to the same
// `l8ui a14,a5,0` twice with `l32r a5,@_line1; add a5,a5,a4` re-deriving
// a5 in between, and the second load used to be blanked.
static bool indirectLoadBase(const char *op, const char *operands, char *base, size_t n)
{
    base[0] = 0;
    if (strcmp(op, "l32i") != 0 && strcmp(op, "l16i") != 0 &&
        strcmp(op, "l16ui") != 0 && strcmp(op, "l8ui") != 0)
        return true;
    const char *b = strchr(operands, ',');
    if (b == NULL)
        return true;
    b++;
    size_t len = strcspn(b, ",");
    if (len == 0 || len >= n)
        return true;
    if (strcspn(operands, ",") == len && strncmp(operands, b, len) == 0)
        return false;
    memcpy(base, b, len);
    base[len] = 0;
    return true;
}

// True if `operands`' first operand is exactly `reg` -- i.e. the line may
// write it. Stores and branches also name a register first without
// writing it; treating those as writes only costs a missed fold.
static bool firstOperandIs(const char *operands, const char *reg)
{
    size_t len = strcspn(operands, ",");
    return reg[0] != 0 && strlen(reg) == len && strncmp(operands, reg, len) == 0;
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
        char base[8] = "";

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
                    if (d.size() > 0 && base[0] != 0 && atoi(base + 1) >= 8 &&
                        (strcmp(d.get(0), "call8") == 0 || strcmp(d.get(0), "callExt") == 0))
                    {
                        // the tracked register survives the call, its base doesn't
                        free(str);
                        str = ownedCopy("__");
                        base[0] = 0;
                    }
                    else if (d.size() > 0 && strcmp(d.get(0), "call8") == 0 && regnum >= 8)
                    {
                        free(str);
                        str = ownedCopy("_");
                    }
                    else if (d.size() > 1)
                    {
                        if (firstOperandIs(d.get(1), base))
                        {
                            free(str);
                            str = ownedCopy("__");
                            base[0] = 0;
                        }
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
                                if (!indirectLoadBase(op, d.get(1), base, sizeof(base)))
                                {
                                    free(str);
                                    str = ownedCopy("__");
                                }
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
    // otherwise dead right after the mov.
    free(before);
    before = ownedCopy(" ");
    indexbefore = 0;

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
                // a2 is this function's own implicit return-value register
                // (retw.n has no textual operand naming it -- the ABI just
                // reads whatever's in a2 -- so this pass, which only ever
                // looks at what the *visible* instruction stream reads
                // again, has no way to see that use). Folding a write into
                // a2 forward into some other destination -- e.g. an
                // outgoing call-argument register (a8..a15) right before a
                // call8/callExt that will clobber it -- would silently
                // lose the return value once optimize() runs a second time
                // over the now-compacted output (removeBlankLines() can
                // make lines adjacent that weren't when this fold first
                // had a chance to run, re-triggering it on the same value
                // a second time): a real, QEMU-confirmed bug for any
                // function that computes its return value, passes that
                // same value to an external call, and returns it
                // afterward (e.g. `int f(){int c; c=...; report(c); return
                // c;}`). Keeping the extra `mov aY,a2` intact is always
                // correct, just one instruction short of maximally
                // optimal.
                if (strcmp(p.get(0), "a2") == 0 && strcmp(p2.get(0), "a2") != 0)
                {
                    // fall through without folding
                }
                else if (p.size() > 0 && p2.size() > 1 && strcmp(p.get(0), p2.get(1)) == 0)
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
                  strcmp(op, "ssl") == 0) &&
                 d.size() > 1)
        {
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
            bool found = false;
            int ind = -1;
            char *finalSrc = d2.get(1);
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(1), from.get(mp)) == 0)
                {
                    finalSrc = to.get(mp);
                    newstr = str_concat("%s%s", newstr, to.get(mp));
                    text->replaceText(index.get(mp), " ");
                    found = true;
                    ind = mp;
                }
            if (!found)
                newstr = str_concat("%s%s", newstr, d2.get(1));
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
        char base[8] = "";

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
                        if (firstOperandIs(d.get(1), base))
                        {
                            free(str);
                            str = ownedCopy("__");
                            base[0] = 0;
                        }
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
                                if (!indirectLoadBase(op, d.get(1), base, sizeof(base)))
                                {
                                    free(str);
                                    str = ownedCopy("__");
                                }
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

// ---------------------------------------------------------------------
// preserveAcrossCalls() -- see optimize.h.
// ---------------------------------------------------------------------

// Copies `src` into `dst` without leading/trailing blanks.
static void trimmedLine(const char *src, char *dst, size_t n)
{
    dst[0] = 0;
    if (src == NULL)
        return;
    while (*src == ' ')
        src++;
    size_t len = strlen(src);
    while (len > 0 && src[len - 1] == ' ')
        len--;
    if (len >= n)
        len = n - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

static bool isCallLine(const char *line)
{
    return strncmp(line, "callExt ", 8) == 0 || strncmp(line, "call8 ", 6) == 0;
}

static bool isFunctionLabel(const char *line)
{
    size_t len = strlen(line);
    return len > 2 && strncmp(line, "@_", 2) == 0 && line[len - 1] == ':';
}

// Instructions whose first operand is written without being read. For
// anything not listed here (stores, branches, maddn.s-style accumulating
// ops, anything unknown), a register named as first operand is assumed to
// be read -- the safe direction: it can only cause a save/restore pair
// that wasn't strictly needed.
static bool writesFirstOperandOnly(const char *op)
{
    static const char *ops[] = {
        "movi", "mov", "mov.s", "l32i", "l16i", "l16ui", "l16si", "l8ui", "lsi", "l32r", "movExt",
        "add", "addi", "sub", "mull", "quos", "quou", "remu", "rems", "and", "or", "xor", "extui",
        "slli", "srli", "srai", "addx2", "addx4", "addx8", "subx8", "neg", "neg.s", "abs.s",
        "float.s", "ufloat.s", "trunc.s", "wfr", "rfr", "add.s", "sub.s", "mul.s", "const.s",
        "nexp01.s", "div0.s"};
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++)
        if (strcmp(op, ops[i]) == 0)
            return true;
    return false;
}

enum RegRole
{
    REG_UNTOUCHED,
    REG_READ,
    REG_WRITTEN
};

// What `line` does to `reg`. A call line itself is REG_UNTOUCHED: its
// outgoing arguments are always set by explicit instructions just before
// it, and treating it as a write would hide a value that has to survive
// two calls in a row (`a * (f() + g())`) from the first one's scan.
static RegRole regRole(const char *line, const char *reg)
{
    if (line[0] == 0 || strchr(line, ':') != NULL || isCallLine(line))
        return REG_UNTOUCHED;
    const char *sp = strchr(line, ' ');
    if (sp == NULL)
        return REG_UNTOUCHED;
    char op[24];
    size_t opLen = (size_t)(sp - line);
    if (opLen >= sizeof(op))
        return REG_UNTOUCHED;
    memcpy(op, line, opLen);
    op[opLen] = 0;

    size_t regLen = strlen(reg);
    bool first = false, later = false;
    const char *p = sp;
    int index = 0;
    while (*p)
    {
        while (*p == ' ' || *p == ',')
            p++;
        size_t len = strcspn(p, ", ");
        if (len == 0)
            break;
        if (len == regLen && strncmp(p, reg, len) == 0)
        {
            if (index == 0)
                first = true;
            else
                later = true;
        }
        index++;
        p += len;
    }
    if (later)
        return REG_READ;
    if (first)
        return writesFirstOperandOnly(op) ? REG_WRITTEN : REG_READ;
    return REG_UNTOUCHED;
}

// a8 is the callExt scratch register and a10 carries the return value, so
// neither can hold anything across a call to begin with. f0 (the float
// division helper) and f2 (script functions) carry float return values,
// which a restore placed right after the call would overwrite, and f1/f2
// are float argument registers; expression temporaries start at f15.
static const char *const kCallClobbered[] = {
    "a9", "a11", "a12", "a13", "a14", "a15",
    "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15"};
#define NUM_CALL_CLOBBERED ((int)(sizeof(kCallClobbered) / sizeof(kCallClobbered[0])))

// Registers from kCallClobbered whose value is read again after the call
// at `callPos` before being rewritten, scanning straight down to `end`
// (the end of the function). Following the text order rather than the
// real control flow can only add registers, never miss one that a
// forward path reads.
static int liveAcrossCall(Text *text, int callPos, int end, const char *live[])
{
    int n = 0;
    char line[160];
    for (int r = 0; r < NUM_CALL_CLOBBERED; r++)
    {
        for (int j = callPos + 1; j < end; j++)
        {
            trimmedLine(*text->getChildAtPos(j), line, sizeof(line));
            RegRole role = regRole(line, kCallClobbered[r]);
            if (role == REG_READ)
            {
                live[n++] = kCallClobbered[r];
                break;
            }
            if (role == REG_WRITTEN)
                break;
        }
    }
    return n;
}

void preserveAcrossCalls(Text *text)
{
    char line[160];
    int i = 0;
    while (i < text->size())
    {
        trimmedLine(*text->getChildAtPos(i), line, sizeof(line));
        if (strncmp(line, "entry a1,", 9) != 0)
        {
            i++;
            continue;
        }
        int entryPos = i;
        int frame = atoi(line + 9);
        int end = entryPos + 1;
        while (end < text->size())
        {
            trimmedLine(*text->getChildAtPos(end), line, sizeof(line));
            if (isFunctionLabel(line))
                break;
            end++;
        }

        // The save slots start where the frame's reserved top
        // _FRAME_RESERVED bytes currently are, and the frame grows by as
        // much as they take, so that reserved area moves up intact: the
        // window overflow handler stores the caller's a0..a3 in the top
        // 16 and this frame's own a4..a7 in the 16 below (see
        // _visitdefFunctionNode()). lsi/ssi/l32i/s32i can only reach
        // offset 1020.
        const char *live[NUM_CALL_CLOBBERED];
        int maxSlots = 0;
        for (int c = entryPos + 1; c < end; c++)
        {
            trimmedLine(*text->getChildAtPos(c), line, sizeof(line));
            if (!isCallLine(line))
                continue;
            int n = liveAcrossCall(text, c, end, live);
            if (n > maxSlots && frame - _FRAME_RESERVED + 4 * (n - 1) <= 1020)
                maxSlots = n;
        }
        if (maxSlots == 0)
        {
            i = end;
            continue;
        }
        int growth = ((maxSlots * 4 + 7) / 8) * 8;

        // Anything this function addresses at or above its old frame
        // size lives in the caller's frame (arguments past the sixth,
        // see _visitdefInputArgumentsNode()'s `stek`) and is now
        // `growth` bytes further away.
        for (int c = entryPos + 1; c < end; c++)
        {
            trimmedLine(*text->getChildAtPos(c), line, sizeof(line));
            char *base = strstr(line, ",a1,");
            if (base == NULL)
                continue;
            char *num = base + 4;
            char *endNum = NULL;
            long off = strtol(num, &endNum, 10);
            if (endNum == num || *endNum != 0 || off < frame)
                continue;
            *num = 0;
            char *moved = string_format("%s%d", line, (int)off + growth);
            text->replaceText(c, (const char *)moved);
            free(moved);
        }
        char *newEntry = string_format("entry a1,%d", frame + growth);
        text->replaceText(entryPos, (const char *)newEntry);
        free(newEntry);

        for (int c = entryPos + 1; c < end; c++)
        {
            trimmedLine(*text->getChildAtPos(c), line, sizeof(line));
            if (!isCallLine(line))
                continue;
            int n = liveAcrossCall(text, c, end, live);
            if (n == 0 || n > maxSlots)
                continue;
            for (int k = 0; k < n; k++)
            {
                bool isFloat = live[k][0] == 'f';
                text->addAfter(c, string_format(isFloat ? "lsi %s,a1,%d" : "l32i %s,a1,%d", live[k], frame - _FRAME_RESERVED + 4 * k));
            }
            for (int k = 0; k < n; k++)
            {
                bool isFloat = live[k][0] == 'f';
                text->addAfter(c - 1, string_format(isFloat ? "ssi %s,a1,%d" : "s32i %s,a1,%d", live[k], frame - _FRAME_RESERVED + 4 * k));
            }
            // the call is now at c + n, its restores at c + n + 1 .. c + 2n
            c += 2 * n;
            end += 2 * n;
        }
        i = end;
    }
}
