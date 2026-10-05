/*
 * A bump allocator over the heap of link.ld, from __heap_start up to STACK_MARGIN bytes
 * below the stack at the time of the call: free does nothing.  Each block is 2-aligned
 * and preceded by a 2-byte header holding the requested size, so realloc knows how much
 * to copy.  The same design as AVR's malloc.s, in C.
 */
#include <stddef.h>
#include <string.h>

#define STACK_MARGIN 256

extern char __heap_start[];

static char *brk = __heap_start;

void *malloc(size_t n)
{
    char probe; /* where the stack is now */
    char *limit = &probe - STACK_MARGIN;
    size_t room = brk < limit ? (size_t)(limit - brk) : 0;
    if (n > room || room - n < 2 + 1)
        return NULL;
    size_t *header = (size_t *)brk;
    *header        = n;
    brk += 2 + ((n + 1) & ~(size_t)1);
    return header + 1;
}

void *calloc(size_t n, size_t size)
{
    if (size != 0 && n > (size_t)-1 / size)
        return NULL;
    void *p = malloc(n * size);
    if (p)
        memset(p, 0, n * size);
    return p;
}

/* A block that shrinks stays where it is. */
void *realloc(void *p, size_t n)
{
    if (!p)
        return malloc(n);
    size_t old = ((size_t *)p)[-1];
    if (n <= old)
        return p;
    void *q = malloc(n);
    if (q)
        memcpy(q, p, old);
    return q;
}

void free(void *p)
{
    (void)p;
}
