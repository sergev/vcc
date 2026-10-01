# Console and exit for qemu `virt`: the ns16550 UART at 0x10000000 and the SiFive
# test finisher at 0x100000.

    .equ    UART, 0x10000000
    .equ    UART_LSR, 5
    .equ    LSR_THRE, 0x20
    .equ    FINISHER, 0x100000

    .text

# void putbyte(int b): write one byte, waiting for the transmitter.
    .globl  putbyte
    .p2align 2
putbyte:
    li      t0, UART
1:  lbu     t1, UART_LSR(t0)
    andi    t1, t1, LSR_THRE
    beqz    t1, 1b
    sb      a0, 0(t0)
    ret

# int putchar(int c), void putch(unsigned c): putbyte, returning c.
    .globl  putchar
    .globl  putch
    .p2align 2
putchar:
putch:
    li      t0, UART
1:  lbu     t1, UART_LSR(t0)
    andi    t1, t1, LSR_THRE
    beqz    t1, 1b
    sb      a0, 0(t0)
    andi    a0, a0, 0xff
    ret

# int puts(const char *s): the string and a newline.
    .globl  puts
    .p2align 2
puts:
    li      t0, UART
1:  lbu     t2, 0(a0)
    beqz    t2, 3f
2:  lbu     t1, UART_LSR(t0)
    andi    t1, t1, LSR_THRE
    beqz    t1, 2b
    sb      t2, 0(t0)
    addi    a0, a0, 1
    j       1b
3:  lbu     t1, UART_LSR(t0)
    andi    t1, t1, LSR_THRE
    beqz    t1, 3b
    li      t2, '\n'
    sb      t2, 0(t0)
    li      a0, 0
    ret

# void flush(void): output is unbuffered.
    .globl  flush
    .p2align 2
flush:
    ret

# _Noreturn void exit(int status): stop qemu.  Status 0 passes (qemu exits 0); any
# other fails with the status as qemu's exit code.
    .globl  exit
    .p2align 2
exit:
    li      t0, FINISHER
    li      t1, 0x5555
    beqz    a0, 1f
    slli    t1, a0, 16
    li      t2, 0x3333
    or      t1, t1, t2
1:  sw      t1, 0(t0)
2:  j       2b
