// The ARM run-time ABI (RTABI) 64-bit division with remainder, over the C routines of
// libc/ilp32/int64.c.  Each family of helpers is an object of its own, so that a
// program pulls in only what it calls.

    .syntax unified
    .arm

    .text

// {q, r} = n / d with n in r0:r1 and d in r2:r3: q in r0:r1, r in r2:r3.  `div`
// computes the quotient; the remainder is n - q * d, modulo 2^64 either way.
    .macro  divmod name, div
    .globl  \name
    .p2align 2
    .type   \name, %function
\name:
    push    {r4, r5, r6, r7, r8, lr}
    mov     r4, r0
    mov     r5, r1
    mov     r6, r2
    mov     r7, r3
    bl      \div
    umull   r2, r3, r0, r6              // q * d, low 64 bits
    mla     r3, r0, r7, r3
    mla     r3, r1, r6, r3
    subs    r2, r4, r2
    sbc     r3, r5, r3
    pop     {r4, r5, r6, r7, r8, pc}
    .size   \name, .-\name
    .endm

    divmod  __aeabi_uldivmod, __udivdi3
    divmod  __aeabi_ldivmod, __divdi3
