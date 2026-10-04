// double sqrt(double), float sqrtf(float): the VFP instructions, correctly rounded as
// IEEE 754 requires; AAPCS-VFP passes and returns the value in d0 or s0.  The compiler
// emits vsqrt.f64 for a call of sqrt itself; these serve its address and code
// compiled by clang.

    .syntax unified
    .arm

    .text

    .globl  sqrt
    .p2align 2
    .type   sqrt, %function
sqrt:
    vsqrt.f64 d0, d0
    bx      lr
    .size   sqrt, .-sqrt

    .globl  sqrtf
    .p2align 2
    .type   sqrtf, %function
sqrtf:
    vsqrt.f32 s0, s0
    bx      lr
    .size   sqrtf, .-sqrtf
