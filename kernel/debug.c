#include "debug.h"
#include "print.h"
#include "interrupt.h"

void panic_spin(char* filename,int line,const char* func,const char* condition)
{
    intr_disable();                 //中断关闭防止中断后cpu处理其他进程被调换 编译外部程序的时候可以先注释再取消
    put_str("\n\n\n\\**********ERROR\\**********\\\n");
    put_str("Filename: ");put_str(filename);put_char('\n');
    put_str("Line: "); put_int(line); put_char('\n');
    put_str("Func: ");put_str((char*)func);put_char('\n');
    put_str("Condition: ");put_str((char*)condition);put_char('\n');
    put_str("\\**********ERROR\\**********\\\n");
    while(1);
}
