#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H
#include "stdint.h"

/*
(struct_type*)0: 将数字0强制类型转换为指向struct_type类型的指针，相当于创建了一个虚拟的结构体对象，地址为0
*/
#define offset(struct_type,member) (int) (&((struct_type*)0)->member)          
/*
成员地址减去成员偏移量便是结构体的位置
*/
#define elem2entry(struct_type,struct_member_name,elem_ptr) \
	(struct_type*)((int)elem_ptr - offset(struct_type,struct_member_name))


struct list_elem
{
    struct list_elem* prev; //前面的节点
    struct list_elem* next; //后面的节点
};

struct list
{
    struct list_elem head; //头部
    struct list_elem tail; //尾部
};

typedef int (function) (struct list_elem*,int arg);

void list_init(struct list*);
void list_insert_before(struct list_elem* before,struct list_elem* elem);
void list_push(struct list* plist,struct list_elem* elem);
void list_append(struct list* plist,struct list_elem* elem);
void list_remove(struct list_elem* pelem);
struct list_elem* list_pop(struct list* plist);
int list_empty(struct list* plist);
uint32_t list_len(struct list* plist);
struct list_elem* list_traversal(struct list* plist,function func,int arg);
int elem_find(struct list* plist,struct list_elem* obj_elem);

#endif