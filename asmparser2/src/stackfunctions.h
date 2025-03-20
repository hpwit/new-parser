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

    void replaceText(int pos, char *str);

    char **getChildAtPos(int pos);

    void putIteratorAtPos(int pos);

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
    char cc[1] = {'\0'};
    int position;
    char **_it;
    //char _space[1]="";
};
#endif