#pragma once
#ifndef _PARSER_DEFINE_
#define _PARSER_DEFINE_

#define PARSER_LOG(...) {printf("[%s %s line:%d] ",__FILE__, __FUNCTION__,__LINE__); printf(__VA_ARGS__);printf("\n");}



#define __TEST_DEBUG

#endif