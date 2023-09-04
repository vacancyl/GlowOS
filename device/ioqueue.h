#ifndef __DEVICE__IOQUEUE_H
#define __DEVICE__IOQUEUE_H

#include "stdint.h"
#include "thread.h"
#include "sync.h"
#include "global.h"

#define bufsize 64


struct ioqueue
{
    struct lock lock;//缓冲区的锁，操作缓冲区需要申请
    struct task_struct* consumer;//消费者，缓冲区空时的消费者线程
    struct task_struct* producer;//生产者，缓冲区满时的生产者线程    都睡眠
    char buf[bufsize];
    uint32_t head;			//头部读入数据
    uint32_t tail;			//尾部拿数据
};

void init_ioqueue(struct ioqueue* ioq);
uint32_t next_pos(uint32_t pos);
int ioq_full(struct ioqueue* ioq);
int ioq_empty(struct ioqueue* ioq);
void ioq_wait(struct task_struct** waiter); //这里是waiter的二级指针 取二级指针的原因是这样可以对指针的地址值进行修改
void wakeup(struct task_struct** waiter); 
char ioq_getchar(struct ioqueue* ioq);
void ioq_putchar(struct ioqueue* ioq,char chr);

#endif
