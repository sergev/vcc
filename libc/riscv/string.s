# String routines of the runtime stub, until the shared C library (plan step R17).

    .text

# size_t strlen(const char *s)
    .globl  strlen
    .p2align 2
strlen:
    mv      t0, a0
1:  lbu     t1, 0(t0)
    beqz    t1, 2f
    addi    t0, t0, 1
    j       1b
2:  sub     a0, t0, a0
    ret

# int strcmp(const char *a, const char *b): compares as unsigned char.
    .globl  strcmp
    .p2align 2
strcmp:
1:  lbu     t0, 0(a0)
    lbu     t1, 0(a1)
    bne     t0, t1, 2f
    beqz    t0, 2f
    addi    a0, a0, 1
    addi    a1, a1, 1
    j       1b
2:  sub     a0, t0, t1
    ret
