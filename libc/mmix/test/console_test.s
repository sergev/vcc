% The runtime on its own (console_test.sh): main prints "hello" through putbyte, copies
% StdIn to stdout through getch, and returns the number of bytes it copied, or -3 when
% it has a command-line argument.
	.text
	.global	main
	.p2align 2
main:
	get	$2,rJ
	cmp	$3,$0,1
	bp	$3,4f			% argc > 1
	geta	$3,hello
1:	ldbu	$6,$3,0
	bz	$6,2f
	pushj	$5,putbyte
	addu	$3,$3,1
	jmp	1b
2:	setl	$3,0			% bytes copied
3:	pushj	$4,getch
	bn	$4,5f
	addu	$3,$3,1
	set	$6,$4
	pushj	$5,putbyte
	jmp	3b
4:	negu	$3,0,3
5:	set	$0,$3
	put	rJ,$2
	pop	1,0

	.section .rodata
	.p2align 2			% geta reaches only multiples of 4
hello:
	.asciz	"hello\n"
