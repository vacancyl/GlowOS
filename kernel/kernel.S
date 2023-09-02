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
    push fs
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
