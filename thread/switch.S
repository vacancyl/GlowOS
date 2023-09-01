[bits 32]
section .text
global switch_to
switch_to:
    ;备份当前线程环境
    push esi            ;这里是根据ABI原则保护四个寄存器 放到栈里面
    push edi
    push ebx
    push ebp
    
    mov eax,[esp+20]    ;esp+20的位置是cur cur的pcb赋值给eax
    mov [eax],esp       ;[eax]为pcb的内核栈指针变量 把当前环境的esp值记录下来 每个函数新的栈 这里esp是记录到第一个值，也就是kstack而不是覆盖中断栈
    
    ;回复下一个线程的环境
    mov eax,[esp+24]
    mov esp,[eax]       ;把要切换的线程的pcb 内核栈esp取出来，取出来为什么esp就指向的是最低端呢：因为初始化的时候把stack的地址放到了这些寄存器下面，

   
    pop ebp     ;pop之后再push，自然就是对的，没有push的话，初始化的时候就指向了下面 
    pop ebx
    pop edi
    pop esi
    ret                 ;这里的返回地址为 kernel_thread的地址
