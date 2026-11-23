#pragma once
#ifndef __STACK_FUNCTIONS__
#define __STACK_FUNCTIONS__
#include <stdio.h>
#include "vect.h"
#include "string.h"
#include "parser_define.h"

template <class T>
class Stack
{
public:
    Stack() {}
    Stack(T def)
    {
        _default = def;
    }
    void push(T a)
    {
        _stack.push_back(a);
    }
    T pop()
    {
        if (_stack.size() < 1)
            return _default;
        T sav = _stack.back();
        _stack.pop_back();
        return sav;
    }
    T get()
    {

        if (_stack.size() < 1)
            return _default;
        return _stack.back();
    }
    void duplicate()
    {
        if (_stack.size() > 0)
            _stack.push_back(_stack.back());
        else
        {
            _stack.push_back(_default);
            _stack.push_back(_default);
        }
    }
    T front()
    {
        if (_stack.size() > 0)
            return _stack.front();
        else
            return _default;
    }
    void swap()
    {

        T sav = pop();
        T sav2 = pop();
        push(sav);
        push(sav2);
    }
    void set(T k)
    {
        pop();
        push(k);
    }
    void increase()
    {
        //  if( typeid(T).hash_code()==typeid(int).hash_code())
        //  {
        int sav = (int)pop();
        push(sav + 1);
        //  }
    }
    void decrease()
    {
        //  if( typeid(T).hash_code()==typeid(int).hash_code())
        //  {

        int sav = (int)pop();
        push(sav - 1);
        //  }
    }
    void clear()
    {
        _stack.clear();
        _stack.shrink_to_fit();
    }
    vect<T> _stack;
    T _default;
};

class Text
{
public:
Text()
    {
        //_texts=t;
        _texts.clear();
       
        position = 0;
        // _texts.push_back(cc);
        _it = _texts.begin();
    }

    int findText(char *str);

    int addText(char *str, uint16_t si);

    int addText(char *str);
    int addText(char *str,char *e);
    int addText(const char *str);
int addTextNoDelete(char* str);
    void addAfter(int pos, char *s);

    void addBefore(int pos, char *s);

    void addAfter(const char *str);

    void addAfter(char *str);

    char *back();

    char *current();

    void blankCurrent();

    char *front();

    char *textAt(int pos);

    void pop_front();

    void addAfterNoDouble(char *s);

    void addBefore(char *s);
    void addBefore(const char *s);
    void replaceText(int pos, char *str);
    void replaceText(int pos, const char *str);
    // Removes line `pos`, freeing its string unless another line shares
    // it (identical lines are pooled, see findText()).
    void eraseLine(int pos);

    char **getChildAtPos(int pos);

    void putIteratorAtPos(int pos);

    // Real index of _it within _texts -- unlike get() (which returns
    // position-1, a monotonic insert counter that desyncs from _it's
    // actual location once putIteratorAtPos() rewinds _it without
    // touching position), this always matches what replaceText()/
    // getChildAtPos() expect. Safe to call any time, rewound or not.
    int currentPos();

    void end();

    void clear();

    int size();

    char *getText(int pos);

    bool isReused(int pos);

    void pop();

    int get();

    void begin();

    void display();

    Stack<int> sp;
    vect<char *> _texts;

private:
    // The line pool: every string a slot of _texts points at is owned by
    // this open-addressing hash (content -> one canonical copy), with a
    // use count in 4 bytes in front of the characters. addText()/
    // addAfter()/addBefore()/replaceText() all share through it (what
    // findText()'s linear scan used to give in O(n) a line), and the
    // destructive paths (eraseLine, pop, pop_front, replaceText,
    // blankCurrent) put a use back and free the copy with the last one
    // (what isReused()'s O(n) scan was for). _poolIdx in an entry is the
    // slot addText() first gave that content -- only meaningful in
    // append-only buffers (the tokenizer's all_text), which are the only
    // callers of addText()'s dedup return.
    struct PoolEntry
    {
        uint32_t h;
        uint32_t idx;
        char *ptr;
    };
    PoolEntry *_pool = NULL;
    uint32_t _poolCap = 0;    // a power of two, or 0
    uint32_t _poolUsed = 0;   // live + tombstones
    static uint32_t poolHash(const char *s);
    PoolEntry *poolLookup(const char *s, uint32_t h);
    PoolEntry *poolSlotFor(uint32_t h);  // insert position (empty or tombstone)
    void poolGrow();
    // takes ownership of m (a plain malloc'd string): returns the pooled
    // copy, its use count raised, and frees m when it was a duplicate
    char *poolAcquire(char *m, int *firstIdx);
    // one use less; frees the copy and empties its entry on the last
    void poolRelease(char *p);
    void poolReset();

    int position;
    char **_it;
    // Set by putIteratorAtPos(-1): the next addAfter() inserts in front
    // of the first line rather than after _it. A position saved while the
    // buffer was still empty is -1, and means exactly that.
    bool _beforeFirst = false;
    //char _space[1]="";
};
#endif