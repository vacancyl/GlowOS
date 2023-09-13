#ifndef __LIB__STDIO_H
#define __LIB__STDIO_H
#include "stdint.h"


typedef void* va_list;

//ap指向第一个固定参数v ap和v都是char*,这里强制转换
#define va_start(ap,v) ap = (va_list)&v         

//ap指向下一个参数并返回其值
#define va_arg(ap,t)   *((t*)(ap +=4))	        

//清除ap
#define va_end(ap)	   ap = ((void*)0)                


void itoa(uint32_t value,char** buf_ptr_addr,uint8_t base);
uint32_t vsprintf(char* str,const char* format,va_list ap);
uint32_t printf(const char* format, ...);

#endif
