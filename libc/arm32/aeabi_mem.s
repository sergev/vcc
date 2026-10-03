// The ARM run-time ABI (RTABI) memcpy/memmove/memset/memclr family, over the C library
// routines.

    .syntax unified
    .arm

    .text

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
