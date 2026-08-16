#include "binding.h"
#include "string_functions.h"
#include "vect.h"
#include "stackfunctions.h"
#include "parser_define.h"
#include "string_constants.h"

Text extern_text;
vect<_binding> binded_assets;

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

