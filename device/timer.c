#include "io.h"
#include "print.h"
#include "../kernel/interrupt.h"


#define IRQ0_FREQUENCY 	    100                                 //中断频率
#define INPUT_FREQUENCY     1193180
#define COUNTER0_VALUE		INPUT_FREQUENCY / IRQ0_FREQUENCY
#define COUNTER0_PORT		0X40                                //计数器端口
#define COUNTER0_NO 		0                                   //使用的计数器的序号
#define COUNTER_MODE		2                                   //计数器工作方式
#define READ_WRITE_LATCH	3                                   //读写方式
#define PIT_COUNTROL_PORT	0x43

void frequency_set(uint8_t counter_port ,uint8_t counter_no,uint8_t rwl,uint8_t counter_mode,uint16_t counter_value);
void timer_init(void);

void frequency_set(uint8_t counter_port ,uint8_t counter_no,uint8_t rwl,uint8_t counter_mode,uint16_t counter_value)
{
    outb(PIT_COUNTROL_PORT,(uint8_t) (counter_no << 6 | rwl << 4 | counter_mode << 1));
    outb(counter_port,(uint8_t)counter_value);
    outb(counter_port,(uint8_t)counter_value >> 8);
    return;
} 

void timer_init(void)
{
    put_str("timer_init start!\n");
    frequency_set(COUNTER0_PORT,COUNTER0_NO,READ_WRITE_LATCH,COUNTER_MODE,COUNTER0_VALUE);
    put_str("timer_init done!\n");
    return;
}
