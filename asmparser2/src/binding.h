#pragma once
#ifndef _BINDING_H_
#define _BINDING_H_
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
void bindVariable( char *out,char *name,char *in,void * ptr);
void replaceExternal(char *name, void *ptr);
int findLink(char *label, externalType op);
uint32_t *createExternalLinks();
extern vect<_binding> binded_assets;

extern Text extern_text;
#endif