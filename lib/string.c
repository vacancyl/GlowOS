#include "string.h"
#include "debug.h"
#include "global.h"

/*将dst 起始的 size个字节置为value */
void memset(void* dst_,uint8_t value,uint32_t size)
{
    ASSERT(dst_ != NULL);
    uint8_t* dst = (uint8_t*) dst_;
    while((size--) > 0)
    	*(dst++) = value;
    return;
}

/*将src起始的size个字节复制到dst */
void memcpy(void* dst_,const void* src_,uint32_t size)
{
    ASSERT(dst_ != NULL && src_ != NULL);
    uint8_t* dst = dst_;
    const uint8_t* src = src_;
    while((size--) > 0)
    	*(dst++) = *(src++);
    return;
}

/*连续比较以地址a和地址b开头的size个字节，若相等则返回0, 若a(ascii)大于b, 返回+1, 否则返回-1*/
int memcmp(const void* a_,const void* b_, uint32_t size)
{
    const char* a = a_;
    const char* b = b_;
    ASSERT(a != NULL || b != NULL);
    while((size--) > 0)
    {
    	if(*a != *b)return (*a > *b) ? 1 : -1;
   	    ++a,++b;
    }
    return 0;
}

/*将字符串从src复制到dst*/
char* strcpy(char* dsc_,const char* src_)
{
    ASSERT(dsc_ != NULL && src_ != NULL);
    char* dsc = dsc_;
    while((*(dsc_++) = *(src_++) ));
    return dsc;     
}
/*返回字符串长度*/
uint32_t strlen(const char* str)
{
    ASSERT(str != NULL);
    const char* ptr = str;
    while(*(ptr++));
    return (ptr - str - 1);             //'h' | 'e' | 'l' | 'l' | 'o' | '\0' 因为最后ptr指向的是'\0'后面的，所以需要减1
}

/*比较两个字符串，若a中的字符大于b中的字符返回1, 相等时返回0, 否则返回-1*/
int8_t strcmp(const char* a,const char* b)
{
    ASSERT(a != NULL && b != NULL);
    while(*a && *a == *b)
    {
    	a++,b++;
    }   
    return (*a < *b) ? -1 : (*a > *b) ; 
}

/*从左到右查找字符串str中首次出现字符ch的地 址*/
char* strchr(const char* str,const char ch)
{
    ASSERT(str != NULL);
    while(*str)
    {
    	if(*str == ch)	return (char*)str;
    	++str;
    } 
    return NULL;
}

/*从后往前查找字符串 str 中首次出现字符ch 的地址*也就是最后一次出现的地址*/
char* strrchr(const char* str,const uint8_t ch)
{
    ASSERT(str != NULL);
    char* last_chrptr = NULL;
    while(*str != 0)
    {
    	if(ch == *str)	last_chrptr = (char*)str;
    	str++;
    }
    return last_chrptr;   
}

/*将字符串src拼接到dst后，返回拼接的串地址*/
char* strcat(char* dsc_,const char* src_)
{
    ASSERT(dsc_ != NULL && src_ != NULL);
    char* str = dsc_;
    while(*(str++));
    str--;//指向字符串结束符的位置
    while ((*(str++) = *(src_++)));
    return dsc_;
}

/*返回字符ch 在字符串 str 中出现的次数  */
uint32_t strchrs(const char* str,uint8_t ch)
{
    ASSERT(str != NULL);
    uint32_t ch_cnt = 0;
    while(*str)
    {
    	if(*str == ch) ++ch_cnt;
    	++str;
    }
    return ch_cnt;
}
