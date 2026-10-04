// C startup for bare-metal qemu (`microvm`, or any x86 machine) booted through the PVH
// ELF note: qemu enters _start in 32-bit protected mode with paging off.  Switch to
// long mode over an identity map, enable SSE and the x87, install exception handlers,
// clear .bss, call main and pass its result to exit.  Built with -DPRINT_STATUS it
// first prints main's result as "%d\n" (the book run tests compare it).
//
// The map is four 1 GiB pages, 0-4 GiB, write-back: RAM and nothing else, since the
// console and the exit device are I/O ports.  An access above 4 GiB page-faults.
// Interrupts stay disabled throughout, which is what makes the red zone safe.

    .equ    CR0_MP, 1 << 1
    .equ    CR0_EM, 1 << 2
    .equ    CR0_NE, 1 << 5
    .equ    CR0_PG, 1 << 31
    .equ    CR4_PAE, 1 << 5
    .equ    CR4_OSFXSR, 1 << 9
    .equ    CR4_OSXMMEXCPT, 1 << 10
    .equ    MSR_EFER, 0xc0000080
    .equ    EFER_LME, 1 << 8

// The PVH entry point, for qemu -kernel.
    .section .note.Xen, "a", @note
    .p2align 2
    .long   4, 4, 18                    // name size, descriptor size, PHYS32_ENTRY
    .asciz  "Xen"
    .long   _start

    .section .text.start, "ax"
    .code32
    .globl  _start
_start:
    cli
    movl    $pml4, %eax
    movl    %eax, %cr3
    movl    %cr4, %eax
    orl     $(CR4_PAE | CR4_OSFXSR | CR4_OSXMMEXCPT), %eax
    movl    %eax, %cr4
    movl    $MSR_EFER, %ecx
    rdmsr
    orl     $EFER_LME, %eax
    wrmsr
    movl    %cr0, %eax
    andl    $~CR0_EM, %eax
    orl     $(CR0_PG | CR0_NE | CR0_MP), %eax
    movl    %eax, %cr0
    lgdt    gdtr
    ljmp    $8, $long_mode

    .code64
long_mode:
    movl    $16, %eax
    movl    %eax, %ds
    movl    %eax, %es
    movl    %eax, %ss
    xorl    %eax, %eax
    movl    %eax, %fs
    movl    %eax, %gs
    leaq    __stack_top(%rip), %rsp
    fninit                              // 64-bit precision, exceptions masked
    ldmxcsr mxcsr_default(%rip)

    leaq    __bss_start(%rip), %rdi
    leaq    __bss_end(%rip), %rcx
    subq    %rdi, %rcx
    xorl    %eax, %eax
    rep stosb

    // The IDT: an interrupt gate for each of the 32 exception vectors.
    leaq    idt(%rip), %rdi
    leaq    stubs(%rip), %rsi
    movl    $32, %ecx
1:  movq    (%rsi), %rax
    movw    %ax, (%rdi)                 // offset 15:0
    movw    $8, 2(%rdi)                 // code segment
    movw    $0x8e00, 4(%rdi)            // present, DPL 0, interrupt gate
    shrq    $16, %rax
    movw    %ax, 6(%rdi)                // offset 31:16
    shrq    $16, %rax
    movl    %eax, 8(%rdi)               // offset 63:32
    movl    $0, 12(%rdi)
    addq    $8, %rsi
    addq    $16, %rdi
    decl    %ecx
    jnz     1b
    lidt    idtr(%rip)

    call    main
    movl    %eax, %ebx                  // the status, for exit
#ifdef PRINT_STATUS
    movl    %eax, %r12d
    testl   %r12d, %r12d
    jns     2f
    movl    $'-', %edi
    call    putbyte
    negl    %r12d                       // INT_MIN stays itself, read as unsigned below
2:  subq    $16, %rsp
    movq    %rsp, %r13                  // digits, least significant first
3:  movl    %r12d, %eax
    xorl    %edx, %edx
    movl    $10, %ecx
    divl    %ecx
    addb    $'0', %dl
    movb    %dl, (%r13)
    incq    %r13
    movl    %eax, %r12d
    testl   %eax, %eax
    jnz     3b
4:  decq    %r13
    movzbl  (%r13), %edi
    call    putbyte
    cmpq    %rsp, %r13
    jne     4b
    addq    $16, %rsp
    movl    $'\n', %edi
    call    putbyte
#endif
    // Code that left a value on the x87 stack corrupts every later long double
    // operation; catch it here, as status 254, rather than as a wrong result later.
    subq    $32, %rsp
    fnstenv (%rsp)
    cmpw    $0xffff, 8(%rsp)            // the tag word: all registers empty
    je      5f
    leaq    x87_text(%rip), %r12
    call    puts_raw
    movl    $254, %ebx
5:  movl    %ebx, %edi
    call    exit

// Any exception (an invalid opcode, a page fault, a divide error) prints its vector,
// error code, rip and cr2, and ends the run with exit status 255 rather than hanging.
// Each stub pushes a zero where the CPU pushes no error code, then its vector.
    .text
    .irp    v, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
stub\v:
    .if     (\v == 8) || ((\v >= 10) && (\v <= 14)) || (\v == 17) || (\v == 21) || (\v == 29) || (\v == 30)
    .else
    pushq   $0
    .endif
    pushq   $\v
    jmp     trap
    .endr

trap:
    movq    (%rsp), %r14                // vector
    movq    8(%rsp), %r15               // error code
    movq    16(%rsp), %rbx              // rip
    andq    $-16, %rsp
    leaq    trap_text(%rip), %r12
    call    puts_raw
    movq    %r14, %rdi
    call    puthex
    call    puts_raw                    // " error="
    movq    %r15, %rdi
    call    puthex
    call    puts_raw                    // " rip="
    movq    %rbx, %rdi
    call    puthex
    call    puts_raw                    // " cr2="
    movq    %cr2, %rdi
    call    puthex
    movl    $'\n', %edi
    call    putbyte
    movl    $255, %edi
    call    exit

// Print the NUL-terminated string at r12 and leave r12 just past it.
puts_raw:
1:  movzbl  (%r12), %edi
    incq    %r12
    testl   %edi, %edi
    jz      2f
    pushq   %rdi                        // keeps rsp 16-aligned at the call
    call    putbyte
    popq    %rdi
    jmp     1b
2:  ret

// Print rdi as 16 hex digits.
puthex:
    pushq   %r13
    pushq   %rbp
    subq    $8, %rsp
    movq    %rdi, %r13
    movl    $60, %ebp
1:  movq    %r13, %rdi
    movl    %ebp, %ecx
    shrq    %cl, %rdi
    andl    $15, %edi
    leal    '0'(%rdi), %eax
    leal    'a'-10(%rdi), %edi
    cmpl    $'9', %eax
    cmovbe  %eax, %edi
    call    putbyte
    subl    $4, %ebp
    jns     1b
    addq    $8, %rsp
    popq    %rbp
    popq    %r13
    ret

    .section .rodata
trap_text:
    .asciz  "\nexception: vector "
    .asciz  " error="
    .asciz  " rip="
    .asciz  " cr2="
x87_text:
    .asciz  "\nx87 stack not empty after main\n"
    .p2align 2
mxcsr_default:
    .long   0x1f80                      // round to nearest, exceptions masked
    .p2align 3
stubs:
    .irp    v, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
    .quad   stub\v
    .endr

// Static, so that clearing .bss cannot touch them.
    .data
    .p2align 3
gdt:
    .quad   0
    .quad   0x00af9a000000ffff          // 8: 64-bit code
    .quad   0x00cf92000000ffff          // 16: data
gdtr:
    .word   gdtr - gdt - 1
    .quad   gdt
    .p2align 12
pml4:
    .quad   pdpt + 3                    // present, writable
    .fill   511, 8, 0
pdpt:
    .quad   0x0000000083                // 1 GiB pages: present, writable, PS
    .quad   0x0040000083
    .quad   0x0080000083
    .quad   0x00c0000083
    .fill   508, 8, 0

    .bss
    .p2align 4
idt:
    .zero   32 * 16
    .section .rodata
    .p2align 1
idtr:
    .word   32 * 16 - 1
    .quad   idt
