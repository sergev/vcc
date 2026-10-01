# C startup for bare-metal qemu `virt`: set gp and sp, enable the FPU, catch traps,
# clear .bss, call main and pass its result to exit.  Built with -DPRINT_STATUS it
# first prints main's result as "%d\n" (the book run tests compare it).

    .section .text.start, "ax"
    .globl  _start
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    la      sp, __stack_top
    li      t0, 0x2000          # mstatus.FS = initial: FP instructions do not trap
    csrs    mstatus, t0
    la      t0, trap
    csrw    mtvec, t0
    la      t0, __bss_start
    la      t1, __bss_end
1:  bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:  call    main
#ifdef PRINT_STATUS
    sext.w  s0, a0
    mv      s1, s0
    bgez    s1, 3f
    li      a0, '-'
    call    putbyte
    neg     s1, s1
3:  addi    sp, sp, -32
    mv      s2, sp              # digits, least significant first
    li      t0, 10
4:  remu    t1, s1, t0
    divu    s1, s1, t0
    addi    t1, t1, '0'
    sb      t1, 0(s2)
    addi    s2, s2, 1
    bnez    s1, 4b
5:  addi    s2, s2, -1
    lbu     a0, 0(s2)
    call    putbyte
    bne     s2, sp, 5b
    li      a0, '\n'
    call    putbyte
    mv      a0, s0
#endif
    call    exit

# Any trap (an illegal instruction, a misaligned or faulting access) ends the run with
# exit status 255 rather than hanging.
    .p2align 2
trap:
    li      a0, 255
    call    exit
