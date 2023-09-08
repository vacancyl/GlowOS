#ifndef __DEVICE_TIME_H
#define __DEVICE_TIME_H
#include "stdint.h"
void timer_init(void);
void mtime_sleep(uint32_t m_seconds);
void ticks_to_sleep(uint32_t sleep_ticks);
#endif
