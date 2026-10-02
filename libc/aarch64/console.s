// Console and exit for qemu `virt`: the PL011 UART at 0x09000000, and semihosting
// (qemu -semihosting) for the exit status.

    .equ    UART, 0x09000000
    .equ    UART_FR, 0x18
    .equ    FR_TXFF_BIT, 5
    .equ    SYS_EXIT_EXTENDED, 0x20
    .equ    ADP_STOPPED_APPLICATION_EXIT, 0x20026

    .text

// void putbyte(int b): write one byte, waiting while the transmit FIFO is full.
    .globl  putbyte
    .p2align 2
    .type   putbyte, @function
putbyte:
    mov     x9, #UART
1:  ldr     w10, [x9, #UART_FR]
    tbnz    w10, #FR_TXFF_BIT, 1b
    strb    w0, [x9]
    ret
    .size   putbyte, .-putbyte

// void putch(unsigned c): putbyte.
    .globl  putch
    .p2align 2
    .type   putch, @function
putch:
    b       putbyte
    .size   putch, .-putch

// void flush(void): output is unbuffered.
    .globl  flush
    .p2align 2
    .type   flush, @function
flush:
    ret
    .size   flush, .-flush

// _Noreturn void exit(int status): stop qemu, which exits with `status` (modulo 256).
    .globl  exit
    .p2align 2
    .type   exit, @function
exit:
    sxtw    x0, w0
    ldr     x1, =ADP_STOPPED_APPLICATION_EXIT
    stp     x1, x0, [sp, #-16]!
    mov     x1, sp
    mov     w0, #SYS_EXIT_EXTENDED
    hlt     #0xf000
1:  b       1b
    .size   exit, .-exit
