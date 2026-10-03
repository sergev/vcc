// The ARM run-time ABI (RTABI) 64-bit multiply and shifts.

    .syntax unified
    .arm

    .text

// long long __aeabi_lmul(long long a, long long b): the low 64 bits of a * b.
    .globl  __aeabi_lmul
    .p2align 2
    .type   __aeabi_lmul, %function
__aeabi_lmul:
    mul     r1, r1, r2                  // hi(a) * lo(b)
    mla     r1, r0, r3, r1              // + lo(a) * hi(b)
    umull   r0, r12, r0, r2             // lo(a) * lo(b)
    add     r1, r1, r12
    bx      lr
    .size   __aeabi_lmul, .-__aeabi_lmul

// Shifts of r0:r1 by r2 (0..63).  A register-specified shift by 32 or more gives 0
// (lsl, lsr) or the sign (asr), which the cases below rely on.
    .globl  __aeabi_llsl
    .p2align 2
    .type   __aeabi_llsl, %function
__aeabi_llsl:
    subs    r3, r2, #32
    rsb     r12, r2, #32
    lslmi   r1, r1, r2
    orrmi   r1, r1, r0, lsr r12
    lslpl   r1, r0, r3
    lsl     r0, r0, r2
    bx      lr
    .size   __aeabi_llsl, .-__aeabi_llsl

    .globl  __aeabi_llsr
    .p2align 2
    .type   __aeabi_llsr, %function
__aeabi_llsr:
    subs    r3, r2, #32
    rsb     r12, r2, #32
    lsrmi   r0, r0, r2
    orrmi   r0, r0, r1, lsl r12
    lsrpl   r0, r1, r3
    lsr     r1, r1, r2
    bx      lr
    .size   __aeabi_llsr, .-__aeabi_llsr

    .globl  __aeabi_lasr
    .p2align 2
    .type   __aeabi_lasr, %function
__aeabi_lasr:
    subs    r3, r2, #32
    rsb     r12, r2, #32
    lsrmi   r0, r0, r2
    orrmi   r0, r0, r1, lsl r12
    asrpl   r0, r1, r3
    asr     r1, r1, r2
    bx      lr
    .size   __aeabi_lasr, .-__aeabi_lasr
