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
    // fact.
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

        if (index.size() == 0)
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
            for (int mp = 0; mp < index.size(); mp++)
                if (strcmp(d2.get(1), from.get(mp)) == 0)
                {
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
                  strcmp(op, "quou") == 0 || strcmp(op, "quos") == 0) &&
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
