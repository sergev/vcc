; Integer multiply, under the MSP430 EABI names clang's code calls when the device has no
; hardware multiplier (clang's default).  Ordinary calls: the operands in R12, R13 (int)
; or R12:R13, R14:R15 (long), the product in R12 or R12:R13.  They clobber at most
; R11-R15, the call-clobbered registers, and preserve R4-R10.
;
;   __mspabi_mpyi  R12 * R13         -> R12        clobbers R13, R14
;   __mspabi_mpyl  R12:R13 * R14:R15 -> R12:R13    clobbers R11, R14, R15
;
; Both are shift-and-add over the multiplier, low bit first, and stop when no multiplier
; bit is left.  The low half of the product is the same signed or unsigned.
    .text

    .globl  __mspabi_mpyi
    .type   __mspabi_mpyi, @function
__mspabi_mpyi:
    mov     r12, r14                    ; the multiplicand, shifted left as we go
    clr     r12                         ; the product
1:  tst     r13
    jeq     3f
    clrc
    rrc     r13                         ; the next multiplier bit into C
    jnc     2f
    add     r14, r12
2:  rla     r14
    jmp     1b
3:  ret
    .size   __mspabi_mpyi, .-__mspabi_mpyi

    .globl  __mspabi_mpyl
    .type   __mspabi_mpyl, @function
__mspabi_mpyl:
    push    r10
    mov     r12, r10                    ; the multiplicand r11:r10
    mov     r13, r11
    clr     r12                         ; the product r13:r12
    clr     r13
1:  tst     r14
    jne     2f
    tst     r15
    jeq     4f
2:  clrc
    rrc     r15
    rrc     r14                         ; the next multiplier bit into C
    jnc     3f
    add     r10, r12
    addc    r11, r13
3:  rla     r10
    rlc     r11
    jmp     1b
4:  pop     r10
    ret
    .size   __mspabi_mpyl, .-__mspabi_mpyl
