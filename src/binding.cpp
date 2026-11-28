#include <ctype.h>
#include "binding.h"
#include "string_functions.h"
#include "vect.h"
#include "stackfunctions.h"
#include "parser_define.h"
#include "string_constants.h"

Text extern_text;
vect<_binding> binded_assets;
vect<char *> binded_structs;

void bindStruct(const char *definition)
{
    // own copy, ending in a newline, put in front of each script text
    binded_structs.push_back(string_format("%s\n", definition));
}

// The script lays fields out as C does (each on a multiple of its size,
// these being 1, 2 or 4 bytes): the definition is bound only if that gives
// every field its C offset and the struct its C size, which a missing or
// reordered field would not.
bool bindStructFields(const char *name, int size, const scriptFieldInfo *fields, int count)
{
    int offset = 0, align = 1;
    for (int i = 0; i < count; i++)
    {
        int a = fields[i].size;
        offset = (offset + a - 1) / a * a;
        if (offset != fields[i].offset)
        {
            printf("BIND_STRUCT(%s): field %s is at byte %d in C but would be at byte %d in the script; "
                   "list every field, in order\n",
                   name, fields[i].name, fields[i].offset, offset);
            return false;
        }
        offset += a;
        if (a > align)
            align = a;
    }
    offset = (offset + align - 1) / align * align;
    if (offset != size)
    {
        printf("BIND_STRUCT(%s): %d bytes in C, %d from the fields listed; a field is missing\n", name, size, offset);
        return false;
    }
    char *definition = string_format("struct %s {", name);
    for (int i = 0; i < count; i++)
        definition = str_concat("%s %s %s;", definition, fields[i].type, fields[i].name);
    definition = str_concat("%s };", definition);
    bindStruct(definition);
    free(definition);
    return true;
}

// Resolves a symbol referenced in generated assembly (e.g. "@_rnd(d)" or
// just "rnd") to its binded_assets entry by comparing against the bare
// shortname -- anything from the first '(' onward is ignored, matching how
// call-site labels carry their signature suffix.
int findLink(char *label, externalType op)
{
    char *paren = strchr(label, '(');
    int len = paren ? (int)(paren - label) : (int)strlen(label);
    for (int i = 0; i < binded_assets.size(); i++)
    {
        char *shortname = extern_text.getText(binded_assets.get(i).shortname_ref);
        if ((int)strlen(shortname) == len && strncmp(shortname, label, len) == 0)
            return i;
    }
    return -1;
}

void replaceExternal(char *name, void *ptr)
{
    for (int i = 0; i < binded_assets.size(); i++)
    {
        if (strcmp(extern_text.getText(binded_assets.get(i).shortname_ref), name) == 0)
        {
            binded_assets.getptr(i)->ptr = ptr;
            return;
        }
    }
}

// A flat, indexable table of every bound function/variable's host pointer,
// in binded_assets order -- what the assembler's relocation step patches
// into the generated binary for each external reference. Pointer values
// only mean anything on the real target, so this is a no-op on host/test
// builds.
static uint32_t external_links_array[60];
uint32_t *createExternalLinks()
{
    for (int i = 0; i < binded_assets.size(); i++)
    {
#ifndef __TEST_DEBUG
        external_links_array[i] = (uint32_t)(uintptr_t)binded_assets.get(i).ptr;
#endif
    }
    return external_links_array;
}

// The name of the structure a bindFunction() parameter type names
// ("ball", "ball *", "struct ball*", "const ball *"), if that structure was
// bound with bindStruct(); NULL otherwise. Returns a static buffer.
static const char *boundStructName(const char *type)
{
    static char word[48];
    const char *p = type;
    for (;;)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        int n = 0;
        while ((isalnum((unsigned char)p[n]) || p[n] == '_') && n < (int)sizeof(word) - 1)
            n++;
        if (n == 0)
            return NULL;
        memcpy(word, p, n);
        word[n] = '\0';
        p += n;
        if (strcmp(word, "const") != 0 && strcmp(word, "struct") != 0)
            break;
    }
    const size_t len = strlen(word);
    for (int i = 0; i < binded_structs.size(); i++)
    {
        const char *d = binded_structs[i];
        while (*d == ' ' || *d == '\t' || *d == '\n')
            d++;
        if (strncmp(d, "struct", 6) != 0)
            continue;
        d += 6;
        while (*d == ' ' || *d == '\t' || *d == '\n')
            d++;
        if (strncmp(d, word, len) == 0 && !(isalnum((unsigned char)d[len]) || d[len] == '_'))
            return word;
    }
    return NULL;
}

// A bindFunction() parameter as its type alone: "int d" -> "int",
// "uint8_t *buf" -> "uint8_t *", " int " -> "int", so that the parameters
// may be named ("int x,int y") or not ("int,int"), or both. The last word
// goes only when something comes before it and it isn't a type itself
// ("unsigned int", "const ball", "Args" stay). Allocated: free() it.
static char *parameterType(const char *param)
{
    static const char *const typeWords[] = {
        "int", "char", "float", "ffloat", "bool", "void", "long", "short", "double", "unsigned",
        "signed", "s_int", "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t",
        "CRGB", "CRGBW", "Args", "const", "struct",
    };
    while (*param == ' ' || *param == '\t')
        param++;
    size_t len = strlen(param);
    while (len > 0 && (param[len - 1] == ' ' || param[len - 1] == '\t'))
        len--;
    size_t word = len;
    while (word > 0 && (isalnum((unsigned char)param[word - 1]) || param[word - 1] == '_'))
        word--;
    size_t before = word;
    while (before > 0 && (param[before - 1] == ' ' || param[before - 1] == '\t'))
        before--;
    bool strip = word < len && before > 0;
    if (strip)
    {
        char last[48];
        const size_t n = len - word < sizeof(last) - 1 ? len - word : sizeof(last) - 1;
        memcpy(last, param + word, n);
        last[n] = '\0';
        for (size_t k = 0; k < sizeof(typeWords) / sizeof(typeWords[0]); k++)
            if (strcmp(last, typeWords[k]) == 0)
                strip = false;
        if (strip && boundStructName(last) != NULL)
            strip = false;
    }
    if (strip)
        len = before;
    char *type = (char *)malloc(len + 1);
    memcpy(type, param, len);
    type[len] = '\0';
    return type;
}

void bindFunction(char * out,char * name,char *  in, void * ptr)
{
  
  _binding asmex;
   // asmex.name_ref=extern_text.addText( name);
   
   char * _name=string_format("external %s %s(",out,name);
    asmex.shortname_ref=extern_text.addText(string_format("%s",name));
   
    asmex.type=function;
    //asmex.in=in;
    //asmex.out=out;
    if(in==NULL)
    {
    asmex.sign_ref =extern_text.addText(string_format("%s%s",name,_opencloseparenthesis_));
    _name=str_concat("%s%s",_name,");");
  
    }
    else
    {
     
      char * signature=string_format(_s_s_,name,_openparenthesis_);

    vect<char *> j;
    str_split(&j,in,(char*)_comma_);
    // the parameters' names, if the binding gives them, go
    for (int i=0;i<j.size();i++)
    {
      char *type = parameterType(j[i]);
      free(j[i]);
      *j.getptr(i) = type;
    }
    for (int i=0;i<j.size();i++)
    {
     
      if(strstr(j[i],"Args")==NULL)
     //if(j[i].find("Args")==string::npos)tu vas penser
      {
       // Base type marker matches _varTypes[]'s varName strings
       // (tokenize.cpp) so a signature built here from a bindFunction()
       // call matches one built from parsing an explicit `external ...;`
       // declaration for the same C type -- both need to agree for
       // findLabel()/findCandidate() to resolve a call site against its
       // declaration. Numeric types all collapse to "num" (Xtensa passes
       // them in the same 32-bit register regardless of int vs float),
       // matching upstream ESPLiveScript's own convention.
       if(strstr(j[i],"int")!=NULL or strstr(j[i],"float")!=NULL or strstr(j[i],"bool")!=NULL)
        signature=str_concat(_s_s_,signature,"num");
       else if(strstr(j[i],"CRGBW")!=NULL)
        signature=str_concat(_s_s_,signature,"CRGBW");
       else if(strstr(j[i],"CRGB")!=NULL)
        signature=str_concat(_s_s_,signature,"CRGB");
       else if(strstr(j[i],"char")!=NULL)
        signature=str_concat(_s_s_,signature,"char");
       else if(boundStructName(j[i]) != NULL)
        // a structure bound with bindStruct(): the parser names it in
        // signatures by its own name, by value ("ball") or pointer ("ball*").
        // Either way the function receives the structure's address.
        signature=str_concat(_s_s_,signature,boundStructName(j[i]));
       else
        signature=str_concat(_s_s_,signature,"num");
      }
      else
       //asmex.signature= asmex.signature+"Args";
       signature=str_concat("%s%s",signature,"Args");

      // if(j[i].find("*")!=string::npos)
      if(strstr(j[i],"*")!=NULL)
       // asmex.signature= asmex.signature+"*";
       signature=str_concat(_s_s_,signature,"*");
     //asmex.name=string_format("%s%s a%d",asmex.name.c_str(),j[i].c_str(),i);
      _name=str_concat("%s%s a%d",_name,j[i],i);
       if (i<j.size()-1)
       {
        // asmex.signature= asmex.signature+"|";
         signature=str_concat(_s_s_,signature,"|");
        //  asmex.name=asmex.name+",";
          _name=str_concat(_s_s_,_name,",");
       }
    }
    j.empty();

   // asmex.signature= asmex.signature+")";
   // asmex.name=asmex.name+");";

    signature=str_concat(_s_s_,signature,_closeparenthesis_);
   
      _name=str_concat(_s_s_s_,_name,_closeparenthesis_,";");
    
      asmex.sign_ref=extern_text.addText(signature);
      
    }
    asmex.name_ref=extern_text.addText(_name);
    PARSER_LOG("%s %s \n\r",extern_text.getText( asmex.sign_ref), extern_text.getText( asmex.name_ref))
    if(ptr!=NULL)
         asmex.ptr=ptr;
    binded_assets.push_back(asmex);
         
}

void bindVariable( char *out,char *name,char *in,void * ptr)
{
  _binding asmex;
  asmex.name_ref=extern_text.addText(string_format("%s",name));
  asmex.shortname_ref=extern_text.addText(string_format("%s",name));
  if(in!=NULL)
    asmex.sign_ref=extern_text.addText(string_format("external %s %s%s;",out,name,in));
  else
  asmex.sign_ref=extern_text.addText(string_format("external %s %s;",out,name));
    asmex.type=value;
    if(ptr!=NULL)
         asmex.ptr=ptr;
         binded_assets.push_back(asmex);
}

