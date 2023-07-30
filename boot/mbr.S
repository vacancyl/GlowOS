;主引导程序，起始编译地址为0x7c00
;LOADER_BASE_ADDR equ 0xA000
;LOADER_START_SECTOR equ 0x2
;-----------------------------------------------------------------------------------------
SECTION MBR vstart=0x7c00 
    mov ax,cs
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax
    mov sp,0x7c00
    mov ax,0xb800   ;显存的用于文本模式显示适配器，这里是段基址
                    ;ds = es = ss = 0 栈指针指向MBR开始位置
    mov gs,ax

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

;输出背景绿色，前景红色吗，自动跳动的字符串2 MBR
mov byte [gs:0x00],'1'
mov byte [gs:0x01],0xA4 ;A表示绿色背景闪烁，4表示前景色为红色 1010 0200 

mov byte [gs:0x02],' '
mov byte [gs:0x03],0xA4

mov byte [gs:0x04],'M'
mov byte [gs:0x05],0xA4 

mov byte [gs:0x06],'B'
mov byte [gs:0x07],0xA4

mov byte [gs:0x08],''
mov byte [gs:0x09],0xA4 

;程序收尾
;--------------------------------------------------------------------------------------
;预留两个字节 其余空余的全部用0填满 为使检测当前扇区最后两字节为0x55 0xaa 检测是否为有效扇区
;字符串声明 db == define byte dw == define word ascii一个字符占一个字节
    jmp $ ;无限循环 一直跳转到当前命令位置
   
    ;510 = 512字节-2预留字节  再减去（当前位置偏移量-段开始位置偏移量）求出来的是剩余空间
    times 510 - ($ - $$) db 0 
    db 0x55,0xaa
