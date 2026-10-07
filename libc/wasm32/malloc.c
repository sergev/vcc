/*
 * A bump allocator over the linear memory, from wasm-ld's __heap_base up, growing the
 * memory as it goes: free does nothing.  Each block is 16-aligned and preceded by a
 * header holding the requested size, so realloc knows how much to copy.  The same
 * design as MMIX's.
 */
#include <stddef.h>
#include <string.h>

#define PAGE 65536UL

extern char __heap_base[];
unsigned long __vcc_memory_size(void);
long __vcc_memory_grow(unsigned long pages);

static char *brk = __heap_base;

void *malloc(size_t n)
{
    size_t block = ((size_t)brk + 16 + 15) & ~(size_t)15;
    if (n > (size_t)-1 - block)
        return NULL;
    size_t end  = block + n;
    size_t have = __vcc_memory_size() * PAGE;
    if (end > have && __vcc_memory_grow((end - have + PAGE - 1) / PAGE) < 0)
        return NULL;
    ((size_t *)block)[-1] = n;
    brk                   = (char *)end;
    return (void *)block;
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
