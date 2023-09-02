#include "ioqueue.h"
#include "interrupt.h"
#include "global.h"
#include "debug.h"

//初始话缓冲区
void init_ioqueue(struct ioqueue* ioq)
{
    lock_init(&ioq->lock);
    ioq->consumer = ioq->producer = NULL;
    ioq->head = ioq->tail = 0;
}

uint32_t next_pos(uint32_t pos)
{
    return (pos+1)%bufsize;
}
//队列是否满额
int ioq_full(struct ioqueue* ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);
    return next_pos(ioq->head) == ioq->tail;
}

//队列是否为空
int ioq_empty(struct ioqueue* ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);
    return ioq->head == ioq->tail;
}

//线程指针的地址，使得当前的线程睡眠（缓冲区的）
void ioq_wait(struct task_struct** waiter)
{
    ASSERT(waiter != NULL && *waiter == NULL);
    *waiter = running_thread();
    thread_block(TASK_BLOCKED);
}

//唤醒
void wakeup(struct task_struct** waiter)
{
    ASSERT(*waiter != NULL);
    thread_unblock(*waiter);
    *waiter = NULL;
}
//每次只操作一个字节，

//从队尾返回一个字节，取数据
char ioq_getchar(struct ioqueue* ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);
    
    //空的时候就需要睡眠 醒过来之后可能别的别的消费者已经把数据取走
    //所以还需要判断
    while(ioq_empty(ioq))
    {
        lock_acquire(&ioq->lock);
        ioq_wait(&ioq->consumer);
        lock_release(&ioq->lock);
    }
    
    char retchr = ioq->buf[ioq->tail];
    ioq->tail = next_pos(ioq->tail);
    
    //如果之前是满的，那么生产者就会堵塞住，所以判断生产者，如果有等待的，就唤醒，因为现在腾出一个空间了
    if(ioq->producer)
    	wakeup(&ioq->producer);
    
    return retchr;
}

void ioq_putchar(struct ioqueue* ioq,char chr)
{
    ASSERT(intr_get_status() == INTR_OFF);
    
    while(ioq_full(ioq))
    {
    	lock_acquire(&ioq->lock);
    	ioq_wait(&ioq->producer);
    	lock_release(&ioq->lock);
    }
    
    ioq->buf[ioq->head] = chr;
    ioq->head = next_pos(ioq->head);
    
    if(ioq->consumer)
    	wakeup(&ioq->consumer);
}
