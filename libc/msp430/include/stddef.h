/*
 * <stddef.h> — common definitions (C11 §7.19), MSP430.
 *
 * int and pointers are 16 bits: size_t is unsigned int and ptrdiff_t int; wchar_t is
 * long, as msp430-elf-gcc has it (clang's is int).  Ahead of the shared 16-bit one in
 * libc/ip16/include, which AVR keeps.
 */
#ifndef _STDDEF_H
#define _STDDEF_H

typedef int          ptrdiff_t;
typedef unsigned int size_t;
typedef long         wchar_t;
typedef struct {
    long long __ll;
    long double __ld;
} max_align_t;

/* For <stdlib.h>: RAND_MAX fits an int. */
#define _RAND_MAX 32767

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t) & (((type *)0)->member))

#endif /* _STDDEF_H */
