// double sqrt(double), float sqrtf(float): the instructions, correctly rounded as
// IEEE 754 requires.  The compiler emits fsqrt for a call of sqrt itself; these serve
// its address and code compiled by clang.

    .text

    .globl  sqrt
    .p2align 2
    .type   sqrt, @function
sqrt:
    fsqrt   d0, d0
    ret
    .size   sqrt, .-sqrt

    .globl  sqrtf
    .p2align 2
    .type   sqrtf, @function
sqrtf:
    fsqrt   s0, s0
    ret
    .size   sqrtf, .-sqrtf
