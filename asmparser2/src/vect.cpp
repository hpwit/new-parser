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
    void * tmp=realloc(original,new_size);
    assert(tmp!=NULL);
    return tmp;
}