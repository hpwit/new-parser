#pragma once
#ifndef _BINDING_H_
#include "string_functions.h"
#include "vect.h"
#include "stackfunctions.h"

enum  externalType
{
  function,
  value,
};

typedef struct 
{
    void *ptr;
    int offset;
    uint32_t hash;
    uint16_t name_ref;
    uint16_t sign_ref;
    uint16_t shortname_ref;

    externalType type;
} _binding;

void bindFunction(char * out,char * name,char *  in, void * ptr);

extern Text extern_text;
extern vect<_binding> binded_assets;
#endif