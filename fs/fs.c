#include "fs.h"
#include "stdint.h"
#include "inode.h"
#include "dir.h"
#include "super_block.h"
#include "stdio-kernel.h"
#include "string.h"
#include "debug.h"
#include "list.h"
#include "file.h"
#include "super_block.h"

struct partition *cur_part; // 默认操作分区

void print_sdb_info(struct partition *part)
{
     struct super_block* sb = part->sb;
    printk("    magic:0x%x\n    part_lba_base:0x%x\n    all_sectors:0x%x\n    \
inode_cnt:0x%x\n    block_bitmap_lba:0x%x\n    block_bitmap_sectors:0x%x\n    \
inode_bitmap_lba:0x%x\n    inode_bitmap_sectors:0x%x\n    \
inode_table_lba:0x%x\n    inode_table_sectors:0x%x\n    \
data_start_lba:0x%x\n",
           sb->magic, sb->part_lba_base, sb->sec_cnt, sb->inode_cnt, sb->block_bitmap_lba, sb->block_bitmap_sects,
           sb->inode_bitmap_lba, sb->inode_bitmap_sects, sb->inode_table_lba,
           sb->inode_table_sects, sb->data_start_lba);
}

// 格式化分区，初始化分区的元信息
void partition_format(struct disk *hd, struct partition *part)
{
    /*************************************根据分区part的大小，计算分区文件系统各元信息需要的扇区数及位置*********************************************/
    uint32_t boot_sector_sects = 1;                                                                      // 引导块一个块
    uint32_t super_block_sects = 1;                                                                      // 超级块一个块
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);                     // inode位图占的块数 总的文件数/每个扇区的位数
                                                                                                         // inode数组所占的块数
    uint32_t inode_table_sects = DIV_ROUND_UP((sizeof(struct inode) * MAX_FILES_PER_PART), SECTOR_SIZE); // 总大小除块大小 inode结构体总的大小/每个扇区的大小

    // 注意这里的used_sects还没有包含block_bitmap_sects 但是为了简单处理 要先得到free_sects才能推  所以到后面block_bitmap_sects 要除两次
    uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
    uint32_t free_sects = part->sec_cnt - used_sects;

    // 块位图占据的扇区数
    uint32_t block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);  // 一位一块 剩余的块的总数/每块的位数
    uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;          // 再减去block_bitmap占据的块
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR); // 更新 (A000 - A6D)空闲块数 / 4096 实际等于9，所以这里的位图实际是很合适的
    /***********************************************内存中创建超级快，写入元信息**************************************************/
    // 超级快初始化
    struct super_block sb;              // 利用栈来初始化超级块 我们的栈此刻在
    sb.magic = 0x19590318;              // 魔数
    sb.sec_cnt = part->sec_cnt;         // 该分区总扇区数
    sb.inode_cnt = MAX_FILES_PER_PART;  // 该分区总inode数
    sb.part_lba_base = part->start_lba; // 该分区lba起始扇区位置

    // 引导块 超级块 空闲块位图 inode位图 inode数组 根目录 空闲块区域
    sb.block_bitmap_lba = part->start_lba + boot_sector_sects + super_block_sects; // 空闲块位图
    sb.block_bitmap_sects = block_bitmap_sects;

    sb.inode_bitmap_lba = sb.block_bitmap_lba + block_bitmap_sects; // inode位图
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + inode_bitmap_sects; // inode数组
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + inode_table_sects;
    sb.root_inode_no = 0;                         // 根目录 inode数组种的第0个
    sb.dir_entry_size = sizeof(struct dir_entry); // 目录项大小

    printk("%s  info:\n", part->name);
    printk("    magic:0x%x\n    part_lba_base:0x%x\n    all_sectors:0x%x\n    \
inode_cnt:0x%x\n    block_bitmap_lba:0x%x\n    block_bitmap_sectors:0x%x\n    \
inode_bitmap_lba:0x%x\n    inode_bitmap_sectors:0x%x\n    \
inode_table_lba:0x%x\n    inode_table_sectors:0x%x\n    \
data_start_lba:0x%x\n",
           sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba, sb.block_bitmap_sects,
           sb.inode_bitmap_lba, sb.inode_bitmap_sects, sb.inode_table_lba,
           sb.inode_table_sects, sb.data_start_lba);

    /********************************************把超级快元信息写入本分区的1扇区******************************************************************/
    // 跨过引导扇区
    ide_write(hd, part->start_lba + boot_sector_sects, &sb, super_block_sects);
    printk("    super_block_lba:0x%x\n", part->start_lba + 1);

    // 找一个最大的数据缓冲区 我们的栈已经不足以满足我们的各种信息的储存了 这里是inodetable  0x260 * 512
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects) ? sb.block_bitmap_sects : sb.inode_bitmap_sects;
    buf_size = ((buf_size >= inode_table_sects) ? buf_size : inode_table_sects) * SECTOR_SIZE;

    // 申请缓冲空间之后通过buf给磁盘写入数据
    uint8_t *buf = (uint8_t *)sys_malloc(buf_size); // 所以这里是8字节

    /********************************************块位图写入******************************************************************/
    // 后面的多余的需要设置为1，反应状态
    buf[0] |= 0x1;                                                             // 先占位，根目录 占用一个位，0000 0001
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;                // 占用多少字节
    uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;                  // 最后还有剩余多少位
    uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE); // 不足一扇区的

    // 处理字节 把可能多的一字节全部置成1
    memset(&buf[block_bitmap_last_byte], 0xff, last_size); // 全部置1 保证不会被使用 也就是本来最后一字节是还有多余的

    // 处理最后的位 有效位变成0
    uint8_t bit_idx = 0;
    while (bit_idx <= block_bitmap_last_bit)
        buf[block_bitmap_last_byte] &= ~(1 << (bit_idx++)); // 有效位

    // 把位图元信息给写到硬盘中
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

    /********************************************inode位图写入******************************************************************/
    memset(buf, 0, buf_size);                                       // 缓冲区设置0
    buf[0] |= 0x1;                                                  // 第一个inode用于存根目录
    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects); // 第一个inode初始化在后面 inode刚好占用一个扇区

    /********************************************inode数组写入******************************************************************/
    //根目录所在的第0项
    memset(buf, 0, buf_size);
    struct inode *i = (struct inode *)buf; // 先初始化第一个inode 根目录所在的
    i->i_size = sb.dir_entry_size * 2;     //. 和 ..
    i->i_no = 0;                           //第0个node
    i->i_sectors[0] = sb.data_start_lba;   //根目录所在扇区就是最开始的第一个扇区

    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

    /********************************************根目录写入******************************************************************/
    memset(buf, 0, buf_size);
    struct dir_entry *p_de = (struct dir_entry *)buf;

    memcpy(p_de->filename, ".", 1); // 名称
    p_de->i_no = 0;                 // 根目录. inode仍然是自己
    p_de->f_type = FT_DIRECTORY;
    p_de++; // 移动到下一条目录项从root

    memcpy(p_de->filename, "..", 2);
    p_de->i_no = 0; // 根目录的父目录仍然是自己 因为自己是固定好的 根基
    p_de->f_type = FT_DIRECTORY;

    ide_write(hd, sb.data_start_lba, buf, 1); // 把根目录文件写到第一个扇区中

    printk("    root_dir_lba:0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);

    //释放缓冲区
    sys_free(buf); // 临时借用的 现在得还回去了
}

// 分区链表中找到part_name分区指针赋值给cur_part 初始化文件系统最后每个成员都要执行
bool mount_partition(struct list_elem *pelem, int arg)
{
    char *part_name = (char *)arg;
    struct partition *part = elem2entry(struct partition, part_tag, pelem); // 得到分区指针 partition*


    if (!strcmp(part->name, part_name))                                     // 字符串相匹配
    {
        cur_part = part; // 赋值指针
        struct disk *hd = cur_part->my_disk;

        //存储读入的超级块
        struct super_block *sb_buf = (struct super_block *)sys_malloc(SECTOR_SIZE);
        if (sb_buf == NULL)
            PANIC("alloc memory failed!");

        memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);
        //信息复制到当前快中
        cur_part->sb = sb_buf;

        //硬盘上的块位图读取
        cur_part->block_bitmap.bits = (uint8_t *)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
        if (cur_part->block_bitmap.bits == NULL)PANIC("alloc memory failed!");
        
        cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;
        ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);

        //inode位图读取
        cur_part->inode_bitmap.bits = (uint8_t *)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
        if (cur_part->inode_bitmap.bits == NULL)PANIC("alloc memory failed!");
        cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;
        ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);

        //返回true list_traversal才会停止遍历
        list_init(&cur_part->open_inodes);
        printk("mount %s done!\n", part->name);
        return true; // 停止循环
    }
    return false; // 继续便利
}

// 文件系统初始化 磁盘上搜索 如果没有则格式化分区 并创建文件系统
void filesys_init(void)
{
    uint8_t channel_no = 0, dev_no, part_idx = 0;
    struct super_block *sb_buf = (struct super_block *)sys_malloc(SECTOR_SIZE);

    if (sb_buf == NULL)PANIC("alloc memory failed!");
    printk("searching filesysteam......\n");
    while (channel_no < channel_cnt)
    {
        dev_no = 0;
        while (dev_no < 2)
        {
            if (0 == dev_no) // 跳过hd60M.img主盘
            {
                ++dev_no;
                continue;
            }

            struct disk *hd = &channels[0].devices[1]; // 得到硬盘指针
            struct partition *part = hd->prim_parts;   // 先为主区创建文件系统

            while (part_idx < 12)                      // 4个主区 + 8个逻辑分区
            {
                if (part_idx == 4) part = hd->logic_parts;
                if (part->sec_cnt != 0) // 分区存在 如果没有初始化 即所有成员都为0
                {
                    memset(sb_buf, 0, SECTOR_SIZE);
                    ide_read(hd, part->start_lba + 1, sb_buf, 1); // 读取超级块的扇区

                    if (sb_buf->magic != 0x19590318) // 还没有创建文件系统
                    {
                        printk("formatting %s's partition %s......\n",
                               hd->name, part->name);
                        partition_format(hd, part);
                    }
                    else
                        printk("%s has filesystem\n", part->name);
                }
                ++part_idx;
                ++part; // 到下一个分区看
            }
            ++dev_no; // 切换盘号
        }
        ++channel_no; // 增加ide通道号
    }

    sys_free(sb_buf);
    char default_part[8] = "sdb1"; // 参数为int 4字节字符串指针传的进去
    list_traversal(&partition_list, mount_partition, (int)default_part);
    print_sdb_info(cur_part);

    open_root_dir(cur_part);//打开当前分区的根目录

    //初始化文件表
    uint32_t fd_idx = 0;
    while(fd_idx < MAX_FILE_OPEN)
    {
        file_table[fd_idx++].fd_inode = NULL;
    }
}

//解析路径 并把下一级路径的字符串赋值给name_store 返回现在已经解析完的指针位置
char* path_parse(char* pathname,char* name_store)
{

    if(pathname[0] == 0)return NULL;

    if(pathname[0] == '/')//根目录不需要解析，一个或者多个
        while(*(++pathname) == '/');	//直到pathname位置不是

    while(*pathname != '/' && *pathname != '\0')
    	*(name_store++) = *(pathname++);//赋值返回
    	
    return pathname;
}

//返回路径的层数 
int32_t path_depth_cnt(char* pathname)
{
    ASSERT(pathname != NULL);
    char* p = pathname;
    char name[MAX_FILE_NAME_LEN];
    
    uint32_t depth = 0;
    
    p = path_parse(p,name);
    while(name[0])
    {
    	++depth;
    	memset(name,0,MAX_FILE_NAME_LEN);
    	if(p)
    	    p = path_parse(p,name);
    }
    return depth;
}

//搜索文件 找到则返回inode号 否则返回-1 pathname是全路径
int search_file(const char* pathname,struct path_search_record* searched_record)
{
    //如果是根目录 则直接判定返回即可 下面的工作就不需要做了
    if(!strcmp(pathname,"/") || !strcmp(pathname,"/.") || !strcmp(pathname,"/.."))
    {
    	searched_record->parent_dir = &root_dir;
    	searched_record->file_type  = FT_DIRECTORY;
    	searched_record->searched_path[0] = 0;		//置空
    	return 0;	//根目录inode编号为0
    }
    
    uint32_t path_len = strlen(pathname);
    
    //保证目录类似 /x
    ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);		
    char* sub_path = (char*)pathname;
    struct dir* parent_dir = &root_dir;			//每个刚开始都是从根目录开始
    struct dir_entry dir_e;					    //存放目录项的临时变量
    
    //记录解析出来的各级名称 /a/b/c a b c 
    char name[MAX_FILE_NAME_LEN] = {0};
    
    searched_record->parent_dir = parent_dir;
    searched_record->file_type  = FT_UNKNOWN;
    uint32_t parent_inode_no = 0;				//父目录的inode号
    
    sub_path = path_parse(sub_path,name);			//解析目录
    while(name[0])
    {
    	ASSERT(strlen(searched_record->searched_path) < 512);
    	strcat(searched_record->searched_path,"/");
    	strcat(searched_record->searched_path,name);
    	
        //在根目录中递归查找
    	if(search_dir_entry(cur_part,parent_dir,name,&dir_e))
    	{
    	    memset(name,0,MAX_FILE_NAME_LEN);
    	    if(sub_path)    sub_path = path_parse(sub_path,name);   //继续拆分
    	    
    	    if(FT_DIRECTORY == dir_e.f_type)	//打开的是目录继续解析即可
    	    {
    	    	
    	    	parent_inode_no = parent_dir->inode->i_no;//记录上一层，最后需要回到上一层 最后的文件可能是，目录
    	    	dir_close(parent_dir);//关闭上一级目录
    	    	parent_dir = dir_open(cur_part,dir_e.i_no);//更新当前找到的目录为父目录
    	    	searched_record->parent_dir = parent_dir;
    	    }
    	    else if(FT_REGULAR == dir_e.f_type)
    	    {
    	    	searched_record->file_type = FT_REGULAR;
    	    	return dir_e.i_no;
    	    }
    	}
    	else 	return -1;//父目录不要关闭。可能要创建
    }
    
    dir_close(searched_record->parent_dir);
    searched_record->parent_dir = dir_open(cur_part,parent_inode_no);
    searched_record->file_type = FT_DIRECTORY;
    return dir_e.i_no;
}

//打开或者创建文件之后，返回其文件描述符
int32_t sys_open(const char* pathname,uint8_t flags)
{
    //最后一位是'/'则无法辨析 这里是打开文件
    if(pathname[strlen(pathname) - 1] == '/')
    {
    	printk("cant open a directory %s\n",pathname);
    	return -1;
    }
    ASSERT(flags <= 7);
    int32_t fd = -1;
    
    struct path_search_record searched_record;	     //记录访问记录
    memset(&searched_record,0,sizeof(struct path_search_record));
    
    uint32_t pathname_depth = path_depth_cnt((char*)pathname);//记录目录深度
    
    int inode_no = search_file(pathname,&searched_record); //搜索文件
    bool found = (inode_no == -1) ? false : true;
    
    if(searched_record.file_type == FT_DIRECTORY)	     //如果是目录文件类型
    {
    	printk("cant open a directory with open(),use opendir() to instead\n");
    	dir_close(searched_record.parent_dir);
    	return -1;
    }
    
    uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
    
    //判断是否在中间某个目录失败 他俩记录的路径应该是一杨的
    if(pathname_depth != path_searched_depth)
    {
    	printk("cannot access %s:Not a directory,subpath %s isnt exist\n",pathname,searched_record.searched_path);
    	dir_close(searched_record.parent_dir);
    	return -1;	
    }
    //没找到且不创建文件
    if(!found && !(flags & O_CREAT))
    {
    	printk("in path %s: Not a directory, subpath %s isnt exist\n",pathname,searched_record.searched_path);
    	dir_close(searched_record.parent_dir);
    	return -1;
    }
    else if(found && (flags & O_CREAT))//创建的文件已经存在
    {
    	printk("%s has already exist!\n",pathname);
    	dir_close(searched_record.parent_dir);
    	return -1;
    }
    
    switch(flags & O_CREAT)
    {
    	case O_CREAT:
    	    printk("creating file\n");
    	    fd = file_create(searched_record.parent_dir,strrchr(pathname,'/') + 1,flags);
    	    dir_close(searched_record.parent_dir);
            break;
        default://其他的都是打开已经存在的文件
            fd = file_open(inode_no,flags);
    }
    
    return fd;//pcb中的fd
}

// 文件描述符（pcb）转换为文件表（全局）的下标
uint32_t fd_local2global(uint32_t local_fd)
{
    struct task_struct* cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd >= 0 && global_fd <= MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}

//关闭pcb文件描述符fd的文件 成功返回0 失败-1
int32_t sys_close(int32_t fd)
{
    int32_t ret = -1;	//返回值默认为-1
    if(fd > 2)		// 0 1都是标准输入 输出
    {
    	uint32_t _fd = fd_local2global(fd);
    	ret = file_close(&file_table[_fd]);
    	running_thread()->fd_table[fd] = -1;	//pcb fd恢复可用 可以继续分配
    }
    return ret;
}

//buf中连续的count字节写到文件描述符fd 成功返回写入的字节数 失败返回-1
int32_t sys_write(int32_t fd,const void* buf,uint32_t count)
{
    if(fd < 0)
    {
        printk("sys_write: fd error\n");
        return -1;
    }
    if(fd == stdout_no)
    {
        char tmp_buf[1024] = {0};
        memcpy(tmp_buf,buf,count);
        console_put_str(tmp_buf);
        return count;
    }
    
    uint32_t _fd = fd_local2global(fd);
    struct file* wr_file = &file_table[_fd];
    if(wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR)
    {
        uint32_t bytes_written = file_write(wr_file,buf,count);
        return bytes_written;
    }
    else
    {
        console_put_str("sys_write: not allowed to write file without flag O_RDWR or O_WRONLY\n");
        return -1;
    }
}

//从文件描述符fd中读取count个字节到buf成功返回读取的字节数 到文件尾返回-1
int32_t sys_read(int32_t fd,void* buf,uint32_t count)
{
    if(fd < 0)
    {
    	printk("sys_read: fd error\n");
    	return -1;
    }
    ASSERT(buf != NULL);
    uint32_t _fd = fd_local2global(fd);
    return file_read(&file_table[_fd],buf,count);
}


//重置文件的读写指针 成功返回偏移 失败返回-1
int32_t sys_lseek(int32_t fd,int32_t offset,uint8_t whence)
{
    if(fd < 0)
    {
        printk("sys_lseek: fd error\n");
        return -1;
    }
    ASSERT(whence > 0 && whence < 4);
    uint32_t _fd = fd_local2global(fd);
    struct file* file = &file_table[_fd];
    int32_t new_pos = 0;
    int32_t file_size = (int32_t)file->fd_inode->i_size;
    
    switch(whence)
    {
        case SEEK_SET:
            new_pos = offset; //相对文件开始的偏移
            break;
        case SEEK_CUR:
            new_pos = offset + (int32_t)file->fd_pos;
            break;
        case SEEK_END:
            new_pos = offset + (int32_t)file_size;
            break;    
    }
    if(new_pos < 0 || new_pos > (file_size -1))
    	return -1;
   
    file->fd_pos = new_pos;
    return file->fd_pos;
}


//删除非目录文件 成功返回0 失败返回-1
int32_t sys_unlink(const char* pathname)
{
    ASSERT(strlen(pathname) < MAX_PATH_LEN);
    
    //查找文件
    struct path_search_record searched_record;
    memset(&searched_record,0,sizeof(struct path_search_record));
    int inode_no = search_file(pathname,&searched_record);
    ASSERT(inode_no != 0);
    if(inode_no == -1)
    {
        printk("file %s not found!\n",pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }
    if(searched_record.file_type == FT_DIRECTORY)
    {
        printk("cant delete a directory with unlink(),use rmdir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }
    
    //检查在不在已经打开文件列表 如果在的话就说明 正在被打开 不能删除
    uint32_t file_idx  = 0;
    while(file_idx < MAX_FILE_OPEN)
    {
        if(file_table[file_idx].fd_inode != NULL && (uint32_t)inode_no == file_table[file_idx].fd_inode->i_no)
            break;
        file_idx++;
    }
    if(file_idx < MAX_FILE_OPEN)
    {
        dir_close(searched_record.parent_dir);
        printk("file %s is in use , not allowed to delete!\n",pathname);
        return -1;
    }
    
    ASSERT(file_idx == MAX_FILE_OPEN);
    
    void* io_buf = sys_malloc(1024);
    if(io_buf == NULL)
    {
        dir_close(searched_record.parent_dir);
        printk("sys_unlink: malloc for io_buf failed!\n");
        return -1;
    }
    
    struct dir* parent_dir = searched_record.parent_dir;
    delete_dir_entry(cur_part,parent_dir,inode_no,io_buf);//删除目录项
    inode_release(cur_part,inode_no);			    //删除inode
    sys_free(io_buf);
    dir_close(searched_record.parent_dir);
    return 0;	//成功删除
    
}

//创建目录 成功返回0 失败返回-1
int32_t sys_mkdir(const char* pathname)
{
    uint8_t rollback_step = 0;
    void* io_buf = sys_malloc(SECTOR_SIZE * 2);
    if(io_buf == NULL)
    {
        printk("sys_mkdir: sys_malloc for io_buf failed\n");
        return -1;
    }
    
    struct path_search_record searched_record;
    memset(&searched_record,0,sizeof(struct path_search_record));
    int inode_no = -1;
    inode_no = search_file(pathname,&searched_record);//目录肯定是打开的 所以后面step = 1要关闭
    if(inode_no != -1)	//如果找到同名的 目录或者文件 失败返回
    {
        printk("sys_mkdir: file or directory %s exist\n",pathname);
        rollback_step = 1;
        goto rollback;
    }
    else
    {
        uint32_t pathname_depth = path_depth_cnt((char*)pathname); 
        uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
        //看深度是否相同 不相同说明中间目录失败了
        if(pathname_depth != path_searched_depth)
        {
            printk("sys_mkdir: cannot access %s: Not a directory,subpath %s isnt exist\n",pathname,searched_record.searched_path);
            rollback_step = 1;
            goto rollback;
        }
    }
    
    struct dir* parent_dir = searched_record.parent_dir; //得到最后路径的直接父路径
    char* dirname = strrchr(searched_record.searched_path,'/') + 1; //得到最后目标目录名称
    inode_no = inode_bitmap_alloc(cur_part);  //得到新分配的inode结点 

    if(inode_no == -1)
    {
        printk("sys_mkdir: allocate inode failed\n");
        rollback_step = 1;
        goto rollback;
    }
    
    //即将要使用的inode 负责memcpy到硬盘中
    struct inode new_dir_inode;
    inode_init(inode_no,&new_dir_inode);
    
    //再分配一个块
    uint32_t block_bitmap_idx = 0;
    int32_t  block_lba = -1;
    block_lba = block_bitmap_alloc(cur_part);

    if(block_lba == -1)
    {
        printk("sys_mkdir: block_bitmap_alloc for create directory failed\n");
        rollback_step = 2; //把之前的inode给回滚了 inode 和打开目录项
        goto rollback;
    }   

    //初始化第一个数据块
    new_dir_inode.i_sectors[0] = block_lba;

    //分配的块位图写入
    block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    bitmap_sync(cur_part,block_bitmap_idx,BLOCK_BITMAP);
    
    //'.' 和 '..'写入目录中
    memset(io_buf,0,SECTOR_SIZE * 2);
    struct dir_entry* p_de = (struct dir_entry*)io_buf;
    
    //初始化当前目录 .
    memcpy(p_de->filename,".",1);
    p_de->i_no = inode_no;
    p_de->f_type = FT_DIRECTORY;
    
    //初始化父目录 移动到下一个目录项的位置 ..
    p_de++;  
    memcpy(p_de->filename,"..",2);
    p_de->i_no = parent_dir->inode->i_no;
    p_de->f_type = FT_DIRECTORY;
    ide_write(cur_part->my_disk,new_dir_inode.i_sectors[0],io_buf,1);
    
    new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;//父目录和当前目录项
    
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry,0,sizeof(struct dir_entry));
    memset(io_buf,0,SECTOR_SIZE * 2);
    //目录项存放在new_dir_entry中 new_inode已经初始化好了 里面的数据也都填了
    create_dir_entry(dirname,inode_no,FT_DIRECTORY,&new_dir_entry); 
    
    //失败了
    if(!sync_dir_entry(parent_dir,&new_dir_entry,io_buf))//这个目录项写入到父目录中
    {
        printk("sys_mkdir: sync_dir_entry to disk_failed\n");
        rollback_step = 2;
        goto rollback;
    }
    
    //父结点的inode写入到硬盘中
    memset(io_buf,0,SECTOR_SIZE * 2);
    inode_sync(cur_part,parent_dir->inode,io_buf);
    
    //新建立的写入到硬盘中
    memset(io_buf,0,SECTOR_SIZE * 2);
    inode_sync(cur_part,&new_dir_inode,io_buf);
    
    //inode位图同步到硬盘
    bitmap_sync(cur_part,inode_no,INODE_BITMAP);
    
    sys_free(io_buf);
    dir_close(searched_record.parent_dir);
    return 0;
    
    rollback:
        switch(rollback_step)
        {
            case 2:
                bitmap_set(&cur_part->inode_bitmap,inode_no,0);
            case 1:
                dir_close(searched_record.parent_dir);
                break;
        }
        sys_free(io_buf);
        return -1;


}





//打开指定目录 返回其目录指针 失败返回NULL
struct dir* sys_opendir(const char* name)
{
    //如果是根目录
    ASSERT(strlen(name) < MAX_PATH_LEN);
    if(name[0] == '/' && (name[1] == 0 || name[0] == '.'))// /. /.. /
        return &root_dir;
        
    struct path_search_record searched_record;
    memset(&searched_record,0,sizeof(struct path_search_record));
    int inode_no = search_file(name,&searched_record);
    struct dir* ret = NULL;
    if(inode_no == -1) 
    {
        printk("sys_opendir:In %s,sub path %s not exist\n",name,searched_record.searched_path);
        return NULL;
    }
    else
    {
        if(searched_record.file_type == FT_REGULAR)
            printk("%s is regular file!\n",name);
        else if(searched_record.file_type == FT_DIRECTORY)
            ret = dir_open(cur_part,inode_no);
    }    
    dir_close(searched_record.parent_dir);
    return ret;
}


/* 成功关闭目录dir返回0,失败返回-1 */
int32_t sys_closedir(struct dir* dir) {
   int32_t ret = -1;
   if (dir != NULL) {
      dir_close(dir);
      ret = 0;
   }
   return ret;
}

//读取目录的一个目录项，成功返回目录项地址，失败返回NULL（结尾或者出错）
struct dir_entry* sys_readdir(struct dir* dir)
{
    ASSERT(dir != NULL);
    return dir_read(dir); //返回读的地址	
}

//目录指针位置dir_pos置0
void sys_rewinddir(struct dir* dir)
{
    dir->dir_pos = 0;
}

//删除空目录项 成功返回0 失败-1
int32_t sys_rmdir(const char* pathname)
{
    //检查文件是否存在
    struct path_search_record searched_record;
    memset(&searched_record,0,sizeof(struct path_search_record));
    int inode_no = search_file(pathname,&searched_record);
    ASSERT(inode_no != 0);
    int retval = -1;
    if(inode_no == -1)
        printk("In %s,sub path %s not exist\n",pathname,searched_record.searched_path);
    else
    {
        if(searched_record.file_type == FT_REGULAR)
        {
            printk("%s is regular file!\n",pathname);
        }
        else
        {
            struct dir* dir = dir_open(cur_part,inode_no);
            if(!dir_is_empty(dir)) //必须要是空目录
                printk("dir %s is not empty,it is not allowed to delete a nonempty  directory!\n",pathname);
            else
            {
                if(!dir_remove(searched_record.parent_dir,dir))
                    retval = 0;
            }
        }
    }
    dir_close(searched_record.parent_dir);
    return retval;
}

//得到父目录的inode编号
uint32_t get_parent_dir_inode_nr(uint32_t child_inode_nr,void* io_buf)
{
    struct inode* child_dir_inode = inode_open(cur_part,child_inode_nr);
    //. 和 ..位于目录的第0块
    uint32_t block_lba = child_dir_inode->i_sectors[0];
    ASSERT(block_lba >= cur_part->sb->data_start_lba);
    inode_close(child_dir_inode);

    ide_read(cur_part->my_disk,block_lba,io_buf,1);
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    ASSERT(dir_e[1].i_no < 4096 && dir_e[1].f_type == FT_DIRECTORY);
    return dir_e[1].i_no;
}

//在inode编号为p_inode_nr的目录中寻找编号c_inode_nr子目录的名字 且把路径存到path中 成功返回0 失败返回-1
int get_child_dir_name(uint32_t p_inode_nr,uint32_t c_inode_nr,char* path,void* io_buf)
{
    struct inode* parent_dir_inode = inode_open(cur_part,p_inode_nr);
    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0},block_cnt = 12;
    
    while(block_idx < 12)
    {
        all_blocks[block_idx] = parent_dir_inode->i_sectors[block_idx];
        ++block_idx;
    }
    //有间接块的话 直接读
    if(parent_dir_inode->i_sectors[12])
    {
        ide_read(cur_part->my_disk,parent_dir_inode->i_sectors[12],all_blocks + 12,1);
        block_cnt = 140;
    }
    inode_close(parent_dir_inode);
    
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = (512 / dir_entry_size);
    block_idx = 0;
    
    while(block_idx < block_cnt)
    {
        if(all_blocks[block_idx])
        {
            memset(io_buf,0,SECTOR_SIZE);
            ide_read(cur_part->my_disk,all_blocks[block_idx],io_buf,1);
            uint8_t dir_e_idx = 0;
            while(dir_e_idx < dir_entrys_per_sec)
            {
                if((dir_e_idx + dir_e)->i_no == c_inode_nr)
                {
                    strcat(path,"/");
                    strcat(path,(dir_e + dir_e_idx)->filename);
                    return 0;
                }
                ++dir_e_idx;
            }
            ++block_idx;
        }
    }
    return -1;
}

//当前工作目录写入buf 如果buf为NULL 则操作系统提供并写入
char* sys_getcwd(char* buf,uint32_t size)
{
    //确保buf不空
    ASSERT(buf != NULL);
    void* io_buf = sys_malloc(SECTOR_SIZE);
    if(io_buf == NULL)
        return NULL;
        
    struct task_struct* cur_thread = running_thread();
    int32_t parent_inode_nr = 0;
    int32_t child_inode_nr = cur_thread->cwd_inode_nr;
    ASSERT(child_inode_nr >= 0 && child_inode_nr < 4096);

    //当前是跟目录
    if(child_inode_nr == 0)
    {
        buf[0] = '/';
        buf[1] = 0;
        return buf;
    }
    
    memset(buf,0,size);
    char full_path_reverse[MAX_PATH_LEN] = {0};
    //从下一直找，只到找到更目录 inode = 0
    while((child_inode_nr))
    {
        parent_inode_nr = get_parent_dir_inode_nr(child_inode_nr,io_buf); //得到工作目录父结点
        //如果失败了 则退出
        if(get_child_dir_name(parent_inode_nr,child_inode_nr,full_path_reverse,io_buf) == -1)
        {
            sys_free(io_buf);
            return NULL;
        }
        child_inode_nr = parent_inode_nr; //子结点inode数等于父结点的 然后一路递归上去 则路径就是从子->父
    }
    ASSERT(strlen(full_path_reverse) <= size);
    
    char* last_slash;
    //倒着把路径给录入进去 通过get_child_dir_name写的路径是反的 因为从下往上
    while((last_slash = strrchr(full_path_reverse,'/') ))
    {
        uint16_t len = strlen(buf);
        strcpy(buf + len,last_slash);
        *last_slash = 0;
    }
    sys_free(io_buf);
    return buf;
}

//更改当前的工作目录
int32_t sys_chdir(const char* path)
{
    int32_t ret = -1;
    struct path_search_record searched_record;
    memset(&searched_record,0,sizeof(struct path_search_record));
    int inode_no = search_file(path,&searched_record);
    //找到了就继续
    if(inode_no != -1)
    {
        if(searched_record.file_type == FT_DIRECTORY)
        {
            //切换工作cwd_inode_nr
            running_thread()->cwd_inode_nr = inode_no;
            ret = 0;
        }
        else
            printk("sys_chdir: %s is regular file or other!\n");
    }
    else
    {
        printk("cant find path\n");
    }
    dir_close(searched_record.parent_dir);
    return ret;   
}


//在buf中填充文件结构相关信息 成功返回0 失败返回-1
int32_t sys_stat(const char* path,struct stat* buf)
{
    //如果是根目录
    if(!strcmp(path,"/") || !strcmp(path,"/.") || !strcmp(path,"/.."))
    {
        buf->st_filetype = FT_DIRECTORY;
        buf->st_ino = 0;
        buf->st_size = root_dir.inode->i_size;
    }
    
    int32_t ret = -1;
    struct path_search_record searched_record;
    memset(&searched_record,0,sizeof(struct path_search_record));
    
    int inode_no = search_file(path,&searched_record);
    if(inode_no != -1)
    {
        struct inode* obj_inode = inode_open(cur_part,inode_no);
        buf->st_size = obj_inode->i_size;
        buf->st_filetype = searched_record.file_type;
        buf->st_ino = inode_no;
        inode_close(obj_inode);
        ret = 0;
    }
    else
        printk("sys_stat: %s not found\n",path);
    
    dir_close(searched_record.parent_dir);
    return ret;
}
