#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "debug.h"
#include "string.h"
#include "sync.h"
#include "interrupt.h"

#define PG_SIZE 4096
struct mem_block_desc k_block_descs[DESC_CNT]; // 内核内存块描述符数组

/*因为0xc009f000 是内核主线程栈顶，0xc009e000是内核主线程的pcb。
 *一个页框大小的位图可表示128MB 内存，（4Kb * 4Kb * 8 ）位图位置安排在地址0xc009a000,(0x9e000 - 0x4000)
 *这样本系统最大支持4个页框的位图，即512MB */
// 位图开始存放的位置
#define MEM_BITMAP_BASE 0xc009a000

/* 0xc0000000 是内核从虚拟地址3G 起。
0x100000意指跨过低端1MB 内存，使虚拟地址在逻辑上连续*/
// 内核栈起始位置
#define K_HEAP_START 0xc0100000

#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22) // 返回虚拟地址的高10位
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12) // 返回虚拟地址的中10位

// 两个实例用于管理内核内存池和用户内存池 物理内存池
struct pool
{
    struct bitmap pool_bitmap; // 位图来管理内存使用
    uint32_t phy_addr_start;   // 内存池开始的起始地址
    uint32_t pool_size;        // 池容量
    struct lock lock;
};

struct pool kernel_pool, user_pool; // 生成内核内存池 和 用户内存池
struct virtual_addr kernel_vaddr;   // 内核虚拟内存管理池 虚拟无限

/*在 pf 表示的虚拟内存池中申请 pg cnt 个虚拟页，成功则返回虚拟页的起始地址，失败则返回 NULL */
void *vaddr_get(enum pool_flags pf, uint32_t pg_cnt)
{
    int vaddr_start = 0, bit_idx_start = -1;
    uint32_t cnt = 0;
    if (pf == PF_KERNEL)
    {
        bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1)
            return NULL;

        while (cnt < pg_cnt)
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + (cnt++), 1); // 先赋值，再++
        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;       // 因为这里的 kernel_vaddr.vaddr_start 是不变的
    }
    else
    {
        // 用户内存池
        struct task_struct *cur = running_thread();
        bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1)
            return NULL;

        while (cnt < pg_cnt)
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);

        /*(0xc0000000-PG SIZE)作为用户3级栈已经在 start process  被分配*/
        vaddr_start = cur->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE; // 用户进程块的虚拟地址，所以这里只有内核虚拟池
        ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));
    }
    return (void *)vaddr_start;
}

/*得到虚拟地址 vaddr 对应的 pte 指针*/
// 这里找到的分别是 页目录物理地址（索引得到）+页表对应的页目录项（页表物理地址）+页表中对应的具体页（这里只有索引，所以需要12位也就是*4）
uint32_t *pte_ptr(uint32_t vaddr)
{
    uint32_t *pte = (uint32_t *)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4); // 前一个当成页
    return pte;
}

// 得到pde指针
// 最后一个页目录项访问到的是页目录的物理地址
uint32_t *pde_ptr(uint32_t vaddr)
{
    uint32_t *pde = (uint32_t *)((0xfffff000) + PDE_IDX(vaddr) * 4); // 前两个当成页 索引 这里是可以循环的
    return pde;
}

/*在 m_pool 指向的物理内存池中分配1个物理页，成功时则返回页框的物理地址，失败时则返回 NULL*/
void *palloc(struct pool *m_pool)
{
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);
    if (bit_idx == -1)
        return NULL;

    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);
    uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
    return (void *)page_phyaddr;
}

/*页表中添加虚拟地址 vaddr 与物理地址 page_phyaddr 的映射*/
/*本质上是在页表中添加此虚拟地址对应的页表项 pte,
并把物理页的物理地址写入此页表项 pte 。*/
void page_table_add(void *_vaddr, void *_page_phyaddr)
{
    uint32_t vaddr = (uint32_t)_vaddr, page_phyaddr = (uint32_t)_page_phyaddr;
    uint32_t *pde = pde_ptr(vaddr);
    uint32_t *pte = pte_ptr(vaddr);

    /*先在页目录内判断 目录项的p位,若为1，则表示该表已存在*/
    if (*pde & 0x00000001)
    {
        ASSERT(!(*pte & 0x00000001));
        if (!(*pte & 0x00000001))
        {
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
        else
        {
            PANIC("pte repeat");
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
    }
    else
    {
        // 分配新的页然后设置pde
        uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);
        *pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);

        // 页目录对应的页表，初始化
        memset((void *)((int)pte & 0xfffff000), 0, PG_SIZE);
        ASSERT(!(*pte & 0x00000001));
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
    }
    return;
}

/*pf所指向的内存池中分配 pg_cnt 个页，成功则返回起始虚拟地址，失败时返回 NULL*/
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt)
{
    /*
    内核和用户空间各约 16MB 空间，保守起见用 15MB 来限制，申请的内存页数要小于内存池大小，
    */
    /*
    通过 vaddr_get 在虚拟内存池中申请虚拟地址
    通过 palloc 在物理内存池中申请物理页。
    通过 page_table_add 将以上两步得到的虚拟地址和物理地址在页表中完成映射
    */
    ASSERT(pg_cnt > 0 && pg_cnt < 3840);

    void *vaddr_start = vaddr_get(pf, pg_cnt);
    if (vaddr_start == NULL)
        return NULL;

    uint32_t vaddr = (uint32_t)vaddr_start, cnt = pg_cnt;
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;

    /*
    虚拟地址是连续的，但物理地址可能连续，也可能不连续，因此第1步中可以1次性，申请 pg_cnt 个虚拟页。成功申请之后，根据申请的页数，通过循环依次为每一个虚拟页申请物理页，再
    将它们在页表中依次映射关联。
    */
    while (cnt-- > 0)
    {
        void *page_phyaddr = palloc(mem_pool);
        if (page_phyaddr == NULL)
            return NULL;
        page_table_add((void *)vaddr, page_phyaddr);
        vaddr += PG_SIZE;
    }
    return vaddr_start;
}

/*
从内核物理内存池中申请 pg_cnt 页内存，成功则返回其虚拟地址，失败则返回 NULL
*/
void *get_kernel_pages(uint32_t pg_cnt)
{
    void *vaddr = malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL)
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    return vaddr;
}

/*在用户空间申请4K内存 返回虚拟地址*/
void *get_user_pages(uint32_t pg_cnt)
{
    // 用户进程可能会产生冲突 大部分时间都在用户进程 内核进程可以理解基本不会冲突
    lock_acquire(&user_pool.lock);
    void *vaddr = malloc_page(PF_USER, pg_cnt);
    if (vaddr != NULL)
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    lock_release(&user_pool.lock);
    return vaddr;
}

// 将地址vaddr和pf池中的物理地址关联，返回其虚拟地址 申请1页，并和vaddr关联
void *get_a_page(enum pool_flags pf, uint32_t vaddr)
{
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    lock_acquire(&mem_pool->lock);

    struct task_struct *cur = running_thread();
    int32_t bit_idx = -1;

    // 双重判断
    /*
    在上面的代码中，当cur->pgdir不为NULL时，这意味着当前正在操作一个用户进程。
    用户进程的地址空间是由操作系统管理的，它包含了用户进程的代码和数据，而cur->pgdir指向了该进程的页目录或页表。因此，判断cur->pgdir不为NULL是为了确保当前正在操作的是一个用户进程的地址空间。

    如果cur->pgdir为NULL，那么意味着当前不处于用户进程的上下文中，而可能处于内核代码执行的上下文中。
    在这种情况下，操作系统可能正在进行内核级别的任务，例如内核初始化、中断处理等
    */
    // 虚拟地址位图置1
    // 用户进程申请，那么就修改用户进程自己的虚拟地址位图
    if (cur->pgdir != NULL && pf == PF_USER)
    {
        bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);
    }
    // 如果申请的内核内存，就修改kernel_vaddr
    else if (cur->pgdir == NULL && pf == PF_KERNEL)
    {
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
    }
    else
        PANIC("get_a_page:not allow kernel alloc userspace or user alloc kernelspace by get_a_page");

    void *page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL)
        return NULL;
    page_table_add((void *)vaddr, page_phyaddr);
    lock_release(&mem_pool->lock);
    return (void *)vaddr;
}

/* 得到虚拟地址映射的物理地址 */
uint32_t addr_v2p(uint32_t vaddr)
{
    uint32_t *pte = pte_ptr(vaddr);
    return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}

// 内存池的初始化
void mem_pool_init(uint32_t all_mem)
{
    put_str("    mem_pool_init start!\n");
    uint32_t page_table_size = PG_SIZE * 256;       // 页表占用的大小 页目录表1和页表（0=768 769-1022）255 = 256
    uint32_t used_mem = page_table_size + 0x100000; // 低端1MB的内存 + 页表所占用的大小 0x20 0000
    uint32_t free_mem = all_mem - used_mem;

    uint16_t all_free_pages = free_mem / PG_SIZE; // 空余的页数 = 总空余内存 / 一页的大小

    uint16_t kernel_free_pages = all_free_pages / 2;               // 内核 与 用户 各平分剩余内存
    uint16_t user_free_pages = all_free_pages - kernel_free_pages; // 万一是奇数 就会少1 减去即可

    // kbm kernel_bitmap ubm user_bitmap
    uint32_t kbm_length = kernel_free_pages / 8; // 一位即可表示一页 8位一个数，字节
    uint32_t ubm_length = user_free_pages / 8;

    // kp kernel_pool up user_pool
    uint32_t kp_start = used_mem;                               // 0x20 0000
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE; // 110 0000 32MB - 2MB =30MB 所以这里分配了15MB

    // 池的start地址
    kernel_pool.phy_addr_start = kp_start;
    user_pool.phy_addr_start = up_start;

    // 池的大小
    kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
    user_pool.pool_size = user_free_pages * PG_SIZE;

    // 池的位图的地址
    kernel_pool.pool_bitmap.bits = (void *)MEM_BITMAP_BASE;              // 0xc009a000
    user_pool.pool_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length); // 0xc009a1e0

    // 池的位图大小bytes_len
    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length; // 都是1e0 3840个页 15MB

    put_str("        kernel_pool_bitmap_start:");
    put_int((int)kernel_pool.pool_bitmap.bits);
    put_str(" kernel_pool_phy_addr_start:");
    put_int(kernel_pool.phy_addr_start);
    put_char('\n');
    put_str("        user_pool_bitmap_start:");
    put_int((int)user_pool.pool_bitmap.bits);
    put_str(" user_pool_phy_addr_start:");
    put_int(user_pool.phy_addr_start);
    put_char('\n');

    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);

    // 内核虚拟地址的初始化
    kernel_vaddr.vaddr_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length + ubm_length); // 最大支持512MB
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;                                // 15MB
    kernel_vaddr.vaddr_start = K_HEAP_START;

    bitmap_init(&kernel_vaddr.vaddr_bitmap);

    lock_init(&kernel_pool.lock);
    lock_init(&user_pool.lock);
    put_str("    mem_pool_init done\n");
    return;
}

// 为malloc准备
void block_desc_init(struct mem_block_desc *desc_array)
{
    uint16_t desc_idx, block_size = 16;

    // 初始化每个mem_block_desc
    for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++)
    {
        desc_array[desc_idx].block_size = block_size;
        desc_array[desc_idx].block_per_arena = (PG_SIZE - sizeof(struct arena)) / block_size;
        list_init(&desc_array[desc_idx].free_list);
        block_size *= 2;
    }
}
// 返回idx个内存块的地址 空间布局为 arena元信息+n个内存块
struct mem_block *arena2block(struct arena *a, uint32_t idx)
{
    // 跳过元信息 这里是每个内存页都有一个arena信息
    return (struct mem_block *)((uint32_t)a + sizeof(struct arena) + idx * a->desc->block_size);
}

// 返回内存块b所在的arena地址 自然页框的最低点处
struct arena *block2arena(struct mem_block *b)
{
    return (struct arena *)((uint32_t)b & 0xfffff000);
}

// 申请的字节数 全部设为0
void *sys_malloc(uint32_t size)
{
    enum pool_flags PF;
    struct pool *mem_pool;
    uint32_t pool_size;
    struct mem_block_desc *descs;
    struct task_struct *cur_thread = running_thread();

    // 判断用的哪个内存池
    if (cur_thread->pgdir == NULL) // 内核线程
    {
        PF = PF_KERNEL;
        pool_size = kernel_pool.pool_size;
        mem_pool = &kernel_pool;
        descs = k_block_descs;
    }
    else
    {
        PF = PF_USER;
        pool_size = user_pool.pool_size;
        mem_pool = &user_pool;
        descs = cur_thread->u_block_desc;
    }
    // size 超出空间大小或者为 0 负数
    if (!(size > 0 && size < pool_size))
    {
        return NULL;
    }

    struct arena *a;
    struct mem_block *b;
    lock_acquire(&mem_pool->lock);

    // 超过页框直接分配页框
    if (size > 1024)
    {
        uint32_t page_cnt = DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE); // 向一页取整
        a = malloc_page(PF, page_cnt);
        if (a != NULL)
        {
            memset(a, 0, page_cnt * PG_SIZE);
            a->desc = NULL;
            a->cnt = page_cnt;
            a->large = true;
            lock_release(&mem_pool->lock);
            return (void *)(a + 1); // 返回地址 struct arena + sizeof(struct arena)
        }
        else
        {
            // 分配失败
            lock_release(&mem_pool->lock);
            return NULL;
        }
    }
    else // 小于1024则直接适配
    {
        // 匹配合适的内存块
        uint8_t desc_idx;
        for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++)
        {
            if (size <= descs[desc_idx].block_size)
            {
                break;
            }
        }

        // 已经空了
        if (list_empty(&descs[desc_idx].free_list))
        {
            a = malloc_page(PF, 1);
            if (a == NULL)
            {
                lock_release(&mem_pool->lock);
                return NULL;
            }
            memset(a, 0, PG_SIZE);

            a->desc = &descs[desc_idx];
            a->large = false;
            a->cnt = descs[desc_idx].block_per_arena;
            uint32_t block_idx;

            enum intr_status old_status = intr_disable();
            // 拆分为内存块，添加到内存块描述符free_list中
            for (block_idx = 0; block_idx < descs[desc_idx].block_per_arena; ++block_idx)
            {
                b = arena2block(a, block_idx);
                ASSERT(!elem_find(&a->desc->free_list, &b->free_elem));
                list_append(&a->desc->free_list, &b->free_elem);
            }
            intr_set_status(old_status);
        }

        // 分配内存块
        b = (struct mem_block *)list_pop(&(descs[desc_idx].free_list));
        memset(b, 0, descs[desc_idx].block_size);

        a = block2arena(b);
        --a->cnt; // 可用内存块减少
        lock_release(&mem_pool->lock);
        return (void *)b;
    }
}

void mem_init()
{
    put_str("mem_init start!\n");
    uint32_t mem_bytes_total = (*(uint32_t *)(0xb00)); // 我们把总内存的值放在0xb00
    mem_pool_init(mem_bytes_total);
    block_desc_init(k_block_descs);
    put_str("mem_init done!\n");
    return;
}

// 物理地址回收到物理内存池 回收一个页
void pfree(uint32_t pg_phy_addr)
{
    struct pool *mem_pool;
    uint32_t bit_idx = 0;
    if (pg_phy_addr >= user_pool.phy_addr_start) // 用户物理内存池
    {
        mem_pool = &user_pool;
        bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PG_SIZE;
    }
    else
    {
        mem_pool = &kernel_pool;
        bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PG_SIZE;
    }
    bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0); // 全部置0
}

// 去掉映射，只需要p置0 去掉pte
void page_table_pte_remove(uint32_t vaddr)
{
    uint32_t *pte = pte_ptr(vaddr);
    *pte &= ~PG_P_1;
    asm volatile("invlpg %0" ::"m"(vaddr) : "memory"); // 更新tlb 刷新映射关系
}

// 在虚拟内存池中释放vaddr开始的pg_cnt个虚拟页地址
void vaddr_remove(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt)
{
    uint32_t bit_idx_start = 0, vaddr = (uint32_t)_vaddr, cnt = 0;

    if (pf == PF_KERNEL) // 内核虚拟池
    {
        bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt)
        {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + (cnt++), 0);
        }
    }
    else
    {
        struct task_struct *cur_thread = running_thread();
        bit_idx_start = (vaddr - cur_thread->userprog_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt)
        {
            bitmap_set(&cur_thread->userprog_vaddr.vaddr_bitmap, bit_idx_start + (cnt++), 0);
        }
    }
}

// 释放虚拟地址vaddr开始的cnt个物理页框
void mfree_page(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt)
{
    uint32_t pg_phy_addr;
    uint32_t vaddr = (int32_t)_vaddr, page_cnt = 0;
    ASSERT(pg_cnt >= 1 && vaddr % PG_SIZE == 0);

    // 获取对应的物理地址
    pg_phy_addr = addr_v2p(vaddr);

    // 确保释放的物理地址内存在低端1MB + 1页目录表 + 1页表范围内
    ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= 0x102000);

    // 用户还是内核 先分配的内核，所以先判断用户
    if (pg_phy_addr >= user_pool.phy_addr_start)
    {
        vaddr -= PG_SIZE;//为了和下面的呼应
        while (page_cnt < pg_cnt)
        {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);

            // 确保物理地址属于用户物理内存池
            ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= user_pool.phy_addr_start);

            // 物理页释放
            pfree(pg_phy_addr);
            // 清除页表 虚拟地址所在的pte
            page_table_pte_remove(vaddr);
            page_cnt++;
        }

        // 清空虚拟地址
        vaddr_remove(pf, _vaddr, pg_cnt);
    }
    else
    {
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt)
        {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);
            ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= kernel_pool.phy_addr_start && pg_phy_addr < user_pool.phy_addr_start);
            pfree(pg_phy_addr);
            page_table_pte_remove(vaddr);
            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);
    }
}

//回收内存ptr
void sys_free(void *ptr)
{
    ASSERT(ptr != NULL);
    if (ptr != NULL)
    {
        enum pool_flags PF;
        struct pool *mem_pool;

        if (running_thread()->pgdir == NULL)//内核线程
        {
            ASSERT((uint32_t)ptr >= K_HEAP_START);
            PF = PF_KERNEL;
            mem_pool = &kernel_pool;
        }
        else
        {
            PF = PF_USER;
            mem_pool = &user_pool;
        }

        //获取元信息
        lock_acquire(&mem_pool->lock);
        struct mem_block *b = ptr;
        struct arena *a = block2arena(b);

        ASSERT(a->large == 0 || a->large == 1);
        if (a->desc == NULL && a->large == true)//大于1024
        {
            mfree_page(PF, a, a->cnt);//直接释放页
        }
        else//小于1024
        {
            //先回收到free_list 但是不会释放，是一页一页的释放
            list_append(&a->desc->free_list, &b->free_elem);
            //内存块是否都是空闲
            if (++a->cnt == a->desc->block_per_arena)
            {
                //释放整个arena
                uint32_t block_idx;
                for (block_idx = 0; block_idx < a->desc->block_per_arena; block_idx++)
                {
                    struct mem_block *b = arena2block(a, block_idx);
                    ASSERT(elem_find(&a->desc->free_list, &b->free_elem));
                    list_remove(&b->free_elem);
                }
                mfree_page(PF, a, 1);
            }
        }
        lock_release(&mem_pool->lock);
    }
}
