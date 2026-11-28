#include "stackfunctions.h"
#include "parser_define.h"
#include "string_constants.h"
#include <stdio.h>

#include "string.h"
//#define __SPEED

    // ---- the line pool (see stackfunctions.h): FNV-1a, linear probing;
    // a string is [use count:4][slot index:4][characters], one heap block

    static inline uint32_t &refsOf(char *p) { return *(uint32_t *)(p - 8); }
    static inline int32_t &idxOf(char *p) { return *(int32_t *)(p - 4); }

    uint32_t Text::poolHash(const char *s)
    {
        uint32_t h = 2166136261u;
        while (*s)
        {
            h ^= (uint8_t)*s++;
            h *= 16777619u;
        }
        return h == 0 ? 1 : h;
    }

    char **Text::poolLookup(const char *s, uint32_t h)
    {
        if (_poolCap == 0)
            return NULL;
        uint32_t i = h & (_poolCap - 1);
        while (true)
        {
            char *p = _pool[i];
            if (p == NULL)
                return NULL;
            if (p != (char *)1 && strcmp(p, s) == 0)
                return &_pool[i];
            i = (i + 1) & (_poolCap - 1);
        }
    }

    char **Text::poolSlotFor(uint32_t h)
    {
        uint32_t i = h & (_poolCap - 1);
        while (true)
        {
            char *p = _pool[i];
            if (p == NULL || p == (char *)1)
                return &_pool[i];
            i = (i + 1) & (_poolCap - 1);
        }
    }

    void Text::poolGrow()
    {
        // mostly tombstones: the same size, rehashed, clears them
        uint32_t newCap = _poolCap == 0 ? 256 : (_poolLive * 2 < _poolCap ? _poolCap : _poolCap * 2);
        char **old = _pool;
        uint32_t oldCap = _poolCap;
        _pool = (char **)compile_alloc((size_t)newCap * sizeof(char *));
        assert(_pool != NULL);
        memset(_pool, 0, (size_t)newCap * sizeof(char *));
        _poolCap = newCap;
        _poolUsed = 0;
        for (uint32_t i = 0; i < oldCap; i++)
            if (old[i] != NULL && old[i] != (char *)1)
            {
                *poolSlotFor(poolHash(old[i])) = old[i];
                _poolUsed++;
            }
        free(old);
    }

    char *Text::poolAcquire(char *m)
    {
        uint32_t h = poolHash(m);
        char **e = poolLookup(m, h);
        if (e != NULL)
        {
            char *p = *e;
            if (m != p)
                free(m);
            refsOf(p) += 1;
            return p;
        }
        if (_poolCap == 0 || _poolUsed * 4 >= _poolCap * 3)
            poolGrow();
        size_t n = strlen(m);
        char *q = (char *)malloc(8 + n + 1);
        assert(q != NULL);
        char *p = q + 8;
        refsOf(p) = 1;
        idxOf(p) = -1;
        memcpy(p, m, n + 1);
        free(m);
        char **slot = poolSlotFor(h);
        if (*slot == NULL)
            _poolUsed++;
        *slot = p;
        _poolLive++;
        return p;
    }

    void Text::poolRelease(char *p)
    {
        if (p == NULL)
            return;
        char **e = poolLookup(p, poolHash(p));
        if (e == NULL || *e != p)
        {
            // not a pooled copy (addText(str, si)'s raw append): its own
            free(p);
            return;
        }
        if (--refsOf(p) == 0)
        {
            *e = (char *)1;
            _poolLive--;
            free(p - 8);
        }
    }

    void Text::poolReset()
    {
        free(_pool);
        _pool = NULL;
        _poolCap = 0;
        _poolUsed = 0;
        _poolLive = 0;
    }

    int Text::findText(char *str)
    {
        char **e = poolLookup(str, poolHash(str));
        if (e == NULL)
            return -1;
        char *p = *e;
        // the slot addText() recorded, when it still holds this string
        int idx = idxOf(p);
        if (idx >= 0 && idx < _texts.size() && _texts.get(idx) == p)
            return idx;
        // a line addAfter()/addBefore() put in (no slot recorded), or the
        // recorded one moved: an entry's uses all live in this buffer, so
        // a scan by pointer finds one (rare: the few existence checks)
        for (int i = 0; i < _texts.size(); i++)
            if (_texts.get(i) == p)
                return i;
        return -1;
    }
    int Text::addText(char *str, uint16_t si)
    {
        // NOT pooled: si bytes may be binary (.bytes payloads hold NULs),
        // and the pool hashes and compares as C strings. poolRelease()
        // recognises such a raw copy (no entry holds its pointer) and
        // frees it plainly.
        char *m = (char *)malloc(si + 1);
        memcpy(m, str, si);
        m[si] = 0;
        _texts.push_back(m);
        position++;
        return _texts.size() - 1;
    }
    int Text::addText(const char *str)
    {
        char *   m = (char *)malloc(strlen(str)+1);
        memcpy(m, str, strlen(str));
        m[strlen(str)] = 0;
       return addText(m);
    }
    int Text::addText(char* str,char * e)
    { 
        assert(e>=str);
        char *   m = (char *)malloc(e-str+2);
        memcpy(m, str, e-str+1);
        m[e-str+1] = 0;

       return addText(m);
        
    }
    int Text::addTextNoDelete(char *str)
    {
        return addText(str);
    }
    // the line itself deduplicated: the slot its content first got (the
    // tokenizer's string table), a new one otherwise
    int Text::addText(char *str)
    {
        char *p = poolAcquire(str);
        // the slot recorded for this content, when it still holds it --
        // pop() (the tokenizer's #define path) can have taken it away
        int idx = idxOf(p);
        if (idx >= 0 && idx < _texts.size() && _texts.get(idx) == p)
        {
            // already a slot: no new one, the use we just took given back
            refsOf(p) -= 1;
            return idx;
        }
        _texts.push_back(p);
        position++;
        idxOf(p) = _texts.size() - 1;
        return idxOf(p);
    }
    void Text::addAfter(int pos, char * s)
    {
        _beforeFirst = false;
        // "After line -1" is the very start of the buffer -- what a saved
        // position means when it was taken while the buffer was still
        // empty. getChildAtPos(-1) is end(), so this used to append
        // instead: the first global initializer in the footer got its
        // store's address loaded after the store.
        if (pos < 0)
        {
            char *m = poolAcquire(s);
            if (_texts.size() == 0)
                _texts.push_back(m);
            else
                _texts.insertBefore(_texts.begin(), m);
            position++;
            _it = getChildAtPos(position - 1);
            return;
        }
        _it = getChildAtPos(pos);
        // printf(" on recupere %d:%s\n",pos,(*__it).c_str());
        // if((*_it).compare(s)!=0)
        //{
        addAfter(s);
        position--;
        _it = getChildAtPos(position);
        position++;
        // }
    }

    void Text::addBefore(int pos, char * s)
    {
        _beforeFirst = false;
        _it = getChildAtPos(pos-1);
        // printf(" on recupere %d:%s\n",pos,(*__it).c_str());
        // if((*_it).compare(s)!=0)
        //{
        addBefore(s);
        position--;
        _it = getChildAtPos(position);
        position++;
        // }
    }
   void Text::addAfter(const char * str)
   {
    char *m=(char *)malloc(strlen(str)+1);
    memcpy(m,str,strlen(str));
    m[strlen(str)]=0;
    addAfter(m);
   }
    void Text::addAfter(char *str)
    {
        char *m = poolAcquire(str);
        if (_beforeFirst)
        {
            _beforeFirst = false;
            _it = _texts.insertBefore(_texts.begin(), m);
        }
        else if (_it == _texts.end())
        {
           
            _texts.push_back(m);
            _it = _texts.end();
            _it--;
        }
        else
        {
 
            _it = _texts.insertAfter(_it, m);
        }
        position++;
    }
    char* Text::back()
    {
              if (_texts.size() > 0)
            return _texts.back();
        else
            return _end_text;
    }
    char * Text::current()
    {
        return *_it;
    }
    void Text::blankCurrent()
    {
        int idx2 = currentPos();
        if (idx2 < 0 || idx2 >= _texts.size())
            return;
        char *old2 = _texts.get(idx2);
        char *m2 = (char *)malloc(2);
        m2[0] = 32;
        m2[1] = 0;
        char *blank = poolAcquire(m2);
        if (blank == old2)
        {
            // already blank: the use we just took given back
            refsOf(blank) -= 1;
            return;
        }
        poolRelease(old2);
        *getChildAtPos(idx2) = blank;
        return;
    }

    char * Text::front()
    {
        if (_texts.size() > 0)
            return _texts.front();
        else
            return _end_text;
    }
    void Text::pop_front()
    {
        if (_texts.size() > 0)
        {
            poolRelease(_texts.front());
            _texts.erase(_texts.begin());
        }
    }
    void Text::addAfterNoDouble(char* s)
    {

       // char *str;
        if (_it != _texts.end())
        {

            if (strcmp(*_it,s) == 0)
            {
                // the same line again: not added, and not kept either (it
                // leaked, a few hundred bytes per float-heavy script)
                free(s);
                return;
            }
  

        }

        addAfter(s);
    }
    void Text::addBefore(char *s)
    {
        _it = _texts.insertBefore(_it, poolAcquire(s));
        _it++;
        position++;
    }
    void Text::addBefore(const char * s)
    {
        char *m=(char *)malloc(strlen(s)+1);
        memcpy(m,s,strlen(s));
        m[strlen(s)]=0;
        addBefore(m);
    }

    void Text::replaceText(int pos, char * str)
    {
        if (pos >= 0 and pos < size())
        {
            // Look up a pooled duplicate and check reuse before freeing
            // anything -- both scan the whole pool, including this slot's
            // current (about-to-be-replaced) entry, so freeing it first
            // would leave them reading a dangling pointer.
            // the new string pooled before the old use goes back, so a
            // replace by the same content keeps the copy alive throughout
            char *old = _texts.get(pos);
            char *p = poolAcquire(str);
            poolRelease(old);
            *_texts.getptr(pos) = p;
        }
        else
        {
            free(str);
        }
    }
    void Text::eraseLine(int pos)
    {
        if (pos < 0 || pos >= size())
            return;
        poolRelease(_texts.get(pos));
        _texts.erase(pos);
    }
    void Text::releaseLine(int pos)
    {
        if (pos < 0 || pos >= size())
            return;
        poolRelease(_texts.get(pos));
        *_texts.getptr(pos) = NULL;
    }
    int Text::compactLines(int from, int to)
    {
        if (from < 0)
            from = 0;
        if (to > size())
            to = size();
        int w = from;
        for (int r = from; r < to; r++)
            if (_texts.get(r) != NULL)
                *_texts.getptr(w++) = _texts.get(r);
        if (to > w)
            _texts.eraseRange(w, to - w);
        return to - w;
    }
    void Text::replaceText(int pos, const char * str)
    {
        char *m = (char *)malloc(strlen(str) + 1);
        memcpy(m, str, strlen(str));
        m[strlen(str)] = 0;
        replaceText(pos, m);
    }
    char ** Text::getChildAtPos(int pos)
    {
        
        if (pos >= _texts.size() || pos < 0)
        {
            return _texts.end();
        }
        else
        {
             return _texts.getptr(pos);
        }
        return _texts.end();
    }
    void Text::putIteratorAtPos(int pos)
    {
        _beforeFirst = pos < 0 && _texts.size() > 0;
        _it = getChildAtPos(pos);
        //position=pos-1;
    }
    int Text::currentPos()
    {
        if (_it == _texts.end())
            return _texts.size() - 1;
        return (int)(_it - _texts.begin());
    }
    void Text::end()
    {
        _beforeFirst = false;
        _it = getChildAtPos(_texts.size() - 1);
    }
    void Text::clear()
    {
        _beforeFirst = false;
        // the raw slots first (addText(str, si)'s binary payloads: no
        // pool entry holds their pointer), while the pooled strings are
        // still alive to compare against
        for (int i = 0; i < _texts.size(); i++)
        {
            char *p = _texts.get(i);
            if (p == NULL)
                continue;
            char **e = poolLookup(p, poolHash(p));
            if (e == NULL || *e != p)
            {
                free(p);
                // the same raw pointer never sits in two slots
                for (int j = i + 1; j < _texts.size(); j++)
                    if (_texts.get(j) == p)
                        *_texts.getptr(j) = NULL;
                *_texts.getptr(i) = NULL;
            }
        }
        // every pooled string still alive, once, whatever its use count
        for (uint32_t i = 0; i < _poolCap; i++)
            if (_pool[i] != NULL && _pool[i] != (char *)1)
                free(_pool[i] - 8);
        poolReset();
        _texts.clear();
        sp.clear();
        position = 0;
        _it = _texts.begin();
    }
    int Text::size()
    {
        return _texts.size();
    }
    char *Text::getText(int pos)
    {
        if (pos >= 0 and pos < _texts.size())
        {
            
            return _texts[pos];
        }
        else
        {
            return _end_text;
        }
    }
    bool Text::isReused(int pos)
    {
        if (pos < 0 or pos >= _texts.size())
            return false;
        char *c = _texts[pos];
        char **e = poolLookup(c, poolHash(c));
        return e != NULL && *e == c && refsOf(c) > 1;
    }
    void Text::pop()
    {
        if (size() > 0)
        {
            poolRelease(_texts.back());
            _texts.pop_back();
           // _texts.shrink_to_fit();
            position--;
            _it = _texts.end();
            _it--;
        }
    }
    int Text::get()
    {

        return position - 1;
    }
    void Text::begin()
    {
        _beforeFirst = false;
        _it = _texts.begin();
        position = 0;
    }
    void Text::display()
    {
       for(int i=0;i<_texts.size();i++)
       {
        printf("%d:%s \n",i,_texts.get(i));
       }
    }
