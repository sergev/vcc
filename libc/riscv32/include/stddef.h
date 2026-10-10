/*
 * <stddef.h> — common definitions (C11 §7.19), RISC-V ILP32 target.
 *
 * size_t and ptrdiff_t are long, as the compiler types sizeof and a pointer
 * difference; clang says int, which is the same in this ABI.
 */
#ifndef _STDDEF_H
#define _STDDEF_H

typedef long ptrdiff_t;
typedef unsigned long size_t;
typedef int wchar_t;

typedef struct {
    long long __ll;
    long double __ld;
} max_align_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif /* _STDDEF_H */
