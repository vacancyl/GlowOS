#ifndef __DEVICE_KEYBOARD_H
#define __DEVICE_KEYBOARD_H

void intr_keyboard_handler(void);
void keyboard_init(void);
extern struct ioqueue kb_buf;
#endif
