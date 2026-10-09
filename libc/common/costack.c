/*
 * The memory co_alloca takes in an ordinary function, on a target whose backend does not
 * allocate on the stack itself (all but wasm32, which expands __builtin_alloca and the
 * rest on its shadow stack): a static arena, given back in LIFO order at the end of the
 * co_alloca's block (docs/Coroutines_Internals.md §5).  A longjmp out of such a block
 * leaves its memory taken until an enclosing block gives back its own.
 */
#include <stddef.h>
#include <stdint.h>

#if SIZE_MAX <= 0xffff
#define COSTACK_BYTES 1024
#else
#define COSTACK_BYTES 65536
#endif

_Noreturn void __coro_trap(const char *name, const char *coroutine);

static _Alignas(16) char costack[COSTACK_BYTES];
static char *costack_top = costack;

void *__coro_stack_save(void)
{
    return costack_top;
}

/* `bytes`, a multiple of 16, aligned to 16. */
void *__coro_alloca(size_t bytes)
{
    char *p = costack_top;
    if (bytes > (size_t)(costack + COSTACK_BYTES - p))
        __coro_trap("CO_TRAP_NO_SPACE", "co_alloca");
    costack_top = p + bytes;
    return p;
}

void __coro_stack_restore(void *p)
{
    costack_top = p;
}
