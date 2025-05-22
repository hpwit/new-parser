#pragma once
#ifndef _PARSER_DEFINE_
#define _PARSER_DEFINE_

#define _START_2 32
#define _STACK_SIZE (_START_2 + 6 * 4)
#define _MAX_FOR_DEPTH_REG 4
#define _MAX_FOR_DEPTH_REG_2 2
#define _TRIGGER 20

#define PARSER_LOG(...) {printf("[%s %s line:%d] ",__FILE__, __FUNCTION__,__LINE__); printf(__VA_ARGS__);printf("\n");}
//#define PARSER_LOG(...)


#define __TEST_DEBUG
#define __MEM_PARSER

#define EOF_TEXT 0
#define EOF_TEXTARRAY 9999
#define EOF_VARTYPE 14
#endif