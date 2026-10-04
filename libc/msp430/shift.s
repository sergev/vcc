; Shifts of a long by a variable count, under the MSP430 EABI names clang's code calls
; (it shifts an int, and a long long, with an inline loop).  The value in R12:R13, the
; count in R14 (clang zero-extends it from a byte), the result in R12:R13.  A count of
; 32 or more is undefined in C; here it simply shifts that many times.
;
;   __mspabi_slll  R12:R13 << R14            -> R12:R13   clobbers R14
;   __mspabi_srll  R12:R13 >> R14, logical   -> R12:R13   clobbers R14
;   __mspabi_sral  R12:R13 >> R14, signed    -> R12:R13   clobbers R14
    .text

    .globl  __mspabi_slll
    .type   __mspabi_slll, @function
__mspabi_slll:
    tst     r14
    jeq     2f
1:  rla     r12
    rlc     r13
    dec     r14
    jne     1b
2:  ret
    .size   __mspabi_slll, .-__mspabi_slll

    .globl  __mspabi_srll
    .type   __mspabi_srll, @function
__mspabi_srll:
    tst     r14
    jeq     2f
1:  clrc
    rrc     r13
    rrc     r12
    dec     r14
    jne     1b
2:  ret
    .size   __mspabi_srll, .-__mspabi_srll

    .globl  __mspabi_sral
    .type   __mspabi_sral, @function
__mspabi_sral:
    tst     r14
    jeq     2f
1:  rra     r13
    rrc     r12
    dec     r14
    jne     1b
2:  ret
    .size   __mspabi_sral, .-__mspabi_sral
