; setjmp/longjmp for the ATmega1280.  The buffer, 23 bytes: r2-r17, r28, r29, SP after
; the return (low, high), SREG, the return address (a word address, low, high).

    .equ    __SREG__, 0x3f
    .equ    __SP_H__, 0x3e
    .equ    __SP_L__, 0x3d

    .text
; int setjmp(jmp_buf env): env in r25:r24.
    .globl  setjmp
setjmp:
    movw    r26, r24
    st      X+, r2
    st      X+, r3
    st      X+, r4
    st      X+, r5
    st      X+, r6
    st      X+, r7
    st      X+, r8
    st      X+, r9
    st      X+, r10
    st      X+, r11
    st      X+, r12
    st      X+, r13
    st      X+, r14
    st      X+, r15
    st      X+, r16
    st      X+, r17
    st      X+, r28
    st      X+, r29
    pop     r31                         ; the return address, high byte on top
    pop     r30
    in      r0, __SP_L__
    st      X+, r0
    in      r0, __SP_H__
    st      X+, r0
    in      r0, __SREG__
    st      X+, r0
    st      X+, r30
    st      X+, r31
    clr     r24
    clr     r25
    ijmp

; _Noreturn void longjmp(jmp_buf env, int val): env in r25:r24, val in r23:r22;
; setjmp then returns val, or 1 for 0.
    .globl  longjmp
longjmp:
    movw    r26, r24
    ld      r2, X+
    ld      r3, X+
    ld      r4, X+
    ld      r5, X+
    ld      r6, X+
    ld      r7, X+
    ld      r8, X+
    ld      r9, X+
    ld      r10, X+
    ld      r11, X+
    ld      r12, X+
    ld      r13, X+
    ld      r14, X+
    ld      r15, X+
    ld      r16, X+
    ld      r17, X+
    ld      r28, X+
    ld      r29, X+
    ld      r30, X+
    ld      r31, X+
    ld      r0, X+
    cli                                 ; SP and SREG as setjmp found them
    out     __SP_H__, r31
    out     __SREG__, r0
    out     __SP_L__, r30
    ld      r30, X+
    ld      r31, X+
    movw    r24, r22
    cp      r24, r1
    cpc     r25, r1
    brne    1f
    ldi     r24, 1
1:  ijmp
