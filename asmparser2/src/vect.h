#pragma once
#ifndef __VECT__
#define __VECT__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assert.h"
#include "parser_define.h"


void * p_realloc(void *original,int new_size);
template <class T>
class vect
{
public:
    vect();
    ~vect();
    void init();
    int size();
    T *push_back(T asset);
    T get(int i);
    T *getptr(int i);
    T *backptr();
    T back();
    T *frontptr();
    T front();
    T pop_back();
    T pop_front();
    T *begin();
    T *end();
    T *insertBefore(T *object,T asset );
    void erase(T *asset);
    void erase(int k);
    void shrink_to_fit();
    T *insertAfter(T *object,T asset );
    T *push_front(T asset);
    void clear();
    void empty();
    T operator[](int i)
    {
        return get(i);
    }

private:
    T *point=NULL;
     uint16_t _size=0;
    uint16_t size_item;
   
};

template <typename T>
vect<T>::vect()
{
    point = NULL;
    size_item = sizeof(T);
    _size = 0;
}
template <typename T>
void vect<T>::init()
{
    point = NULL;
    size_item = sizeof(T);
    _size = 0;
}
template <typename T>
vect<T>::~vect()
{
    clear();
}
template <typename T>
int vect<T>::size()
{
    return _size;
}


template <typename T>
T *vect<T>::push_back(T asset)
{
    T *point2 = NULL;

    if (point == NULL)
        point2 = (T *)malloc(size_item);
    else
        point2 = (T *)p_realloc(point, (_size + 1) * size_item);
    if (point2 == NULL)
    {
        return NULL;
    }
    else
    {
        memcpy(point2 + _size, &asset, size_item);
        point = point2;
        _size++;
        return point2 + _size - 1;
    }
}

template <typename T>
T vect<T>::get(int i)
{
    assert(i >= 0 and i < size());
    return *(point + i);
}

template <typename T>
T *vect<T>::getptr(int i)
{
    assert(i >= 0 and i < size());
    return point + i;
}

template <typename T>
T *vect<T>::backptr()
{
    assert(_size > 0);
    return point + _size - 1;
}

template <typename T>
T vect<T>::back()
{
    assert(_size > 0);
    return *(point + _size - 1);
}

template <typename T>
T *vect<T>::frontptr()
{
    assert(_size > 0);
    return point;
}

template <typename T>
T vect<T>::front()
{
    assert(_size > 0);
    return *point;
}

template <typename T>
T vect<T>::pop_back()
{
    assert(_size > 0);
    T res = back();
    if (_size > 1)
        point = (T *)p_realloc(point, (_size - 1) * size_item);
    else
    {
        free(point);
        point = NULL;
    }
    _size--;
    return res;
}

template <typename T>
T vect<T>::pop_front()
{
    assert(_size > 0);
    T res = front();
    memmove(point, point + 1, (_size - 1) * size_item);
    if (_size > 1)
        point = (T *)p_realloc(point, (_size - 1) * size_item);
    else
    {
        free(point);
        point = NULL;
    }
    _size--;
    return res;
}

template <typename T>
T *vect<T>::begin()
{
    return point;
}

template <typename T>
T *vect<T>::end()
{
    if (point == NULL)
        return NULL;
    return point + _size;
}

template <typename T>
T *vect<T>::insertBefore(T *object,T asset )
{
    assert(object - point <= size_item * _size);
    uint32_t diff = object - point;
    T *point2 = (T *)p_realloc(point, (_size + 1) * size_item);
    memmove(point2 + diff + 1, point2 + diff, (size_item) * (_size - diff));
    memcpy(point2 + diff, &asset, size_item);
    _size++;
    point = point2;
    return (point2 + diff);
}

template <typename T>
void vect<T>::erase(T *asset)
{
    assert(asset - point < _size);
    uint32_t diff = asset - point;
    memmove(point + diff, point + diff + 1, (size_item) * (_size - diff - 1));
    // Erasing the last remaining element shrinks to a 0-byte request.
    // realloc(ptr, 0) is standards-compliant to return NULL (it's free()'s
    // job in that case) -- p_realloc()'s assert(tmp!=NULL) treats that as
    // an allocation failure, aborting. Harmless on host malloc (which
    // historically tends to return a small non-NULL pointer here instead),
    // but ASan's allocator and real hardware (confirmed: this exact path
    // crashes on ESP32 for a trivial script) both return NULL as the
    // standard permits. Special-case it the same way shrink_to_fit()
    // already does below, instead of going through p_realloc at all.
    if (_size == 1)
    {
        free(point);
        point = NULL;
    }
    else
    {
        point = (T *)p_realloc(point, (_size - 1) * size_item);
    }
    _size--;
}

template <typename T>
void vect<T>::erase(int k)
{
    if (k >= 0 and k < _size)
    {
        erase(getptr(k));
    }
}

template <typename T>
void vect<T>::shrink_to_fit()
{
    
    if (_size == 0 and point != NULL)
    {
        free(point);
        point = NULL;
        
    }
    else if(_size > 0)
    point = (T *)p_realloc(point, _size * size_item);
}
template <typename T>
T *vect<T>::insertAfter(T *object,T asset )
{
    assert(object - point < size_item * _size);
    uint32_t diff = object - point;
    T *point2 = (T *)p_realloc(point, (_size + 1) * size_item);
    memmove(point2 + diff + 2, point2 + diff + 1, (size_item) * (_size - diff - 1));
    memcpy(point2 + diff + 1, &asset, size_item);
    _size++;
    point = point2;
    return (point2 + diff + 1);
}

template <typename T>
T *vect<T>::push_front(T asset)
{
    if (point != NULL)
        return insertBefore(asset, point);
    else
        return push_back(asset);
}

template <typename T>
void vect<T>::clear()
{
    if (point != NULL)
        free(point);
    _size = 0;
    point = NULL;
}
template <typename T>
void vect<T>::empty()
{
    for(int i=0;i<_size;i++)
    {
        if(*(point+i)!=NULL)
        {
            for(int j=i+1;j<_size;j++)
            {
                if(*(point+i)==*(point+j))
                *(point+j)=NULL;
            }
            free(*(point+i));
            *(point+i)=NULL;
        }
    }
    clear();
}


#endif