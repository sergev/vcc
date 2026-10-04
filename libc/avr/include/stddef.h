/*
 * <stddef.h> — common definitions (C11 §7.19), AVR.
 *
 * int and pointers are 16 bits: size_t is unsigned int, ptrdiff_t int, as in avr-gcc
 * and clang for the target.
 */
#ifndef _STDDEF_H
#define _STDDEF_H

typedef int          ptrdiff_t;
typedef unsigned int size_t;
typedef int          wchar_t;
typedef struct {
    long long __ll;
    long double __ld;
} max_align_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t) & (((type *)0)->member))

#endif /* _STDDEF_H */
