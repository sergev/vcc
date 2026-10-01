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

# void *memset(void *s, int c, size_t n)
    .globl  memset
    .p2align 2
memset:
    mv      t0, a0
    add     t1, a0, a2
1:  beq     t0, t1, 2f
    sb      a1, 0(t0)
    addi    t0, t0, 1
    j       1b
2:  ret

# void *memcpy(void *d, const void *s, size_t n)
    .globl  memcpy
    .p2align 2
memcpy:
    mv      t0, a0
    add     t1, a0, a2
1:  beq     t0, t1, 2f
    lbu     t2, 0(a1)
    sb      t2, 0(t0)
    addi    t0, t0, 1
    addi    a1, a1, 1
    j       1b
2:  ret

# int memcmp(const void *a, const void *b, size_t n)
    .globl  memcmp
    .p2align 2
memcmp:
    add     t2, a0, a2
1:  beq     a0, t2, 2f
    lbu     t0, 0(a0)
    lbu     t1, 0(a1)
    bne     t0, t1, 3f
    addi    a0, a0, 1
    addi    a1, a1, 1
    j       1b
2:  li      a0, 0
    ret
3:  sub     a0, t0, t1
    ret

# int strncmp(const char *a, const char *b, size_t n)
    .globl  strncmp
    .p2align 2
strncmp:
    li      t0, 0
    li      t1, 0
1:  beqz    a2, 2f
    lbu     t0, 0(a0)
    lbu     t1, 0(a1)
    bne     t0, t1, 2f
    beqz    t0, 2f
    addi    a0, a0, 1
    addi    a1, a1, 1
    addi    a2, a2, -1
    j       1b
2:  sub     a0, t0, t1
    ret
