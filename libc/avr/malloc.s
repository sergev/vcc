; A bump allocator over the heap of link.ld, from __heap_start up to 256 bytes below
; the stack pointer at the time of the call: free does nothing.  Each block is preceded
; by a 2-byte header holding the requested size, so realloc knows how much to copy.
    .equ    __SP_H__, 0x3e
    .equ    __SP_L__, 0x3d
    .equ    STACK_MARGIN, 256

    .data
brk:
    .short  __heap_start

    .text
; void *malloc(size_t n): NULL when the heap would come within STACK_MARGIN bytes of
; the stack.  Clobbers r22, r23, r26, r27, r30, r31.
    .globl  malloc
    .type   malloc, @function
malloc:
    lds     r26, brk
    lds     r27, brk + 1
    movw    r30, r26                    ; the new break: brk + 2 + n
    adiw    r30, 2
    add     r30, r24
    adc     r31, r25
    brcs    1f
    in      r22, __SP_L__               ; the limit: SP - STACK_MARGIN
    in      r23, __SP_H__
    subi    r23, hi8(STACK_MARGIN)
    cp      r22, r30
    cpc     r23, r31
    brlo    1f
    st      X+, r24                     ; the header
    st      X+, r25
    sts     brk, r30
    sts     brk + 1, r31
    movw    r24, r26
    ret
1:  clr     r24
    clr     r25
    ret
    .size   malloc, .-malloc

; void *calloc(size_t n, size_t size): zeroed; NULL when n * size overflows.
    .globl  calloc
    .type   calloc, @function
calloc:
    mul     r25, r23                    ; both high bytes nonzero: overflow
    or      r0, r1
    brne    2f
    mul     r24, r23                    ; n.lo * size.hi << 8
    tst     r1
    brne    2f
    mov     r31, r0
    mul     r25, r22                    ; n.hi * size.lo << 8
    tst     r1
    brne    2f
    add     r31, r0
    brcs    2f
    mul     r24, r22                    ; n.lo * size.lo
    mov     r30, r0
    add     r31, r1
    brcs    2f
    clr     r1
    movw    r24, r30
    push    r30
    push    r31
    rcall   malloc
    pop     r27                         ; X = the size
    pop     r26
    movw    r30, r24                    ; Z = the block
    sbiw    r30, 0
    breq    3f
1:  sbiw    r26, 0
    breq    3f
    st      Z+, r1
    sbiw    r26, 1
    rjmp    1b
2:  clr     r1
    clr     r24
    clr     r25
3:  ret
    .size   calloc, .-calloc

; void *realloc(void *p, size_t n): NULL p is malloc(n); a block already big enough is
; returned as is; otherwise a new block gets a copy of the old contents.  NULL on
; exhaustion, with p left intact.
    .globl  realloc
    .type   realloc, @function
realloc:
    sbiw    r24, 0
    brne    1f
    movw    r24, r22
    rjmp    malloc
1:  movw    r30, r24
    ld      r27, -Z                     ; X = the old size
    ld      r26, -Z
    cp      r26, r22
    cpc     r27, r23
    brlo    2f
    ret
2:  push    r24                         ; the old block
    push    r25
    push    r26
    push    r27
    movw    r24, r22
    rcall   malloc
    pop     r19                         ; r19:r18 = the old size
    pop     r18
    pop     r31                         ; Z = the old block
    pop     r30
    sbiw    r24, 0
    breq    4f
    movw    r26, r24                    ; X = the new block
3:  cp      r18, r1
    cpc     r19, r1
    breq    4f
    ld      r0, Z+
    st      X+, r0
    subi    r18, 1
    sbci    r19, 0
    rjmp    3b
4:  ret
    .size   realloc, .-realloc

; void free(void *p)
    .globl  free
    .type   free, @function
free:
    ret
    .size   free, .-free
