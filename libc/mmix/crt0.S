% C startup for Knuth's MMIX simulator mmix.  The .mmo loader enters Main with argc in $0
% and argv in $1; Main makes $32..$255 global, as GCC's crti does, so that our code and
% GCC's agree that locals end at $31, sets the stack pointer $254, calls main and passes
% its result to exit.  Built with -DPRINT_STATUS it first prints main's result as "%d\n"
% (the book run tests compare it).
%
% divu divides the 128-bit rD:$Y, so the ABI assumes rD = 0 everywhere.  The loader
% leaves it 0 and no code writes it: Main checks that rather than setting it, and halts
% with status 0xfe and no exit report when it is not.

	.equ	Halt, 0
	.equ	Fputs, 7
	.equ	StdErr, 2

	.text
	.global	Main
	.p2align 2
Main:
	setl	$255,32
	put	rG,$255
	get	$255,rD
	bnz	$255,bad_rd
	geta	$255,stack_start
	ldou	$254,$255,0
	set	$4,$1			% argv
	set	$3,$0			% argc
	pushj	$2,main
	slu	$2,$2,32		% the int result, unextended from GCC's main
	sr	$2,$2,32
#ifdef PRINT_STATUS
	subu	$254,$254,32		% a buffer for the digits
	set	$4,$2
	addu	$5,$254,32
	pushj	$3,__mmix_fmtdec	% $3: the first digit
	addu	$4,$254,32		% $4: the end
1:	cmpu	$6,$3,$4
	bz	$6,2f
	ldbu	$6,$3,0
	pushj	$5,putbyte
	addu	$3,$3,1
	jmp	1b
2:	setl	$6,10			% '\n'
	pushj	$5,putbyte
	addu	$254,$254,32
#endif
	set	$3,$2
	pushj	$2,exit

bad_rd:
	geta	$255,rd_msg
	trap	0,Fputs,StdErr
	setl	$255,0xfe
	trap	0,Halt,0

	.p2align 3
stack_start:
	.quad	__Stack_start

% Reserve the global registers $247..$254, as GCC's crtn.o does: $254 is the stack
% pointer, $251 the structure-result address, $253 and $252 GCC's frame pointer and
% static chain.  The linker places what objects put in this section at the top of the
% register file, just below $255, which it never allocates, and allocates the base
% registers that `lda`, `ldo` and the like need below them: without these it would take
% $254 first.
	.section .MMIX.reg_contents
	.quad	0, 0, 0, 0, 0, 0, 0, 0

	.section .rodata
	.p2align 2			% geta reaches only multiples of 4
rd_msg:
	.asciz	"crt0: rD is not zero\n"
