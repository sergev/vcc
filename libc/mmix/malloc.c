/*
 * A bump allocator over the data segment, from the linker's _end up: free does nothing.
 * The memory stack is a segment of its own (0x6000000000000000 down), so the heap never
 * meets it; the data segment ends where the pool segment begins, at
 * 0x4000000000000000.  mmix simulates the memory sparsely, so the pages come into being
 * as they are touched.  Each block is 8-aligned and preceded by an 8-byte header holding
 * the requested size, so realloc knows how much to copy.  The same design as MSP430's.
 */
#include <stddef.h>
#include <string.h>

#define HEAP_END 0x4000000000000000UL

extern char _end[];

static char *brk = _end;

void *malloc(size_t n)
{
    size_t start = ((size_t)brk + 7) & ~(size_t)7;
    size_t room  = start < HEAP_END ? HEAP_END - start : 0;
    if (room < 8 || n > room - 8)
        return NULL;
    size_t *header = (size_t *)start;
    *header        = n;
    brk            = (char *)(start + 8 + ((n + 7) & ~(size_t)7));
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
