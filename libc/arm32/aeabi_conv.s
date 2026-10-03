// The ARM run-time ABI (RTABI) conversions between long long and float or double.
//
// Every RTABI helper uses the base procedure call standard, hard-float program or not:
// a double travels in r0:r1 and a float in r0.
// The conversions are compiled C (libc/ilp32/int64.c) under the hard-float standard, so
// these wrappers move the value between the core and VFP registers.

    .syntax unified
    .arm

    .text

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
