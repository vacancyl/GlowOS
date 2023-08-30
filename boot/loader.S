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

	total_mem_bytes dd 0				;用于保存内存容量，字节为单位，当前的地址是0xb00 （- 0x900 = 0x200）

                                        ;选择子
    SELECTOR_CODE  equ (0x0001<<3) + TI_GDT + RPL0
    SELECTOR_DATA  equ (0x0002<<3) + TI_GDT + RPL0
    SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

    gdt_ptr dw GDT_LIMIT
            dd GDT_BASE                 ;注意这里是dd 48字节

	ards_buf times 244 db 0				;对齐：total_mem_bytes4+gdt_ptr6+buf244+ards_nr2 = 256字节
	ards_nr  dw 0						;记录ARDS结构体数量

    

	;也就是此处的地址是0x900 + 512字节 + 256 字节 = 0xc00
	;当然读取的时候，前面这些是被读取到了0x900的

loader_start:
;-----------------------------------获取内存布局--------------------------------------------------------
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
	;jmp $

;------------------------------------------加载内核kernel---------------------------------------------
    mov eax,KERNEL_START_SECTOR  			;起始扇区的LBA地址
    mov ebx,KERNEL_BIN_BASE_ADDR 			;写入的地址
    mov ecx,200                  			;待读入的扇区数 以上传递的参数
    call rd_disk_m_32            			;读取程序的cx扇区 32位模式下读取硬盘



;--------------------------------------------分页机制------------------------------------------
	call setup_page

	sgdt [gdt_ptr]							;描述符地址和偏移量写入内存gdt_ptr,这里是存储到原来gdt所有的位置

	mov ebx, [gdt_ptr + 2]					;将gdt的描述符中的视频段描述符中的段基址+0xc000 0000，打印功能将来也是在内核中实现，肯定不能让用户进程直接能控制显存
	or dword [ebx + 0x18 + 4],0xc0000000	;第三个描述符，每个8字节，所以是0x18,段描述符的高4字节的最高位是段基址的31-24位，+2才能得到GDT的基地址

	add dword [gdt_ptr + 2],  0xc0000000	;gdt的基地址加上0xc0000000使得其成为内核所在的高地址
	add esp,0xc0000000						;栈指针同样映射到内核地址

	;页目录地址赋给cr3
	mov eax, PAGE_DIR_TABLE_POS
	mov cr3, eax

	;打开cr0的PG位
	mov eax, cr0
	or  eax, 0x80000000  					;8=1000b_
	mov cr0, eax

	;用gdt的新地址重新加载
	lgdt [gdt_ptr]

	mov byte [gs:160], 'V'					;视频段的段基址被更新，virtual addr
	
	jmp SELECTOR_CODE:enter_kernel			;刷新流水线，更新gdt

enter_kernel:
	mov byte [gs:160], 'K'
	call kernel_init
	mov esp, 0xc009f000
	mov byte [gs:160], 'N'
	jmp KERNEL_ENTER_ADDR					;


;---------------------------------------------------------------------------------------------
;创建页目录和页表
;---------------------------------------------------------------------------------------------
setup_page:
;页目录占用的空间先全部清零，逐个字节
	mov ecx, 4096							;loop次数
	mov esi, 0
.clear_page_dir:
	mov byte [PAGE_DIR_TABLE_POS + esi], 0
	inc esi
	loop .clear_page_dir

;开始创建页目录项
.create_PDE:
	mov eax, PAGE_DIR_TABLE_POS
	add eax, 0x1000						;此时的eax是在第一个页表处的位置和属性0x10_1000
	mov ebx, eax 						;为create_PTE准备，ebx是为基址
;注意这里不要和虚拟地址转换物理地址反了，这里只需要页表和页的物理地址，
;将页目录项0和0xcc都存为第一个页表的地址，每个页表表示4MB
;这样0xc03ffffff以下的地址和0x003ffffff以下的地址都指向相同的页表这里是将地址映射为内核地址准备

	or eax, PG_US_U | PG_RW_W | PG_P	;rw和p设置为1，us为1，表示用户属性，左右的特权级都可以访问
										;init进程，它是用户级程序，而它位于内核地址空间，将来会在特权级3的情况下执行 init
										;这会访问到内核空间，这就是此处用属性 PG US_U 的目的
	mov [PAGE_DIR_TABLE_POS + 0x0],  eax;第一个目录项，写入第一个页表的位置（0x101000以及属性）00_[01_0000_0001]_11个0_7
	mov [PAGE_DIR_TABLE_POS + 0xc00],eax;第768个页表所占用的页目录项，指向的也是第一个页表 0xc00以上的目录项用于内核空间，也就是页表的0xc000_0000-0xffff_ffff共计1G属于内核
										;0x0 - 0xbfff_ffff共计3G属于用户进程

	sub eax, 0x1000
	mov [PAGE_DIR_TABLE_POS + 4092], eax;最后一个目录项指向页目录表自己的地址，所以内核实际是只有1GB-4MB


;创建页表项（PTE）							
	mov ecx, 256						;1M低端内存/4K = 256,这里只用到了1MB空间，用到的是第0个页目录项，一共是4MB
	mov esi, 0
	mov edx, PG_US_U | PG_RW_W |PG_P	;属性为7，US=1，RW=1，P=1

.create_PTE:
	mov [ebx + esi*4], edx				;ebx = 0x10_1000第一个页表的位置
	add edx,4096						;edx是从0开始的额
	inc esi
	loop .create_PTE

;创建内核其他页表的PDE						
										;，此处在页目录表中把内核空间的目录项写满，目的是为将来的用户进程做准备，使所有用户进程共享内核空间。
										;  我们将来要完成的任务是让每个用户进程都有独立的页表，也就是独立的虚拟4GB空间。其中低3GB属于用户进程自己的空间，
										;  高1GB是内核空间，内核将被所有用户进程共享。为了实现所有用户进程共享内核，各用户进程的高1GB必须“都”指向内核所在的物理内存空间，
										;  也就是说每个进程页目录表中第768～1022个页目录项都是与其他进程相同的(各进程页目录表中第1023个目录项指向页目录表自身),
										;  因此在为用户进程创建页表时，我们应该把内核页表中第768～1022个页目录项复制到用户进程页目录表中的相同位置
										;  一个页目录项对应一个页表地址，页表地址固定了，后来新增的页表项也只会加在这些固定的页表中。
										;  如果不这样的话，进程陷入内核时，假设内核为了某些需求为内核空间新增页表(通常是申请大量内存),
										;  因此还需要把新内核页表同步到其他进程的页表中，否则内核无法被“完全"共享，只能是"部分”共享。
										;  所以，实现内核完全共享最简单的办法是提前把内核的所有页目录项定下来，也就是提前把内核的页表固定下来，

										;  如果此处仅仅是安装第 768 个页目录项的话，769 1022 个目录项是空的，
										;  将来即使把第 768 1022 个目录项复制给用户进程时，内核空间也仅是其 4MB 被所有进程共享，
										;  万一在某些情况下内核使用的空间超过 4MB ，要用到第 769 个页目录项对应的页表，由于此处未提前准备该目录项，
										;  创建的新页表只会安装在当时那个进程的页目录表中，而切换了其他进程后，新进程的页目录表中井不包含新页表的 PDE ，因此无法访问到最新的内核空间

	mov eax, PAGE_DIR_TABLE_POS			
	add eax, 0x2000						;第二个页表的位置
	or  eax, PG_US_U | PG_RW_W |PG_P
	mov ebx, PAGE_DIR_TABLE_POS
	mov ecx, 254						;769-1022的所有的目录项的数量
	mov esi, 769

.create_kernel_PDE:
	mov [ebx+esi*4], eax
	inc esi
	add eax, 0x1000 
	loop .create_kernel_PDE
	ret

;------------------------------------------kernel.bin中额segment拷贝到编译的地址----------------------
kernel_init:
    xor eax, eax   
    xor ebx,ebx								;ebx记录程序头表地址
    xor ecx,ecx								;cx记录程序投标中program header的大小
    xor edx,edx								;dx记录program header尺寸，即e_phentsize
    
    
   
    mov dx, [KERNEL_BIN_BASE_ADDR+42]		;获取程序段表每个条目描述符字节大小
	mov ebx,[KERNEL_BIN_BASE_ADDR+28]		;获取到program在文件中的偏移量
    add ebx, KERNEL_BIN_BASE_ADDR           ;ebx当前位置为程序段表
    mov cx, [KERNEL_BIN_BASE_ADDR+44]       ;一共有几个段
    
     
.each_segment:
    cmp dword [ebx+0], PT_NULL
    je .PTNULL                           	;空则跳转，说明此program header未使用
    
	mov eax,[ebx+8]
    cmp eax,0xc0001500
    jb .PTNULL


    push dword [ebx+16]                     ;ebx+16在存储的数是filesz,为mem_cpy压入参数。memcpy(dst src, siz),从右往左压入
                                                               
    mov eax,[ebx+4]                                            
    add eax,KERNEL_BIN_BASE_ADDR			;eax为该段的物理地址
    push eax                               	;压入p_offset,src      
    push dword [ebx+8]                      ;p_vaddr, 目标位置
     
    call mem_cpy
    add esp,12                             	;清理栈中的三个参数
    
.PTNULL:
    add  ebx,edx                           	;每个条目描述符字节大小，这里是0x20
    loop .each_segment                                       
    ret
;--------------------------------------逐字节拷贝-------------------------------------------------------------------                                       
mem_cpy:
    cld                               		;向高地址自动加数字 cld std 向低地址自动移动 clean set
    push ebp                            	;保存ebp 因为访问的时候通过ebp，栈底，到时候就可以返回了
    mov  ebp,esp 
    push ecx                               	;外层循环还要用 必须保存 外层eax存储着还有几个段
    
                                                               
                                                               
    mov edi,[ebp+8]                         ;dst                  
    mov esi,[ebp+12]                        ;src
    mov ecx,[ebp+16]                        ;size               
    
    rep movsb                               ;逐个字节拷贝，每执行一次ecx-1,esi,edi自动加上搬运的字节数
    
	;恢复环境
    pop ecx 
    pop ebp
    ret
    


;--------------------------------------------------------------------
;功能：读取硬盘的cx个扇区
rd_disk_m_32:
;注意 我们读取部分要到0x70000 明显0x7000016位是不能满足需求的我们只需要在函数的最后一部分 bx改成ebx即可
;--------------------------------------------------------------------
                                ;eax=LBA扇区号
                                ;bx=将数据写入的内存地址
                                ;cx=读入的扇区数
    mov esi,eax                 ;备份eax 后面eax会被使用到
    mov di,cx                   ;备份cx
;读写硬盘：
;第一步：设置要读取的扇区数
    mov dx,0x1f2                ;先选择通道 根据配置文件，ata0为PRIMARY通道，
                                ;所以sector count为0x1f2
    mov al,cl
    out dx,al                   ;往端口号中写入读取的扇区数
    mov eax,esi                 ;恢复ax，前面用到al写数据

;第二步：将LBA地址存入0x1f3 ~ 0x1f6
    mov dx,0x1f3                ;LBA地址的7-0位写入端口0x1f3
    out dx,al 

    mov cl,8                    ;LBA地址的15-8位写入端口0x1f4
    shr eax,cl                  ;目的操作数顺序右移CL寄存器指定的位数
    mov dx,0x1f4                ;右移8位，刚好需要的位数存放在al中
    out dx,al                   

    shr eax,cl                  ;LBA地址23-16位写入端口0x1f5
    mov dx,0x1f5
    out dx,al

    shr eax,cl                  ;LBA地址27-24位写入端口0x1f6（device）
    and al,0x0f                 ;0x0f 0000 1111 al的第四位不变，高四位置0
    or  al,0xe0                 ;0xe0 1110 0000 第四位不变，高四位设置为1110
    mov dx,0x1f6                ;1 MBS固定 1 LBA 1 MBS 0 主盘
    out dx,al       

;第三步；向0x1f7端口写入读命令0x20 command
    mov dx,0x1f7
    mov al,0x20
    out dx,al
;第四步：检测硬盘状态
;同一端口，写时表示写入命令字，读时表示读入硬盘状态
.not_ready:
    nop                         ;空操作，相当于sleep一下
    in  al,dx                   ;读的是status寄存器
    and al,0x88                 ;1000 1000 第四位为1：硬盘准备好数据
                                ;第七位为1表示硬盘忙，这里是把其他位置0
    cmp al,0x08                 ;al - 0x08 
    jnz .not_ready              ;结果不为零（或不相等）则转移
                                ;这里需要满足的条件是硬盘不忙而且数据准备好了即：0x08

;第五步：从0x1f0端口读取数据
    mov ax,di                   ;di存储的是读取的扇区数
    mov dx,256                  ;一个扇区512字节，每次读入一个字，就是两个字节，需要di*512/2
    mul dx                      ;被乘数为ax，结果高位在dx，低位在ax
    mov cx,ax

    mov dx,0x1f0

.go_on_read:                    ;不断的读取数据
    in ax,dx
    mov [ebx],ax                 ;bx 最大不过64kb范围
    add ebx,2
    loop .go_on_read
    ret

