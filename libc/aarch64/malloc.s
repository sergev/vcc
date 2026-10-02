// A bump allocator over the heap of link.ld: free does nothing.
// Each block is preceded by a 16-byte header whose first word holds the
// requested size, so realloc knows how much to copy.

    .text

// void *malloc(size_t n): 16-byte aligned, NULL when the heap is exhausted.
    .globl  malloc
    .p2align 2
    .type   malloc, @function
malloc:
    adrp    x9, heap_next
    add     x9, x9, :lo12:heap_next
    ldr     x10, [x9]
    cbnz    x10, 1f
    adrp    x10, __heap_start
    add     x10, x10, :lo12:__heap_start
1:  add     x10, x10, #15
    and     x10, x10, #-16
    adds    x11, x0, #16
    b.cs    2f                      // n + 16 overflowed
    adds    x11, x10, x11
    b.cs    2f                      // wrapped around
    adrp    x12, __heap_end
    add     x12, x12, :lo12:__heap_end
    cmp     x11, x12
    b.hi    2f
    str     x11, [x9]
    str     x0, [x10]               // header: the block's size
    add     x0, x10, #16
    ret
2:  mov     x0, #0
    ret
    .size   malloc, .-malloc

// void *calloc(size_t n, size_t size): zeroed.
    .globl  calloc
    .p2align 2
    .type   calloc, @function
calloc:
    stp     x29, x30, [sp, #-32]!
    mov     x29, sp
    mul     x0, x0, x1
    str     x0, [sp, #16]
    bl      malloc
    cbz     x0, 1f
    mov     w1, #0
    ldr     x2, [sp, #16]
    bl      memset
1:  ldp     x29, x30, [sp], #32
    ret
    .size   calloc, .-calloc

// void *realloc(void *p, size_t n): NULL p is malloc(n); a block already big
// enough is returned as is; otherwise a new block gets a copy of the old
// contents.  NULL on exhaustion, with p left intact.
    .globl  realloc
    .p2align 2
    .type   realloc, @function
realloc:
    cbz     x0, malloc
    ldr     x9, [x0, #-16]          // the old size
    cmp     x1, x9
    b.ls    2f
    stp     x29, x30, [sp, #-32]!
    mov     x29, sp
    stp     x0, x9, [sp, #16]
    mov     x0, x1
    bl      malloc
    cbz     x0, 1f
    ldp     x1, x2, [sp, #16]
    bl      memcpy                  // returns the new block
1:  ldp     x29, x30, [sp], #32
2:  ret
    .size   realloc, .-realloc

// void free(void *p)
    .globl  free
    .p2align 2
    .type   free, @function
free:
    ret
    .size   free, .-free

    .bss
    .p2align 3
heap_next:
    .zero   8
