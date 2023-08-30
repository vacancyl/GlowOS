#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H

void panic_spin(char* filename,int line,const char* func,const char* condition);

//__VA_ARGS__是预处理所支持的专用标识符，代表所有与省略号对应的参数，...表示定义的宏其参数可变
//它只允许在具有可变参数的宏替换列表中出现，它代表所有与省略号“．．．”相对应的参数。该参数至少有一个，但可以为空。
#define PANIC(...) panic_spin (__FILE__,__LINE__,__func__,__VA_ARGS__)

#ifdef NDEBUG 
#define ASSERT(CONDITION) ((void)0)
#else
#define ASSERT(CONDITION) \
   if(CONDITION){}        \
   else{ PANIC(#CONDITION); }//#让编译器将宏的参数转换为字符串字面量

#endif


#endif

