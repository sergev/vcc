/*
 * <stddef.h> — common definitions (C11 §7.19), AArch64 LP64 target.
 *
 * wchar_t is unsigned int (AAPCS64), not int as on RISC-V.
 */
#ifndef _STDDEF_H
#define _STDDEF_H

typedef long ptrdiff_t;
typedef unsigned long size_t;
typedef unsigned int wchar_t;

typedef struct {
    long long __ll;
    long double __ld;
} max_align_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif /* _STDDEF_H */
