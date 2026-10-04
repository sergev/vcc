; Integer division and remainder, under their libgcc names, which clang's code calls.
; Each returns the quotient and the remainder together.  The register contracts are
; avr-gcc's, narrower than an ordinary call's: clang assumes only an ordinary call's
; (every call-clobbered register), and the AVR backend may rely on the narrow ones.
;
;   __udivmodqi4  r24 / r22         -> quotient r24,     remainder r25      clobbers r23
;   __divmodqi4   r24 / r22         -> quotient r24,     remainder r25      clobbers r0, r22, r23, T
;   __udivmodhi4  r25:r24 / r23:r22 -> quotient r23:r22, remainder r25:r24  clobbers r21, r26, r27
;   __divmodhi4   r25:r24 / r23:r22 -> quotient r23:r22, remainder r25:r24  clobbers r0, r21, r26, r27, T
;   __udivmodsi4  r25:r22 / r21:r18 -> quotient r21:r18, remainder r25:r22  clobbers r26, r27, r30, r31
;   __divmodsi4   r25:r22 / r21:r18 -> quotient r21:r18, remainder r25:r22  clobbers r0, r26, r27, r30, r31, T
;
; The quotient truncates toward zero and the remainder takes the dividend's sign (C11
; §6.5.5).  A zero divisor gives an all-ones quotient and the dividend as the
; remainder (unsigned); the most negative dividend over -1 gives itself.
;
; The unsigned ones divide by restoring shift-and-subtract: the dividend shifts into
; the remainder one bit at a time, and each quotient bit is the inverted carry of the
; compare, so the quotient comes out complemented and is flipped at the end.
    .text

    .globl  __udivmodqi4
    .type   __udivmodqi4, @function
__udivmodqi4:
    sub     r25, r25                    ; remainder 0, carry clear
    ldi     r23, 9                      ; 8 bits, and one more to shift the last in
    rjmp    2f
1:  rol     r25                         ; the next dividend bit into the remainder
    cp      r25, r22
    brcs    2f                          ; remainder < divisor: quotient bit 0 (C = 1)
    sub     r25, r22                    ; quotient bit 1 (C = 0)
2:  rol     r24                         ; the quotient bit in, the next dividend bit out
    dec     r23
    brne    1b
    com     r24
    ret
    .size   __udivmodqi4, .-__udivmodqi4

    .globl  __divmodqi4
    .type   __divmodqi4, @function
__divmodqi4:
    bst     r24, 7                      ; T = sign of the dividend, so of the remainder
    mov     r0, r24
    eor     r0, r22                     ; r0.7 = sign of the quotient
    sbrc    r24, 7
    neg     r24
    sbrc    r22, 7
    neg     r22
    rcall   __udivmodqi4
    sbrc    r0, 7
    neg     r24
    brtc    1f
    neg     r25
1:  ret
    .size   __divmodqi4, .-__divmodqi4

    .globl  __udivmodhi4
    .type   __udivmodhi4, @function
__udivmodhi4:
    sub     r26, r26                    ; remainder r27:r26 = 0, carry clear
    sub     r27, r27
    ldi     r21, 17
    rjmp    2f
1:  rol     r26
    rol     r27
    cp      r26, r22
    cpc     r27, r23
    brcs    2f
    sub     r26, r22
    sbc     r27, r23
2:  rol     r24
    rol     r25
    dec     r21
    brne    1b
    com     r24
    com     r25
    movw    r22, r24                    ; quotient
    movw    r24, r26                    ; remainder
    ret
    .size   __udivmodhi4, .-__udivmodhi4

    .globl  __divmodhi4
    .type   __divmodhi4, @function
__divmodhi4:
    bst     r25, 7                      ; T = sign of the dividend, so of the remainder
    mov     r0, r23
    brtc    1f
    com     r0                          ; r0.7 = sign of the quotient
    rcall   neg_r24                     ; |dividend|
1:  sbrc    r23, 7
    rcall   neg_r22                     ; |divisor|
    rcall   __udivmodhi4
    sbrc    r0, 7
    rcall   neg_r22                     ; the quotient's sign
    brtc    2f
    rjmp    neg_r24                     ; the remainder's sign
2:  ret
neg_r24:
    com     r25
    neg     r24
    sbci    r25, 0xff
    ret
neg_r22:
    com     r23
    neg     r22
    sbci    r23, 0xff
    ret
    .size   __divmodhi4, .-__divmodhi4

    .globl  __udivmodsi4
    .type   __udivmodsi4, @function
__udivmodsi4:
    ldi     r26, 33                     ; the loop count lives in r1, 0 again at the end
    mov     r1, r26
    sub     r26, r26                    ; remainder r31:r30:r27:r26 = 0, carry clear
    sub     r27, r27
    movw    r30, r26
    rjmp    2f
1:  rol     r26
    rol     r27
    rol     r30
    rol     r31
    cp      r26, r18
    cpc     r27, r19
    cpc     r30, r20
    cpc     r31, r21
    brcs    2f
    sub     r26, r18
    sbc     r27, r19
    sbc     r30, r20
    sbc     r31, r21
2:  rol     r22
    rol     r23
    rol     r24
    rol     r25
    dec     r1
    brne    1b
    com     r22
    com     r23
    com     r24
    com     r25
    movw    r18, r22                    ; quotient
    movw    r20, r24
    movw    r22, r26                    ; remainder
    movw    r24, r30
    ret
    .size   __udivmodsi4, .-__udivmodsi4

    .globl  __divmodsi4
    .type   __divmodsi4, @function
__divmodsi4:
    bst     r25, 7                      ; T = sign of the dividend, so of the remainder
    mov     r0, r21
    brtc    1f
    com     r0                          ; r0.7 = sign of the quotient
    rcall   neg_r22_4                   ; |dividend|
1:  sbrc    r21, 7
    rcall   neg_r18_4                   ; |divisor|
    rcall   __udivmodsi4
    sbrc    r0, 7
    rcall   neg_r18_4                   ; the quotient's sign
    brtc    2f
    rjmp    neg_r22_4                   ; the remainder's sign
2:  ret
neg_r22_4:
    com     r25
    com     r24
    com     r23
    neg     r22
    sbci    r23, 0xff
    sbci    r24, 0xff
    sbci    r25, 0xff
    ret
neg_r18_4:
    com     r21
    com     r20
    com     r19
    neg     r18
    sbci    r19, 0xff
    sbci    r20, 0xff
    sbci    r21, 0xff
    ret
    .size   __divmodsi4, .-__divmodsi4
