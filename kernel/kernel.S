[bits 32]
%define ERROR_CODE nop		        
;若在相关的异常中cpu已经自动压入了错误码,为保持栈中格式统一,这里不做操作.运行该指令时单片机什么都不做，但是会占用一个指令的时间。
%define ZERO push 0		            
;若在相关的异常中cpu没有压入错误码,为了统一栈中格式,就手工压入一个0

                                            ;声明外部函数
extern put_str;
extern idt_table;


section .data                               ;把下面的data中的内容合并到此处后面
intr_str db "interupt occur", 0xa,0         ;换行和结束

global intr_entry_table
intr_entry_table:                           ;不懂

%macro VECTOR 2
section .text
intr%1entry:		                        ;每个中断处理程序都要压入中断向量号,所以一个中断类型一个中断处理程序，自己知道自己的中断向量号是多少

    %2				                        ;中断若有错误码会压在eip后面 
                                            ;以下是保存上下文环境
    push ds
    push es
    push fs                                 ;再次被调度到的时候恢复
    push gs                     
    pushad			                        ; PUSHAD指令压入32位寄存器,其入栈顺序是: EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
                                            ; 要调用C程序，一定会使当前寄存器环境破坏，所以要保存当前所使用的寄存器环境

                                            ;如果是从片上进入的中断,除了往从片上发送EOI外,还要往主片上发送EOI 
    mov al,0x20                             ;中断结束命令EOI 0010 0000
    out 0xa0,al                             ;向从片发送
    out 0x20,al                             ;向主片发送

    push %1			                        ; 不管idt_table中的目标程序是否需要参数,都一律压入中断向量号,调试时很方便
    call [idt_table + %1*4]                 ; 调用idt_table中的C版本中断处理函数
    jmp intr_exit
	
section .data
   dd    intr%1entry	                    ;存储各个中断入口程序的地址，形成intr_entry_table数组,%1会替换


%endmacro


section .text
global intr_exit
intr_exit:	     
                                            ; 以下是恢复上下文环境
    add esp, 4			                    ; 跳过中断号
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 4			                    ; 跳过error_code
    iretd

[bits 32]
extern syscall_table
section .text 
global syscall_handler
syscall_handler:
;保存上下文环境
    push 0                  ;格式统一
    
    push ds
    push es
    push fs
    push gs
    pushad                  ;a c d b sp bp si di

    push 0x80			    ;压入中断向量号 统一格

;传递参数
    push edx			     ;第三个参数
    push ecx			     ;第二个参数
    push ebx			     ;第一个参数

;调用处理函数
    call [syscall_table + eax*4]
    add  esp,12             ;跨过三个参数

;调用后的返回值放到eax
    mov [esp + 8*4],eax	    ;返回值给放到eax中 eax前面有 中断号+EDI+ESI+EBP+ESP+EBX+EDX+ECX 
    jmp intr_exit



                                            ;值为Y, 表示相应中断会由CPU 压入错误码
VECTOR 0x00 ,ZERO
VECTOR 0X01 ,ZERO
VECTOR 0X02 ,ZERO
VECTOR 0x03 ,ZERO
VECTOR 0X04 ,ZERO
VECTOR 0X05 ,ZERO
VECTOR 0x06 ,ZERO
VECTOR 0X07 ,ZERO
VECTOR 0X08 ,ERROR_CODE
VECTOR 0x09 ,ZERO
VECTOR 0X0A ,ERROR_CODE
VECTOR 0X0B ,ERROR_CODE
VECTOR 0X0C ,ERROR_CODE
VECTOR 0X0D ,ERROR_CODE
VECTOR 0X0E ,ERROR_CODE
VECTOR 0X0F ,ZERO
VECTOR 0X10 ,ZERO
VECTOR 0X11 ,ERROR_CODE
VECTOR 0x12 ,ZERO
VECTOR 0X13 ,ZERO
VECTOR 0X14 ,ZERO
VECTOR 0x15 ,ZERO
VECTOR 0X16 ,ZERO
VECTOR 0X17 ,ZERO
VECTOR 0X18 ,ZERO
VECTOR 0X19 ,ZERO
VECTOR 0X1A ,ZERO
VECTOR 0X1B ,ZERO
VECTOR 0X1C ,ZERO
VECTOR 0X1D ,ZERO
VECTOR 0X1E ,ERROR_CODE                               
VECTOR 0X1F ,ZERO
VECTOR 0X20 ,ZERO					;时钟中断
VECTOR 0X21 ,ZERO					;键盘中断
VECTOR 0X22 ,ZERO					;级联
VECTOR 0X23 ,ZERO					;串口2
VECTOR 0X24 ,ZERO					;串口1
VECTOR 0X25 ,ZERO					;并口2
VECTOR 0X26 ,ZERO					;软盘
VECTOR 0X27 ,ZERO					;并口1
VECTOR 0X28 ,ZERO					;实时时钟
VECTOR 0X29 ,ZERO					;重定向
VECTOR 0X2A ,ZERO					;保留
VECTOR 0x2B ,ZERO					;保留
VECTOR 0x2C ,ZERO					;ps/2 鼠标
VECTOR 0x2D ,ZERO					;fpu 浮点单元异常
VECTOR 0x2E ,ZERO					;硬盘
VECTOR 0x2F ,ZERO					;保留
VECTOR 0x30 ,ZERO
VECTOR 0x31 ,ZERO
VECTOR 0x32 ,ZERO
VECTOR 0x33 ,ZERO
VECTOR 0x34 ,ZERO
VECTOR 0x35 ,ZERO
VECTOR 0x36 ,ZERO
VECTOR 0x37 ,ZERO
VECTOR 0x38 ,ZERO
VECTOR 0x39 ,ZERO
VECTOR 0x3A ,ZERO
VECTOR 0x3B ,ZERO
VECTOR 0x3C ,ZERO
VECTOR 0x3D ,ZERO
VECTOR 0x3E ,ZERO
VECTOR 0x3F ,ZERO
VECTOR 0x40 ,ZERO
VECTOR 0x41 ,ZERO
VECTOR 0x42 ,ZERO
VECTOR 0x43 ,ZERO
VECTOR 0x44 ,ZERO
VECTOR 0x45 ,ZERO
VECTOR 0x46 ,ZERO
VECTOR 0x47 ,ZERO
VECTOR 0x48 ,ZERO
VECTOR 0x49 ,ZERO
VECTOR 0x4A ,ZERO
VECTOR 0x4B ,ZERO
VECTOR 0x4C ,ZERO
VECTOR 0x4D ,ZERO
VECTOR 0x4E ,ZERO
VECTOR 0x4F ,ZERO
VECTOR 0x50 ,ZERO
VECTOR 0x51 ,ZERO
VECTOR 0x52 ,ZERO
VECTOR 0x53 ,ZERO
VECTOR 0x54 ,ZERO
VECTOR 0x55 ,ZERO
VECTOR 0x56 ,ZERO
VECTOR 0x57 ,ZERO
VECTOR 0x58 ,ZERO
VECTOR 0x59 ,ZERO
VECTOR 0x5A ,ZERO
VECTOR 0x5B ,ZERO
VECTOR 0x5C ,ZERO
VECTOR 0x5D ,ZERO
VECTOR 0x5E ,ZERO
VECTOR 0x5F ,ZERO
VECTOR 0x61 ,ZERO
VECTOR 0x62 ,ZERO
VECTOR 0x63 ,ZERO
VECTOR 0x64 ,ZERO
VECTOR 0x65 ,ZERO
VECTOR 0x66 ,ZERO
VECTOR 0x67 ,ZERO
VECTOR 0x68 ,ZERO
VECTOR 0x69 ,ZERO
VECTOR 0x6A ,ZERO
VECTOR 0x6B ,ZERO
VECTOR 0x6C ,ZERO
VECTOR 0x6D ,ZERO
VECTOR 0x6E ,ZERO
VECTOR 0x6F ,ZERO
VECTOR 0x70 ,ZERO
VECTOR 0x71 ,ZERO
VECTOR 0x72 ,ZERO
VECTOR 0x73 ,ZERO
VECTOR 0x74 ,ZERO
VECTOR 0x75 ,ZERO
VECTOR 0x76 ,ZERO
VECTOR 0x77 ,ZERO
VECTOR 0x78 ,ZERO
VECTOR 0x79 ,ZERO
VECTOR 0x7A ,ZERO
VECTOR 0x7B ,ZERO
VECTOR 0x7C ,ZERO
VECTOR 0x7D ,ZERO
VECTOR 0x7E ,ZERO
VECTOR 0x7F ,ZERO
VECTOR 0x80 ,ZERO
