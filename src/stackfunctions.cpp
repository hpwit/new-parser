#include "stackfunctions.h"
#include "parser_define.h"
#include "string_constants.h"
#include <stdio.h>

#include "string.h"
//#define __SPEED

    // ---- the line pool (see stackfunctions.h): FNV-1a, linear probing,
    // a tombstone is an entry whose ptr is (char *)1

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

    Text::PoolEntry *Text::poolLookup(const char *s, uint32_t h)
    {
        if (_poolCap == 0)
            return NULL;
        uint32_t i = h & (_poolCap - 1);
        while (true)
        {
            PoolEntry *e = &_pool[i];
            if (e->ptr == NULL)
                return NULL;
            if (e->ptr != (char *)1 && e->h == h && strcmp(e->ptr, s) == 0)
                return e;
            i = (i + 1) & (_poolCap - 1);
        }
    }

    Text::PoolEntry *Text::poolSlotFor(uint32_t h)
    {
        uint32_t i = h & (_poolCap - 1);
        while (true)
        {
            PoolEntry *e = &_pool[i];
            if (e->ptr == NULL || e->ptr == (char *)1)
                return e;
            i = (i + 1) & (_poolCap - 1);
        }
    }

    void Text::poolGrow()
    {
        uint32_t newCap = _poolCap == 0 ? 256 : _poolCap * 2;
        PoolEntry *old = _pool;
        uint32_t oldCap = _poolCap;
        _pool = (PoolEntry *)calloc(newCap, sizeof(PoolEntry));
        assert(_pool != NULL);
        _poolCap = newCap;
        _poolUsed = 0;
        for (uint32_t i = 0; i < oldCap; i++)
            if (old[i].ptr != NULL && old[i].ptr != (char *)1)
            {
                PoolEntry *e = poolSlotFor(old[i].h);
                *e = old[i];
                _poolUsed++;
            }
        free(old);
    }

    char *Text::poolAcquire(char *m, int *firstIdx)
    {
        uint32_t h = poolHash(m);
        PoolEntry *e = poolLookup(m, h);
        if (e != NULL)
        {
            if (m != e->ptr)
                free(m);
            *(uint32_t *)(e->ptr - 4) += 1;
            if (firstIdx)
                *firstIdx = (int)e->idx;
            return e->ptr;
        }
        if (_poolCap == 0 || _poolUsed * 4 >= _poolCap * 3)
            poolGrow();
        size_t n = strlen(m);
        char *q = (char *)malloc(4 + n + 1);
        assert(q != NULL);
        *(uint32_t *)q = 1;
        memcpy(q + 4, m, n + 1);
        free(m);
        e = poolSlotFor(h);
        e->h = h;
        e->ptr = q + 4;
        e->idx = (uint32_t)-1;
        _poolUsed++;
        if (firstIdx)
            *firstIdx = -1;
        return q + 4;
    }

    void Text::poolRelease(char *p)
    {
        if (p == NULL)
            return;
        uint32_t h = poolHash(p);
        PoolEntry *e = poolLookup(p, h);
        if (e == NULL || e->ptr != p)
        {
            // not a pooled copy (addText(str, si)'s raw append): its own
            free(p);
            return;
        }
        uint32_t *refs = (uint32_t *)(p - 4);
        if (--*refs == 0)
        {
            e->ptr = (char *)1;
            free(p - 4);
        }
    }

    void Text::poolReset()
    {
        free(_pool);
        _pool = NULL;
        _poolCap = 0;
        _poolUsed = 0;
    }

    int Text::findText(char *str)
    {
        PoolEntry *e = poolLookup(str, poolHash(str));
        if (e == NULL)
            return -1;
        // the slot addText() recorded, when it still holds this string
        if (e->idx != (uint32_t)-1 && (int)e->idx < _texts.size() && _texts.get((int)e->idx) == e->ptr)
            return (int)e->idx;
        // a line addAfter()/addBefore() put in (no slot recorded), or the
        // recorded one moved: an entry's uses all live in this buffer, so
        // a scan by pointer finds one (rare: the few existence checks)
        for (int i = 0; i < _texts.size(); i++)
            if (_texts.get(i) == e->ptr)
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
        char *p = poolAcquire(str, NULL);
        PoolEntry *e = poolLookup(p, poolHash(p));
        // the slot recorded for this content, when it still holds it --
        // pop() (the tokenizer's #define path) can have taken it away
        if (e->idx != (uint32_t)-1 && (int)e->idx < _texts.size() && _texts.get((int)e->idx) == p)
        {
            // already a slot: no new one, the use we just took given back
            *(uint32_t *)(p - 4) -= 1;
            return (int)e->idx;
        }
        _texts.push_back(p);
        position++;
        e->idx = (uint32_t)(_texts.size() - 1);
        return (int)e->idx;
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
            char *m = poolAcquire(s, NULL);
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
        char *m = poolAcquire(str, NULL);
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
        char *blank = poolAcquire(m2, NULL);
        if (blank == old2)
        {
            // already blank: the use we just took given back
            *(uint32_t *)(blank - 4) -= 1;
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
        _it = _texts.insertBefore(_it, poolAcquire(s, NULL));
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
            char *p = poolAcquire(str, NULL);
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
            PoolEntry *e = poolLookup(p, poolHash(p));
            if (e == NULL || e->ptr != p)
            {
                free(p);
                // the same raw pointer never sits in two slots
                for (int j = i + 1; j < _texts.size(); j++)
                    if (_texts.get(j) == p)
                        *_texts.getptr(j) = NULL;
                *_texts.getptr(i) = NULL;
            }
        }
        // every pooled string exactly once, whatever its use count
        for (uint32_t i = 0; i < _poolCap; i++)
            if (_pool[i].ptr != NULL && _pool[i].ptr != (char *)1)
                free(_pool[i].ptr - 4);
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
        PoolEntry *e = poolLookup(c, poolHash(c));
        return e != NULL && e->ptr == c && *(uint32_t *)(c - 4) > 1;
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
