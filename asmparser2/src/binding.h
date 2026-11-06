#pragma once
#ifndef _BINDING_H_
#define _BINDING_H_
#include "string_functions.h"
#include "vect.h"
#include "stackfunctions.h"
#include <stddef.h>
#include <type_traits>

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
// Makes a struct the host program defines known to every script compiled
// afterwards, so the script needn't repeat it: pass the C definition,
// e.g. bindStruct("struct key_t { char key; char code; };"). Script
// structs follow C's layout, so a bound variable of that type
// (bindVariable("key_t", "key", NULL, &hostKey)) reads the host's own
// fields. A script may still define it, if identically.
void bindStruct(const char *definition);
// Declares a C struct and keeps its definition as text for bindStruct(),
// so it is written once:
//   SCRIPT_STRUCT(sensor, { char id; int value; float average; })
//   ...
//   bindStruct(sensor_definition);
#define SCRIPT_STRUCT(name, ...)  \
    struct name __VA_ARGS__;      \
    static const char *name##_definition = "struct " #name " " #__VA_ARGS__ ";";
extern vect<char *> binded_structs;
// Binds a struct the sketch didn't write itself (one from a library's
// header): the script definition is made from the C type, field by field,
// and the C layout is checked against the script's before anything is
// bound. List every field, in order:
//   BIND_STRUCT(key_return, key_char, key_code);
// A field must be a number, a char or a bool (1, 2 or 4 bytes; an
// unsigned char is the script's uint8_t). Returns false, and says why,
// when the layouts would differ; at most 16 fields.
struct scriptFieldInfo
{
    const char *name;
    const char *type;
    int offset;
    int size;
};

template <typename F>
constexpr const char *scriptTypeOf()
{
    typedef typename std::remove_cv<F>::type T;
    static_assert(std::is_arithmetic<T>::value, "BIND_STRUCT: a field must be a number, a char or a bool");
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4, "BIND_STRUCT: a field must be 1, 2 or 4 bytes (no double, no 64-bit)");
    return std::is_same<T, bool>::value ? "bool"
           : std::is_floating_point<T>::value ? "float"
           : std::is_same<T, char>::value ? "char"
           : sizeof(T) == 1 ? (std::is_signed<T>::value ? "char" : "uint8_t")
           : sizeof(T) == 2 ? (std::is_signed<T>::value ? "s_int" : "uint16_t")
                            : (std::is_signed<T>::value ? "int" : "uint32_t");
}

bool bindStructFields(const char *name, int size, const scriptFieldInfo *fields, int count);

#define _BS_FIELD(T, f) {#f, scriptTypeOf<decltype(((T *)0)->f)>(), (int)offsetof(T, f), (int)sizeof(((T *)0)->f)},
#define _BS_E1(T, a) _BS_FIELD(T, a)
#define _BS_E2(T, a, ...) _BS_FIELD(T, a) _BS_E1(T, __VA_ARGS__)
#define _BS_E3(T, a, ...) _BS_FIELD(T, a) _BS_E2(T, __VA_ARGS__)
#define _BS_E4(T, a, ...) _BS_FIELD(T, a) _BS_E3(T, __VA_ARGS__)
#define _BS_E5(T, a, ...) _BS_FIELD(T, a) _BS_E4(T, __VA_ARGS__)
#define _BS_E6(T, a, ...) _BS_FIELD(T, a) _BS_E5(T, __VA_ARGS__)
#define _BS_E7(T, a, ...) _BS_FIELD(T, a) _BS_E6(T, __VA_ARGS__)
#define _BS_E8(T, a, ...) _BS_FIELD(T, a) _BS_E7(T, __VA_ARGS__)
#define _BS_E9(T, a, ...) _BS_FIELD(T, a) _BS_E8(T, __VA_ARGS__)
#define _BS_E10(T, a, ...) _BS_FIELD(T, a) _BS_E9(T, __VA_ARGS__)
#define _BS_E11(T, a, ...) _BS_FIELD(T, a) _BS_E10(T, __VA_ARGS__)
#define _BS_E12(T, a, ...) _BS_FIELD(T, a) _BS_E11(T, __VA_ARGS__)
#define _BS_E13(T, a, ...) _BS_FIELD(T, a) _BS_E12(T, __VA_ARGS__)
#define _BS_E14(T, a, ...) _BS_FIELD(T, a) _BS_E13(T, __VA_ARGS__)
#define _BS_E15(T, a, ...) _BS_FIELD(T, a) _BS_E14(T, __VA_ARGS__)
#define _BS_E16(T, a, ...) _BS_FIELD(T, a) _BS_E15(T, __VA_ARGS__)
#define _BS_COUNT(...) _BS_COUNT_(__VA_ARGS__, 16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1)
#define _BS_COUNT_(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16, N, ...) N
#define _BS_CAT(a, b) _BS_CAT_(a, b)
#define _BS_CAT_(a, b) a##b
#define BIND_STRUCT(T, ...)                                                                   \
    ([]() {                                                                                   \
        static const scriptFieldInfo _fields[] = {_BS_CAT(_BS_E, _BS_COUNT(__VA_ARGS__))(T, __VA_ARGS__)}; \
        return bindStructFields(#T, (int)sizeof(T), _fields, (int)(sizeof(_fields) / sizeof(_fields[0]))); \
    }())
void replaceExternal(char *name, void *ptr);
int findLink(char *label, externalType op);
uint32_t *createExternalLinks();
extern vect<_binding> binded_assets;

extern Text extern_text;
#endif