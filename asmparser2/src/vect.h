#pragma once
#ifndef __VECT__
#define __VECT__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
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
    // How many elements `point` actually has room for -- may be larger
    // than _size (see push_back()'s amortized-doubling growth below).
    // Distinct from _size, which is "how many are actually live" --
    // get()/getptr()/etc. all bounds-check against _size, never this.
    uint16_t _capacity=0;
    void _growIfFull();
};

template <typename T>
vect<T>::vect()
{
    point = NULL;
    size_item = sizeof(T);
    _size = 0;
    _capacity = 0;
}
template <typename T>
void vect<T>::init()
{
    point = NULL;
    size_item = sizeof(T);
    _size = 0;
    _capacity = 0;
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
    // Amortized-doubling growth: only reallocate once _size actually
    // catches up to _capacity, and then by roughly 2x, not by exactly
    // one element every single call. A script with a few thousand
    // instruction lines used to mean a few thousand one-element-at-a-
    // time realloc() calls for `content` alone (before this, _size and
    // capacity were always the same thing) -- brutal on a real,
    // non-compacting embedded heap allocator: confirmed as the actual
    // cause of `p_realloc`'s assert(tmp!=NULL) firing on real ESP32-S3
    // (no PSRAM) hardware compiling MultiEffectController.ino, where
    // realloc() legitimately returned NULL from heap fragmentation, not
    // from the already-fixed realloc(ptr,0) case (see erase()'s own
    // comment) -- there was plenty of nominal free heap, just no single
    // contiguous block big enough after thousands of tiny one-at-a-time
    // grow/shrink cycles. Doubling turns that into O(log n) reallocations
    // instead of O(n).
    if (_size >= _capacity)
    {
        int new_capacity = (_capacity == 0) ? 1 : (int)_capacity * 2;
        T *point2 = (point == NULL) ? (T *)malloc((size_t)new_capacity * size_item)
                                     : (T *)p_realloc(point, new_capacity * size_item);
        if (point2 == NULL)
        {
            return NULL;
        }
        point = point2;
        _capacity = (uint16_t)new_capacity;
    }
    memcpy(point + _size, &asset, size_item);
    _size++;
    return point + _size - 1;
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
    // Matches std::vector: popping never shrinks capacity/reallocates --
    // only shrink_to_fit() (or clear(), which frees outright) gives
    // memory back. See push_back()'s comment for why avoiding a realloc
    // here on every single call matters.
    assert(_size > 0);
    T res = back();
    _size--;
    return res;
}

template <typename T>
T vect<T>::pop_front()
{
    assert(_size > 0);
    T res = front();
    memmove(point, point + 1, (_size - 1) * size_item);
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

// Shared by insertBefore()/insertAfter(): grows capacity by doubling
// (same reasoning as push_back()'s own comment) before an insert that's
// about to need one more slot than is currently available.
template <typename T>
void vect<T>::_growIfFull()
{
    if (_size >= _capacity)
    {
        int new_capacity = (_capacity == 0) ? 1 : (int)_capacity * 2;
        point = (T *)p_realloc(point, new_capacity * size_item);
        _capacity = (uint16_t)new_capacity;
    }
}

template <typename T>
T *vect<T>::insertBefore(T *object,T asset )
{
    // `object - point` is pointer arithmetic on T* -- an *element*
    // count -- but this used to compare it against `size_item * _size`,
    // a *byte* count (size_item times too large a bound for any
    // size_item > 1, i.e. almost always). A genuinely out-of-bounds
    // `object` could never actually trip this, silently letting the
    // memmove below run with garbage `diff` instead of catching the
    // misuse where it happens. erase()'s own bounds assert already
    // compares element counts to element counts correctly -- match it.
    assert(object - point <= _size);
    uint32_t diff = object - point;
    _growIfFull();
    memmove(point + diff + 1, point + diff, (size_item) * (_size - diff));
    memcpy(point + diff, &asset, size_item);
    _size++;
    return (point + diff);
}

template <typename T>
void vect<T>::erase(T *asset)
{
    // Matches pop_back()/pop_front(): never reallocates on removal, so
    // the realloc(ptr, 0) case that used to need special-casing here
    // (see git history) can't come up at all any more -- there's no
    // p_realloc() call on this path left to hit it.
    assert(asset - point < _size);
    uint32_t diff = asset - point;
    memmove(point + diff, point + diff + 1, (size_item) * (_size - diff - 1));
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
    // The one place that actually gives unused capacity back -- safe to
    // call after a burst of push_back()s/insertBefore()s followed by a
    // long stretch where the vect's final size is now known and stable
    // (tokenize.cpp/stackfunctions.h already do, at exactly those
    // points).
    if (_size == 0 and point != NULL)
    {
        free(point);
        point = NULL;
        _capacity = 0;
    }
    else if (_size > 0 and _size < _capacity)
    {
        point = (T *)p_realloc(point, _size * size_item);
        _capacity = _size;
    }
}
template <typename T>
T *vect<T>::insertAfter(T *object,T asset )
{
    // Same element-count-vs-byte-count fix as insertBefore()'s assert.
    assert(object - point < _size);
    uint32_t diff = object - point;
    _growIfFull();
    memmove(point + diff + 2, point + diff + 1, (size_item) * (_size - diff - 1));
    memcpy(point + diff + 1, &asset, size_item);
    _size++;
    return (point + diff + 1);
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
    _capacity = 0;
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