#include "print.h"
#include "init.h"
#include "debug.h"
#include "string.h"
#include "memory.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "keyboard.h"
#include "process.h"
#include "syscall.h"
#include "syscall-init.h"
#include "stdio.h"
#include "stdio-kernel.h"
#include "file.h"
#include "fs.h"
#include "shell.h"

int main(void)
{
	put_str("I am kernel\n");
	init_all();
	intr_enable(); // 开中断

	/*************    写入应用程序    *************/
	uint32_t file_size = 15340;
	uint32_t sec_cnt = DIV_ROUND_UP(file_size, 512);//30
	struct disk *sda = &channels[0].devices[0];
	void *prog_buf = sys_malloc(file_size);
	ide_read(sda, 300, prog_buf, sec_cnt);
	int32_t fd = sys_open("/prog_no_arg", O_CREAT | O_RDWR);
	if (fd != -1)
	{
		if (sys_write(fd, prog_buf, file_size) == -1)
		{
			printk("file write error!\n");
			while (1)
				;
		}
	}
	/*************    写入应用程序结束   *************/
	cls_screen();
	console_put_str("[vacancy@localhost /]$ ");

	while (1)
		;
	return 0;
}

// 这段不能放前面，不然0xc0001500的位置不是主函数

void init(void)
{
	uint32_t ret_pid = fork();
	if (ret_pid)
		while (1)
			;
	else
		my_shell();
	PANIC("init: should not be here");
}
