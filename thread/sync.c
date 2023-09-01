#include "sync.h"
#include "list.h"
#include "stdint.h"
#include "thread.h"
#include "debug.h"
#include "interrupt.h"


//初始化信号量
void sema_init(struct semaphore* psema,uint32_t value)
{
    psema->value = value;
    list_init(&psema->waiters);
}
//初始化锁plock
void lock_init(struct lock* plock)
{
    sema_init(&plock->semaphore,1);  //信号量初始值都设置成1 
    plock->holder = 0;
    plock->holder_repeat_nr = 0;
}

//信号量的down操作
void sema_down(struct semaphore* psema)
{
    //保证原子操作
    enum intr_status old_status = intr_disable();

    //被唤醒之后可能有其他的线程竞争锁，所以还需要判断，不能直接减，如果被其他线程拿到锁，那就继续等待
    while(0 == psema->value)//表示已经被人持有
    {
    	//访问锁的不应该在锁的等待队列
    	ASSERT(!elem_find(&psema->waiters,&(running_thread()->general_tag)));
    	if(elem_find(&psema->waiters,&(running_thread()->general_tag)))
    	    PANIC("sema_down: seme_down thread already in ready_list\n");
    	list_append(&psema->waiters,&(running_thread()->general_tag));  //添加到等待队列
    	thread_block(TASK_BLOCKED);                                     //阻塞并切换线程
    }
    --psema->value;							  //否则占用信号量
    ASSERT(0 == psema->value);	
    intr_set_status(old_status);	//恢复中断状态
}

//信号量value增加
void sema_up(struct semaphore* psema)
{

    enum intr_status old_status = intr_disable();
    ASSERT(0 == psema->value);
    if(!list_empty(&psema->waiters))
    {
    	thread_unblock((struct task_struct*)((uint32_t)list_pop(&psema->waiters) & 0xfffff000));
    }
    ++psema->value;
    ASSERT(psema->value == 1);
    intr_set_status(old_status);
}

//获取锁资源
void lock_acquire(struct lock* plock)
{
    //有时候线程可能嵌套申请同一把锁，这种情况再申请锁，就会导致死锁
    //判断自己是不是锁的持有者
    if(plock->holder != running_thread())
    {
    	sema_down(&plock->semaphore);		//如果已经被占用 则会被阻塞 迟早会返回的

    	plock->holder = running_thread();	//之前被阻塞的线程 返回之后继续执行 设置为当前的持有者
    	ASSERT(0 == plock->holder_repeat_nr);
    	plock->holder_repeat_nr = 1;			//访问数为1 第一次申请锁
    }
    else	++plock->holder_repeat_nr;
}

/*
在已经持有锁的情况下继续申请该锁，若仍 sema_down ，则线程会陷入睡眠，等待锁的持有者将自己叫醒。而锁的持有者又是其本身，自己可不能叫醒自己，因此系统陷入死锁。
所以这里为了应对重复申请锁的情况，当第二次申请时（内层），仅 holder_repeat_nr++ ；当释放锁时，肯定是先从内层释放，所以仅 holder_repeat_nr-- ；外层释放时，再 sema_up 。
*/

//释放锁资源
void lock_release(struct lock* plock)
{
    ASSERT(plock->holder == running_thread());	//释放锁的线程必须是其拥有者
    if(plock->holder_repeat_nr > 1)	//说明多次申请锁 //减少到的当前一个线程 次数只有一次访问这个锁时 才允许到下面
    {
    	--plock->holder_repeat_nr;		
    	return;
    }
    ASSERT(plock->holder_repeat_nr == 1);	//举个例子 该线程在拥有锁时 两次获取锁 第二次肯定是无法获取到的
    						//但是必须同样也要有两次release才算彻底释放 不然只有第一次的relase
    						//第二次都不需要release 就直接释放了 肯定是不行的
    plock->holder = 0;
    plock->holder_repeat_nr = 0;
    sema_up(&plock->semaphore);   
    /*
    要把持有者置空语句 “plock->holder =NULL”放在 sema up 操作之前。原因是释放锁的操作 并不在关中断下进行，有可能会被调度器换下处理器。
    若 sema up 操作在前的话， sema up 会先把 value 置1,若老线程刚执行完sema up, 还未执行 “plock->holder=NULL” 便被换下处理器，新调度上来的进程 
    有可能也申请了这个锁， value为1,因此申请成功，锁的持有者 plock->holder将变成这个新进程的 PCB。
    假如这个新线程还未释放锁又被换下了处理器，老线程又被调度上来执行，它会继续执行“plock->holder = NULL",  将持有者置空，这就乱了
    */
}
