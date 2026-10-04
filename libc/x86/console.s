// Console and exit for bare-metal qemu: the 16550 UART COM1 at I/O port 0x3f8
// (qemu -serial), the debug console at port 0xe9 (qemu -debugcon) and the
// isa-debug-exit device at port 0xf4 (qemu -device isa-debug-exit,iobase=0xf4).

    .equ    COM1, 0x3f8
    .equ    COM1_LSR, COM1 + 5
    .equ    LSR_THRE, 0x20
    .equ    DEBUGCON, 0xe9
    .equ    DEBUG_EXIT, 0xf4

    .text

// void putbyte(int b): write one byte, waiting until the transmitter is ready.
    .globl  putbyte
    .p2align 4
    .type   putbyte, @function
putbyte:
    movw    $COM1_LSR, %dx
1:  inb     %dx, %al
    testb   $LSR_THRE, %al
    jz      1b
    movl    %edi, %eax
    movw    $COM1, %dx
    outb    %al, %dx
    ret
    .size   putbyte, .-putbyte

// void putch(unsigned c): putbyte.
    .globl  putch
    .p2align 4
    .type   putch, @function
putch:
    jmp     putbyte
    .size   putch, .-putch

// void flush(void): output is unbuffered.
    .globl  flush
    .p2align 4
    .type   flush, @function
flush:
    ret
    .size   flush, .-flush

// _Noreturn void exit(int status): the status byte goes to the debug console, where
// the run harness reads it, then the exit device stops qemu.  That device makes qemu
// exit with (value << 1) | 1, which an 8-bit process status cannot hold whole.
    .globl  exit
    .p2align 4
    .type   exit, @function
exit:
    movl    %edi, %eax
    movw    $DEBUGCON, %dx
    outb    %al, %dx
    movw    $DEBUG_EXIT, %dx
    outl    %eax, %dx
1:  cli
    hlt
    jmp     1b
    .size   exit, .-exit
