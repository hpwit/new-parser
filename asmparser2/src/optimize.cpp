#include "optimize.h"
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
                if (p.size() > 0 && p2.size() > 1 && strcmp(p.get(0), p2.get(1)) == 0)
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
