#include "process.h"

//构建用户进程初始化上下文信息
void start_process(void* filename_)
{
    //schedule线程调度后 来到这里
    //特权级0级 到 3级通过 iretd "欺骗" cpu  把用户进程的环境给准备好 iretd即进入
    void* function = filename_;
    struct task_struct* cur = running_thread();
    cur->self_kstack += sizeof(struct thread_stack);
    struct intr_struct* proc_stack = (struct intr_struct*)cur->self_kstack;
    												//环境全存放在intr_stack中了
    proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
    proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
    proc_stack->gs = 0;
    proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;			//数据段选择子
    proc_stack->eip = function;								//函数地址 ip
    proc_stack->cs = SELECTOR_U_CODE;								//cs ip cs选择子
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);				//不能够关闭中断 ELFAG_IF_1 不然会导致无法调度
    proc_stack->esp = (void*)((uint32_t)get_a_page(PF_USER,USER_STACK3_VADDR) + PG_SIZE);// esp 指向最高处 栈空间在0xc0000000以下一页的地方 从用户空间的最高地址 当然物理内存是操作系统来分配
    proc_stack->ss = SELECTOR_U_DATA;								//数据段选择子
    asm volatile ("movl %0,%%esp;jmp intr_exit": : "g"(proc_stack) : "memory");//栈的esp替换成中断栈的位置

    //执行用户进程，再次发生中断的时候，调度的时候，如果下一是内核线程，那么，处理器检测到时从3特权级进入到0，那么就要恢复之前保存的esp0恢复到初始化内核线程的pcb的地址，
    //然后保存中断的状态，包括当前的特权级3栈指针，然后发生调度，注意中断栈此时已经保存（保存在用户进程中的中断栈中，包括cs eip等继续执行），然后调用Switch，保存当前的用户进程栈到中断栈的后面，
    //然后切换到用户线程调用对应的函数
    //如果此时切换到主线程，主线程之前已经执行过，中断的时候保存了寄存器状态，此时的kstack（假设这里是线程a，分析，之前执行的时候刚好把线程栈出栈，（参数什么的）然后此时已经到了中断栈，此处完成闭合
    //因为之后完成中断要恢复状态，任务被完全恢复
    //如果此时发生中断回到用户进程，下一进程为3特权级，需要保存esp0，然后就会被切换到上次的kstack，上次切换的时候保存了四个寄存器，这次后面出栈，然后就ret一直ret到intr_exit，恢复之前的状态
    //也就是开始的时候靠的是线程栈的函数地址去执行，再次调用的时候就是恢复之前的中断栈然后执行。根据出栈的cs：eip，发现特权级将会从0到3，故esp最后又被赋值成用户进程a保存在0特权级栈的3特权级栈
    //这是在中断的时候保存的

    //实际这里是嵌套的
}
/*
切换特权级别：iretd 指令通常在中断或异常处理过程中使用，用于从中断或异常处理程序返回到原始的执行上下文。
这个过程涉及到从用户态切换到内核态，因此需要切换特权级别。

恢复上下文：iretd 指令会从堆栈中弹出保存的寄存器值，包括代码段选择子、指令指针（EIP）、堆栈指针（ESP）以及标志寄存器（EFLAGS）。
这些值将被用于恢复原始的执行上下文，以便从中断或异常处理程序返回到用户程序或操作系统内核。

设置标志：iretd 指令还可以用于设置标志寄存器中的一些特权级别相关标志位，以确保特权级别的正确切换和执行。
*/

/*
执行此函数时，当前任务可能是线程。之所以对线程也要重新安装页表，原因是上一次被调度的可能是进程，
否则不恢复页表的话，线程就会使用进程的页表了。
*/
void page_dir_activate(struct task_struct* p_thread)
{
    uint32_t pagedir_phy_addr = 0x100000; //之前设置的页目录表的物理地址 线程默认使用这个
    if(p_thread->pgdir != NULL)//用户进程有自己的页目录表 线程的页表和其他线程公用
    {
    	pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir); //得到实际页表地址
    }
    //更新页表寄存器
    asm volatile ("movl %0,%%cr3" : : "r"(pagedir_phy_addr) : "memory");
}

//激活线程或者进程的页表 更新tss中的esp0为进程的特权级0的栈
void process_activate(struct task_struct* p_thread)
{
    //激活页表
    page_dir_activate(p_thread);
    
    //tss切换需要 内核线程特权级就是0 处理器进入中断不会从tss中获取0特权级栈地址，所以不需要更新
    //更新tss的esp0
    if(p_thread->pgdir)
    	updata_tss_esp(p_thread);
}

//创建页目录表，将当前页表的表示内核的pde复制 成功返回页目录地址的虚拟地址
uint32_t* create_page_dir(void)
{
    /*
    用户进程通常具有自己的页目录表（Page Directory Table）或页表（Page Table）。
    在操作系统中，每个进程都拥有自己独立的地址空间，这包括用户空间和内核空间。
    为了管理这个地址空间，每个进程都需要拥有自己的页目录表和页表
    */
    //用户进程的页表不能让用户直接访问到，所以在内核空间申请
    //申请的是页目录表
    uint32_t* page_dir_vaddr = get_kernel_pages(1);				//得到内存
    if(page_dir_vaddr == NULL)
    {
    	console_put_str("create_page_dir: get_kernel_page failed!\n");
    	return NULL;
    }
    
    //复制页表 把内核的页目录项复制到用户进程使用的页目录表中
    memcpy((uint32_t*)((uint32_t)page_dir_vaddr + 0x300*4),(uint32_t*)(0xfffff000+0x300*4),1024); // 256项 0x300 == 768
    
    //更新页目录地址的最后一项指向的是页目录表的物理地址
    uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);                    
    page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;                    //最后一项是页目录项自己的地址

    return page_dir_vaddr;									     
}

//创建用户进程的虚拟地址位图 user prog->userprog vaddr, 也就是按照用户进程的虚拟内存信息初始化位图结构体 struct virtual_addr。
void create_user_vaddr_bitmap(struct task_struct* user_prog)
{
    user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;	 //位图开始管理的位置
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START)/ PG_SIZE / 8,PG_SIZE); //向上取整取虚拟页数
    user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
    user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
    bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}
//创建用户进程
void process_execute(void* filename,char* name)
{
    // pcb内核的数据结构，由内核来维护进程信息，因此要在内核内存池中申请
    struct task_struct* thread = get_kernel_pages(1);  //分配一页空间 得到pcb
    init_thread(thread,name,default_prio);		 //初始化pcb
    create_user_vaddr_bitmap(thread);			 //为虚拟地址位图初始化 分配空间
    thread_create(thread,start_process,filename);	 //创造线程 start_process 之后通过start_process intr_exit跳转到用户进程
    thread->pgdir = create_page_dir();		 //把页目录表的地址分配了 并且把内核的页目录都给复制过去 这样操作系统对每个进程都可见
    block_desc_init(thread->u_block_desc);//初始化用户堆内存块描述符
    enum intr_status old_status = intr_disable();     //关中断
    ASSERT(!elem_find(&thread_ready_list,&thread->general_tag));
    list_append(&thread_ready_list,&thread->general_tag);     //添加线程 start_process到就绪队列中
    
    ASSERT(!elem_find(&thread_all_list,&thread->all_list_tag));
    list_append(&thread_all_list,&thread->all_list_tag);
    intr_set_status(old_status);//恢复
}

