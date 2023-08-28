	.file	"main.c"
	.text
	.globl	main
	.type	main, @function
main:
.LFB0:
	.cfi_startproc
	leal	4(%esp), %ecx
	.cfi_def_cfa 1, 0
	andl	$-16, %esp
	pushl	-4(%ecx)
	pushl	%ebp
	movl	%esp, %ebp
	.cfi_escape 0x10,0x5,0x2,0x75,0
	pushl	%ebx
	pushl	%ecx
	.cfi_escape 0xf,0x3,0x75,0x78,0x6
	.cfi_escape 0x10,0x3,0x2,0x75,0x7c
	call	__x86.get_pc_thunk.bx
	addl	$_GLOBAL_OFFSET_TABLE_, %ebx
	subl	$12, %esp
	pushl	$107
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$101
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$114
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$110
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$101
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$108
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$33
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$10
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$105
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$109
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$32
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$99
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$111
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$109
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$49
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$8
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$105
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$110
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$103
	call	put_char@PLT
	addl	$16, %esp
	subl	$12, %esp
	pushl	$33
	call	put_char@PLT
	addl	$16, %esp
.L2:
	jmp	.L2
	.cfi_endproc
.LFE0:
	.size	main, .-main
	.section	.text.__x86.get_pc_thunk.bx,"axG",@progbits,__x86.get_pc_thunk.bx,comdat
	.globl	__x86.get_pc_thunk.bx
	.hidden	__x86.get_pc_thunk.bx
	.type	__x86.get_pc_thunk.bx, @function
__x86.get_pc_thunk.bx:
.LFB1:
	.cfi_startproc
	movl	(%esp), %ebx
	ret
	.cfi_endproc
.LFE1:
	.ident	"GCC: (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0"
	.section	.note.GNU-stack,"",@progbits
