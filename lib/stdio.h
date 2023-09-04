#ifndef __LIB__STDIO_H
#define __LIB__STDIO_H
#include "stdint.h"
#include "global.h"

typedef void* va_list;
void itoa(uint32_t value,char** buf_ptr_addr,uint8_t base);
uint32_t vsprintf(char* str,const char* format,va_list ap);
uint32_t printf(const char* format, ...);

#endif
