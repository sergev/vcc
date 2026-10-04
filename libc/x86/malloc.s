// A bump allocator over the heap of link.ld: free does nothing.
// Each block is preceded by a 16-byte header whose first word holds the
// requested size, so realloc knows how much to copy.

    .text

// void *malloc(size_t n): 16-byte aligned, NULL when the heap is exhausted.
    .globl  malloc
    .p2align 4
    .type   malloc, @function
malloc:
    movq    heap_next(%rip), %rax
    testq   %rax, %rax
    jnz     1f
    leaq    __heap_start(%rip), %rax
1:  addq    $15, %rax
    andq    $-16, %rax
    movq    %rdi, %rcx
    addq    $16, %rcx
    jc      2f                      // n + 16 overflowed
    addq    %rax, %rcx
    jc      2f                      // wrapped around
    leaq    __heap_end(%rip), %rdx
    cmpq    %rdx, %rcx
    ja      2f
    movq    %rcx, heap_next(%rip)
    movq    %rdi, (%rax)            // header: the block's size
    addq    $16, %rax
    ret
2:  xorl    %eax, %eax
    ret
    .size   malloc, .-malloc

// void *calloc(size_t n, size_t size): zeroed; NULL when n * size overflows.
    .globl  calloc
    .p2align 4
    .type   calloc, @function
calloc:
    pushq   %rbx
    movq    %rdi, %rax
    mulq    %rsi
    jc      1f
    movq    %rax, %rbx
    movq    %rax, %rdi
    call    malloc
    testq   %rax, %rax
    jz      2f
    movq    %rax, %rdi
    xorl    %esi, %esi
    movq    %rbx, %rdx
    call    memset                  // returns the block
    jmp     2f
1:  xorl    %eax, %eax
2:  popq    %rbx
    ret
    .size   calloc, .-calloc

// void *realloc(void *p, size_t n): NULL p is malloc(n); a block already big
// enough is returned as is; otherwise a new block gets a copy of the old
// contents.  NULL on exhaustion, with p left intact.
    .globl  realloc
    .p2align 4
    .type   realloc, @function
realloc:
    testq   %rdi, %rdi
    jnz     1f
    movq    %rsi, %rdi
    jmp     malloc
1:  movq    -16(%rdi), %rcx         // the old size
    cmpq    %rcx, %rsi
    jbe     3f
    pushq   %rbx
    pushq   %r12
    subq    $8, %rsp
    movq    %rdi, %rbx
    movq    %rcx, %r12
    movq    %rsi, %rdi
    call    malloc
    testq   %rax, %rax
    jz      2f
    movq    %rax, %rdi
    movq    %rbx, %rsi
    movq    %r12, %rdx
    call    memcpy                  // returns the new block
2:  addq    $8, %rsp
    popq    %r12
    popq    %rbx
    ret
3:  movq    %rdi, %rax
    ret
    .size   realloc, .-realloc

// void free(void *p)
    .globl  free
    .p2align 4
    .type   free, @function
free:
    ret
    .size   free, .-free

    .bss
    .p2align 3
heap_next:
    .zero   8
