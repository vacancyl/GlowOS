#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "debug.h"
#include "string.h"

#define PG_SIZE 4096

/*因为0xc009f000 是内核主线程栈顶，0xc009e000是内核主线程的pcb。
*一个页框大小的位图可表示128MB 内存，位图位置安排在地址0xc009a000,(0x9e000 - 0x4000)
*这样本系统最大支持4个页框的位图，即512MB */
//位图开始存放的位置
#define MEM_BITMAP_BASE 0Xc009a000   

/* 0xc0000000 是内核从虚拟地址3G 起。
0x100000意指跨过低端1MB 内存，使虚拟地址在逻辑上连续*/
//内核栈起始位置
#define K_HEAP_START    0xc0100000  

#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22) //返回虚拟地址的高10位
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12) //返回虚拟地址的中10位

//两个实例用于管理内核内存池和用户内存池 物理内存池
struct pool
{
    struct bitmap pool_bitmap; //位图来管理内存使用
    uint32_t phy_addr_start;   //内存池开始的起始地址
    uint32_t pool_size;	//池容量
};

struct pool kernel_pool ,user_pool;  //生成内核内存池 和 用户内存池
struct virtual_addr kernel_vaddr;    //内核虚拟内存管理池 虚拟无限

/*在 pf 表示的虚拟内存池中申请 pg cnt 个虚拟页，成功则返回虚拟页的起始地址，失败则返回 NULL */
void* vaddr_get(enum pool_flags pf,uint32_t pg_cnt)
{
    int vaddr_start = 0,bit_idx_start = -1;
    uint32_t cnt = 0;
    if(pf == PF_KERNEL)
    {
    	bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap,pg_cnt);
    	if(bit_idx_start == -1)	return NULL;
    	
        while(cnt < pg_cnt)bitmap_set(&kernel_vaddr.vaddr_bitmap,bit_idx_start + (cnt++),1);
    	vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
    }
    else
    {
        //用户内存池
    }
    return (void*)vaddr_start;
}

/*得到虚拟地址 vaddr 对应的 pte 指针*/
//这里找到的分别是 页目录物理地址（索引得到）+页表对应的页目录项（页表物理地址）+页表中对应的具体页（这里只有索引，所以需要12位也就是*4）
uint32_t* pte_ptr(uint32_t vaddr)
{
    uint32_t* pte = (uint32_t*)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);//前一个当成页
    return pte;
}

//得到pde指针
//最后一个页目录项访问到的是页目录的物理地址
uint32_t* pde_ptr(uint32_t vaddr)
{
    uint32_t* pde = (uint32_t*) ((0xfffff000) + PDE_IDX(vaddr) * 4);//前两个当成页 索引 这里是可以循环的
    return pde;
}

/*在 m_pool 指向的物理内存池中分配1个物理页，成功时则返回页框的物理地址，失败时则返回 NULL*/
void* palloc(struct pool* m_pool)
{
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap,1);
    if(bit_idx == -1)return NULL;

    bitmap_set(&m_pool->pool_bitmap,bit_idx,1);
    uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
    return (void*)page_phyaddr;
}

/*页表中添加虚拟地址 vaddr 与物理地址 page_phyaddr 的映射*/
/*本质上是在页表中添加此虚拟地址对应的页表项 pte,
并把物理页的物理地址写入此页表项 pte 。*/
void page_table_add(void* _vaddr,void* _page_phyaddr)
{
    uint32_t vaddr = (uint32_t)_vaddr,page_phyaddr = (uint32_t)_page_phyaddr;
    uint32_t* pde = pde_ptr(vaddr);
    uint32_t* pte = pte_ptr(vaddr);
    
    /*先在页目录内判断 目录项的p位,若为1，则表示该表已存在*/
    if(*pde & 0x00000001)
    {
    	ASSERT(!(*pte & 0x00000001));
    	if(!(*pte & 0x00000001))
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
        //分配新的页然后设置pde
    	uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);
    	*pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);

        //页目录对应的页表，初始化
    	memset((void*)((int)pte & 0xfffff000),0,PG_SIZE);
    	ASSERT(!(*pte & 0x00000001));
    	*pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
    }
    return;
}

/*pf所指向的内存池中分配 pg_cnt 个页，成功则返回起始虚拟地址，失败时返回 NULL*/
void* malloc_page(enum pool_flags pf,uint32_t pg_cnt)
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
    
    void* vaddr_start = vaddr_get(pf,pg_cnt);
    if(vaddr_start == NULL)	return NULL;
    
    
    uint32_t vaddr = (uint32_t)vaddr_start,cnt = pg_cnt;
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    
    /*
    虚拟地址是连续的，但物理地址可能连续，也可能不连续，因此第1步中可以1次性，申请 pg_cnt 个虚拟页。成功申请之后，根据申请的页数，通过循环依次为每一个虚拟页申请物理页，再
    将它们在页表中依次映射关联。
    */
    while(cnt-- > 0)
    {
    	void* page_phyaddr = palloc(mem_pool);
    	if(page_phyaddr == NULL)	return NULL;
    	page_table_add((void*)vaddr,page_phyaddr);
    	vaddr += PG_SIZE;
    }
    return vaddr_start;
}

/*
从内核物理内存池中申请 pg_cnt 页内存，成功则返回其虚拟地址，失败则返回 NULL
*/
void* get_kernel_pages(uint32_t pg_cnt)
{
    void* vaddr = malloc_page(PF_KERNEL,pg_cnt);
    if(vaddr != NULL)memset(vaddr,0,pg_cnt*PG_SIZE);
    return vaddr;
}

//内存池的初始化
void mem_pool_init(uint32_t all_mem)
{
    put_str("    mem_pool_init start!\n");
    uint32_t page_table_size = PG_SIZE * 256;       //页表占用的大小 页目录表1和页表（0=768 769-1022）255 = 256 
    uint32_t used_mem = page_table_size + 0x100000; //低端1MB的内存 + 页表所占用的大小
    uint32_t free_mem = all_mem - used_mem;
    
    uint16_t all_free_pages = free_mem / PG_SIZE;   //空余的页数 = 总空余内存 / 一页的大小
    
    uint16_t kernel_free_pages = all_free_pages /2; //内核 与 用户 各平分剩余内存
    uint16_t user_free_pages = all_free_pages - kernel_free_pages; //万一是奇数 就会少1 减去即可
    
    //kbm kernel_bitmap ubm user_bitmap
    uint32_t kbm_length = kernel_free_pages / 8;    //一位即可表示一页 8位一个数，字节
    uint32_t ubm_length = user_free_pages / 8;
    
    //kp kernel_pool up user_pool
    uint32_t kp_start = used_mem;
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE;
    
    //池的start地址
    kernel_pool.phy_addr_start = kp_start;
    user_pool.phy_addr_start = up_start;
    
    //池的大小
    kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
    user_pool.pool_size = user_free_pages * PG_SIZE;
    
    //池的位图的地址
    kernel_pool.pool_bitmap.bits = (void*)MEM_BITMAP_BASE;
    user_pool.pool_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length);
    
    //池的位图大小bytes_len
    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length; 
    
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
    
    kernel_vaddr.vaddr_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length + ubm_length);
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;
    kernel_vaddr.vaddr_start = K_HEAP_START;

    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("    mem_pool_init done\n");
    return;
}

void mem_init()
{
    put_str("mem_init start!\n");
    uint32_t mem_bytes_total = (*(uint32_t*)(0xb00)); //我们把总内存的值放在0xb00
    mem_pool_init(mem_bytes_total);
    put_str("mem_init done!\n");
    return;
}

