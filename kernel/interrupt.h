#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H
#include "stdint.h"
typedef void *intr_handler;
void idt_init(void);


enum intr_status//中断状态
{
    INTR_ON,    //中断开
    INTR_OFF    //中断关
};

enum intr_status intr_enable(void);
enum intr_status intr_disable(void);
enum intr_status intr_set_status(enum intr_status status);
enum intr_status intr_get_status(void);

#endif
