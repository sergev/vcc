; 32-bit multiplication, under its libgcc name, which clang's code calls.  The register
; contract is avr-gcc's, narrower than an ordinary call's: clang assumes only an ordinary
; call's (every call-clobbered register), and the AVR backend may rely on the narrow one.
;
;   __mulsi3    r25:r22 = r25:r22 * r21:r18     clobbers r0, r26, r27, r30, r31
    .text
    .globl  __mulsi3
    .type   __mulsi3, @function
__mulsi3:
    ; The low 32 bits of A * B, A = a3:a2:a1:a0 = r25:r24:r23:r22,
    ; B = b3:b2:b1:b0 = r21:r20:r19:r18, accumulated in r31:r30:r27:r26.
    mul     r22, r18                    ; a0*b0
    movw    r26, r0
    mul     r22, r20                    ; a0*b2
    movw    r30, r0
    mul     r22, r19                    ; a0*b1 << 8
    add     r27, r0
    adc     r30, r1
    clr     r1                          ; eor: keeps the carry
    adc     r31, r1
    mul     r23, r18                    ; a1*b0 << 8
    add     r27, r0
    adc     r30, r1
    clr     r1
    adc     r31, r1
    mul     r23, r19                    ; a1*b1 << 16
    add     r30, r0
    adc     r31, r1
    mul     r24, r18                    ; a2*b0 << 16
    add     r30, r0
    adc     r31, r1
    mul     r22, r21                    ; a0*b3 << 24
    add     r31, r0
    mul     r23, r20                    ; a1*b2 << 24
    add     r31, r0
    mul     r24, r19                    ; a2*b1 << 24
    add     r31, r0
    mul     r25, r18                    ; a3*b0 << 24
    add     r31, r0
    clr     r1
    movw    r22, r26
    movw    r24, r30
    ret
    .size   __mulsi3, .-__mulsi3
