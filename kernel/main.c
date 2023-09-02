#include "print.h"
#include "init.h"
#include "debug.h"
#include "string.h"
#include "memory.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "keyboard.h"

void k_thread_a(void* arg);
void k_thread_b(void* arg);


int main(void) 
{
   put_str("I am kernel\n");
   init_all();
   thread_start("kernel_thread_a",31,k_thread_a,"argA_");
   thread_start("kernel_thread_b",31,k_thread_b,"argB_");

   intr_enable();//开中断
   while(1);
   return 0;
}

//这段不能放前面，不然0xc0001500的位置不是主函数

void k_thread_a(void* arg)
{
   while(1)
   {
      enum intr_status old_status = intr_disable();
      while(!ioq_empty(&kb_buf))
      {
   	   console_put_str((char*)arg);
    	   char chr = ioq_getchar(&kb_buf);
   	   console_put_char(chr);
	   }
   	intr_set_status(old_status);
   }

}

void k_thread_b(void* arg)
{
   while(1)
   {
      enum intr_status old_status = intr_disable();
      while(!ioq_empty(&kb_buf))
      {
   	   console_put_str((char*)arg);
    	   char chr = ioq_getchar(&kb_buf);
   	   console_put_char(chr);
	   }
   	intr_set_status(old_status);
   }

}