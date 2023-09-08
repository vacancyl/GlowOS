#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "list.h"
#include "global.h"
#include "stdint.h"
#include "ide.h"


struct inode//
{
    uint32_t i_no;	     		    //inode 编号
    uint32_t i_size;	     		//文件大小 或者 目录项总大小 字节
    
    uint32_t i_open_cnts;   		//记录此文件被打开的次数 关闭时回收资源
    bool write_deny;	     		//写文件不能并行 写文件之前检查这个标志
    
    uint32_t i_sectors[13]; 		//这里只实现了一级简介块 12为一级间接块指针 0-11直接是inode编号 扇区512 块索引4字节 128块 总140 扇区地址
    struct list_elem inode_tag;	    //已经打开的inode列表 从硬盘读取速率太慢 此list做缓冲用 当第二次使用时如果list中有
    					            //直接通过list_elem得到inode而不用再读取硬盘
};

//存储inode的位置
struct inode_position
{
    bool two_sec;			    //是否inode存储位置在两个扇区间
    uint32_t sec_lba;			//inode所在的扇区号
    uint32_t off_size;			//在所存储的扇区的偏移位置
};

void inode_locate(struct partition* part,uint32_t inode_no,struct inode_position* inode_pos);
void inode_sync(struct partition* part,struct inode* inode,void* io_buf);
struct inode* inode_open(struct partition* part,uint32_t inode_no);
void inode_close(struct inode* inode);
void inode_init(uint32_t inode_no,struct inode* new_inode);


#endif
