#include "optimize.h"
#include "parser_define.h"
#include "string_functions.h"
#include "vect.h"
#include <ctype.h>
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

enum RegRole
{
    REG_UNTOUCHED,
    REG_READ,
    REG_WRITTEN
};
static RegRole regRole(const char *line, const char *reg, bool allVarargs = false);
static void trimmedLine(const char *src, char *dst, size_t n);
static bool isFunctionLabel(const char *line);
static bool lineWritesRegister(const char *line, const char *reg);
static bool writtenRegister(const char *line, char *out, size_t n);

// Pass 4 helper: blanks the `movr aY,aX` / `mov aY,aX` at `defPos` once a
// read of aY at line `usePos` has been rewritten to read aX instead --
// but only if nothing after `usePos` still reads aY before it is
// rewritten. Pass 4 used to blank the copy unconditionally at its first
// rewritten use; when that use was a branch (which also drops the tracked
// fact) or was followed by a label or call (which reset every fact),
// later reads of aY were left as they were, now reading a register nobody
// had loaded. Seen with safe_mode's bounds check: the index copied into
// a15 was compared (rewritten, copy blanked), then read again by the
// address computation that follows the check.
static void blankCopyIfDead(Text *text, int defPos, const char *reg, int usePos)
{
    char line[160];
    // Every read of aY between the copy and this use that Pass 4 knows how
    // to rewrite has been rewritten already; one still reading aY there is
    // an instruction it doesn't rewrite (srai/slli/mulsh/src... in ffloat
    // arithmetic), which still needs the copy.
    for (int j = defPos + 1; j < usePos; j++)
    {
        trimmedLine(*text->getChildAtPos(j), line, sizeof(line));
        if (regRole(line, reg) == REG_READ)
            return;
    }
    // The line being rewritten may itself overwrite aY
    // (`mull a15,a15,a11` -> `mull a15,a3,a11`): the copy is dead right
    // there, whatever reads a15 afterwards reads the new value.
    // A later write ends the search only on straight-line code: past a
    // branch or a label it may be on one path only (`bnez a13,L / movi
    // a13,-1 / ... / L: quos a14,a14,a13`), so the rest of the function
    // is searched for a read.
    trimmedLine(*text->getChildAtPos(usePos), line, sizeof(line));
    bool overwritten = lineWritesRegister(line, reg);
    bool straight = line[0] != 'b' && strncmp(line, "j ", 2) != 0;
    for (int j = usePos + 1; !overwritten && j < text->size(); j++)
    {
        trimmedLine(*text->getChildAtPos(j), line, sizeof(line));
        if (isFunctionLabel(line))
            break;
        if (strchr(line, ':') != NULL || line[0] == 'b' || strncmp(line, "j ", 2) == 0)
            straight = false;
        RegRole role = regRole(line, reg);
        if (role == REG_READ)
            return;
        if (role == REG_WRITTEN && straight)
            break;
    }
    text->replaceText(defPos, " ");
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
    // otherwise dead right after the mov. movr (a register local read) is
    // folded as l32i (a stack local read) is: the code generator loads a
    // call's arguments one by one into a scratch register that can already
    // hold an earlier argument (`movi a13,1867` for the 4th, then `movr
    // a13,a7 / mov a11,a13` for the 2nd) and relies on this fold to never
    // actually write it. Only the destination is rewritten, never the
    // register local itself.
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
                 strcmp(op, "addi") == 0 || strcmp(op, "movr") == 0))
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
                    blankCopyIfDead(text, index.get(mp), from.get(mp), i);
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
                    blankCopyIfDead(text, index.get(mp), from.get(mp), i);
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
                    blankCopyIfDead(text, index.get(mp), from.get(mp), i);
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
                    blankCopyIfDead(text, index.get(mp), from.get(mp), i);
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
                    blankCopyIfDead(text, index.get(mp), from.get(mp), i);
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
        else if ((strcmp(op, "remu") == 0 || strcmp(op, "rems") == 0 || strcmp(op, "or") == 0 || strcmp(op, "and") == 0 ||
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
                    blankCopyIfDead(text, index.get(mp), from.get(mp), i);
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
                    blankCopyIfDead(text, index.get(mp), from.get(mp), i);
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

        // A copy "aY is aX" stops being true as soon as either register
        // is written -- aY by an instruction none of the branches above
        // knows about, or aX at all.
        // a later read of aY must then not be rewritten to read aX. The
        // branches above only drop a fact when aY itself is written. Seen
        // with call arguments, `mov a14,a10` (keep a call's result) ...
        // `mov a10,a13` (next argument into a10) ... `mov a11,a14`: the
        // last line was rewritten to `mov a11,a10`, passing the other
        // argument twice. Checked after this line's own rewrite, which
        // may legitimately read aX before writing it.
        char *now = *text->getChildAtPos(i);
        char written[8];
        if (now != NULL && writtenRegister(now, written, sizeof(written)))
        {
            for (int mp = index.size() - 1; mp >= 0; mp--)
                if (strcmp(to.get(mp), written) == 0 ||
                    (strcmp(from.get(mp), written) == 0 && index.get(mp) != i))
                    trackRemoveAt(from, to, index, mp);
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

    // Pass 3: an `ssai N` while SAR is already known to hold N is
    // redundant -- every ffloat multiply ends in "ssai 16 / src", so a
    // run of them sets SAR over and over. SAR is known from an ssai until
    // a label (a jump may come in with any SAR), a function start, a call
    // (the callee may shift), or any other instruction that writes SAR.
    int sar = -1;
    for (int i = 0; i < text->size(); i++)
    {
        char *tmp = *text->getChildAtPos(i);
        if (tmp == NULL || strcmp(tmp, " ") == 0 || strcmp(tmp, "") == 0)
            continue;
        char line[64];
        trimmedLine(tmp, line, sizeof(line));
        if (strncmp(line, "@_", 2) == 0 || strchr(line, ':') != NULL || strncmp(line, "call", 4) == 0)
        {
            sar = -1;
            continue;
        }
        if (strncmp(line, "ssai ", 5) == 0)
        {
            int n = atoi(line + 5);
            if (n == sar)
                text->replaceText(i, " ");
            sar = n;
        }
        else if (strncmp(line, "ssl ", 4) == 0 || strncmp(line, "ssr ", 4) == 0 ||
                 strncmp(line, "ssa8", 4) == 0 || strncmp(line, "wsr", 3) == 0 ||
                 strncmp(line, "xsr", 3) == 0)
            sar = -1;
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
        "movi", "mov", "movr", "fill", "pop", "mov.s", "l32i", "l16i", "l16ui", "l16si", "l8ui", "lsi", "l32r", "movExt",
        "add", "addi", "sub", "mull", "quos", "quou", "remu", "rems", "and", "or", "xor", "extui",
        "slli", "srli", "srai", "sll", "srl", "sra", "addx2", "addx4", "addx8", "subx8", "neg", "neg.s", "abs.s", "abs", "mulsh", "src",
        "float.s", "ufloat.s", "trunc.s", "wfr", "rfr", "add.s", "sub.s", "mul.s", "const.s",
        "nexp01.s", "div0.s"};
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++)
        if (strcmp(op, ops[i]) == 0)
            return true;
    return false;
}

// The register `line` writes, if any, into `out`. Every instruction this
// compiler emits that names a register first writes it, except stores,
// branches, jumps, calls and returns.
static bool writtenRegister(const char *line, char *out, size_t n)
{
    char buf[160];
    trimmedLine(line, buf, sizeof(buf));
    if (buf[0] == 0 || strchr(buf, ':') != NULL)
        return false;
    char *sp = strchr(buf, ' ');
    if (sp == NULL)
        return false;
    *sp = 0;
    const char *op = buf;
    if (op[0] == 'b' || op[0] == 'j' || strncmp(op, "call", 4) == 0 || strncmp(op, "retw", 4) == 0 ||
        strcmp(op, "s8i") == 0 || strcmp(op, "s16i") == 0 || strcmp(op, "s32i") == 0 || strcmp(op, "ssi") == 0 ||
        strcmp(op, "entry") == 0)
        return false;
    const char *reg = sp + 1;
    while (*reg == ' ')
        reg++;
    size_t len = strcspn(reg, ", ");
    if (len == 0 || len >= n || (reg[0] != 'a' && reg[0] != 'f'))
        return false;
    memcpy(out, reg, len);
    out[len] = 0;
    return true;
}

// True if `line` is an instruction that writes its first operand and that
// operand is `reg` -- whether or not it also reads `reg` as a source.
static bool lineWritesRegister(const char *line, const char *reg)
{
    const char *sp = strchr(line, ' ');
    if (sp == NULL || strchr(line, ':') != NULL)
        return false;
    char op[24];
    size_t opLen = (size_t)(sp - line);
    if (opLen >= sizeof(op))
        return false;
    memcpy(op, line, opLen);
    op[opLen] = 0;
    if (!writesFirstOperandOnly(op))
        return false;
    while (*sp == ' ')
        sp++;
    size_t len = strcspn(sp, ", ");
    return len == strlen(reg) && strncmp(sp, reg, len) == 0;
}

// What `line` does to `reg` (see the call case inside).
// Number of argument registers (a10..) a call line passes, from its
// label's signature: `call8 @_f(num|num)` -> 2, `callExt a8,@_g()` -> 0.
// A struct method (`@_Foo.bump()`) also gets `this` in a10. A variadic
// one (`@_printfln(char*|Args)`) may use all six: `allVarargs` counts
// them all, otherwise its signature's count stands (what the liveness
// scan of preserveAcrossCalls() has always used).
static int callArgumentCount(const char *line, bool allVarargs = false)
{
    const char *open = strchr(line, '(');
    if (open == NULL)
        return 0;
    if (allVarargs && strstr(open, "Args") != NULL)
        return 6;
    const char *label = strchr(line, '@');
    int n = label != NULL && memchr(label, '.', (size_t)(open - label)) != NULL ? 1 : 0;
    if (open[1] != ')')
    {
        n++;
        for (const char *p = open + 1; *p && *p != ')'; p++)
            if (*p == '|')
                n++;
    }
    return n > 6 ? 6 : n;
}

static RegRole regRole(const char *line, const char *reg, bool allVarargs)
{
    if (line[0] == 0 || strchr(line, ':') != NULL)
        return REG_UNTOUCHED;
    if (isCallLine(line))
    {
        // A call reads its argument registers a10..a(10+n-1) -- a value
        // set up as an argument, then kept there across another call made
        // for a later argument, is live across that other call. Anything
        // else the call changes is not a read; the caller sets arguments
        // with explicit instructions, so treating it as a write would hide
        // a value that has to survive two calls in a row (`a * (f() + g())`)
        // from the first one's scan.
        if (reg[0] == 'a')
        {
            int r = atoi(reg + 1);
            if (r >= 10 && r < 10 + callArgumentCount(line, allVarargs))
                return REG_READ;
        }
        return REG_UNTOUCHED;
    }
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
            // Nothing after an unconditional return is reached by falling
            // through from this call, so a later read is some other
            // path's business (a bounds check's `callExt ...; retw.n`
            // would otherwise have every temporary used after it saved).
            if (strcmp(line, "retw.n") == 0)
                break;
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

// Registers from a3..a7 that no line of the function between `start` and
// `end` names. The callee never sees the caller's a0..a7, so one of these
// keeps a value across a call without a frame slot. A register the
// function never names is not an argument it reads (arguments arrive in
// a2..a7) nor a promoted local (see promoteFrameSlots()).
static int unusedWindowRegisters(Text *text, int start, int end, const char *out[])
{
    static const char *const candidates[] = {"a3", "a4", "a5", "a6", "a7"};
    int n = 0;
    char line[160];
    for (int r = 0; r < 5; r++)
    {
        bool used = false;
        for (int j = start; j < end && !used; j++)
        {
            trimmedLine(*text->getChildAtPos(j), line, sizeof(line));
            for (const char *p = strstr(line, candidates[r]); p != NULL && !used; p = strstr(p + 1, candidates[r]))
            {
                bool startOk = p == line || p[-1] == ' ' || p[-1] == ',';
                bool endOk = !isalnum((unsigned char)p[2]);
                used = startOk && endOk;
            }
        }
        if (!used)
            out[n++] = candidates[r];
    }
    return n;
}

// `line` with every operand naming `from` renamed to `to` -- all but the
// first one when `skipFirst`.
static char *renameRegister(const char *line, const char *from, const char *to, bool skipFirst = false)
{
    char out[200];
    size_t o = 0, fl = strlen(from), tl = strlen(to);
    const char *sp = strchr(line, ' ');
    const char *firstComma = sp != NULL ? strchr(sp, ',') : NULL;
    const char *from0 = skipFirst ? firstComma : sp;
    for (const char *p = line; *p && o + tl < sizeof(out) - 1;)
    {
        bool operand = from0 != NULL && p > from0 && (p[-1] == ' ' || p[-1] == ',');
        if (operand && strncmp(p, from, fl) == 0 && !isalnum((unsigned char)p[fl]) && p[fl] != '.')
        {
            memcpy(out + o, to, tl);
            o += tl;
            p += fl;
        }
        else
            out[o++] = *p++;
    }
    out[o] = 0;
    return string_format("%s", out);
}

static bool endsStraightLine(const char *line)
{
    return line[0] == 0 || strchr(line, ':') != NULL || line[0] == 'b' || line[0] == 'j' ||
           strncmp(line, "retw", 4) == 0;
}

// Moves the value `reg` holds across the call at `callPos` into `spare`
// for its whole life instead of copying it there and back: from the line
// that last wrote it before the call to its last read after. Only when
// that whole stretch is straight-line code (no label, branch or return
// before the last read) and `reg` is read-only in it, so nothing else can
// see `reg` while it is renamed. False when it does not apply.
static bool renameAcrossCall(Text *text, int callPos, int start, int end, const char *reg, const char *spare)
{
    char line[160];
    int def = -1;
    for (int j = callPos - 1; j > start; j--)
    {
        trimmedLine(*text->getChildAtPos(j), line, sizeof(line));
        if (endsStraightLine(line) || isCallLine(line))
            return false;
        RegRole role = regRole(line, reg);
        if (role == REG_WRITTEN)
        {
            def = j;
            break;
        }
        if (role == REG_READ && lineWritesRegister(line, reg))
            return false;
    }
    if (def < 0)
        return false;
    int last = -1;
    for (int j = callPos + 1; j < end; j++)
    {
        trimmedLine(*text->getChildAtPos(j), line, sizeof(line));
        RegRole role = regRole(line, reg);
        if (role == REG_READ)
        {
            if (lineWritesRegister(line, reg))
            {
                last = j; // reads it one last time, then holds something new
                break;
            }
            last = j;
            continue;
        }
        if (role == REG_WRITTEN || strncmp(line, "retw", 4) == 0)
            break;
        if (endsStraightLine(line) || isCallLine(line))
            return false;
    }
    if (last < 0)
        return false;
    // The spare may already be in use there: the save/restore copies of
    // the previous call sit right before this value's first write.
    for (int j = def; j <= last; j++)
    {
        trimmedLine(*text->getChildAtPos(j), line, sizeof(line));
        if (regRole(line, spare) != REG_UNTOUCHED)
            return false;
    }
    for (int j = def; j <= last; j++)
    {
        trimmedLine(*text->getChildAtPos(j), line, sizeof(line));
        // the last line may also write reg: rename only its sources then
        char *renamed = renameRegister(line, reg, spare, j == last && lineWritesRegister(line, reg));
        text->replaceText(j, (const char *)renamed);
        free(renamed);
    }
    return true;
}

// Deletes, between `start` and `end`, register-to-itself copies and pure
// writes (see writesFirstOperandOnly()) to a3..a15 that no line of the
// function reads, a call reading its argument registers (see
// callArgumentCount()). a2 (the return value), a8 (callExt's scratch)
// and the float registers are left alone. Typically left behind once preserveAcrossCalls() has
// renamed a value out of the register it was first loaded into. Returns
// how many lines it removed.
static int removeUnreadWrites(Text *text, int start, int end)
{
    int removed = 0;
    bool changed = true;
    char line[160], reg[16];
    // A hand-written callx8/call4/... reads argument registers this
    // function cannot see; leave such a function as written.
    for (int j = start; j < end; j++)
    {
        trimmedLine(*text->getChildAtPos(j), line, sizeof(line));
        if (strncmp(line, "call", 4) == 0 && !isCallLine(line))
            return 0;
    }
    while (changed)
    {
        changed = false;
        for (int j = start; j < end; j++)
        {
            trimmedLine(*text->getChildAtPos(j), line, sizeof(line));
            char op[16], a[16], b[16];
            bool dead = false;
            if (sscanf(line, "%15s %15[^,],%15s", op, a, b) == 3 &&
                (strcmp(op, "mov") == 0 || strcmp(op, "movr") == 0 || strcmp(op, "mov.s") == 0) && strcmp(a, b) == 0)
                dead = true;
            else if (writtenRegister(line, reg, sizeof(reg)) && reg[0] == 'a' && atoi(reg + 1) >= 3 &&
                     strcmp(reg, "a8") != 0 && lineWritesRegister(line, reg) &&
                     regRole(line, reg) == REG_WRITTEN)
            {
                dead = true;
                for (int k = start; k < end && dead; k++)
                {
                    trimmedLine(*text->getChildAtPos(k), line, sizeof(line));
                    if (regRole(line, reg, true) == REG_READ)
                        dead = false;
                }
            }
            if (dead)
            {
                text->_texts.erase(j);
                end--;
                j--;
                removed++;
                changed = true;
            }
        }
    }
    return removed;
}

// Folds `mov(r) aT,aS` into the instructions that read aT, between
// `start` and `end`: when every read of aT before its next write comes
// later in the same straight-line run (no label, branch, call or return
// first) and aS is not rewritten before the last of them, those reads
// are renamed to aS and the copy is deleted. Promoted locals leave
// these behind (`movr a14,a3; slli a15,a14,7`). Returns how many lines it
// removed.
static int foldCopies(Text *text, int start, int end)
{
    int removed = 0;
    char line[160], op[16], t[16], src[16];
    for (int j = start; j < end; j++)
    {
        trimmedLine(*text->getChildAtPos(j), line, sizeof(line));
        if (sscanf(line, "%15s %15[^,],%15s", op, t, src) != 3 ||
            (strcmp(op, "mov") != 0 && strcmp(op, "movr") != 0) || t[0] != 'a' || src[0] != 'a' ||
            strcmp(t, src) == 0 || strcmp(t, "a1") == 0 || strcmp(src, "a1") == 0)
            continue;
        int reads[32], nReads = 0;
        bool srcChanged = false, ok = false;
        int k;
        for (k = j + 1; k < end; k++)
        {
            trimmedLine(*text->getChildAtPos(k), line, sizeof(line));
            if (endsStraightLine(line) || isCallLine(line))
                break;
            RegRole role = regRole(line, t, true);
            if (role == REG_READ)
            {
                if (srcChanged || nReads == 32)
                    break;
                // As a first operand it is only safely a read for a store;
                // anything else may write it (renaming would move the
                // write to aS).
                char lop[16], first[16];
                if (sscanf(line, "%15s %15[^,]", lop, first) == 2 && strcmp(first, t) == 0 &&
                    strcmp(lop, "s8i") != 0 && strcmp(lop, "s16i") != 0 && strcmp(lop, "s32i") != 0)
                    break;
                reads[nReads++] = k;
                if (lineWritesRegister(line, t))
                {
                    ok = true; // read one last time, then rewritten
                    break;
                }
            }
            else if (role == REG_WRITTEN)
            {
                ok = true;
                break;
            }
            if (regRole(line, src, true) == REG_WRITTEN || lineWritesRegister(line, src))
                srcChanged = true;
        }
        if (!ok)
            continue;
        for (int r = 0; r < nReads; r++)
        {
            trimmedLine(*text->getChildAtPos(reads[r]), line, sizeof(line));
            char *renamed = renameRegister(line, t, src, lineWritesRegister(line, t));
            text->replaceText(reads[r], (const char *)renamed);
            free(renamed);
        }
        text->_texts.erase(j);
        end--;
        j--;
        removed++;
    }
    return removed;
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
        // The first live values of each call go to unused window
        // registers, the rest to frame slots.
        const char *spare[5];
        int nSpare = unusedWindowRegisters(text, entryPos + 1, end, spare);
        const char *live[NUM_CALL_CLOBBERED];
        int maxSlots = 0;
        for (int c = entryPos + 1; c < end; c++)
        {
            trimmedLine(*text->getChildAtPos(c), line, sizeof(line));
            if (!isCallLine(line))
                continue;
            int n = liveAcrossCall(text, c, end, live) - nSpare;
            if (n > maxSlots && frame - _FRAME_RESERVED + 4 * (n - 1) <= 1020)
                maxSlots = n;
        }
        // spill/fill (temporaries saved around a call whose argument
        // registers overlap them) and push/pop (call arguments parked
        // while the others are evaluated) from the code generator nest
        // like the calls do: one stack of slots, after the ones above.
        // spill/push store a register into the next slot; fill/pop load
        // the most recent one -- fill into the register it came from, pop
        // into whichever register it names.
        int spillDepth = 0, maxSpill = 0;
        for (int c = entryPos + 1; c < end; c++)
        {
            trimmedLine(*text->getChildAtPos(c), line, sizeof(line));
            if ((strncmp(line, "spill ", 6) == 0 || strncmp(line, "push ", 5) == 0) && ++spillDepth > maxSpill)
                maxSpill = spillDepth;
            else if (strncmp(line, "fill ", 5) == 0 || strncmp(line, "pop ", 4) == 0)
                spillDepth--;
        }
        int growth = (((maxSlots + maxSpill) * 4 + 7) / 8) * 8;

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
        if (growth > 0)
        {
            char *newEntry = string_format("entry a1,%d", frame + growth);
            text->replaceText(entryPos, (const char *)newEntry);
            free(newEntry);
        }

        spillDepth = 0;
        for (int c = entryPos + 1; c < end; c++)
        {
            trimmedLine(*text->getChildAtPos(c), line, sizeof(line));
            int store = strncmp(line, "spill ", 6) == 0 ? 6 : strncmp(line, "push ", 5) == 0 ? 5 : 0;
            int load = strncmp(line, "fill ", 5) == 0 ? 5 : strncmp(line, "pop ", 4) == 0 ? 4 : 0;
            bool isSpill = store != 0;
            if (!store && !load)
                continue;
            if (load)
                spillDepth--;
            int off = frame - _FRAME_RESERVED + 4 * (maxSlots + spillDepth);
            char *converted = string_format(isSpill ? "s32i %s,a1,%d" : "l32i %s,a1,%d",
                                            line + (isSpill ? store : load), off);
            text->replaceText(c, (const char *)converted);
            free(converted);
            if (isSpill)
                spillDepth++;
        }

        for (int c = entryPos + 1; c < end; c++)
        {
            trimmedLine(*text->getChildAtPos(c), line, sizeof(line));
            if (!isCallLine(line))
                continue;
            int n = liveAcrossCall(text, c, end, live);
            if (n == 0 || n > maxSlots + nSpare)
                continue;
            // Integer values that fit in a spare register for their whole
            // life are renamed into it; the rest are saved and restored.
            int used = 0;
            for (int k = 0; k < n && used < nSpare;)
            {
                if (live[k][0] == 'a' && renameAcrossCall(text, c, entryPos, end, live[k], spare[used]))
                {
                    used++;
                    live[k] = live[--n];
                    continue;
                }
                k++;
            }
            if (n == 0)
                continue;
            int freeSpare = nSpare - used;
            for (int k = 0; k < n; k++)
            {
                bool isFloat = live[k][0] == 'f';
                if (k < freeSpare)
                    text->addAfter(c, isFloat ? string_format("wfr %s,%s", live[k], spare[used + k])
                                              : string_format("mov %s,%s", live[k], spare[used + k]));
                else
                    text->addAfter(c, string_format(isFloat ? "lsi %s,a1,%d" : "l32i %s,a1,%d", live[k],
                                                    frame - _FRAME_RESERVED + 4 * (k - freeSpare)));
            }
            for (int k = 0; k < n; k++)
            {
                bool isFloat = live[k][0] == 'f';
                if (k < freeSpare)
                    text->addAfter(c - 1, isFloat ? string_format("rfr %s,%s", spare[used + k], live[k])
                                                  : string_format("mov %s,%s", spare[used + k], live[k]));
                else
                    text->addAfter(c - 1, string_format(isFloat ? "ssi %s,a1,%d" : "s32i %s,a1,%d", live[k],
                                                        frame - _FRAME_RESERVED + 4 * (k - freeSpare)));
            }
            // the call is now at c + n, its restores at c + n + 1 .. c + 2n
            c += 2 * n;
            end += 2 * n;
        }
        end -= removeUnreadWrites(text, entryPos + 1, end);
        end -= foldCopies(text, entryPos + 1, end);
        i = end;
    }
}

// ---------------------------------------------------------------------
// promoteFrameSlots() -- see optimize.h.
// ---------------------------------------------------------------------

#define PROMOTE_MAX_SLOTS 64

// Splits "op a,b,c" into op and up to 4 operands (in place in `buf`).
static int splitInstruction(char *buf, char **op, char **ops)
{
    *op = buf;
    char *sp = strchr(buf, ' ');
    if (sp == NULL)
        return 0;
    *sp = 0;
    int n = 0;
    char *p = sp + 1;
    while (n < 4 && p != NULL && *p)
    {
        ops[n++] = p;
        char *c = strchr(p, ',');
        if (c == NULL)
            break;
        *c = 0;
        p = c + 1;
    }
    return n;
}

static bool isLabelLine(const char *line)
{
    size_t len = strlen(line);
    return len > 1 && line[len - 1] == ':' && strncmp(line, "@_", 2) != 0;
}

// Whether `line` names register `reg` as one of its operands.
static bool mentionsRegister(const char *line, const char *reg)
{
    size_t n = strlen(reg);
    for (const char *p = line; (p = strstr(p, reg)) != NULL; p += n)
    {
        bool startOk = p == line || p[-1] == ' ' || p[-1] == ',';
        bool endOk = p[n] == 0 || p[n] == ',' || p[n] == ' ';
        if (startOk && endOk)
            return true;
    }
    return false;
}

static bool isBlockBoundary(const char *line)
{
    return strchr(line, ':') != NULL || line[0] == 'b' || strncmp(line, "j ", 2) == 0 ||
           strncmp(line, "call", 4) == 0 || strncmp(line, "ret", 3) == 0;
}

static bool isPromotedFloat(const char *reg, const char **promoted, int n)
{
    for (int k = 0; k < n; k++)
        if (strcmp(reg, promoted[k]) == 0)
            return true;
    return false;
}

// Rebuilds "op o0,o1,..." into `out`.
static void joinInstruction(char *out, size_t size, const char *op, char **ops, int n)
{
    snprintf(out, size, "%s", op);
    for (int k = 0; k < n; k++)
    {
        size_t l = strlen(out);
        snprintf(out + l, size - l, "%s%s", k == 0 ? " " : ",", ops[k]);
    }
}

// The copies promotion leaves around the float registers it hands out:
// `mov.s fT,fN` (a load) has its later reads of fT read fN instead, up to
// the first label, branch or call, and goes once fT is overwritten;
// `op fT,...` followed by `mov.s fN,fT` (a store) writes fN directly when
// fT is overwritten before being read again.
static void foldFloatCopies(Text *text, int start, int end, const char **promoted, int npf)
{
    char line[160], buf[160], other[160], out[160];
    char *op, *ops[4], *op2, *ops2[4];
    for (int i = start; i < end; i++)
    {
        trimmedLine(*text->getChildAtPos(i), line, sizeof(line));
        strcpy(buf, line);
        if (splitInstruction(buf, &op, ops) != 2 || strcmp(op, "mov.s") != 0 ||
            isPromotedFloat(ops[0], promoted, npf) || !isPromotedFloat(ops[1], promoted, npf))
            continue;
        const char *ft = ops[0], *fn = ops[1];
        bool dead = false;
        for (int j = i + 1; j < end; j++)
        {
            trimmedLine(*text->getChildAtPos(j), other, sizeof(other));
            if (other[0] == 0)
                continue;
            if (isBlockBoundary(other) || lineWritesRegister(other, fn))
                break;
            RegRole role = regRole(other, ft);
            if (role == REG_UNTOUCHED)
                continue;
            char tmp[160];
            strcpy(tmp, other);
            int n = splitInstruction(tmp, &op2, ops2);
            bool writesFirst = writesFirstOperandOnly(op2);
            if (n > 0 && strcmp(ops2[0], ft) == 0 && !writesFirst)
                break; // reads and writes fT (madd.s...): leave it
            for (int k = writesFirst ? 1 : 0; k < n; k++)
                if (strcmp(ops2[k], ft) == 0)
                    ops2[k] = (char *)fn;
            joinInstruction(out, sizeof(out), op2, ops2, n);
            text->replaceText(j, string_format("%s", out));
            if (n > 0 && strcmp(ops2[0], ft) == 0 && writesFirst)
            {
                dead = true;
                break;
            }
        }
        if (dead)
            text->replaceText(i, " ");
    }

    for (int i = start + 1; i < end; i++)
    {
        trimmedLine(*text->getChildAtPos(i), line, sizeof(line));
        strcpy(buf, line);
        if (splitInstruction(buf, &op, ops) != 2 || strcmp(op, "mov.s") != 0 ||
            !isPromotedFloat(ops[0], promoted, npf) || isPromotedFloat(ops[1], promoted, npf))
            continue;
        const char *fn = ops[0], *ft = ops[1];
        int p = i - 1;
        while (p >= start)
        {
            trimmedLine(*text->getChildAtPos(p), other, sizeof(other));
            if (other[0] != 0)
                break;
            p--;
        }
        if (p < start || isBlockBoundary(other))
            continue;
        int n = splitInstruction(other, &op2, ops2);
        if (n < 2 || strcmp(ops2[0], ft) != 0 || !writesFirstOperandOnly(op2) || strchr(op2, '.') == NULL)
            continue;
        bool dead = false;
        for (int j = i + 1; j < end; j++)
        {
            char next[160];
            trimmedLine(*text->getChildAtPos(j), next, sizeof(next));
            if (next[0] == 0)
                continue;
            if (isBlockBoundary(next))
                break;
            RegRole role = regRole(next, ft);
            if (role == REG_READ)
                break;
            if (role == REG_WRITTEN)
            {
                dead = true;
                break;
            }
        }
        if (!dead)
            continue;
        ops2[0] = (char *)fn;
        joinInstruction(out, sizeof(out), op2, ops2, n);
        text->replaceText(p, string_format("%s", out));
        text->replaceText(i, " ");
    }
}

// Returns how many lines it inserted (the float saves around calls).
static int promoteFunction(Text *text, int start, int end)
{
    char line[160], buf[160];
    char *op, *ops[4];
    int offsets[PROMOTE_MAX_SLOTS];
    int weights[PROMOTE_MAX_SLOTS];
    bool bad[PROMOTE_MAX_SLOTS];
    bool floatOnly[PROMOTE_MAX_SLOTS];
    int nslots = 0;
    int badLo[PROMOTE_MAX_SLOTS], badHi[PROMOTE_MAX_SLOTS];
    int nbad = 0;

    // Loop weight of each line: x8 per enclosing backward jump.
    int len = end - start;
    int *weight = (int *)malloc(sizeof(int) * (len > 0 ? len : 1));
    if (weight == NULL)
        return 0;
    for (int i = 0; i < len; i++)
        weight[i] = 1;
    for (int i = start; i < end; i++)
    {
        trimmedLine(*text->getChildAtPos(i), line, sizeof(line));
        if (!(line[0] == 'b' || strncmp(line, "j ", 2) == 0))
            continue;
        const char *target = strrchr(line, ',');
        target = target ? target + 1 : strchr(line, ' ') + 1;
        for (int k = start; k < i; k++)
        {
            trimmedLine(*text->getChildAtPos(k), buf, sizeof(buf));
            size_t tl = strlen(target);
            if (isLabelLine(buf) && strncmp(buf, target, tl) == 0 && buf[tl] == ':')
            {
                for (int m = k; m <= i; m++)
                    if (weight[m - start] < (1 << 20))
                        weight[m - start] *= 8;
                break;
            }
        }
    }

    for (int i = start; i < end; i++)
    {
        trimmedLine(*text->getChildAtPos(i), line, sizeof(line));
        if (line[0] == 0 || !mentionsRegister(line, "a1"))
            continue;
        strcpy(buf, line);
        int n = splitInstruction(buf, &op, ops);
        if (strcmp(op, "entry") == 0)
            continue;
        bool word = strcmp(op, "l32i") == 0 || strcmp(op, "s32i") == 0 || strcmp(op, "lsi") == 0 || strcmp(op, "ssi") == 0;
        bool narrow = strcmp(op, "l8ui") == 0 || strcmp(op, "s8i") == 0 || strcmp(op, "l16ui") == 0 ||
                      strcmp(op, "l16si") == 0 || strcmp(op, "s16i") == 0;
        if (n != 3 || strcmp(ops[1], "a1") != 0 || strcmp(ops[0], "a1") == 0 || (!word && !narrow))
        {
            // the frame's address is used some other way: leave it alone
            free(weight);
            return 0;
        }
        int off = atoi(ops[2]);
        if (narrow)
        {
            if (nbad < PROMOTE_MAX_SLOTS)
            {
                badLo[nbad] = off;
                badHi[nbad++] = off + (op[1] == '8' || op[2] == '8' ? 1 : 2);
            }
            else
            {
                free(weight);
                return 0;
            }
            continue;
        }
        int s = 0;
        while (s < nslots && offsets[s] != off)
            s++;
        if (s == nslots)
        {
            if (nslots == PROMOTE_MAX_SLOTS)
            {
                free(weight);
                return 0;
            }
            offsets[s] = off;
            weights[s] = 0;
            bad[s] = false;
            floatOnly[s] = true;
            nslots++;
        }
        weights[s] += weight[i - start];
        if (op[0] != 'l' || op[1] != 's') // not lsi
            floatOnly[s] = floatOnly[s] && strcmp(op, "ssi") == 0;
    }
    // What one float register costs at calls: a save and a restore each.
    int callWeight = 0;
    for (int i = start; i < end; i++)
    {
        trimmedLine(*text->getChildAtPos(i), line, sizeof(line));
        if (strncmp(line, "call", 4) == 0)
            callWeight += weight[i - start];
    }
    free(weight);

    // Slots overlapping each other or a narrow access stay in memory.
    for (int s = 0; s < nslots; s++)
    {
        for (int t = 0; t < nslots; t++)
            if (t != s && offsets[t] > offsets[s] - 4 && offsets[t] < offsets[s] + 4)
                bad[s] = true;
        for (int b = 0; b < nbad; b++)
            if (badLo[b] < offsets[s] + 4 && badHi[b] > offsets[s])
                bad[s] = true;
    }

    // Float-only slots first: float registers the function never names
    // (f0..f2 carry ___div's arguments and result). Calls clobber them, so
    // each is stored back to its slot before every call and reloaded after:
    // only worth it for a slot used more than that costs.
    const char *promotedF[10];
    int promotedOff[10];
    int npf = 0;
    for (int r = 3; r <= 12; r++)
    {
        char reg[4];
        snprintf(reg, sizeof(reg), "f%d", r);
        bool used = false;
        for (int i = start; i < end && !used; i++)
        {
            trimmedLine(*text->getChildAtPos(i), line, sizeof(line));
            used = mentionsRegister(line, reg);
        }
        if (used)
            continue;
        int best = -1;
        for (int t = 0; t < nslots; t++)
            if (!bad[t] && floatOnly[t] && weights[t] > 2 * callWeight && (best < 0 || weights[t] > weights[best]))
                best = t;
        if (best < 0)
            break;
        promotedF[npf] = r == 3 ? "f3" : r == 4 ? "f4" : r == 5 ? "f5" : r == 6 ? "f6" : r == 7 ? "f7" : r == 8 ? "f8" : r == 9 ? "f9" : r == 10 ? "f10" : r == 11 ? "f11" : "f12";
        promotedOff[npf++] = offsets[best];
        for (int i = start; i < end; i++)
        {
            trimmedLine(*text->getChildAtPos(i), line, sizeof(line));
            if (!mentionsRegister(line, "a1"))
                continue;
            strcpy(buf, line);
            int n = splitInstruction(buf, &op, ops);
            if (n != 3 || strcmp(ops[1], "a1") != 0 || atoi(ops[2]) != offsets[best])
                continue;
            if (strcmp(op, "lsi") == 0)
                text->replaceText(i, string_format("mov.s %s,%s", ops[0], reg));
            else
                text->replaceText(i, string_format("mov.s %s,%s", reg, ops[0]));
        }
        weights[best] = 0;
    }

    // a4..a7 survive call8/callExt (the callee's window starts at a8);
    // only those the function never names are free.
    const char *candidates[] = {"a4", "a5", "a6", "a7"};
    const char *freeRegs[4];
    int nfree = 0;
    for (int r = 0; r < 4; r++)
    {
        bool used = false;
        for (int i = start; i < end && !used; i++)
        {
            trimmedLine(*text->getChildAtPos(i), line, sizeof(line));
            used = mentionsRegister(line, candidates[r]);
        }
        if (!used)
            freeRegs[nfree++] = candidates[r];
    }

    for (int k = 0; k < nfree; k++)
    {
        int best = -1;
        for (int t = 0; t < nslots; t++)
            if (!bad[t] && weights[t] > 0 && (best < 0 || weights[t] > weights[best]))
                best = t;
        if (best < 0)
            break;
        const char *reg = freeRegs[k];
        for (int i = start; i < end; i++)
        {
            trimmedLine(*text->getChildAtPos(i), line, sizeof(line));
            if (!mentionsRegister(line, "a1"))
                continue;
            strcpy(buf, line);
            int n = splitInstruction(buf, &op, ops);
            if (n != 3 || strcmp(ops[1], "a1") != 0 || atoi(ops[2]) != offsets[best])
                continue;
            if (strcmp(op, "l32i") == 0)
                text->replaceText(i, string_format("movr %s,%s", ops[0], reg));
            else if (strcmp(op, "s32i") == 0)
                text->replaceText(i, string_format("mov %s,%s", reg, ops[0]));
            else if (strcmp(op, "lsi") == 0)
                text->replaceText(i, string_format("wfr %s,%s", ops[0], reg));
            else if (strcmp(op, "ssi") == 0)
                text->replaceText(i, string_format("rfr %s,%s", reg, ops[0]));
        }
        weights[best] = 0;
    }

    int inserted = 0;
    if (npf == 0)
        return 0;
    foldFloatCopies(text, start, end, promotedF, npf);
    for (int i = end - 1; i >= start; i--)
    {
        trimmedLine(*text->getChildAtPos(i), line, sizeof(line));
        if (strncmp(line, "call", 4) != 0)
            continue;
        for (int k = 0; k < npf; k++)
            text->addAfter(i, string_format("lsi %s,a1,%d", promotedF[k], promotedOff[k]));
        for (int k = 0; k < npf; k++)
            text->addAfter(i - 1, string_format("ssi %s,a1,%d", promotedF[k], promotedOff[k]));
        inserted += 2 * npf;
    }
    return inserted;
}

bool promoteFrameSlotsEnabled = true;

void promoteFrameSlots(Text *text)
{
    if (!promoteFrameSlotsEnabled)
        return;
    char line[160];
    int i = 0;
    while (i < text->size())
    {
        trimmedLine(*text->getChildAtPos(i), line, sizeof(line));
        if (!isFunctionLabel(line))
        {
            i++;
            continue;
        }
        int end = i + 1;
        while (end < text->size())
        {
            trimmedLine(*text->getChildAtPos(end), line, sizeof(line));
            if (isFunctionLabel(line))
                break;
            end++;
        }
        i = end + promoteFunction(text, i + 1, end);
    }
}
