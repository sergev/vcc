; C startup for the classic MSP430 under mspsim: stop the watchdog, set the stack, copy
; .data from its load address, clear .bss, call main and pass its result to exit.
; Built with -DPRINT_STATUS it first prints main's result as "%d\n" (the book run tests
; compare it).
;
; Interrupts stay disabled throughout; every vector but reset goes to __bad_interrupt,
; which reports and stops.

    .equ    WDTCTL, 0x0120
    .equ    WDTPW_HOLD, 0x5a80          ; the password and WDTHOLD

    .text
    .globl  _start
    .type   _start, @function
_start:
    mov     #WDTPW_HOLD, &WDTCTL        ; a real device starts with the watchdog running
    mov     #__stack_top, r1

; Copy .data from its load address (in ROM) to RAM.
    mov     #__data_load_start, r14
    mov     #__data_start, r15
    jmp     2f
1:  mov.b   @r14+, r13
    mov.b   r13, 0(r15)
    inc     r15
2:  cmp     #__data_end, r15
    jlo     1b

; Clear .bss.
    mov     #__bss_start, r15
    jmp     2f
1:  clr.b   0(r15)
    inc     r15
2:  cmp     #__bss_end, r15
    jlo     1b

; The canary between .bss and the heap: exit reports a stack that ran into it.
    mov     #0xa55a, &__stack_canary

; GCC's main, and ours, refer to this piece of newlib's startup; here it is a name.
    .globl  __crt0_call_exit
__crt0_call_exit:
    call    #main
#ifdef PRINT_STATUS
    mov     r12, r10                    ; the status, for exit
    mov     r12, r9                     ; the value printed
    tst     r9
    jge     1f
    mov     #'-', r12
    call    #putbyte
    inv     r9                          ; -32768 stays itself, read as unsigned below
    inc     r9
1:  clr     r8                          ; digits pushed, least significant first
2:  mov     r9, r12
    mov     #10, r13
    call    #__mspabi_divu              ; quotient r12, remainder r14
    add     #'0', r14
    push    r14
    inc     r8
    mov     r12, r9
    tst     r9
    jne     2b
3:  pop     r12
    call    #putbyte
    dec     r8
    jne     3b
    mov     #10, r12                    ; '\n'
    call    #putbyte
    mov     r10, r12
#endif
    br      #exit
    .size   _start, .-_start

; An interrupt, which nothing enables: say so, and stop with status 0xfe.
    .type   __bad_interrupt, @function
__bad_interrupt:
    mov     #bad_msg, r10
1:  mov.b   @r10+, r12
    tst.b   r12
    jeq     2f
    call    #putbyte
    jmp     1b
2:  mov     #0xfe, r12
    br      #exit
    .size   __bad_interrupt, .-__bad_interrupt

    .section .rodata
bad_msg:
    .asciz  "unexpected interrupt\n"

; The vector table at 0xffe0: fifteen interrupts, then reset.
    .section .vectors, "ax", @progbits
    .rept   15
    .short  __bad_interrupt
    .endr
    .short  _start

; The canary, outside .bss so that clearing .bss does not touch it.
    .section .noinit, "aw", @nobits
    .p2align 1
    .globl  __stack_canary
__stack_canary:
    .zero   2
