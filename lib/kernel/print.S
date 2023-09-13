RPL0    equ 00b
TI_GDT  equ 000b
SELECTOR_VIDEO          equ (0X0003<<3) + TI_GDT + RPL0     ;选择子16位，高12位为索引值
SELECTOR_DATA	        equ (0X0002<<3) + TI_GDT + RPL0


[bits 32]
section .data 
put_int_buffer dq 0             ;0字节



section .text

global put_str
;------------------- put_str 函数实现 -------------------------------------------
;使用put_char实现
;---------------------------------------------------------------------------------
put_str:
    push ebx
    push ecx                    ;备份
    xor ecx,ecx
    mov ebx,[esp+12]            ;待打印字符串的地址，ebx ecx 返回地址

.goon:
    mov cl, [ebx]
    cmp cl, 0                   ;如果到字符串尾，跳转到结束处返回
    jz .str_over                ;等于0跳转
    push ecx                    ;为put_char传递参数
    call put_char
    add esp, 4                  ;回收参数空间
    inc ebx                     ;指向下一个字符
    jmp .goon

.str_over:
    pop ecx
    pop ebx
    ret

global put_int
;--------------------   将小端字节序的数字变成对应的ascii后，倒置   -----------------------
;输入：栈中参数为待打印的数字
;输出：在屏幕上打印16进制数字,并不会打印前缀0x,如打印10进制15时，只会直接打印f，不会是0xf
;------------------------------------------------------------------------------------------
put_int:
   pushad
   mov ebp, esp
   mov eax, [ebp+4*9]		        ; call的返回地址占4字节+pushad的8个4字节
   mov edx, eax
   mov edi, 7                       ; 指定在put_int_buffer中初始的偏移量
   mov ecx, 8			            ; 32位数字中,16进制数字的位数是8个
   mov ebx, put_int_buffer

;将32位数字按照16进制的形式从低位到高位逐个处理,共处理8个16进制数字
.16based_4bits:			       ; 每4位二进制是16进制数字的1位,遍历每一位16进制数字
   and edx, 0x0000000F		   ; 解析16进制数字的每一位。and与操作后,edx只有低4位有效
   cmp edx, 9			       ; 数字0～9和a~f需要分别处理成对应的字符
   jg .is_A2F 
   add edx, '0'			       ; ascii码是8位大小。add求和操作后,edx低8位有效。
   jmp .store
.is_A2F:
   sub edx, 10			       ; A~F 减去10 所得到的差,再加上字符A的ascii码,便是A~F对应的ascii码
   add edx, 'A'

;将每一位数字转换成对应的字符后,按照类似“大端”的顺序存储到缓冲区put_int_buffer
;高位字符放在低地址,低位字符要放在高地址,这样和大端字节序类似,只不过这里是字符序.
.store:
; 此时dl中应该是数字对应的字符的ascii码
   mov [ebx+edi], dl		       
   dec edi                      ;此时已经是倒置，只需要顺序打印即可
   shr eax, 4                   ;4位是一个数字
   mov edx, eax 
   loop .16based_4bits

;现在put_int_buffer中已全是字符,打印之前,
;把高位连续的字符去掉,比如把字符000123变成123
.ready_to_print:
   inc edi			            ;此时edi退减为-1(0xffffffff),加1使其为0

.skip_prefix_0:  
   cmp edi,8			        ;若已经比较第9个字符了，表示待打印的字符串为全0 
   je .full0 

;找出连续的0字符, edi做为非0的最高位字符的偏移
.go_on_skip:   
   mov cl, [put_int_buffer+edi]
   inc edi
   cmp cl, '0' 
   je .skip_prefix_0		    ; 继续判断下一位字符是否为字符0(不是数字0)
   dec edi			            ; edi在上面的inc操作中指向了下一个字符,若当前字符不为'0',要恢复edi指向当前字符		       
   jmp .put_each_num

.full0:
   mov cl,'0'			        ; 输入的数字为全0时，则只打印0
.put_each_num:
   push ecx			            ; 此时cl中为可打印的字符
   call put_char
   add esp, 4                   ; 回收空间
   inc edi			            ; 使edi指向下一个字符
   mov cl, [put_int_buffer+edi]	; 获取下一个字符到cl寄存器
   cmp edi,8
   jl .put_each_num

   popad
   ret


global put_char
;------------------- put_char 函数实现 -------------------------------------------
;把字符写到光标位置
;---------------------------------------------------------------------------------
put_char:
    pushad           		    ;push all double寄存器 备份32位寄存器环境，需要保证
                                ;EAX->ECX->EDX->EBX-> ESP-> EBP->ESl->EDI
                                ;gs中为正确的视频段选择子，每次打印都给gs赋值
    mov ax,SELECTOR_VIDEO
    mov gs,ax                   ;gs寄存器赋值段选择子
    
    mov dx,0x3d4                ;默认CRT 寄存器索引
    mov al,0x0e                 ;光标位置的高8位，这里用al 不用ax 是因为 此处索引寄存器是一个8字节的寄存器
    out dx,al                   ;由于一个字符占用2字节，所以光标乘以2后才是字符在显存中的地址
    mov dx,0x3d5                ;光标高位值寄存器窗口
    in  al,dx                   ;得到光标位置高8位
    mov ah,al                   ;左移或者赋值皆可
    
    mov dx,0x3d4                ;获取低8位
    mov al,0x0f
    out dx,al
    mov dx,0x3D5
    in  al,dx
    
    mov bx,ax                   ;光标位置存到bx  
    mov byte cl,[esp+36]        ;在栈中获取待打印的字符，pushad压入4*8=32字节
                                ;主调函数4字节的返回地址 36
                               
                               ;回车0xd 换行0xa 退格0x8 
    cmp cl,0xd                 ;比较cl与0xd是否相等
    je .is_carriage_return     ;回车处理函数
    cmp cl,0xa                 ;比较cl与0xa是否相等 
    je .is_line_feed           ;换行处理函数
    cmp cl,0x8                 ;比较cl与0x8是否相等
    je .is_backspace           ;退格处理函数
    
    jmp .put_other_char
       
.is_backspace:
    cmp bx,0                   ;如果bx = 0 则没有办法退格了
    je  set_cursor              
    
    dec bx                     ;光标位置退1
    shl bx,1                   ;x2 一个字符占两个字节 一个字节是属性 一个字节ascii 
    
    mov word [gs:bx],0x0720    ;低字节ascii 32 ascii表示空字符 7 高字符属性空
    shr bx,1                   ;退回原来的位置
    jmp set_cursor            ;交给硬件去处理光标位置
    
.put_other_char:
    shl bx,1                   
    mov [gs:bx],cl             
    inc bx                     
    mov byte [gs:bx],0x7
    inc bx
    shr bx,1
    cmp bx,2000                ;没到边界2000即跳转
    jl  set_cursor            ;没有的话 继续往下进行
                               ;如果到了两千，执行下面的，因为也是要换行的
                               
.is_line_feed:   
.is_carriage_return:
    xor dx,dx                   ;被除数的高16位 ax则是被除数的低16位
    mov ax,bx                   ;div之后余数放在dx 商放在ax
    mov si,80                   ;除80减余数即是行首
    div si
    sub bx,dx
    
.is_carrige_return_end:         ;\r回车处理结束
    add bx,80                   ;一共一页2000 共25行 2000/25=80 则向bx增加80
    cmp bx,2000            
.is_line_feed_end:              ;若是LF\n 光标移80
    jl set_cursor              ;<2000跳转，否则继续执行，进行滚屏
    
.roll_screen:
    cld                         ;从低到高移动,向高地址移动
    mov ecx,960                 ;2000-80=1920搬运，1920*2=3840字节，一次4字节，所以960次
    
    mov esi,0xc00b80a0          ;第1行行首
    mov edi,0xc00b8000          ;第0行行首
    rep movsd                   ;movs doubleword 双子 ds:si->es:di ds es平坦模式0
    
    mov ebx,3840                ;最后一行80*2 = 160 偏移：4000-160 = 3840 最后一行清除了
    mov ecx,80

.cls:
    mov word [gs:ebx], 0x0720		  ;0x0720是黑底白字的空格键
    add ebx, 2
    loop .cls 
    mov bx,1920				  ;将光标值重置为1920,最后一行的首字符.


global set_cursor
set_cursor:
    mov dx,0x3D4                ;高8位
    mov al,0x0e
    out dx,al
    mov dx,0x03d5
    mov al,bh
    out dx,al
    
    mov dx,0x03d4                ;低8位
    mov al,0x0f
    out dx,al
    mov dx,0x03d5
    mov al,bl
    out dx,al

.put_char_done:
    popad                        ;把之前全部储存的给pop出来 还原现场
    ret
    

global cls_screen
cls_screen:                              
   pushad                                ; 用户态不允许访问显存 只能由中断后进入内核态 故此为系统调用 每次进入都会把gs设置为0，所以这里要重新赋值
   mov ax,SELECTOR_VIDEO                 ; ax做中转
   mov gs,ax                             ; 先放到gs里面 进入中断后再
   
   mov ebx,0
   mov ecx,80*25                         ;清屏幕
   

.cls:   
    mov word [gs:ebx],0x0720    ;0x07 0x20 黑底白字属性 空字符
    add ebx, 2    
    loop .cls

    mov ebx,0                
    jmp set_cursor