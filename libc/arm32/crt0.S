// C startup for bare-metal qemu `virt` in SVC mode: set sp, install the exception
// vectors, enable VFP, clear .bss, turn on the MMU and caches over an identity map,
// call main and pass its result to exit.  Built with -DPRINT_STATUS it first prints
// main's result as "%d\n" (the book run tests compare it).
//
// With the MMU off every access is Strongly-ordered, where an unaligned access faults;
// the identity map makes RAM Normal memory, as on any real ARMv7-A system.  Its
// short-descriptor level-1 table maps 1 MiB sections: 0-1 GiB Device, never executable
// (flash, the PL011 UART at 0x09000000), 1-2 GiB Normal write-back cacheable (RAM, from
// 0x40000000), and nothing above.  The table is built at run time, after .bss is clear.

    .syntax unified
    .arm

    .equ    SECTION, 0x2                // section descriptor, domain 0
    .equ    AP_RW, 0x3 << 10            // AP = 11: read/write at any privilege
    .equ    SEC_DEVICE, SECTION | AP_RW | (1 << 4) | (1 << 2)   // XN, shareable Device (B)
    .equ    SEC_NORMAL, SECTION | AP_RW | (1 << 16) | (1 << 12) | (3 << 2)  // S, TEX=001 C B: WBWA
    .equ    SCTLR_MMU, 1                // M
    .equ    SCTLR_ALIGN, 1 << 1         // A: alignment checking, kept off
    .equ    SCTLR_DCACHE, 1 << 2        // C
    .equ    SCTLR_ICACHE, 1 << 12       // I

    .section .text.start, "ax"
    .globl  _start
_start:
    ldr     sp, =__stack_top
    ldr     r0, =vectors
    mcr     p15, 0, r0, c12, c0, 0      // VBAR
    mrc     p15, 0, r0, c1, c0, 2       // CPACR: cp10 and cp11 full access
    orr     r0, r0, #(0xf << 20)
    mcr     p15, 0, r0, c1, c0, 2
    isb
    mov     r0, #(1 << 30)              // FPEXC.EN
    vmsr    fpexc, r0

    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b

    ldr     r0, =l1_table               // 4096 sections, identity
    ldr     r2, =SEC_DEVICE
    ldr     r3, =SEC_NORMAL
    mov     r1, #0
2:  cmp     r1, #0x400
    orrlo   r12, r2, r1, lsl #20
    blo     3f
    cmp     r1, #0x800
    orrlo   r12, r3, r1, lsl #20
    movhs   r12, #0                     // fault
3:  str     r12, [r0, r1, lsl #2]
    add     r1, r1, #1
    cmp     r1, #4096
    bne     2b
    mcr     p15, 0, r0, c2, c0, 0       // TTBR0: non-cacheable walks
    mov     r0, #0
    mcr     p15, 0, r0, c2, c0, 2       // TTBCR: TTBR0 only, short descriptors
    mov     r0, #1
    mcr     p15, 0, r0, c3, c0, 0       // DACR: domain 0 client
    mov     r0, #0
    mcr     p15, 0, r0, c8, c7, 0       // TLBIALL
    mcr     p15, 0, r0, c7, c5, 0       // ICIALLU
    dsb
    isb
    mrc     p15, 0, r0, c1, c0, 0       // SCTLR
    ldr     r1, =(SCTLR_MMU | SCTLR_DCACHE | SCTLR_ICACHE)
    orr     r0, r0, r1
    bic     r0, r0, #SCTLR_ALIGN
    mcr     p15, 0, r0, c1, c0, 0
    isb

    bl      main
#ifdef PRINT_STATUS
    mov     r4, r0                      // the status, for exit
    mov     r5, r0
    cmp     r5, #0
    bge     4f
    mov     r0, #'-'
    bl      putbyte
    rsb     r5, r5, #0
4:  sub     sp, sp, #16
    mov     r6, sp                      // digits, least significant first
    mov     r7, #10
5:  udiv    r1, r5, r7
    mls     r2, r1, r7, r5
    add     r2, r2, #'0'
    strb    r2, [r6], #1
    movs    r5, r1
    bne     5b
6:  ldrb    r0, [r6, #-1]!
    bl      putbyte
    cmp     r6, sp
    bne     6b
    mov     r0, #'\n'
    bl      putbyte
    mov     r0, r4
#endif
    bl      exit

// Any exception (an undefined instruction, an abort, an interrupt) prints its kind,
// the faulting address (lr of the exception mode), DFSR, DFAR and IFSR, and ends the
// run with exit status 255 rather than hanging.  The exception modes have no stack, so
// the handler takes their lr and goes back to SVC mode, whose stack exit needs.
// Kinds: 1 undefined, 2 svc, 3 prefetch abort, 4 data abort, 5 unused, 6 irq, 7 fiq.
    .text
    .p2align 5
vectors:
    .irp    kind, 0, 1, 2, 3, 4, 5, 6, 7
    b       trap\kind
    .endr
    .irp    kind, 0, 1, 2, 3, 4, 5, 6, 7
trap\kind:
    mov     r4, #\kind
    b       trap
    .endr

trap:
    mov     r5, lr
    cps     #0x13                       // SVC mode, interrupts as they were
    ldr     r6, =trap_text
    bl      puts_raw
    mov     r0, r4
    bl      puthex
    bl      puts_raw                    // " lr="
    mov     r0, r5
    bl      puthex
    bl      puts_raw                    // " dfsr="
    mrc     p15, 0, r0, c5, c0, 0
    bl      puthex
    bl      puts_raw                    // " dfar="
    mrc     p15, 0, r0, c6, c0, 0
    bl      puthex
    bl      puts_raw                    // " ifsr="
    mrc     p15, 0, r0, c5, c0, 1
    bl      puthex
    mov     r0, #'\n'
    bl      putbyte
    mov     r0, #255
    bl      exit

// Print the NUL-terminated string at r6 and leave r6 just past it.
puts_raw:
    mov     r9, lr
1:  ldrb    r0, [r6], #1
    cmp     r0, #0
    beq     2f
    bl      putbyte
    b       1b
2:  bx      r9

// Print r0 as 8 hex digits.
puthex:
    mov     r10, lr
    mov     r7, r0
    mov     r8, #28
1:  lsr     r0, r7, r8
    and     r0, r0, #0xf
    cmp     r0, #10
    addlo   r0, r0, #'0'
    addhs   r0, r0, #('a' - 10)
    bl      putbyte
    subs    r8, r8, #4
    bpl     1b
    bx      r10

    .section .rodata
trap_text:
    .asciz  "\nexception: vector "
    .asciz  " lr="
    .asciz  " dfsr="
    .asciz  " dfar="
    .asciz  " ifsr="

    .bss
    .p2align 14
l1_table:
    .space  16384
