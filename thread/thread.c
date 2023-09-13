#include "thread.h" //函数声明 各种结构体
#include "stdint.h" //前缀
#include "string.h" //memset
#include "global.h" //不清楚
#include "memory.h" //分配页需要
#include "debug.h"
#include "interrupt.h"
#include "print.h"
#include "process.h"
#include "sync.h"
#include "file.h"


struct task_struct* main_thread;                        //主线程main_thread的pcb
struct task_struct* idle_thread;			  //休眠线程 空线程
struct list thread_ready_list;			  //就绪队列
struct list thread_all_list;				  //总线程队列
static struct list_elem* thread_tag;    //保存队列中的线程结点 将tag转换成list_elem

extern void switch_to(struct task_struct* cur,struct task_struct* next);

struct lock pid_lock;

void init(void);

pid_t allocate_pid(void)
{
    static pid_t next_pid = 0;			  
    lock_acquire(&pid_lock);
    next_pid++;
    lock_release(&pid_lock);
    return next_pid;
}

/*返回PCB地址此取 当前栈指针的高 20 位作为当前运行线程的 PCB 4KB*/
struct task_struct* running_thread(void)
{
    uint32_t esp;
    asm ("mov %%esp,%0" : "=g"(esp));
    return (struct task_struct*)(esp & 0xfffff000);
}



/*由kernel thread去执行 function(func arg)*/
void kernel_thread(thread_func *function, void *func_arg)
{
    intr_enable();					    //开中断 防止后面的时间中断被屏蔽无法切换线程
    function(func_arg);
}

/*初始化线程栈thread  stack, 将待执行的函数和参数放到thread stack中相应的位置*/
void thread_create(struct task_struct *pthread, thread_func function, void *func_arg)
{
    pthread->self_kstack -= sizeof(struct intr_struct); // 减去中断栈的空间 预留
    pthread->self_kstack -= sizeof(struct thread_stack);
    struct thread_stack *kthread_stack = (struct thread_stack *)pthread->self_kstack; // 地址目前为高地址 mov esp,[eax]做准备
    kthread_stack->eip = kernel_thread;                                               // 地址为kernel_thread 由kernel_thread 执行function
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = kthread_stack->ebx = kthread_stack->ebx = kthread_stack->esi = 0; // 初始化一下
    return;
}

void init_thread(struct task_struct *pthread, char *name, int prio)
{
    memset(pthread, 0, sizeof(*pthread)); // pcb位置清0
    
    pthread->pid = allocate_pid();
    strcpy(pthread->name, name);

    if(pthread == main_thread)
    	pthread->status = TASK_RUNNING;                              //我们的主线程肯定是在运行的
    else
    	pthread->status = TASK_READY;					//放到就绪队列里面

    pthread->priority = prio;
    pthread->ticks = prio;
    pthread->elapsed_ticks = 0;
    pthread->pgdir = NULL;
    pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE); // 刚开始的位置是最低位置 栈顶位置+一页 高地址向低地址扩展的
    pthread->stack_magic = 0x19870916;                                // 设置的魔数 检测是否越界限
    pthread->cwd_inode_nr = 0;                                        //默认根目录
    pthread->parent_pid = -1;                                           //-1表示没有父进程

    //预留标准输出输入出错
    pthread->fd_table[0] = 0;
    pthread->fd_table[1] = 1;
    pthread->fd_table[2] = 2;

    //其余的全部设置为-1
    uint8_t fd_idx = 3;
    while(fd_idx < MAX_FILES_OPEN_PER_PROC)
    {
        pthread->fd_table[fd_idx] = -1;
        fd_idx++;
    }
}
/*
struct list elem 类型中只有两个指针
成员： struct list_ elem* prev 和由uct list_ elem* next ，因此它作为结点的话，结点尺寸就8字节，整个队列
显得轻量小巧，如果换成 PCB 做结点，尺寸也太大了。
*/
struct task_struct *thread_start(char *name, int prio, thread_func function, void *func_arg)
{
    struct task_struct *thread = get_kernel_pages(1);
    init_thread(thread, name, prio);
    thread_create(thread, function, func_arg);

    ASSERT(!elem_find(&thread_ready_list,&thread->general_tag));     //之前不应该在就绪队列里面
    list_append(&thread_ready_list,&thread->general_tag);
    ASSERT(!elem_find(&thread_all_list,&thread->all_list_tag))
    list_append(&thread_all_list,&thread->all_list_tag);

    return thread;
}
//对于主线程，初始的时候esp位于 0xc009f000, 然后执行，esp必定再改变，所以这时候取到的是 0xc009e000 ，此时会进行线程的初始化，
//self_kstack本来是在 0xc009e000，然后会去到0xc009f000，此时如果发生中断，那么就会按照PCB中定义的顺序，中断栈入栈，然后时间片结束
//的时候会调用调度函数，此时线程开始调度，获取到PCB还是0xc009e000，然后获取到下一个的PCB，下一个PCB是已经被初始化好的，每个线程都有其栈指针，此时的主线程
//就会压入对应的四个寄存器，但是没有函数，因为没有经过create这一步，
//主线程的初始化
void make_main_thread(void)
{		
    /*pcb 地址为 Oxc009e000*/
    main_thread = running_thread();					//得到main_thread 的pcb指针
    init_thread(main_thread,"main",31);				
    
    ASSERT(!elem_find(&thread_all_list,&main_thread->all_list_tag));
    list_append(&thread_all_list,&main_thread->all_list_tag);
    
}
/*
在函数体中增加了开中断的函数 intr enable, 它是任务调度的保证。原因是我们的任务调度机制基于时钟中断，由时钟中断这种“不可抗力”来中断所有任务 的执行，
借此将控制权交到内核手中，由内核的任务调度器 schedule (后面有小节专门论述 schedule)
考虑将处理器使用权发放到某个任务的手中，下次中断再发生时，权利将再被回收，周而复始，这样便保证 操作系统不会被“架空”,而且保证所有任务都有运行的机会

线程的首次运行是由时钟中断处理函数调用任务调度器 schedule完成的，进入中断后处理器会自动关 中断，因此在执行 function前要打开中断，
否则 kernel thread 中的 function在关中断的情况下运行，也就 是时钟中断被屏蔽了，再也不会调度到新的线程， function 会独享处理器
*/
void schedule(void)
{
    ASSERT(intr_get_status() == INTR_OFF);
    
    struct task_struct* cur = running_thread();			//得到当前pcb的地址
    if(cur->status == TASK_RUNNING)
    {
    	ASSERT(!elem_find(&thread_ready_list,&cur->general_tag));     //目前在运行的肯定ready_list是不在的
    	list_append(&thread_ready_list,&cur->general_tag);           //加入尾部 标记而已
    	
    	cur->status = TASK_READY;
    	cur->ticks = cur->priority;
    }
    else
    {
        /*若此线程需要某事件发生后才能继续上 cpu 运行，
不需要将其加入队列，因为当前线程不在就绪队列中*/
    }
    
    //没有任务运行，就唤醒
    if(list_empty(&thread_ready_list))
    	thread_unblock(idle_thread);

    ASSERT(!list_empty(&thread_ready_list));
    //从就绪队列弹出
    struct task_struct* thread_tag = list_pop(&thread_ready_list);
    
    struct task_struct* next = elem2entry(struct task_struct,general_tag,thread_tag);
    next->status = TASK_RUNNING;
    process_activate(next);

    switch_to(cur,next);                                              //esp头顶的是 返回地址 +12是next +8是cur
}

void thread_init(void)
{
    put_str("thread_init start!\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    lock_init(&pid_lock);   

    process_execute(init,"init");


    make_main_thread();

    //创建idle线程
    idle_thread = thread_start("idle",10,idle,NULL);

    put_str("thread_init done!\n");
}

//系统空闲时候运行的线程
void idle(void)
{
    while(1)
    {
    	thread_block(TASK_BLOCKED);	//先阻塞后 被唤醒之后即通过命令hlt 使cpu挂起 直到外部中断cpu恢复
        //hlt需要开中断
    	asm volatile ("sti;hlt" : : :"memory");
    }
}

//让出CPU换其他的线程运行
void thread_yield(void)
{
    struct task_struct* cur = running_thread();
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list,&cur->general_tag));
    list_append(&thread_ready_list,&cur->general_tag);	//放到就绪队列末尾
    cur->status = TASK_READY;					//状态设置为READY 可被调度
    schedule();						
    intr_set_status(old_status);
}




/************************************阻塞和唤醒*******************************************/
void thread_block(enum task_status stat)
{
    //设置block状态的参数必须是下面三个以下的，才不会被调度
    ASSERT(((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || stat == TASK_HANGING));
    
    enum intr_status old_status = intr_disable();			 //关中断
    struct task_struct* cur_thread = running_thread();		 
    cur_thread->status = stat;					 //把状态重新设置，这样就不会加入到就绪队列中
    
    //调度器切换其他进程了 而且由于status不是running 不会再被放到就绪队列中
    schedule();	
    				
    //被切换回来之后再进行的指令了
    intr_set_status(old_status);
}

//由锁拥有者来执行的 善良者把原来自我阻塞的线程重新放到队列中
void thread_unblock(struct task_struct* pthread)
{
    enum intr_status old_status = intr_disable();
    ASSERT(((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING)));
    if(pthread->status != TASK_READY)
    {
    	//被阻塞线程 不应该存在于就绪队列中
    	ASSERT(!elem_find(&thread_ready_list,&pthread->general_tag));
    	if(elem_find(&thread_ready_list,&pthread->general_tag))
    	    PANIC("thread_unblock: blocked thread in ready_list\n"); //debug.h中定义过
    	
    	//让阻塞了很久的任务放在就绪队列最前面
    	list_push(&thread_ready_list,&pthread->general_tag);
    	
    	//状态改为就绪态
    	pthread->status = TASK_READY;
    }
    intr_set_status(old_status);
}

pid_t fork_pid(void)
{
    return allocate_pid();
}

//以填充空格的方式输出Buf 不足Buf的用空格输出
void pad_print(char* buf,int32_t buf_len,void* ptr,char format)
{
    memset(buf,0,buf_len);
    uint8_t out_pad_0idx = 0;
    switch(format)
    {
        case 's':
            out_pad_0idx = sprintf(buf,"%s",ptr);
            break;
        case 'd':
            out_pad_0idx = sprintf(buf,"%d",*((int16_t*)ptr));
            break;
        case 'x':
            out_pad_0idx = sprintf(buf,"%x",*((uint32_t*)ptr));   
    }
    while(out_pad_0idx < buf_len)
    {
        buf[out_pad_0idx] = ' ';
        out_pad_0idx++;
    }
    sys_write(stdout_no,buf,buf_len-1);
}

//打印任务信息
bool elem2thread_info(struct list_elem* pelem,int arg)
{
    struct task_struct* pthread = elem2entry(struct task_struct,all_list_tag,pelem);
    char out_pad[16] = {0};
    pad_print(out_pad,16,&pthread->pid,'d');
    
    if(pthread->parent_pid == -1)
    	pad_print(out_pad,16,"NULL",'s');
    else
        pad_print(out_pad,16,&pthread->parent_pid,'d');
        
    switch(pthread->status)
    {
        case 0:
            pad_print(out_pad,16,"RUNNING",'s');
            break;
        case 1:
            pad_print(out_pad,16,"READY",'s');
            break;
        case 2:
            pad_print(out_pad,16,"BLOCKED",'s');
            break;
        case 3:
            pad_print(out_pad,16,"WAITING",'s');
            break;
        case 4:
            pad_print(out_pad,16,"HANGING",'s');
            break;
        case 5:
            pad_print(out_pad,16,"DIED",'s');
            break;
    }
    pad_print(out_pad,16,&pthread->elapsed_ticks,'x');
    
    memset(out_pad,0,16);
    ASSERT(strlen(pthread->name) < 17);
    memcpy(out_pad,pthread->name,strlen(pthread->name));
    strcat(out_pad,"\n");
    sys_write(stdout_no,out_pad,strlen(out_pad));
    return false;//返回false才能继续
}
//打印任务列表
void sys_ps(void)
{
    char* ps_title = "PID             PPID            STAT             TICKS            COMMAND\n";
    sys_write(stdout_no,ps_title,strlen(ps_title));
    list_traversal(&thread_all_list,elem2thread_info,0);
}

