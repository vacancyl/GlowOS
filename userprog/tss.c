#include "tss.h"
#include "global.h"
#include "thread.h"
#include "print.h"
#include "string.h"

struct tss
{
    uint32_t backlink;
    uint32_t* esp0;
    uint32_t ss0;
    uint32_t* esp1;
    uint32_t ss1;
    uint32_t* esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t (*eip) (void);
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint32_t trace;
    uint32_t io_base;
};

static struct tss tss;

//更新TSS中的esp0
//内核栈都是位于 pcb头顶一页的地方 这个栈是用户进入内核态用的栈
void updata_tss_esp(struct task_struct* pthread)
{
    tss.esp0 = (uint32_t*)((uint32_t)pthread + PG_SIZE);
}

//返回生成的描述符
struct gdt_desc make_gdt_desc(uint32_t* desc_addr,uint32_t limit,uint8_t attr_low,uint8_t attr_high)
{
    struct gdt_desc desc;
    uint32_t desc_base = (uint32_t) desc_addr;
    desc.limit_low_word =  limit & 0x0000ffff;
    desc.base_low_word = desc_base & 0x0000ffff;
    desc.base_mid_byte = ((desc_base & 0x00ff0000) >> 16);
    desc.attr_low_byte = (uint8_t)(attr_low);
    desc.limit_high_attr_high = (((limit & 0x000f0000) >> 16) + (uint8_t)(attr_high));
    desc.base_high_byte = desc_base >> 24;
    return desc;
}

//初始化TSS 安装到GDT中
void tss_init()
{
    put_str("tss_init start\n");
    uint32_t tss_size = sizeof(tss);
    memset(&tss,0,tss_size);
    tss.ss0 = SELECTOR_K_STACK;//0级栈段选择子
    tss.io_base = tss_size;//表示此TSS中并没有位图 
    
    
    *((struct gdt_desc*)0xc0000920) = make_gdt_desc((uint32_t*)&tss,tss_size-1,TSS_ATTR_LOW,TSS_ATTR_HIGH);
    *((struct gdt_desc*)0xc0000928) = make_gdt_desc((uint32_t*)0,0xfffff,GDT_CODE_ATTR_LOW_DPL3,\
    									       GDT_ATTR_HIGH);
    *((struct gdt_desc*)0xc0000930) = make_gdt_desc((uint32_t*)0,0xfffff,GDT_DATA_ATTR_LOW_DPL3,\
    									       GDT_ATTR_HIGH);
    
    //lgdt操作数
    uint64_t gdt_operand = ((8*7 - 1) | ((uint64_t)(uint32_t)0xc0000900 << 16));//变成64位才能与，上面的才能覆盖
    
    asm volatile("lgdt %0" :: "m"(gdt_operand));//内存地址作为中转变量，直接读写
    asm volatile("ltr %w0" :: "r"(SELECTOR_TSS));//r通用寄存器
    put_str("tss_init and ltr done\n");
}

