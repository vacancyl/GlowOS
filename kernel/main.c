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

void k_thread_a(void *arg);
void k_thread_b(void *arg);
void u_prog_a(void);
void u_prog_b(void);
int pid_a = 0, pid_b = 0;

int main(void)
{
	put_str("I am kernel\n");
	init_all();

	// process_execute(u_prog_a, "user_prog_a");
	// process_execute(u_prog_b, "user_prog_b");

	intr_enable(); // 开中断
	struct stat stat;
	sys_stat("/", &stat);
	printk("/'s info \n    i_no:%d\n    size:%d\n    filetype:%d\n", stat.st_ino, stat.st_size, stat.st_filetype);
	sys_stat("/dir1", &stat);
	printk("/dir1's info \n    i_no:%d\n    size:%d\n    filetype:%d\n", stat.st_ino, stat.st_size, stat.st_filetype);
	

	while (1)
		;
	return 0;
}

// 这段不能放前面，不然0xc0001500的位置不是主函数

void k_thread_a(void *arg)
{

	// 三页 还包括用户进程的页目录表
	void *addr1 = sys_malloc(256);
	void *addr2 = sys_malloc(255);
	void *addr3 = sys_malloc(254);
	console_put_str("   thread_a malloc addr:0x");
	console_put_int((int)addr1);
	console_put_char(',');
	console_put_int((int)addr2);
	console_put_char(',');
	console_put_int((int)addr3);
	console_put_char('\n');
	int cpu_delay = 100000;
	while (cpu_delay-- > 0)
		;
	sys_free(addr1);
	sys_free(addr2);
	sys_free(addr3);
	while (1)
		;
}

void k_thread_b(void *arg)
{
	void *addr1 = sys_malloc(256);
	void *addr2 = sys_malloc(255);
	void *addr3 = sys_malloc(254);
	console_put_str("   thread_b malloc addr:0x");
	console_put_int((int)addr1);
	console_put_char(',');
	console_put_int((int)addr2);
	console_put_char(',');
	console_put_int((int)addr3);
	console_put_char('\n');
	int cpu_delay = 100000;
	while (cpu_delay-- > 0)
		;
	sys_free(addr1);
	sys_free(addr2);
	sys_free(addr3);
	while (1)
		;
}

void u_prog_a(void)
{
	void *addr1 = malloc(256);
	void *addr2 = malloc(255);
	void *addr3 = malloc(254);
	printf(" prog_a malloc addr:0x%x, 0x%x, 0x%x\n", (int)addr1, (int)addr2, (int)addr3);
	int cpu_delay = 100000;
	while (cpu_delay-- > 0)
		;
	free(addr1);
	free(addr2);
	free(addr3);
	while (1)
		;
}

void u_prog_b(void)
{
	void *addr1 = malloc(256);
	void *addr2 = malloc(255);
	void *addr3 = malloc(254);
	printf(" prog_b malloc addr:0x%x, 0x%x, 0x%x\n", (int)addr1, (int)addr2, (int)addr3);
	int cpu_delay = 100000;
	while (cpu_delay-- > 0)
		;
	free(addr1);
	free(addr2);
	free(addr3);
	while (1)
		;
}
