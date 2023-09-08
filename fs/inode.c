#include "inode.h"
#include "ide.h"
#include "debug.h"
#include "thread.h"
#include "memory.h"
#include "string.h"
#include "list.h"
#include "interrupt.h"
#include "super_block.h"
#include "fs.h"
#include "file.h"

// 得到inode 所在位置 初始化inode_pos
void inode_locate(struct partition *part, uint32_t inode_no, struct inode_position *inode_pos)
{
    ASSERT(inode_no < 4096);
    uint32_t inode_table_lba = (part->sb)->inode_table_lba; // 得到起始扇区lba

    uint32_t inode_size = sizeof(struct inode);
    uint32_t off_size = inode_no * inode_size;
    uint32_t off_sec = off_size / 512;         // 得到偏移扇区数
    uint32_t off_size_in_sec = off_size % 512; // 得到在扇区内偏移字节数

    uint32_t left_in_sec = 512 - off_size_in_sec; // 在扇区内剩余字节数 不满sizeof(struct inode)即在两扇区间
    if (left_in_sec < inode_size)
        inode_pos->two_sec = true;
    else
        inode_pos->two_sec = false;

    inode_pos->sec_lba = inode_table_lba + off_sec;
    inode_pos->off_size = off_size_in_sec;
}

// 把inode写回分区part
void inode_sync(struct partition *part, struct inode *inode, void *io_buf)
{
    // iobuf是用于硬盘io的缓冲区 主调函数的 如果到最后一步申请内存失败，代价太大，提前申请好内存
    uint8_t inode_no = inode->i_no;
    struct inode_position inode_pos; // inode_position
    inode_locate(part, inode_no, &inode_pos);

    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    // 拷贝
    struct inode pure_inode;
    memcpy(&pure_inode, inode, sizeof(struct inode));

    // 统计操作状态 这些都只在内存中有用
    pure_inode.i_open_cnts = 0;
    pure_inode.write_deny = false;
    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

    // 用于拼接同步的数据
    char *inode_buf = (char *)io_buf;
    // 如果跨分区
    if (inode_pos.two_sec)
    {
        // 一次性读两个扇区的内容 复制粘贴完了回读回去
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else
    {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
}

// 返回相对应的inode指针 从缓存队列或者硬盘
struct inode *inode_open(struct partition *part, uint32_t inode_no)
{
    // 先从inode链表找到inode 为了提速而创建的
    struct list_elem *elem = part->open_inodes.head.next; // 第一个结点开始
    struct inode *inode_found;                            // 返回
    while (elem != &part->open_inodes.tail)
    {
        // 每个都遍历转换
        inode_found = elem2entry(struct inode, inode_tag, elem);

        // 找到了
        if (inode_found->i_no == inode_no)
        {
            inode_found->i_open_cnts++; // 被打开的次数
            return inode_found;
        }
        elem = elem->next;
    }

    // 缓冲链表没找到 则需要在硬盘区找
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);

    /*
    为使通过sys malloc创建的新inode被所有任务共享，需要inode置于内核空间，故需要临时
    将cur_pbc->pgdir置为NULL
    */
    struct task_struct *cur = running_thread();
    uint32_t *cur_pagedir_back = cur->pgdir;
    cur->pgdir = NULL;
    inode_found = (struct inode *)sys_malloc(sizeof(struct inode));
    // 这样就在内核空间分配，然后恢复即可，否则只有用户能访问到

    cur->pgdir = cur_pagedir_back;

    char *inode_buf;
    if (inode_pos.two_sec)
    {
        inode_buf = (char *)sys_malloc(1024);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else
    {
        inode_buf = (char *)sys_malloc(512);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
    memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));
    // 放在队首 因为很有可能接下来要访问
    list_push(&part->open_inodes, &inode_found->inode_tag);
    inode_found->i_open_cnts = 1; // 被打开一次

    sys_free(inode_buf);
    return inode_found;
}

// 关闭inode 或减少inode打开数
void inode_close(struct inode *inode)
{
    enum intr_status old_status = intr_disable();
    // 减少次数后 当发现为0了 直接释放即可 否则可能其他的线程使用
    if (--inode->i_open_cnts == 0)
    {
        struct task_struct *cur = running_thread();
        uint32_t *cur_pagedir_back = cur->pgdir;
        cur->pgdir = NULL; // 同样需要在内核空间释放
        sys_free(inode);
        cur->pgdir = cur_pagedir_back;
    }
    intr_set_status(old_status);
}

// 初始化inode inode第一个编号是0
void inode_init(uint32_t inode_no, struct inode *new_inode)
{
    new_inode->i_no = inode_no;
    new_inode->i_size = 0;
    new_inode->i_open_cnts = 0;
    new_inode->write_deny = false;

    // 只有在写文件的时候才为他分配扇区，不知道文件大小提前分配浪费空间
    uint8_t sec_idx = 0;
    while (sec_idx < 13)
        new_inode->i_sectors[sec_idx++] = 0;
}

// 硬盘分区part上的inode清空
void inode_delete(struct partition *part, uint32_t inode_no, void *io_buf)
{
    ASSERT(inode_no < 4096);
    struct inode_position inode_pos; // 存储inode的pos的
    inode_locate(part, inode_no, &inode_pos);
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt)); // 保证在分区之内

    char *inode_buf = (char *)io_buf;
    if (inode_pos.two_sec)
    {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);          // 夹在两个扇区间
        memset((inode_buf + inode_pos.off_size), 0, sizeof(struct inode)); // 把inode信息清除 再次写回硬盘
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else
    {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);          // 只在一个扇区内
        memset((inode_buf + inode_pos.off_size), 0, sizeof(struct inode)); // 把inode信息清除 再次写回硬盘
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
}

/* 回收inode的数据块和inode本身 */
void inode_release(struct partition *part, uint32_t inode_no)
{
    struct inode *inode_to_del = inode_open(part, inode_no);
    ASSERT(inode_to_del->i_no == inode_no);

    /* 1 回收inode占用的所有块 */
    uint8_t block_idx = 0, block_cnt = 12;
    uint32_t block_bitmap_idx;
    uint32_t all_blocks[140] = {0}; // 12个直接块+128个间接块

    /* a 先将前12个直接块存入all_blocks */
    while (block_idx < 12)
    {
        all_blocks[block_idx] = inode_to_del->i_sectors[block_idx];
        block_idx++;
    }

    /* b 如果一级间接块表存在,将其128个间接块读到all_blocks[12~], 并释放一级间接块表所占的扇区 */
    if (inode_to_del->i_sectors[12] != 0)
    {
        ide_read(part->my_disk, inode_to_del->i_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;

        /* 回收一级间接块表占用的扇区 */
        block_bitmap_idx = inode_to_del->i_sectors[12] - part->sb->data_start_lba;
        ASSERT(block_bitmap_idx > 0);

        // 回收一级间接表块占用的位图
        bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
        bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
    }

    /* c inode所有的块地址已经收集到all_blocks中,下面逐个回收 */
    block_idx = 0;
    while (block_idx < block_cnt)
    {
        if (all_blocks[block_idx] != 0)
        {
            block_bitmap_idx = 0;
            block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
            ASSERT(block_bitmap_idx > 0);

            //回收位图
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
        }
        block_idx++;
    }

    /*2 回收该inode所占用的inode */
    bitmap_set(&part->inode_bitmap, inode_no, 0);
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    /******     以下inode_delete是调试用的    ******
     * 此函数会在inode_table中将此inode清0,
     * 但实际上是不需要的,inode分配是由inode位图控制的,
     * 硬盘上的数据不需要清0,可以直接覆盖，根据位图*/
    void *io_buf = sys_malloc(1024);
    inode_delete(part, inode_no, io_buf);
    sys_free(io_buf);
    /***********************************************/

    inode_close(inode_to_del);
}
