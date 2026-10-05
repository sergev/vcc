; Integer division and remainder, under the MSP430 EABI names clang's code calls.
; Ordinary calls: the dividend in R12 (int) or R12:R13 (long), the divisor in R13 or
; R14:R15, the result in R12 or R12:R13.  They clobber at most R11-R15, the
; call-clobbered registers, and preserve R4-R10.  As in TI's runtime, a divide also
; leaves the remainder in R14 (int) or R14:R15 (long), which the remainder entry points
; return; clang does not rely on it.
;
;   __mspabi_divu   R12 / R13          -> R12      remainder R14      clobbers R13, R15
;   __mspabi_remu   R12 % R13          -> R12                         clobbers R13-R15
;   __mspabi_divi   R12 / R13          -> R12      remainder R14      clobbers R11, R13, R15
;   __mspabi_remi   R12 % R13          -> R12                         clobbers R11, R13-R15
;   __mspabi_divul  R12:R13 / R14:R15  -> R12:R13  remainder R14:R15  clobbers R11
;   __mspabi_remul  R12:R13 % R14:R15  -> R12:R13                     clobbers R11, R14, R15
;   __mspabi_divli  R12:R13 / R14:R15  -> R12:R13  remainder R14:R15  clobbers R11
;   __mspabi_remli  R12:R13 % R14:R15  -> R12:R13                     clobbers R11, R14, R15
;
; The quotient truncates toward zero and the remainder takes the dividend's sign (C11
; §6.5.5).  A zero divisor gives an all-ones quotient and the dividend as the remainder
; (unsigned); the most negative dividend over -1 gives itself.
;
; The unsigned ones divide by restoring shift-and-subtract: the dividend shifts left
; into the remainder one bit at a time, and each quotient bit shifts into the dividend's
; vacated low bit.  A remainder that carries out of its width is surely at least the
; divisor.
    .text

    .globl  __mspabi_divu
    .type   __mspabi_divu, @function
__mspabi_divu:
    clr     r14                         ; the remainder
    mov     #16, r15
1:  rla     r12                         ; the next dividend bit out, a quotient 0 in
    rlc     r14
    jc      2f
    cmp     r13, r14
    jlo     3f                          ; remainder < divisor: the quotient bit is 0
2:  sub     r13, r14
    bis     #1, r12
3:  dec     r15
    jne     1b
    ret
    .size   __mspabi_divu, .-__mspabi_divu

    .globl  __mspabi_remu
    .type   __mspabi_remu, @function
__mspabi_remu:
    call    #__mspabi_divu
    mov     r14, r12
    ret
    .size   __mspabi_remu, .-__mspabi_remu

; R11 bit 0: negate the quotient; bit 1: negate the remainder.
    .globl  __mspabi_divi
    .type   __mspabi_divi, @function
__mspabi_divi:
    clr     r11
    tst     r12
    jge     1f
    inv     r12
    inc     r12
    xor     #3, r11
1:  tst     r13
    jge     2f
    inv     r13
    inc     r13
    xor     #1, r11
2:  call    #__mspabi_divu              ; leaves R11 alone
    bit     #1, r11
    jeq     3f
    inv     r12
    inc     r12
3:  bit     #2, r11
    jeq     4f
    inv     r14
    inc     r14
4:  ret
    .size   __mspabi_divi, .-__mspabi_divi

    .globl  __mspabi_remi
    .type   __mspabi_remi, @function
__mspabi_remi:
    call    #__mspabi_divi
    mov     r14, r12
    ret
    .size   __mspabi_remi, .-__mspabi_remi

; The remainder in R11:R10, the bit count in R9.
    .globl  __mspabi_divul
    .globl  __mspabi_divlu              ; GCC's other name for it
    .type   __mspabi_divlu, @function
    .type   __mspabi_divul, @function
__mspabi_divul:
__mspabi_divlu:
    push    r10
    push    r9
    clr     r10
    clr     r11
    mov     #32, r9
1:  rla     r12                         ; the next dividend bit out, a quotient 0 in
    rlc     r13
    rlc     r10
    rlc     r11
    jc      2f
    cmp     r15, r11
    jlo     3f                          ; remainder < divisor: the quotient bit is 0
    jne     2f
    cmp     r14, r10
    jlo     3f
2:  sub     r14, r10
    subc    r15, r11
    bis     #1, r12
3:  dec     r9
    jne     1b
    mov     r10, r14
    mov     r11, r15
    pop     r9
    pop     r10
    ret
    .size   __mspabi_divul, .-__mspabi_divul

    .globl  __mspabi_remul
    .type   __mspabi_remul, @function
__mspabi_remul:
    call    #__mspabi_divul
    mov     r14, r12
    mov     r15, r13
    ret
    .size   __mspabi_remul, .-__mspabi_remul

; R10 bit 0: negate the quotient; bit 1: negate the remainder.
    .globl  __mspabi_divli
    .type   __mspabi_divli, @function
__mspabi_divli:
    push    r10
    clr     r10
    tst     r13
    jge     1f
    inv     r12
    inv     r13
    inc     r12
    adc     r13
    xor     #3, r10
1:  tst     r15
    jge     2f
    inv     r14
    inv     r15
    inc     r14
    adc     r15
    xor     #1, r10
2:  call    #__mspabi_divul             ; preserves R10
    bit     #1, r10
    jeq     3f
    inv     r12
    inv     r13
    inc     r12
    adc     r13
3:  bit     #2, r10
    jeq     4f
    inv     r14
    inv     r15
    inc     r14
    adc     r15
4:  pop     r10
    ret
    .size   __mspabi_divli, .-__mspabi_divli

    .globl  __mspabi_remli
    .type   __mspabi_remli, @function
__mspabi_remli:
    call    #__mspabi_divli
    mov     r14, r12
    mov     r15, r13
    ret
    .size   __mspabi_remli, .-__mspabi_remli
