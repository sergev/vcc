# double sqrt(double), float sqrtf(float): the D and F instructions, correctly rounded
# as IEEE 754 requires.  The compiler emits fsqrt.d for a call of sqrt itself; these
# serve its address and code compiled by clang.  The same for both widths.

    .text

    .globl  sqrt
    .p2align 2
sqrt:
    fsqrt.d fa0, fa0
    ret

    .globl  sqrtf
    .p2align 2
sqrtf:
    fsqrt.s fa0, fa0
    ret
