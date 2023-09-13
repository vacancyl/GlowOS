#include "exec.h"
#include "thread.h"
#include "stdio-kernel.h"
#include "fs.h"
#include "string.h"
#include "global.h"
#include "memory.h"

extern void intr_exit(void);

/* 将文件描述符fd指向的文件中,偏移为offset,大小为filesz的段加载到虚拟地址为vaddr的内存 */
static bool segment_load(int32_t fd, uint32_t offset, uint32_t filesz, uint32_t vaddr)
{
    printf("1\n");

    uint32_t vaddr_first_page = vaddr & 0xfffff000;               // vaddr地址所在的页框
    uint32_t size_in_first_page = PG_SIZE - (vaddr & 0x00000fff); // 加载到内存后,文件在第一个页框中占用的字节大小
    uint32_t occupy_pages = 0;
    /* 若一个页框容不下该段 */
    if (filesz > size_in_first_page)
    {
        uint32_t left_size = filesz - size_in_first_page;
        occupy_pages = DIV_ROUND_UP(left_size, PG_SIZE) + 1; // 1是指vaddr_first_page
    }
    else
    {
        occupy_pages = 1;
    }
    printf("filesz:%d, vaddr:%x, vaddr_first_page:%x, size_in_first_page:%x, occupy_pages:%d\n", filesz, vaddr, vaddr_first_page, size_in_first_page, occupy_pages);

    /* 为进程分配内存 */
    uint32_t page_idx = 0;
    uint32_t vaddr_page = vaddr_first_page;
    while (page_idx < occupy_pages)
    {

        uint32_t *pde = pde_ptr(vaddr_page);
        uint32_t *pte = pte_ptr(vaddr_page);
        printf("%x %x\n",*pde,*pte);

        /* 如果pde不存在,或者pte不存在就分配内存.
         * pde的判断要在pte之前,否则pde若不存在会导致
         * 判断pte时缺页异常 */
        if (!(*pde & 0x00000001) || !(*pte & 0x00000001))
        {
            printf("3\n");
            if (get_a_page(PF_USER, vaddr_page) == NULL)
            {
                return false;
            }

        } // 如果原进程的页表已经分配了,利用现有的物理页,直接覆盖进程体
        vaddr_page += PG_SIZE;
        page_idx++;
    }
    sys_lseek(fd, offset, SEEK_SET);     // 定位好偏移
    sys_read(fd, (void *)vaddr, filesz); // 直接读取到addr
    return true;
}

/* 从文件系统上加载用户程序pathname,成功则返回程序的起始地址,否则返回-1 */
static int32_t load(const char *pathname)
{
    int32_t ret = -1;
    struct Elf32_Ehdr elf_header;
    struct Elf32_Phdr prog_header;
    memset(&elf_header, 0, sizeof(struct Elf32_Ehdr));

    int32_t fd = sys_open(pathname, O_RDONLY);
    if (fd == -1)
    {
        return -1;
    }

    if (sys_read(fd, &elf_header, sizeof(struct Elf32_Ehdr)) != sizeof(struct Elf32_Ehdr))
    {
        ret = -1;
        goto done;
    }

    /* 校验elf头 */
    if (memcmp(elf_header.e_ident, "\177ELF\1\1\1", 7) || elf_header.e_type != 2 || elf_header.e_machine != 3 || elf_header.e_version != 1 || elf_header.e_phnum > 1024 || elf_header.e_phentsize != sizeof(struct Elf32_Phdr))
    { // 程序头表每个条目的大小 e_phentsize 代表每个段
        ret = -1;
        goto done;
    }

    Elf32_Off prog_header_offset = elf_header.e_phoff; // 程序头表在文件中的字节偏移量
    Elf32_Half prog_header_size = elf_header.e_phentsize;

    /* 遍历所有程序头 */
    uint32_t prog_idx = 0;
    while (prog_idx < elf_header.e_phnum)
    { // e_phnum 段的个数
        memset(&prog_header, 0, prog_header_size);

        /* 将文件的指针定位到程序头 */
        sys_lseek(fd, prog_header_offset, SEEK_SET);

        /* 只获取程序头 */
        if (sys_read(fd, &prog_header, prog_header_size) != prog_header_size)
        {
            ret = -1;
            goto done;
        }

        /* 如果是可加载段就调用segment_load加载到内存 */
        if (PT_LOAD == prog_header.p_type)
        {
            if (!segment_load(fd, prog_header.p_offset, prog_header.p_filesz, prog_header.p_vaddr)) // p_vaddr 本段在内存中的虚拟地址
            {
                ret = -1;
                goto done;
            }
        }

        /* 更新下一个程序头的偏移 */
        prog_header_offset += elf_header.e_phentsize;
        prog_idx++;
    }
    ret = elf_header.e_entry;

done:
    sys_close(fd);
    return ret;
}

/* 用path指向的程序替换当前进程 */
int32_t sys_execv(const char *path, const char *argv[])
{
    uint32_t argc = 0;
    while (argv[argc])
    {
        argc++;
    }

    struct task_struct *cur = running_thread();
    

    int32_t entry_point = load(path);
    if (entry_point == -1)
    { // 若加载失败则返回-1
        return -1;
    }

    /* 修改栈中参数 */
    struct intr_struct *intr_0_stack = (struct intr_struct *)((uint32_t)cur + PG_SIZE - sizeof(struct intr_struct));
    /* 参数传递给用户进程 */
    intr_0_stack->ebx = (int32_t)argv;
    intr_0_stack->ecx = argc; // 参数个数
    intr_0_stack->eip = (void *)entry_point;
    /* 使新用户进程的栈地址为最高用户空间地址 */
    intr_0_stack->esp = (void *)0xc0000000;

    /* exec不同于fork,为使新进程更快被执行,直接从中断返回 */
    asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(intr_0_stack) : "memory");
    return 0;
}
