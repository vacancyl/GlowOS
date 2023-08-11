%include "boot.inc"
SECTION loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR 		   ;是个程序都需要有栈区 我设置的0x600以下的区域到0x500区域都是可用空间 况且也用不到
jmp loader_start                      		   ;下面存放数据段 构建gdt 跳跃到下面的代码区


;------------------------------------------
;构建gdt以及内部的描述符
;------------------------------------------
    GDT_BASE:           dd  0x0000_0000     ;第0个不能用
                        dd  0x0000_0000

    CODE_DESC:          dd  0x0000_FFFF     ;段基地址+段界限
                        dd  DESC_CODE_HIGH4

    DATA_STACK_DESC:    dd  0x0000_FFFF
                        dd  DESC_DATA_HIGH4

    VIDEO_DESC:         dd  0x8000_0007     ;limit=(0xbffff-0xb8000)/4k=0x7
                        dd  DESC_VIDEO_HIGH4;文本模式的起始地址
    GDT_SIZE  equ $-GDT_BASE
    GDT_LIMIT equ GDT_SIZE-1

    times 60 dq 0                       ;dq 定义八字节 保留空间，多定义60个描述符
                                        ;选择子
    SELECTOR_CODE  equ (0x0001<<3) + TI_GDT + RPL0
    SELECTOR_DATA  equ (0x0002<<3) + TI_GDT + RPL0
    SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

    gdt_ptr dw GDT_LIMIT
            dd GDT_BASE                 ;注意这里是dd 48字节

    loadermsg db 'loader zone'



loader_start:
;------------------------------------------------------------
; INT 0x10 功能号：0x13 功能描述：打印字符串
;------------------------------------------------------------
;输入：
;AH 子功能号=13H
;BH=页码
;BL=属性(若AL=00H 01H)
;CX=字符串长度
;(DH DL)=坐标｛行、列）
;ES:BP=字符串地址
;AL=显示输出方式
;0——字符串中只含显示字符，其显示属性在 BL，显示后，光标位置不变
;1——字符串中只含显示字符，其显示属性在 BL，显示后，光标位置改变
;2——字符串中含显示字符和显示属性。显示后，光标位置不变
;3——字符串中含显示字符和显示属性。显示后，光标位置改变
;无返回值
    mov sp, LOADER_BASE_ADDR 
	mov bp, loadermsg           ;ES:BP=字符串地址
	mov cx, 11                  ;cx=字符事长度
	mov ax, 0x1301              ;AH = 13, AL = 01h 
	mov bx, 0x001f              ;页号为(BH = 0)蓝底粉红字(BL = 1fh)
	mov dx, 0x1800 
	int 0x10                    ; 10h号中断
    
;--------------------------准备进入保护模式------------------------------
;1 打开A20
;2 加载gdt
;3 将cr0的pe为置1

;-------------------------打开A20---------------------------------------
	in al,0x92
	or al,0000_0010b
	out 0x92,al

;-------------------------加载GDT---------------------------------------
	lgdt [gdt_ptr]

;-------------------------cr0第0位置------------------------------------
	mov eax,cr0
	or eax, 0x0000_0001
	mov cr0,eax


	jmp dword SELECTOR_CODE:p_mdoe_start    ;刷新流水线
                                            ;流水线是CPU为提高效率而采取的一种工作方式，CPU将当前指令及其后面的几条指令同时放在流水线中重叠执行
                                            ;由于在实模式下时，指令按照16位指令格式来译码，这里既有16位指令，又有32位指令，
                                            ;所以流水线把32位指令按照16位译码就会出错。解决这问题的方法就是用无跳转指令清空流水线。

[bits 32]
p_mdoe_start:
	mov  ax,SELECTOR_DATA					;选择子初始化各段寄存器
	mov  ds,ax
	mov  es,ax
	mov  ss,ax
	mov esp,LOADER_STACK_TOP
	mov  ax,SELECTOR_VIDEO
	mov  gs,ax

	mov byte [gs:160], 'P'                  ;第80个字符 80*2
	jmp $
