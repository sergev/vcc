; Shifts under the MSP430 EABI names, the complete groups GCC's libgcc defines (slli.o,
; srai.o, srli.o), so that its code never takes one of those objects, which would define
; our names a second time.  Our own code calls only the long ones; it shifts an int and
; a long long inline.  A count past the width is undefined in C; here it simply shifts
; that many times.
;
;   __mspabi_slli   R12 << R13                -> R12       clobbers R13
;   __mspabi_srli   R12 >> R13, logical       -> R12       clobbers R13
;   __mspabi_srai   R12 >> R13, signed        -> R12       clobbers R13
;   __mspabi_slll   R12:R13 << R14            -> R12:R13   clobbers R14
;   __mspabi_srll   R12:R13 >> R14, logical   -> R12:R13   clobbers R14
;   __mspabi_sral   R12:R13 >> R14, signed    -> R12:R13   clobbers R14
;   __mspabi_sllll  R8-R11 << R12             -> R12-R15   clobbers R11
;   __mspabi_srlll  R8-R11 >> R12, logical    -> R12-R15   clobbers R11
;   __mspabi_srall  R8-R11 >> R12, signed     -> R12-R15   clobbers R11
;   __mspabi_<op>_N (N 1..15): the int or long one by the constant N, falling through a
;   chain of single shifts; clobbers nothing else.
;
; The shared epilogues GCC's functions may end with, from libgcc's epilogue.o:
;   __mspabi_func_epilog_N (N 1..7) pops R(11-N)..R10, then returns.
    .text

; A chain of entries `name_15` ... `name_1`, each one shift step `step` ahead of the
; next, ending in a return.
    .macro  chain name, step:vararg
    .irp    n, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1
    .globl  \name\()_\n
    .type   \name\()_\n, @function
\name\()_\n:
    \step
    .endr
    ret
    .endm

; A variable count in register `cnt`: a loop of one shift step `step`.
    .macro  varshift name, cnt, step:vararg
    .globl  \name
    .type   \name, @function
\name:
    tst     \cnt
    jeq     2f
1:  \step
    dec     \cnt
    jne     1b
2:  ret
    .size   \name, .-\name
    .endm

; A long long one: the value from R8-R11 to R12-R15, the count from R12 to R11.
    .macro  wideshift name, step:vararg
    .globl  \name
    .type   \name, @function
\name:
    mov     r11, r15
    mov     r12, r11
    mov     r10, r14
    mov     r9, r13
    mov     r8, r12
    tst     r11
    jeq     2f
1:  \step
    dec     r11
    jne     1b
2:  ret
    .size   \name, .-\name
    .endm

    .macro  sla16
    rla     r12
    .endm
    .macro  srl16
    clrc
    rrc     r12
    .endm
    .macro  sra16
    rra     r12
    .endm
    .macro  sla32
    rla     r12
    rlc     r13
    .endm
    .macro  srl32
    clrc
    rrc     r13
    rrc     r12
    .endm
    .macro  sra32
    rra     r13
    rrc     r12
    .endm
    .macro  sla64
    rla     r12
    rlc     r13
    rlc     r14
    rlc     r15
    .endm
    .macro  srl64
    clrc
    rrc     r15
    rrc     r14
    rrc     r13
    rrc     r12
    .endm
    .macro  sra64
    rra     r15
    rrc     r14
    rrc     r13
    rrc     r12
    .endm

    varshift __mspabi_slli, r13, sla16
    varshift __mspabi_srli, r13, srl16
    varshift __mspabi_srai, r13, sra16
    varshift __mspabi_slll, r14, sla32
    varshift __mspabi_srll, r14, srl32
    varshift __mspabi_sral, r14, sra32
    wideshift __mspabi_sllll, sla64
    wideshift __mspabi_srlll, srl64
    wideshift __mspabi_srall, sra64

    chain   __mspabi_slli, sla16
    chain   __mspabi_srli, srl16
    chain   __mspabi_srai, sra16
    chain   __mspabi_slll, sla32
    chain   __mspabi_srll, srl32
    chain   __mspabi_sral, sra32

    .irp    n, 7, 6, 5, 4, 3, 2, 1
    .globl  __mspabi_func_epilog_\n
    .type   __mspabi_func_epilog_\n, @function
    .endr
__mspabi_func_epilog_7:
    pop     r4
__mspabi_func_epilog_6:
    pop     r5
__mspabi_func_epilog_5:
    pop     r6
__mspabi_func_epilog_4:
    pop     r7
__mspabi_func_epilog_3:
    pop     r8
__mspabi_func_epilog_2:
    pop     r9
__mspabi_func_epilog_1:
    pop     r10
    ret
