#ifndef __LIB_STDINT_H
#define __LIB_STDINT_H
typedef signed char                     int8_t;
typedef signed short int                int16_t;
typedef signed int                      int32_t;
typedef signed long long int            int64_t;
typedef unsigned char                   uint8_t;    //signed: 这是默认的符号修饰符，通常可以省略。使用signed修饰的整数类型可以表示正数、负数和零
typedef unsigned short int              uint16_t;
typedef unsigned int                    uint32_t;
typedef unsigned long long int          uint64_t;   //long可能32位，long long 64位
#endif
