#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"
#include "list.h"
#include "memory.h"

/*自定义通用函数类型，它将在很多线程函数中作为形参类型*/
typedef void thread_func(void*);
#define PG_SIZE 4096

typedef int16_t pid_t;

#define MAX_FILES_OPEN_PER_PROC 8

/*进程的状态*/                  
enum task_status
{
    TASK_RUNNING, // 0
    TASK_READY,   // 1
    TASK_BLOCKED, // 2
    TASK_WAITING, // 3
    TASK_HANGING, // 4
    TASK_DIED     // 5
};

/*
中 断 栈intr stack
*此结构用于中断发生时保护程序(线程或进程)的上下文环境：
*进程或线程被外部中断或软中断打断时，会按照此结构压入上下文
*寄 存 器 ，intr exit  中的出栈操作是此结构的逆操作
*此栈在线程自己的内核栈中位置固定，所在页的最顶端
*在kernel.S 中的中断入口程序 “intr%lentry”所执行的上下 文保护的一系列压栈操作都是压入了此结构中

初始情况下此栈在线程自己的内核栈中位置固定，在 PCB  所在页的最顶端，
每次进入中断时就不一定了， 如果进入中断时不涉及到特权级变化，它的位置就会在当前的esp 之下，
否则处理器会从 TSS 中获得新的 esp的值，然后该栈在新的esp 之下
*/
struct intr_struct
{
    uint32_t vec_no; //中断号
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;//／虽然 pus had esp 也压入，但 esp 是不断变化的，所以会被 pop ad 忽略
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    
    /* 以下由 cpu 从低特权级进入高特权级时压入*/
    uint32_t err_code;
    void (*eip) (void);        //这里声明了一个函数指针 
    uint32_t cs;
    uint32_t eflags;
    void* esp;
    uint32_t ss;
};


/***********线程栈thread stack  ***********
线程自己的栈，用于存储线程中待执行的函数
此结构在线程自己的内核栈中位置不固定，
仅用在 switch to 时保存线程环境。
实际位置取决于实际运行情况。
*/
/*
位于Intel386硬件体系上的所有寄存器都具有全局性，因此在函数调用时，这些寄存器对主 调函数和被调函数都可见。
这5个寄存器 ebp、ebx、edi、esi、 和 esp 归主调函数所用，其余的寄存器归被调 函数所用。换句话说，
不管被调函数中是否使用了这5个寄存器，在被调函数执行完后，这5个寄存器的值不 该被改变。因此被调函数必须为主调函数保护好这5个寄存器的值，
在被调函数运行完之后，这5个寄存器的 值必须和运行前一样，它必须在自己的栈中存储这些寄存器的值

C 语言和汇编语言是用不同的编译器来编译的， C 语言代码 要先被编译为汇编代码，此汇编代码便是按照 ABI 规则生成的，因此，如果要自己手动写汇编函数，
并且 此函数要供C 语言调用的话，咱们也得按照ABI 的规则去写汇编才行。说到这您肯定明白了，咱们一定是 用汇编语言写了个函数，而且是用C 程序来调用这个汇编函数

sp 的值会由调用约定来保证，因此我们不打算保护 esp 的值。在我们的实现中，由被调函数保存除 esp外的4个寄存器，
这就是线程栈 thread stack 前4个成员的作用，我们将来用 switch to 函数切换时， 先在线程栈thread stack 中压入这4个寄存器的值
*/
struct thread_stack//难道这里是按照顺序往下的
{
    //ABI
    //这里只是说明在这里我需要这么大的空间，可读性，
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;

    //线程第一次执行，eip指向待调用的函数kernel_thread  其他时候，eip指向的是switch_to的返回地址
    void (*eip) (thread_func* func,void* func_arg); //和下面的相互照应 以ret 汇编代码进入kernel_thread函数调用

    //第一次被调度上CPU使用                             结构体在栈的低地址向高地址扩展
    void (*unused_retaddr);                         //占位数 在栈顶站住了返回地址的位置 因为是汇编ret 
    thread_func* function;                          //进入kernel_thread要调用的函数地址
    void* func_arg;				                    //参数指针
};

//进程或者线程的PCB 程序控制块
struct task_struct
{
    uint32_t* self_kstack;                          //pcb中的 kernel_stack 内核栈
    pid_t pid;                          //线程号
    enum task_status status;                        //线程状态
    uint8_t priority;				      //特权级
    char name[16];                        //线程的名字

    uint8_t ticks;				      //在cpu 运行的滴答数 看ticks 来判断是否用完了时间片
    uint32_t elapsed_ticks;                         //一共执行了多久

    int32_t fd_table[MAX_FILES_OPEN_PER_PROC];//文件描述符数组
    
    struct list_elem general_tag;                   //就绪队列中的连接节点
    struct list_elem all_list_tag;		      //全部线程队列的连接节点
    
    uint32_t* pgdir;				      //进程自己页表的虚拟地址 线程没有          
    struct virtual_addr userprog_vaddr;    //用户进程块的虚拟地址块
    struct mem_block_desc u_block_desc[DESC_CNT];   //内存块描述符
    uint32_t cwd_inode_nr;                  //进程所在工作目录的inode编号
    uint32_t stack_magic;			      //越界检查  因为我们pcb上面的就是我们要用的栈了 到时候还要越界检查
};

extern struct list thread_ready_list,thread_all_list;


pid_t allocate_pid(void);
void kernel_thread(thread_func* function,void* func_arg);
void thread_create(struct task_struct* pthread,thread_func function,void* func_arg);
void init_thread(struct task_struct* pthread,char* name,int prio);
struct task_struct* thread_start(char* name,int prio,thread_func function,void* func_arg);

struct task_struct* running_thread(void);
void schedule(void);
void thread_init(void);
void make_main_thread(void);
void schedule(void);
void thread_init(void);
void thread_unblock(struct task_struct* pthread);
void thread_block(enum task_status stat);
void idle(void);
void thread_yield(void);
#endif
