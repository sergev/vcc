/*
 * <alloca.h> — alloca(n): n bytes, aligned for any object, that live until the
 * calling function returns.  The compiler expands __builtin_alloca: on the stack
 * where the backend has the builtins, else on the runtime's arena (64 KiB, 1 KiB
 * with a 16-bit size_t), given back at each return.  Not in a coroutine, and not on
 * BESM-6 (docs/Plan.md).
 */
#ifndef _ALLOCA_H
#define _ALLOCA_H

#include <stddef.h>

void *__builtin_alloca(size_t size);

#define alloca(size) __builtin_alloca(size)

#endif /* _ALLOCA_H */
