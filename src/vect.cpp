#include "vect.h"

void * p_realloc(void *original,int new_size)
{
    // NOTE: this used to be malloc(new_size) + memcpy(tmp, original,
    // new_size) + free(original) -- but original was only ever allocated
    // at the *old*, smaller size, so that memcpy always read past the end
    // of it. It usually didn't crash because the extra bytes it read were
    // immediately overwritten by the caller and happened to still be
    // mapped memory, but it's a real heap-buffer-overflow read (confirmed
    // under AddressSanitizer) and intermittently segfaults when the old
    // allocation lands at the edge of a page. realloc() already does this
    // correctly and is compatible with the malloc/free used everywhere
    // else in this codebase.
    // realloc(p, 0) may free p and return NULL (AddressSanitizer's does):
    // an emptied list keeps a minimal block instead
    if (new_size <= 0)
        new_size = 1;
    void * tmp=compile_realloc(original,new_size);
    assert(tmp!=NULL);
    return tmp;
}

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include "esp_heap_caps.h"
void *compile_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL)
        p = malloc(n);
    return p;
}
void *compile_realloc(void *p, size_t n)
{
    void *q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (q == NULL)
        q = realloc(p, n);
    return q;
}
#else
void *compile_alloc(size_t n) { return malloc(n); }
void *compile_realloc(void *p, size_t n) { return realloc(p, n); }
#endif
