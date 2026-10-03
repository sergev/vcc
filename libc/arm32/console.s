// Console and exit for qemu `virt`: the PL011 UART at 0x09000000, and semihosting
// (qemu -semihosting) for the exit status.

    .syntax unified
    .arm

    .equ    UART, 0x09000000
    .equ    UART_FR, 0x18
    .equ    FR_TXFF, 1 << 5
    .equ    SYS_EXIT_EXTENDED, 0x20
    .equ    ADP_STOPPED_APPLICATION_EXIT, 0x20026

    .text

// void putbyte(int b): write one byte, waiting while the transmit FIFO is full.
    .globl  putbyte
    .p2align 2
    .type   putbyte, %function
putbyte:
    movw    r12, #:lower16:UART
    movt    r12, #:upper16:UART
1:  ldr     r1, [r12, #UART_FR]
    tst     r1, #FR_TXFF
    bne     1b
    strb    r0, [r12]
    bx      lr
    .size   putbyte, .-putbyte

// void putch(unsigned c): putbyte.
    .globl  putch
    .p2align 2
    .type   putch, %function
putch:
    b       putbyte
    .size   putch, .-putch

// void flush(void): output is unbuffered.
    .globl  flush
    .p2align 2
    .type   flush, %function
flush:
    bx      lr
    .size   flush, .-flush

// _Noreturn void exit(int status): stop qemu, which exits with `status` (modulo 256).
// SYS_EXIT_EXTENDED takes the address of {reason, status}; ARMv7 traps to the
// semihosting host with `svc 0x123456`.
    .globl  exit
    .p2align 2
    .type   exit, %function
exit:
    mov     r2, r0
    ldr     r1, =ADP_STOPPED_APPLICATION_EXIT
    push    {r1, r2}                    // {reason, status}: the lower register lower
    mov     r1, sp
    mov     r0, #SYS_EXIT_EXTENDED
    svc     0x123456
1:  b       1b
    .size   exit, .-exit
