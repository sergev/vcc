// C startup for bare-metal qemu `virt` at EL1: set sp, enable FP/SIMD, install the
// exception vectors, turn on the MMU and caches over an identity map, clear .bss, call
// main and pass its result to exit.  Built with -DPRINT_STATUS it first prints main's
// result as "%d\n" (the book run tests compare it).
//
// With the MMU off every access is to Device memory, where an unaligned access faults;
// the identity map makes RAM Normal memory, as on any real AArch64 system.  Its level-1
// table maps 1 GiB blocks: 0-1 GiB Device-nGnRnE, never executable (flash, the PL011
// UART at 0x09000000), and 1-2 GiB Normal write-back cacheable (RAM, from 0x40000000).

    .equ    MAIR_VALUE, 0xff00          // attr 0 Device-nGnRnE, attr 1 Normal WB
    // T0SZ = 32 (4 GiB, the walk starts at level 1), inner/outer write-back walks,
    // inner shareable, 4 KiB granule, no TTBR1 walks (EPD1), 32-bit physical addresses.
    .equ    TCR_VALUE, 32 | (1 << 8) | (1 << 10) | (3 << 12) | (1 << 23)
    .equ    SCTLR_MMU, 1                // M
    .equ    SCTLR_DCACHE, 1 << 2        // C
    .equ    SCTLR_ALIGN, 1 << 1         // A: alignment checking, kept off
    .equ    SCTLR_ICACHE, 1 << 12       // I

    .section .text.start, "ax"
    .globl  _start
_start:
    adrp    x0, __stack_top
    add     x0, x0, :lo12:__stack_top
    mov     sp, x0
    mov     x0, #(3 << 20)              // CPACR_EL1.FPEN: FP/SIMD does not trap
    msr     cpacr_el1, x0
    adrp    x0, vectors
    add     x0, x0, :lo12:vectors
    msr     vbar_el1, x0
    isb

    mov     x0, #MAIR_VALUE
    msr     mair_el1, x0
    ldr     x0, =TCR_VALUE
    msr     tcr_el1, x0
    adrp    x0, l1_table
    add     x0, x0, :lo12:l1_table
    msr     ttbr0_el1, x0
    isb
    tlbi    vmalle1
    dsb     ish
    isb
    mrs     x0, sctlr_el1
    mov     x1, #(SCTLR_MMU | SCTLR_DCACHE | SCTLR_ICACHE)
    orr     x0, x0, x1
    bic     x0, x0, #SCTLR_ALIGN
    msr     sctlr_el1, x0
    isb

    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, __bss_end
    add     x1, x1, :lo12:__bss_end
1:  cmp     x0, x1
    b.hs    2f
    str     xzr, [x0], #8
    b       1b
2:  bl      main
#ifdef PRINT_STATUS
    mov     w19, w0                     // the status, for exit
    mov     w20, w0
    tbz     w20, #31, 3f
    mov     w0, #'-'
    bl      putbyte
    neg     w20, w20
3:  sub     sp, sp, #16
    mov     x21, sp                     // digits, least significant first
    mov     w22, #10
4:  udiv    w1, w20, w22
    msub    w2, w1, w22, w20
    add     w2, w2, #'0'
    strb    w2, [x21], #1
    mov     w20, w1
    cbnz    w20, 4b
5:  ldrb    w0, [x21, #-1]!
    bl      putbyte
    mov     x9, sp
    cmp     x21, x9
    b.ne    5b
    mov     w0, #'\n'
    bl      putbyte
    mov     w0, w19
#endif
    bl      exit

// Any exception (an undefined instruction, a translation or alignment fault, an
// interrupt) prints its kind, ESR_EL1, ELR_EL1 and FAR_EL1, and ends the run with
// exit status 255 rather than hanging.  Each of the 16 entries is 0x80 bytes apart.
    .text
    .p2align 11
vectors:
    .irp    kind, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    .p2align 7
    mov     x19, #\kind
    b       trap
    .endr

trap:
    adrp    x20, trap_text
    add     x20, x20, :lo12:trap_text
    bl      puts_raw
    mov     x0, x19
    bl      puthex
    bl      puts_raw                    // " esr="
    mrs     x0, esr_el1
    bl      puthex
    bl      puts_raw                    // " elr="
    mrs     x0, elr_el1
    bl      puthex
    bl      puts_raw                    // " far="
    mrs     x0, far_el1
    bl      puthex
    mov     w0, #'\n'
    bl      putbyte
    mov     w0, #255
    bl      exit

// Print the NUL-terminated string at x20 and leave x20 just past it.
puts_raw:
    mov     x23, x30
1:  ldrb    w0, [x20], #1
    cbz     w0, 2f
    bl      putbyte
    b       1b
2:  ret     x23

// Print x0 as 16 hex digits.
puthex:
    mov     x24, x30
    mov     x21, x0
    mov     x22, #60
1:  lsr     x0, x21, x22
    and     x0, x0, #0xf
    cmp     x0, #10
    add     x1, x0, #'0'
    add     x0, x0, #('a' - 10)
    csel    x0, x1, x0, lo
    bl      putbyte
    subs    x22, x22, #4
    b.pl    1b
    ret     x24

    .section .rodata
trap_text:
    .asciz  "\nexception: vector "
    .asciz  " esr="
    .asciz  " elr="
    .asciz  " far="

    .p2align 12
l1_table:
    .quad   0x0060000000000401          // 0 GiB: Device (attr 0), AF, UXN, PXN
    .quad   0x0000000040000705          // 1 GiB: Normal (attr 1), AF, inner shareable
    .quad   0
    .quad   0
