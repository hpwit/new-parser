#include "binding.h"
#include "string_functions.h"
#include "vect.h"
#include "stackfunctions.h"
#include "parser_define.h"
#include "string_constants.h"

Text extern_text;
vect<_binding> binded_assets;

void bindFunction(char * out,char * name,char *  in, void * ptr)
{
  
  _binding asmex;
   // asmex.name_ref=extern_text.addText( name);
    asmex.shortname_ref=extern_text.addText(name);
    asmex.type=function;
    //asmex.in=in;
    //asmex.out=out;
    if(out==NULL)
    {
    asmex.sign_ref =extern_text.addText("()");
    }
    else
    {
      char * _name=string_format("external %s %s(",out,name);
      char * signature=string_format(_s_s_,name,_openparenthesis_);

    vect<char *> j;
    str_split(&j,in,",");
    for (int i=0;i<j.size();i++)
    {
     
      if(strstr(j[i],"Args")==NULL)
     //if(j[i].find("Args")==string::npos)
      // asmex.signature= asmex.signature+"d";
       signature=str_concat(_s_s_,signature,"d");
      else
       //asmex.signature= asmex.signature+"Args";
       signature=str_concat("%s%s",signature,"Args");

      // if(j[i].find("*")!=string::npos)
      if(strstr(j[i],"*")==NULL)
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
   
      _name=str_concat(_s_s_,_name,_closeparenthesis_);
  //  printf("%s %s \n\r",asmex.signature.c_str(),asmex.name.c_str());
    }
    if(ptr!=NULL)
         asmex.ptr=ptr;
    binded_assets.push_back(asmex);
         
}