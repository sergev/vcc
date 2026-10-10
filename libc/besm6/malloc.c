//
// Dynamic memory allocation (malloc/calloc/realloc/free) for the BESM-6.
//
// Free memory is kept as a singly linked list sorted by address in ascending
// order; the first segment large enough to satisfy a request is used (first
// fit).  Adjacent free blocks are merged on free(), which keeps fragmentation
// low at the cost of a linear scan.
//
// BESM-6 is word-addressed: every scalar and pointer is one 48-bit word (six
// bytes) and `ptr + n` advances n words.  The allocator therefore counts
// everything in WORDS and does all address arithmetic with typed pointers — a
// heap_header_t is exactly one word, so pointer steps and word steps coincide
// and no integer<->pointer casts are needed.
//
// There is no sbrk on this machine.  Instead the heap occupies the fixed span
// from the linker end-of-program symbol `end` up to the BESM-6 stack base
// (HEAP_LIMIT); it is claimed lazily on the first allocation.  This layout is
// supplied by the Unix (b6ld/b6sim) toolchain — b6ld defines `end` and b6sim
// seeds the stack at HEAP_LIMIT — so the allocator is a Unix-only routine and is
// not assembled into the Madlen libc.bin (see libc/besm6/CMakeLists.txt).
//
#include <malloc.h>
#include <stdlib.h>

//
// Bytes per machine word.
//
#define WORD_BYTES 6

//
// End of heap, start of BESM-6 stack.
//
#define HEAP_LIMIT 070000

//
// Smallest block we keep on the free list: header + one word for its link.
//
#define MIN_BLOCK_WORDS 2

//
// Every block is prefixed by a one-word header holding the block size (in
// words, header included).  In a free block the first payload word is reused
// to chain to the next free block.
//
typedef struct {
    size_t size;
} heap_header_t;

//
// Address of, and value of, the next-free-block link stored after a header.
//
#define NEXTP(h) ((heap_header_t **)((h) + 1))
#define NEXT(h)  (*NEXTP(h))

//
// Free list, sorted by ascending address, and the running free-word total.
//
static heap_header_t *free_list;
static size_t free_words;

//
// Round a byte count up to whole words.
//
static size_t to_words(size_t nbytes)
{
    return (nbytes + WORD_BYTES - 1) / WORD_BYTES;
}

//
// Fill n words at p with zero (memset is not in libc.bin yet).
//
static void zero_words(size_t *p, size_t n)
{
    while (n > 0) {
        *p = 0;
        p++;
        n--;
    }
}

//
// Copy n words from s to d (memcpy is not in libc.bin yet).
//
static void copy_words(size_t *d, const size_t *s, size_t n)
{
    while (n > 0) {
        *d = *s;
        d++;
        s++;
        n--;
    }
}

//
// Claim the whole heap span [end, HEAP_LIMIT) as one free block, once.
// Called lazily on the first allocation or free-memory query.
//
static void heap_setup(void)
{
    // Initialize once.
    static int initialized;
    if (initialized)
        return;
    initialized = 1;

    extern heap_header_t end;
    heap_header_t *h = &end;
    if ((size_t)h >= HEAP_LIMIT) {
        return; // program fills memory up to the stack: no room for a heap
    }

    free_list  = h;
    free_words = HEAP_LIMIT - (size_t)h;
    h->size    = free_words;
    NEXT(h)    = NULL;
}

//
// Find and unlink a block of at least NWORDS words (header included).
// Returns the block header, or NULL if no segment is large enough.
//
static heap_header_t *alloc_words(size_t nwords)
{
    heap_setup();

    heap_header_t *h      = free_list;
    heap_header_t **hprev = &free_list;

    while (h != NULL) {
        if (h->size >= nwords) {
            break;
        }
        hprev = NEXTP(h);
        h     = NEXT(h);
    }

    if (h == NULL) {
        return NULL;
    }

    // Split off the remainder as a new free block when it is large enough to
    // stand on its own; otherwise hand out the whole block.
    if (h->size >= nwords + MIN_BLOCK_WORDS) {
        heap_header_t *newh = h + nwords;
        newh->size          = h->size - nwords;
        h->size             = nwords;
        NEXT(newh)          = NEXT(h);
        *hprev              = newh;
    } else {
        *hprev = NEXT(h);
    }

    free_words -= h->size;
    return h;
}

//
// Allocate uninitialized memory of the given size.
//
void *malloc(size_t nbytes)
{
    size_t nwords = to_words(nbytes);
    if (nwords < 1) {
        nwords = 1; // room for the free-list link once freed
    }

    heap_header_t *h = alloc_words(nwords + 1);
    if (h == NULL) {
        return NULL;
    }
    return h + 1;
}

//
// Allocate zero-filled memory for an array of nmemb elements of size bytes.
//
void *calloc(size_t nmemb, size_t size)
{
    if (nmemb != 0 && size > (size_t)-1 / nmemb) {
        return NULL; // multiplication would overflow
    }

    size_t nbytes = nmemb * size;
    void *p       = malloc(nbytes);
    if (p != NULL) {
        zero_words((size_t *)p, to_words(nbytes));
    }
    return p;
}

//
// Insert a block into the address-sorted free list, coalescing neighbours.
//
static void make_free_block(heap_header_t *newh)
{
    free_words += newh->size;

    heap_header_t *h      = free_list;
    heap_header_t **hprev = &free_list;
    for (;;) {
        if (h == NULL) {
            // End of the list: append.
            *hprev     = newh;
            NEXT(newh) = NULL;
            break;
        }

        if (h > newh) {
            // Insert before the higher-addressed block, merging if adjacent.
            *hprev = newh;
            if (newh + newh->size == h) {
                newh->size += h->size;
                NEXT(newh) = NEXT(h);
            } else {
                NEXT(newh) = h;
            }
            break;
        }

        if (h + h->size == newh) {
            // Append onto the end of the lower-addressed block, then try to
            // also absorb the block following it.
            h->size += newh->size;
            if (h + h->size == NEXT(h)) {
                h->size += NEXT(h)->size;
                NEXT(h) = NEXT(NEXT(h));
            }
            break;
        }

        hprev = NEXTP(h);
        h     = NEXT(h);
    }
}

//
// Release a block previously returned by malloc/calloc/realloc.
//
void free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    make_free_block((heap_header_t *)ptr - 1);
}

//
// Resize a block, preserving its contents up to the smaller of old/new size.
//
void *realloc(void *ptr, size_t nbytes)
{
    if (ptr == NULL) {
        return malloc(nbytes);
    }

    heap_header_t *h = (heap_header_t *)ptr - 1;
    size_t old_words = h->size - 1;
    size_t new_words = to_words(nbytes);

    if (old_words >= new_words) {
        return ptr; // already big enough; keep it in place
    }

    void *block = malloc(nbytes);
    if (block == NULL) {
        return NULL; // original block is left intact
    }
    copy_words((size_t *)block, (size_t *)ptr, old_words);
    free(ptr);
    return block;
}

//
// Total free memory currently available, in bytes.
//
size_t malloc_free_bytes(void)
{
    heap_setup();
    return free_words * WORD_BYTES;
}

//
// Usable payload size, in bytes, of a previously allocated block.
//
size_t malloc_usable_size(void *ptr)
{
    if (ptr == NULL) {
        return 0;
    }
    heap_header_t *h = (heap_header_t *)ptr - 1;
    return (h->size - 1) * WORD_BYTES;
}
