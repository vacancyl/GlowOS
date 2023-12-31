#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"

#define PIC_M_CTRL 0x20 // 这里用的可编程中断控制器是8259A,主片的控制端口是0x20
#define PIC_M_DATA 0x21 // 主片的数据端口是0x21
#define PIC_S_CTRL 0xa0 // 从片的控制端口是0xa0
#define PIC_S_DATA 0xa1 // 从片的数据端口是0xa1

#define IDT_DESC_CNT 0x81 // 目前总共支持的中断数

#define EFLAGS_IF 0x00000200 // if位为1
#define GET_EFLAGS(EFLAGS_VAR) asm volatile("pushfl;popl %0" : "=g"(EFLAGS_VAR))

extern uint32_t syscall_handler(void);

// 开中断，返回的是old_status
enum intr_status intr_enable(void)
{
    if (intr_get_status() != INTR_ON)
    {
        asm volatile("sti");
        return INTR_OFF;
    }
    return INTR_ON;
}

// 关中断
enum intr_status intr_disable(void)
{
    if (intr_get_status() != INTR_OFF)
    {
        asm volatile("cli");
        return INTR_ON;
    }
    return INTR_OFF;
}

// 把中断设置为status的状态
enum intr_status intr_set_status(enum intr_status status)
{
    return (status == INTR_ON) ? intr_enable() : intr_disable();
}

// 获取当前的中断状态
enum intr_status intr_get_status(void)
{
    uint32_t eflags = 0;
    GET_EFLAGS(eflags);
    return (eflags & EFLAGS_IF) ? INTR_ON : INTR_OFF;
}

/*中断门描述符结构体*/
struct gate_desc
{
    uint16_t func_offset_low_word;
    uint16_t selector;
    uint8_t dcount; // 此项为双字计数字段，是门描述符中的第4字节。此项固定值，不用考虑
    uint8_t attribute;
    uint16_t func_offset_high_word;
};

// 静态函数声明,非必须
static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function);
static struct gate_desc idt[IDT_DESC_CNT]; // idt是中断描述符表,本质上就是个中断门描述符数组
void register_handler(uint8_t vec_no, intr_handler function);

char *intr_name[IDT_DESC_CNT]; // 用于保存异常的名字

/********    定义中断处理程序数组    ********
 * 在kernel.S中定义的intrXXentry只是中断处理程序的入口,
 * 最终调用的是ide_table中的处理程序*/
intr_handler idt_table[IDT_DESC_CNT];

/********************************************/
extern intr_handler intr_entry_table[IDT_DESC_CNT]; // 声明引用定义在kernel.S中的中断处理函数入口数组

/* 初始化可编程中断控制器8259A */
static void pic_init(void)
{

    /* 初始化主片 */
    outb(PIC_M_CTRL, 0x11); // ICW1: 边沿触发,级联8259, 需要ICW4.
    outb(PIC_M_DATA, 0x20); // ICW2: 起始中断向量号为0x20,也就是IR[0-7] 为 0x20 ~ 0x27.
    outb(PIC_M_DATA, 0x04); // ICW3: IR2接从片.
    outb(PIC_M_DATA, 0x01); // ICW4: 8086模式, 正常EOI

    /* 初始化从片 */
    outb(PIC_S_CTRL, 0x11); // ICW1: 边沿触发,级联8259, 需要ICW4.
    outb(PIC_S_DATA, 0x28); // ICW2: 起始中断向量号为0x28,也就是IR[8-15] 为 0x28 ~ 0x2F.
    outb(PIC_S_DATA, 0x02); // ICW3: 设置从片连接到主片的IR2引脚
    outb(PIC_S_DATA, 0x01); // ICW4: 8086模式, 正常EOI

    // 打开主片上 IRO ，也就是目前只接受时钟产生的中断
    /*
    主片上的OCW1  为 0xfe, 即第0位为0,表示不 屏蔽 IR0 的时钟中断。其他位都是1,表示都屏蔽。
    从片上的所有外设都屏蔽，所以发送的 OCW1  值为0xff。
    OCW1是写入主、从片的奇地址端口，即主片的0x21端口 (PIC_M_DATA)和从片的0xA1端口(PIC_S_DATA)。

    */
    // outb(PIC_M_DATA, 0xfd); //键盘中断
    // outb(PIC_M_DATA, 0xfe); //时钟中断
    // outb(PIC_M_DATA, 0xfc); //时钟和键盘中断
    // outb(PIC_S_DATA, 0xff);

/*
IRQ2用于级联从片，必须打开，否则无法响应从片上的中断。
主片上打开的中断有IRQO 的时钟， IRQ1 的键盘和级联从片的IRQ2,
其他全部关闭
*/
    outb(PIC_M_DATA, 0xf8);

    //打开IRQ14，接受硬盘控制器的中断
    outb(PIC_S_DATA, 0xbf);

    put_str("pic_init done\n");
}

/* 创建中断门描述符 */
static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function)
{
    p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000FFFF;
    p_gdesc->selector = SELECTOR_K_CODE;
    p_gdesc->dcount = 0;
    p_gdesc->attribute = attr;
    p_gdesc->func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}

/*初始化中断描述符表*/
static void idt_desc_init(void)
{
    int i, lastindex = IDT_DESC_CNT - 1;
    for (i = 0; i < IDT_DESC_CNT; i++)
    {
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }
    /* 单独处理系统调用,系统调用对应的中断门dpl为3,
     * 中断处理程序为单独的syscall_handler */
    make_idt_desc(&idt[lastindex], IDT_DESC_ATTR_DPL3, syscall_handler);
    put_str("idt_desc_init done\n");
}

/* 通用的中断处理函数,一般用在异常出现时的处理 */
static void general_intr_handler(uint8_t vec_nr)
{
    if (vec_nr == 0x27 || vec_nr == 0x2f)
    {           // 0x2f是从片8259A上的最后一个irq引脚，保留
        return; // IRQ7和IRQ15会产生伪中断(spurious interrupt),无须处理。
    }
    set_cursor(0); // 光标设置在0号位
    int cursor_pos = 0;
    while ((cursor_pos++) < 320) // 一行80字 4行空格
        put_char(' ');

    set_cursor(0);
    put_str("!!!!!!            excetion message begin            !!!!!!\n");
    set_cursor(88);             // 第二行第八个字开始打印
    put_str(intr_name[vec_nr]); // 打印中断向量号
    if (vec_nr == 14)
    {
        int page_fault_vaddr = 0;
        asm("movl %%cr2,%0" : "=r"(page_fault_vaddr)); // 把虚拟地址 出错的放到了这个变量里面
        put_str("\npage fault addr is ");
        put_int(page_fault_vaddr);
    }
    put_str("!!!!!!            excetion message end              !!!!!!\n");

    while (1)
        ; // 悬停
}

/* 完成一般中断处理函数注册及异常名称注册 */
static void exception_init(void)
{ // 完成一般中断处理函数注册及异常名称注册
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++)
    {

        /* idt_table数组中的函数是在进入中断后根据中断向量号调用的,
         * 见kernel/kernel.S的call [idt_table + %1*4] */
        idt_table[i] = general_intr_handler; // 默认为general_intr_handler。
                                             // 以后会由register_handler来注册具体处理函数。
        intr_name[i] = "unknown";            // 先统一赋值为unknown
    }
    intr_name[0] = "#DE Divide Error";
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "NMI Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // intr_name[15] 第15项是intel保留项，未使用
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}

void register_handler(uint8_t vec_no, intr_handler function)
{
    // 把相关向量号的注册函数指针放进去了
    idt_table[vec_no] = function;
}

/*完成有关中断的所有初始化工作*/
void idt_init()
{
    put_str("idt_init start\n");
    idt_desc_init();  // 初始化中断描述符表
    exception_init(); // 异常名初始化并注册通常的中断处理函数
    pic_init();       // 初始化8259A

    /* 加载idt */
    /*
(1)先用 sizeof(idt)-1得到 idt的段界限limit, 这用作低16位的段界限。
(2)接下来再将 idt的地址挪到高32位即可，这可以通过把 idt地址左移16位的形式实现。由于数组 名便是地址，即指针，故先将其转换成整数才能参与后面的左移运算。
考虑到32位地址经过左移操作后， 高位将被丢弃，万一原地址高16位不是0,这样会造成数据错误，故需要将 idt 地址转换成64位整型后 再进行左移操作，这样其高32位都是0,
经过左移操作依然能够保证其精度。由于指针只能转换成相同 大小的整型，故32位的指针不能直接转换成64位的整型，所以采取迂回的作法，先将其转换成uint32 t,
再将其转换成 uint64 t,之后再对这个64位的无符号整型数据进行左移16位操作。这样 idt 地址被移到了 16～48位，低16位自动填充为0。
(3)之后再将以上两步的结果通过“按位或”运算符[组合到一起后，存储到变量idt operand中。
虽然经过以上的三步得到的操作数是64位，但由于 lidt 的操作数是从内存地址处获得的，所以 lidt 依然只在该地址处(&idt operand)取其中的48位数据当作操作数。


    */
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16)); // 中断向量表
    asm volatile("lidt %0"
                 :
                 : "m"(idt_operand));
    // 在 “lidt  %0” 中，%0其实是 idt  operand 的地址&idt  operand, 并不是idt  operand 的值。
    // 原因是AT&T语法的汇编语言把内存寻址放在最高级，任何数字都被看成是内存地址(所以立即数需要加 前缀$表示),所以lidt%0 直接便去%0指向的内存地址处获取48位的操作数
    put_str("idt_init done\n");
}
