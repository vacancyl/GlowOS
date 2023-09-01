#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "memory.h"
#include "timer.h"
#include "thread.h"
#include "console.h"


/*负责初始化所有模块 */
void init_all()
{
    put_str("init_all\n");
    idt_init(); // 初始化中断
    timer_init();//初始化时钟
    mem_init();//初始化内存管理
    thread_init();//线程初始化
    console_init();
}
