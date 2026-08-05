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

// Tagged (not anonymous) specifically so the default member initializer
// below doesn't trip "-Wnon-c-typedef-for-linkage" -- this type is only
// ever used from C++ (vect<_binding>, function signatures throughout
// this file), so the C-compatibility the warning is about was never
// actually relevant, just the anonymous-typedef spelling.
struct _binding
{
    // Default-initialized so an intentionally-unbound external (every
    // bindFunction()/bindVariable() call site that passes NULL, e.g.
    // "left unresolved here, a companion loader would bind it later")
    // stays NULL rather than whatever was on the stack the moment the
    // local `_binding asmex;` in bindFunction()/bindVariable() was
    // constructed -- those functions only ever assign `ptr` when the
    // caller's own argument is non-NULL (see their own `if(ptr!=NULL)`
    // guards), so this default is the only thing keeping the NULL case
    // deterministic instead of undefined behavior. A real, reproduced-
    // on-hardware crash (Guru Meditation Error: InstrFetchProhibited,
    // PC=0x00000000) turned out to be *not* a call through a genuine
    // NULL function pointer as its own symptom first suggested, but a
    // call through this exact kind of leftover stack garbage --
    // BouncingBalls.ino's bindFunction(...,"printfln",...,NULL) call
    // (see that example's own fix) left a non-NULL-but-garbage `ptr`
    // behind, and findLink()'s callers have no way to distinguish that
    // from a real binding.
    void *ptr = NULL;
    int offset;
    uint32_t hash;
    uint16_t name_ref;
    uint16_t sign_ref;
    uint16_t shortname_ref;

    externalType type;
};

void bindFunction(char * out,char * name,char *  in, void * ptr);
void bindVariable( char *out,char *name,char *in,void * ptr);
void replaceExternal(char *name, void *ptr);
int findLink(char *label, externalType op);
uint32_t *createExternalLinks();
extern vect<_binding> binded_assets;

extern Text extern_text;
#endif