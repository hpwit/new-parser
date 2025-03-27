#pragma once
#ifndef _PARSER_DEFINE_
#define _PARSER_DEFINE_

#define PARSER_LOG(...) {printf("[%s %s line:%d] ",__FILE__, __FUNCTION__,__LINE__); printf(__VA_ARGS__);printf("\n");}
//#define PARSER_LOG(...)


#define __TEST_DEBUG


#define EOF_TEXT 0
#define EOF_TEXTARRAY 9999
#define EOF_VARTYPE 14
#endif