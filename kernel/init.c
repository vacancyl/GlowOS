#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "memory.h"
#include "timer.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "tss.h"
#include "syscall-init.h"
#include "ide.h"
#include "fs.h"

/*负责初始化所有模块 */
void init_all()
{
    put_str("init_all\n");
    idt_init(); // 初始化中断
    timer_init();//初始化时钟
    mem_init();//初始化内存管理
    thread_init();//线程初始化
    console_init();//控制台输出初始化
    keyboard_init();//键盘初始化
    tss_init();//用户进程初始化
    syscall_init();//系统调用初始化
    ide_init();//硬盘初始化
    filesys_init();//文件系统初始化
}
