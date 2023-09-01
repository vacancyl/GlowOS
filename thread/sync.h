#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H
#include "list.h"
#include "stdint.h"
#include "thread.h"

struct semaphore
{
    uint8_t value;	    //信号量的值
    struct list waiters;   //等待队列 记录在此信号量上等待（阻塞）的所有线程
};

//是锁结构。谁成功申请了锁，就应该记录锁被谁持有
struct lock
{
    struct task_struct* holder;
    struct semaphore semaphore;
    uint32_t holder_repeat_nr;	//在释放的时候 通过这个值决定需要 释放锁几次
};
/*
累积锁的持有者重复申请锁的次数，释放锁的时候会参考此变量的值。原因是一般情况下我们应该在进入临界区之前加锁，
但有时候可能持有了某临界区的锁后，在未释放锁之前，有可能会再次调用重复申请此锁的函数，这样一来，内外层函数在释 
放锁时会对同一个锁释放两次，为了避免这种情况的发生，用此变量来累积重复申请的次数，释放锁时会 根据变量 holder repeat nr的值来执行具体动作。
*/

//揣测了一下 sema 前面加了个p 应该是ptr 指针的意思
void sema_init(struct semaphore* psema,uint32_t value);
void lock_init(struct lock* plock);
void sema_down(struct semaphore* psema);
void sema_up(struct semaphore* psema);
void lock_acquire(struct lock* plock);
void lock_release(struct lock* plock);
#endif
