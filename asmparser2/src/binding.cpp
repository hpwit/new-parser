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
      // asmex.signature= asmex.signature+"d";
       signature=str_concat(_s_s_,signature,"d");
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
    printf("%s %s \n\r",extern_text.getText( asmex.sign_ref), extern_text.getText( asmex.name_ref));
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

