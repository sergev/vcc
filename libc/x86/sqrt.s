// double sqrt(double), float sqrtf(float): the SSE2 instructions, correctly rounded as
// IEEE 754 requires.  The compiler emits sqrtsd for a call of sqrt itself; these serve
// its address and code compiled by clang.

    .text

    .globl  sqrt
    .p2align 4
    .type   sqrt, @function
sqrt:
    sqrtsd  %xmm0, %xmm0
    ret
    .size   sqrt, .-sqrt

    .globl  sqrtf
    .p2align 4
    .type   sqrtf, @function
sqrtf:
    sqrtss  %xmm0, %xmm0
    ret
    .size   sqrtf, .-sqrtf
