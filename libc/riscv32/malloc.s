# A bump allocator over the heap of link.ld, RV32: free does nothing.
# Each block is preceded by a 16-byte header whose first word holds the
# requested size, so realloc knows how much to copy.

    .text

# void *malloc(size_t n): 16-byte aligned, NULL when the heap is exhausted.
    .globl  malloc
    .p2align 2
malloc:
    la      t0, heap_next
    lw      t1, 0(t0)
    bnez    t1, 1f
    la      t1, __heap_start
1:  addi    t1, t1, 15
    andi    t1, t1, -16
    addi    t2, a0, 16
    bltu    t2, a0, 2f              # n + 16 overflowed
    add     t2, t1, t2
    bltu    t2, t1, 2f              # wrapped around
    la      t3, __heap_end
    bgtu    t2, t3, 2f
    sw      t2, 0(t0)
    sw      a0, 0(t1)               # header: the block's size
    addi    a0, t1, 16
    ret
2:  li      a0, 0
    ret

# void *calloc(size_t n, size_t size): zeroed.
    .globl  calloc
    .p2align 2
calloc:
    addi    sp, sp, -16
    sw      ra, 8(sp)
    mul     a0, a0, a1
    sw      a0, 0(sp)
    call    malloc
    beqz    a0, 1f
    li      a1, 0
    lw      a2, 0(sp)
    call    memset
1:  lw      ra, 8(sp)
    addi    sp, sp, 16
    ret

# void *realloc(void *p, size_t n): NULL p is malloc(n); a block already big
# enough is returned as is; otherwise a new block gets a copy of the old
# contents.  NULL on exhaustion, with p left intact.
    .globl  realloc
    .p2align 2
realloc:
    beqz    a0, malloc
    lw      t0, -16(a0)             # the old size
    bleu    a1, t0, 2f
    addi    sp, sp, -32
    sw      ra, 24(sp)
    sw      a0, 16(sp)
    sw      t0, 8(sp)
    mv      a0, a1
    call    malloc
    beqz    a0, 1f
    lw      a1, 16(sp)
    lw      a2, 8(sp)
    call    memcpy                  # returns the new block
1:  lw      ra, 24(sp)
    addi    sp, sp, 32
2:  ret

# void free(void *p)
    .globl  free
    .p2align 2
free:
    ret

    .bss
    .p2align 2
heap_next:
    .zero   4
