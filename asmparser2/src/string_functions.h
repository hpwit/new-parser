#pragma once

#ifndef __STRING_FUNC__
#define __STRING_FUNC__
#include "string.h"
#include "vect.h"
//vect<char *>str_split(char* a_str, char *delim);
void str_split( vect<char *> *result,char* a_str, char *delim);

template<typename ... Args>
char * string_format( const char *format, Args ... args )

{
	int size_s = snprintf( nullptr, 0, format, args ... ) + 1; // Extra space for '\0'
	if( size_s <= 0 ) {
		return (char *)"";
	}

	char * str=(char *)malloc(size_s);
	snprintf( str, size_s, format, args ... );
	return str;

}
template<typename ... Args>
char * str_concat( const char *format,char * _str, Args ... args )

{
   
    int size_s;
    if(_str==NULL)
    size_s = snprintf( nullptr, 0, format,"", args ... ) + 1; // Extra space for '\0'
    else
	 size_s = snprintf( nullptr, 0, format,_str, args ... ) + 1; // Extra space for '\0'
	if( size_s <= 0 ) {
		return (char *)"";
	}

	char * str=(char *)malloc(size_s);
    if(_str==NULL)
    snprintf( str, size_s, format, "",args ... );
   else
	snprintf( str, size_s, format, _str,args ... );
    if(_str!=NULL)
    free(_str);
	return str;

}
#endif