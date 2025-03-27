#include "vect.h"

void * p_realloc(void *original,int new_size)
{
   
    void * tmp=(void*)malloc(new_size);
    //PARSER_LOG("creating %d",new_size);
    assert(tmp!=NULL);
    if(original!=NULL)
    {
        memcpy(tmp,original,new_size);
    free(original);
    }
    return tmp;
}