# A bump allocator over the heap of link.ld, until the shared C library (plan step
# R17): free does nothing.

    .text

# void *malloc(size_t n): 16-byte aligned, NULL when the heap is exhausted.
    .globl  malloc
    .p2align 2
malloc:
    la      t0, heap_next
    ld      t1, 0(t0)
    bnez    t1, 1f
    la      t1, __heap_start
1:  addi    t1, t1, 15
    andi    t1, t1, -16
    add     t2, t1, a0
    la      t3, __heap_end
    bgtu    t2, t3, 2f
    sd      t2, 0(t0)
    mv      a0, t1
    ret
2:  li      a0, 0
    ret

# void *calloc(size_t n, size_t size): zeroed.
    .globl  calloc
    .p2align 2
calloc:
    addi    sp, sp, -16
    sd      ra, 8(sp)
    mul     a0, a0, a1
    sd      a0, 0(sp)
    call    malloc
    beqz    a0, 1f
    li      a1, 0
    ld      a2, 0(sp)
    call    memset
1:  ld      ra, 8(sp)
    addi    sp, sp, 16
    ret

# void free(void *p)
    .globl  free
    .p2align 2
free:
    ret

    .bss
    .p2align 3
heap_next:
    .zero   8
