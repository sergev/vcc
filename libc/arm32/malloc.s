// A bump allocator over the heap of link.ld, as on RV32: free does nothing.
// Each block is preceded by a 16-byte header whose first word holds the
// requested size, so realloc knows how much to copy.

    .syntax unified
    .arm

    .text

// void *malloc(size_t n): 16-byte aligned, NULL when the heap is exhausted.
    .globl  malloc
    .p2align 2
    .type   malloc, %function
malloc:
    movw    r12, #:lower16:heap_next
    movt    r12, #:upper16:heap_next
    ldr     r1, [r12]
    cmp     r1, #0
    movweq  r1, #:lower16:__heap_start
    movteq  r1, #:upper16:__heap_start
    add     r1, r1, #15
    bic     r1, r1, #15
    adds    r2, r0, #16
    bcs     1f                          // n + 16 overflowed
    adds    r2, r1, r2
    bcs     1f                          // wrapped around
    movw    r3, #:lower16:__heap_end
    movt    r3, #:upper16:__heap_end
    cmp     r2, r3
    bhi     1f
    str     r2, [r12]
    str     r0, [r1]                    // header: the block's size
    add     r0, r1, #16
    bx      lr
1:  mov     r0, #0
    bx      lr
    .size   malloc, .-malloc

// void *calloc(size_t n, size_t size): zeroed.
    .globl  calloc
    .p2align 2
    .type   calloc, %function
calloc:
    push    {r4, lr}
    mul     r4, r0, r1
    mov     r0, r4
    bl      malloc
    cmp     r0, #0
    beq     1f
    mov     r1, #0
    mov     r2, r4
    bl      memset
1:  pop     {r4, pc}
    .size   calloc, .-calloc

// void *realloc(void *p, size_t n): NULL p is malloc(n); a block already big
// enough is returned as is; otherwise a new block gets a copy of the old
// contents.  NULL on exhaustion, with p left intact.
    .globl  realloc
    .p2align 2
    .type   realloc, %function
realloc:
    cmp     r0, #0
    beq     malloc
    ldr     r2, [r0, #-16]              // the old size
    cmp     r1, r2
    bxls    lr
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r2
    mov     r0, r1
    bl      malloc
    cmp     r0, #0
    beq     1f
    mov     r1, r4
    mov     r2, r5
    bl      memcpy                      // returns the new block
1:  pop     {r4, r5, r6, pc}
    .size   realloc, .-realloc

// void free(void *p)
    .globl  free
    .p2align 2
    .type   free, %function
free:
    bx      lr
    .size   free, .-free

    .bss
    .p2align 2
heap_next:
    .space  4
