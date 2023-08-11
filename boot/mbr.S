;主引导程序，起始编译地址为0x7c00
;-----------------------------------------------------------------------------------------
%include "boot.inc"
SECTION MBR vstart=0x7c00 
    mov ax,cs
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax
    mov sp,0x7c00
    mov ax,0xb800   ;显存的用于文本模式显示适配器，这里是段基址
                    ;ds = es = ss = 0 栈指针指向MBR开始位置
    mov gs,ax       ;loader 的gs用的也是这个

;利用0x06号功能清屏
;-----------------------------------------------------------------------------------------
;ah = 0x06 功能号
;al = 0x00 上卷的行数 0位全部
;bh: 上卷行属性
;cx: (CL,CH) 窗口左上角(x,y)
;dx: (DL,DH) 窗口右下角(x,y)

    mov ax,0x600 
    mov bx,0x700
    mov cx,0
    mov dx,0x184f           ;0x18=24 0xfe=79,VGA文本模式下一行只能容纳80个字符，共25行
                            ;下标从0开始
    
    int 0x10 

;--------------------------------------------------------------------
    mov eax,LOADER_START_SECTOR  ;起始扇区的LBA地址
    mov bx, LOADER_BASE_ADDR     ;写入的地址
    mov cx, 4                    ;待读入的扇区数 以上传递的参数
    call rd_disk_m_16            ;读取程序的cx扇区 16位模式下读取硬盘

    jmp LOADER_BASE_ADDR         ;MBR跳转到内核加载器的位置，使命完成

;--------------------------------------------------------------------
;功能：读取硬盘的cx个扇区
rd_disk_m_16:
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
    mov [bx],ax                 ;bx 最大不过64kb范围
    add bx,2
    loop .go_on_read
    ret


;程序收尾
;--------------------------------------------------------------------------------------
;预留两个字节 其余空余的全部用0填满 为使检测当前扇区最后两字节为0x55 0xaa 检测是否为有效扇区
;字符串声明 db == define byte dw == define word ascii一个字符占一个字节
    jmp $ ;无限循环 一直跳转到当前命令位置
   
    ;510 = 512字节-2预留字节  再减去（当前位置偏移量-段开始位置偏移量）求出来的是剩余空间
    times 510 - ($ - $$) db 0 
    db 0x55,0xaa
