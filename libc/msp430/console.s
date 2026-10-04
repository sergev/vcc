; Console and exit for the classic MSP430 under mspsim: stdout is the USCI_A0 UART at
; the MSP430G2xx addresses, and exit writes main's result to mspsim's stop register,
; which ends the run with it as the exit status.
    .equ    IFG2, 0x0003
    .equ    UCA0TXIFG, 0x02
    .equ    UCA0TXBUF, 0x0067
    .equ    SIM_STOP, 0x01fe
    .equ    CPUOFF, 0x0010

    .text
; void putbyte(int b): write one byte, waiting until the transmitter is ready.
    .globl  putbyte
    .type   putbyte, @function
putbyte:
1:  bit.b   #UCA0TXIFG, &IFG2
    jeq     1b
    mov.b   r12, &UCA0TXBUF
    ret
    .size   putbyte, .-putbyte

; void putch(unsigned c): putbyte.
    .globl  putch
    .type   putch, @function
putch:
    br      #putbyte
    .size   putch, .-putch

; void flush(void): output is unbuffered.
    .globl  flush
    .type   flush, @function
flush:
    ret
    .size   flush, .-flush

; _Noreturn void exit(int status): the status goes to the stop register, whose low byte
; is mspsim's exit status.  A stack that ran into the canary below it (crt0.S) is
; reported first, and the status is then 0xfd.  On a device without the stop register
; the CPU sleeps with interrupts off.
    .globl  exit
    .type   exit, @function
exit:
    cmp     #0xa55a, &__stack_canary
    jeq     3f
    mov     #overflow_msg, r10
1:  mov.b   @r10+, r12
    tst.b   r12
    jeq     2f
    call    #putbyte
    jmp     1b
2:  mov     #0xfd, r12
3:  mov     r12, &SIM_STOP
4:  dint
    nop
    bis     #CPUOFF, r2
    jmp     4b
    .size   exit, .-exit

    .section .rodata
overflow_msg:
    .asciz  "stack overflow\n"
