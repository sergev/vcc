; setjmp/longjmp for the classic MSP430.  The buffer, 9 words: R4-R10, the call-saved
; registers of GCC's ABI, SP after the return, and the return address.  newlib's
; jmp_buf saves the same registers, in another order.  SR is not saved: nothing here
; enables interrupts.
    .text

; int setjmp(jmp_buf env): env in R12.
    .globl  setjmp
    .type   setjmp, @function
setjmp:
    mov     r4, 0(r12)
    mov     r5, 2(r12)
    mov     r6, 4(r12)
    mov     r7, 6(r12)
    mov     r8, 8(r12)
    mov     r9, 10(r12)
    mov     r10, 12(r12)
    pop     r13                         ; the return address
    mov     r1, 14(r12)
    mov     r13, 16(r12)
    clr     r12
    br      r13
    .size   setjmp, .-setjmp

; _Noreturn void longjmp(jmp_buf env, int val): env in R12, val in R13; setjmp then
; returns val, or 1 for 0.
    .globl  longjmp
    .type   longjmp, @function
longjmp:
    mov     @r12+, r4
    mov     @r12+, r5
    mov     @r12+, r6
    mov     @r12+, r7
    mov     @r12+, r8
    mov     @r12+, r9
    mov     @r12+, r10
    mov     @r12+, r1
    mov     @r12, r14
    mov     r13, r12
    tst     r12
    jne     1f
    mov     #1, r12
1:  br      r14
    .size   longjmp, .-longjmp
