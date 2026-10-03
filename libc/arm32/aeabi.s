// The ARM run-time ABI (RTABI) helpers that clang's code calls, and ours: 64-bit
// division with remainder, multiply and shifts, the conversions between long long and
// float or double, and the memcpy/memmove/memset/memclr family.
//
// Every RTABI helper uses the base procedure call standard, hard-float program or not:
// a double travels in r0:r1 and a float in r0.  The conversions are compiled C
// (libc/ilp32/int64.c) under the hard-float standard, so these wrappers move the value
// between the core and VFP registers.

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

// long long to double or float: the C routine returns in d0 or s0.
    .macro  to_fp name, conv, move
    .globl  \name
    .p2align 2
    .type   \name, %function
\name:
    push    {r4, lr}
    bl      \conv
    \move
    pop     {r4, pc}
    .size   \name, .-\name
    .endm

    to_fp   __aeabi_l2d, __floatdidf, "vmov r0, r1, d0"
    to_fp   __aeabi_ul2d, __floatundidf, "vmov r0, r1, d0"
    to_fp   __aeabi_l2f, __floatdisf, "vmov r0, s0"
    to_fp   __aeabi_ul2f, __floatundisf, "vmov r0, s0"

// double or float to long long, toward zero: the C routine takes d0 or s0.
    .macro  from_fp name, conv, move
    .globl  \name
    .p2align 2
    .type   \name, %function
\name:
    \move
    b       \conv
    .size   \name, .-\name
    .endm

    from_fp __aeabi_d2lz, __fixdfdi, "vmov d0, r0, r1"
    from_fp __aeabi_d2ulz, __fixunsdfdi, "vmov d0, r0, r1"
    from_fp __aeabi_f2lz, __fixsfdi, "vmov s0, r0"
    from_fp __aeabi_f2ulz, __fixunssfdi, "vmov s0, r0"

// The memory helpers.  The 4 and 8 variants promise aligned arguments, which the
// plain routines do not need.  __aeabi_memset takes (dest, n, c), unlike memset.
    .macro  alias name, target, setup
    .globl  \name
    .globl  \name\()4
    .globl  \name\()8
    .p2align 2
    .type   \name, %function
    .type   \name\()4, %function
    .type   \name\()8, %function
\name:
\name\()4:
\name\()8:
    \setup
    b       \target
    .size   \name, .-\name
    .size   \name\()4, .-\name\()4
    .size   \name\()8, .-\name\()8
    .endm

    alias   __aeabi_memcpy, memcpy, ""
    alias   __aeabi_memmove, memmove, ""
    alias   __aeabi_memset, memset, "mov r3, r1; mov r1, r2; mov r2, r3"
    alias   __aeabi_memclr, memset, "mov r2, r1; mov r1, #0"
