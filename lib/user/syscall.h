#ifndef __LIB_USER_SCSCALL_H
#define __LIB_USER_SCSCALL_H
#include "stdint.h"

enum SYSCALL_NR
{
    SYS_GETPID,
    SYS_WRITE,
    SYS_MALLOC,
    SYS_FREE
};

uint32_t getpid(void);
void write(uint32_t fd,const void* buf,uint32_t count);
void* malloc(uint32_t);
void free(void* ptr);
#endif
