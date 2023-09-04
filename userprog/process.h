#ifndef __USERPROG__PROCESS_H
#define __USERPROG__PROCESS_H


#include "tss.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "print.h"
#include "thread.h"
#include "interrupt.h"
#include "debug.h"
#include "console.h"
#include "memory.h"

#define USER_VADDR_START 0x8048000 //这是 Linux 用户程序入口地址，您可以用 readelf 命令查看一下，大部分可执行程序的“En point address ”都是在 Ox8048000 附近。


extern void intr_exit(void);

void start_process(void* filename_);
void page_dir_activate(struct task_struct* p_thread);
void process_activate(struct task_struct* p_thread);
uint32_t* create_page_dir(void);
void create_user_vaddr_bitmap(struct task_struct* user_prog);
void process_execute(void* filename,char* name);

#endif
