; Console and exit for the ATmega1280 on bare-metal qemu: stdout is USART0 (the first
; qemu -serial), and main's result goes out on USART1 (the second -serial), where the run
; harness reads it.  Nothing on qemu's AVR machine makes qemu exit, so the harness stops
; it once that byte has arrived.
    .equ    UCSR0A, 0xc0
    .equ    UDR0, 0xc6
    .equ    UCSR1A, 0xc8
    .equ    UDR1, 0xce
    .equ    UDRE, 5

    .text
; void putbyte(int b): write one byte, waiting until the transmitter is ready.
    .globl  putbyte
    .type   putbyte, @function
putbyte:
1:  lds     r25, UCSR0A
    sbrs    r25, UDRE
    rjmp    1b
    sts     UDR0, r24
    ret
    .size   putbyte, .-putbyte

; void putch(unsigned c): putbyte.
    .globl  putch
    .type   putch, @function
putch:
    rjmp    putbyte
    .size   putch, .-putch

; void flush(void): output is unbuffered.
    .globl  flush
    .type   flush, @function
flush:
    ret
    .size   flush, .-flush

; _Noreturn void exit(int status): the low byte of the status goes to USART1, and the
; CPU stops.  A stack that ran into the canary below it (crt0.S) is reported first, and
; the status is then 0xfd.
    .globl  exit
    .type   exit, @function
exit:
    lds     r22, __stack_canary
    cpi     r22, 0xa5
    brne    1f
    lds     r22, __stack_canary + 1
    cpi     r22, 0x5a
    breq    3f
1:  ldi     r30, lo8(overflow_msg)
    ldi     r31, hi8(overflow_msg)
2:  ld      r24, Z+
    tst     r24
    breq    5f
    rcall   putbyte
    rjmp    2b
5:  ldi     r24, 0xfd
3:  lds     r25, UCSR1A
    sbrs    r25, UDRE
    rjmp    3b
    sts     UDR1, r24
4:  cli
    sleep
    rjmp    4b
    .size   exit, .-exit

    .section .rodata
overflow_msg:
    .asciz  "stack overflow\n"
