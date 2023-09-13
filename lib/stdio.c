#include "stdio.h"
#include "stdint.h"
#include "string.h"
#include "syscall.h"
#include "global.h"


/*
在函数实现中要将转换后的字符写到缓冲区指针指向的缓冲区中的1个或多个位置， 这取决于进制转换后的数值的位数，
比如十六进制0xd 转换成十进制后变成数值13,13要被转换成字符
'1和3',所以数值13变成字符后将占用缓冲区中两个字符位置，字符写到哪里是由缓冲区指针决定的，因 此每写一个字符到缓冲区后，
要更新缓冲区指针的值以使其指向缓冲区中下一个可写入的位置，
这种原地修 改指针的操作，最方便的是用其下一级指针类型来保存此指针的地址，
故将一级指针的地址作为参数传给二级指针 buf ptr addr, 这样便于原地修改一级指针
*/
//整形转换成字符 int ascii buf_ptr_addr保存转化结果的缓冲区指针 base转换到基数 数制 buf会自动的移动到下一个位置
void itoa(uint32_t value,char** buf_ptr_addr,uint8_t base)
{
    uint32_t m = value % base;
    uint32_t i = value / base;  //除数为0即最高位了 输出即可 没到零继续即可
    if(i)
    	itoa(i,buf_ptr_addr,base);
    if(m < 10)                  //m小于10的数
    	*((*buf_ptr_addr)++) = m + '0';
    else			 //m大于10的数
    	*((*buf_ptr_addr)++) = m + 'A' - 10;
}

//把参数ap按照格式format输出到字符串str ，然后返回替换后str长度
uint32_t vsprintf(char* str,const char* format,va_list ap)
{
    char* buf_ptr = str;
    const char* index_ptr = format;
    char index_char = *index_ptr;
    int32_t arg_int;
    char* arg_str;

    while(index_char)		//直到字符串结束符
    {
    	if(index_char != '%')
    	{
    	    *(buf_ptr++) = index_char;
    	    index_char = *(++index_ptr);
    	    continue;
    	}
    	
        //如果是等于%,那么就需要取下一个然后判断
        index_char = *(++index_ptr);

    	switch(index_char)
    	{
    	    case 's':
    	    	arg_str = va_arg(ap,char*);
    	    	strcpy(buf_ptr,arg_str);
    	    	buf_ptr += strlen(arg_str);
    	    	index_char = *(++index_ptr);
    	    	break;
    	    case 'x':
    	    	arg_int = va_arg(ap,int);
    	    	itoa(arg_int,&buf_ptr,16);
    	    	index_char = *(++index_ptr);
    	    	break;
    	    case 'd':
    	    	arg_int = va_arg(ap,int);
    	    	if(arg_int < 0)
    	    	{
    	    	    arg_int = 0 - arg_int;
    	    	    *(buf_ptr++) = '-';
    	    	}
    	    	itoa(arg_int,&buf_ptr,10);
    	    	index_char = *(++index_ptr);
    	    	break;
    	    case 'c':
    	    	*(buf_ptr++) = va_arg(ap,char);
    	    	index_char = *(++index_ptr);
    	}
    }
    return strlen(str);
}

uint32_t printf(const char* format, ...)
{
    va_list args;
    uint32_t retval;
    va_start(args,format);		//args指向char* 的指针 方便指向下一个栈参数
    char buf[1024] = {0};
    retval = vsprintf(buf,format,args);
    va_end(args);
    write(1,buf,strlen(buf));
    return retval;
}

uint32_t sprintf(char* _des,const char* format, ...)
{
    va_list args;
    uint32_t retval;
    va_start(args,format);		//args指向char* 的指针 方便指向下一个栈参数
    retval = vsprintf(_des,format,args);
    va_end(args);
    return retval;
}
