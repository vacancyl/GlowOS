%include "boot.inc"
SECTION loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR 		   ;是个程序都需要有栈区 我设置的0x600以下的区域到0x500区域都是可用空间 况且也用不到
				                      		   ;下面存放数据段 构建gdt 跳跃到下面的代码区


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

    times 60 dq 0                       ;dq 定义八字节 保留空间，多定义60个描述符 480 + 上面的32字节，所以截止到此处是512字节

	total_mem_bytes dd 0				;用于保存内存容量，字节为单位，当前的地址是0xb00 - 0x900 = 0x200

                                        ;选择子
    SELECTOR_CODE  equ (0x0001<<3) + TI_GDT + RPL0
    SELECTOR_DATA  equ (0x0002<<3) + TI_GDT + RPL0
    SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

    gdt_ptr dw GDT_LIMIT
            dd GDT_BASE                 ;注意这里是dd 48字节

	ards_buf times 244 db 0				;对齐：total_mem_bytes4+gdt_ptr6+buf244+ards_nr2 = 256字节
	ards_nr  dw 0						;记录ARDS结构体数量

    

	;也就是此处的地址是0x900 + 512字节 + 256 字节 = 0xc00

loader_start:
;------------------------------------------------------------
; INT 0x15 功能号：0xE820 edx = 534D4150h smap 获取内存布局
;------------------------------------------------------------
	xor ebx, ebx						;第一次调用ebx=0
	mov edx, 0x534D4150
	mov di,  ards_buf					;ards结构缓冲区

.E820_mem_get_loop:						;循环获取每个ards内存范围描述结构
	mov eax, 0x0000e820					;执行此功能之后，每次eax都会改变为0x534D4150h，所以每个循环都需要改变
	mov ecx, 20
	int 0x15
	
	jc .E820_failed_so_try_e801			;cf为1,跳转，出错

	add di, cx							;di增加20字节
	inc word [ards_nr]					;+1
	cmp ebx, 0							;ebx为0且cf不为1，说明ards全部返回
	jnz .E820_mem_get_loop				;不为0转移（是两个结果的相减），cmp = ebx - 0如果等于0 则ZF=1，因为返回值为0的时候，说明是最后一个ards结构

	;在所有的ards结构中，找到base_add_low + length_low最大的，是为内存的容量
	mov cx, [ards_nr]

	mov ebx, ards_buf
	xor edx, edx						;edx为最大的容量

.find_max_mem_area:						
										;不需要判断类型，最大的一块内存一定是可以被使用的
	mov eax, [ebx]						;base_add_low
	add eax, [ebx+8]					;length_low

	add ebx, 20							;指向下一个缓冲区
	cmp edx, eax						;排序

	jge .next_ards						;大于等于时候转移 edx >= eax
	mov edx, eax

.next_ards:
	loop .find_max_mem_area
	jmp .mem_get_ok


;------------------------------------------------------------
; INT 0x15 功能号：0xE801 结果 ax=cx bx=dx
;------------------------------------------------------------
.E820_failed_so_try_e801:
	int 0x15
	jc .E801_failed_so_try88

;先计算低15MB的内存 ax = cx
	mov cx,0x400						;1kb
	mul cx								;ax * cx 结果 DX（高16位）ax(低16位)

	shl edx, 16							;左移16位
	and eax, 0x0000FFFF					;高15位置0
	or  edx, eax						;组合到edx中
	add edx, 0x100000					;加1MB
	mov esi, edx 						;内存容量存入esi中

;再计算16MB以上的内存 bx = dx
	xor eax,eax 
	mov ax,bx
	mov ecx,0x10000						;64KB 
	mul ecx 							;eax * ecx 高32位存入edx 低32位存入eax

	add esi, eax
	mov edx, esi						;最后的内存
	jmp .mem_get_ok


;------------------------------------------------------------
; INT 0x15 功能号：0x88 结果ax
;------------------------------------------------------------
.E801_failed_so_try88:
	mov ah, 0x88
	int 0x15

	jc .error_hlt
	and eax, 0x0000FFFF

	mov cx, 0x400
	mul cx 
	shl edx, 16
	or  edx, eax
	add edx, 0x100000

.error_hlt:
     jmp $

.mem_get_ok:
	mov [total_mem_bytes], edx

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
