% double sqrt(double), float sqrtf(float): fsqrt, correctly rounded as IEEE 754 requires.
% The compiler emits fsqrt for a call of sqrt itself; these serve its address and code
% compiled by GCC.  A float travels as its binary32 bits, so sqrtf widens it through
% memory (sttu, ldsf), takes the binary64 root, and rounds it once to binary32 (stsf):
% the root of a binary32 value in binary64 rounds to the correct binary32 (53 >= 2*24+2).
% The result's bits come back sign-extended (ldt), as GCC returns them.

	.text

	.global	sqrt
	.p2align 2
sqrt:
	fsqrt	$0,0,$0
	pop	1,0

	.global	sqrtf
	.p2align 2
sqrtf:
	subu	$254,$254,8
	sttu	$0,$254,0
	ldsf	$0,$254,0
	fsqrt	$0,0,$0
	stsf	$0,$254,0
	ldt	$0,$254,0
	addu	$254,$254,8
	pop	1,0
