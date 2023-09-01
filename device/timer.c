#include "io.h"
#include "print.h"
#include "interrupt.h"
#include "thread.h"
#include "debug.h"

#define IRQ0_FREQUENCY 	    100                                 //中断频率
#define INPUT_FREQUENCY     1193180
#define COUNTER0_VALUE		INPUT_FREQUENCY / IRQ0_FREQUENCY
#define COUNTER0_PORT		0X40                                //计数器端口
#define COUNTER0_NO 		0                                   //使用的计数器的序号
#define COUNTER_MODE		2                                   //计数器工作方式
#define READ_WRITE_LATCH	3                                   //读写方式
#define PIT_COUNTROL_PORT	0x43

uint32_t ticks;//内核自从中断开始的所有的滴答数

void frequency_set(uint8_t counter_port ,uint8_t counter_no,uint8_t rwl,uint8_t counter_mode,uint16_t counter_value);
void timer_init(void);
void intr_timer_handler(void);

void frequency_set(uint8_t counter_port ,uint8_t counter_no,uint8_t rwl,uint8_t counter_mode,uint16_t counter_value)
{
    outb(PIT_COUNTROL_PORT,(uint8_t) (counter_no << 6 | rwl << 4 | counter_mode << 1));
    outb(counter_port,(uint8_t)counter_value);
    outb(counter_port,(uint8_t)counter_value >> 8);
    return;
} 

void intr_timer_handler(void)
{
    struct task_struct* cur_thread = running_thread();   //得到pcb指针
    ASSERT(cur_thread->stack_magic == 0x19870916);	       //检测栈是否溢出
    
    //先检测的是主线程的时间片
    ++ticks;
    ++cur_thread->elapsed_ticks;
    if(0 == cur_thread->ticks)//若进程时间片用完，就开始调度新的进程上 cpu
    	schedule();
    else    
    	--cur_thread->ticks;
    return;
}

void timer_init(void)
{
    put_str("timer_init start!\n");
    frequency_set(COUNTER0_PORT,COUNTER0_NO,READ_WRITE_LATCH,COUNTER_MODE,COUNTER0_VALUE);
    register_handler(0x20,intr_timer_handler);	        //注册时间中断函数 0x20向量号函数更换
    put_str("timer_init done!\n");
    return;
}
