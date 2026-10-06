/*
 * <stddef.h> — common definitions (C11 §7.19), hosted macOS on Apple silicon.
 *
 * wchar_t is int, and max_align_t is long double, which is double (Apple's arm64 ABI).
 */
#ifndef _STDDEF_H
#define _STDDEF_H

typedef long          ptrdiff_t;
typedef unsigned long size_t;
typedef int           wchar_t;

typedef long double max_align_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t) & (((type *)0)->member))

#endif /* _STDDEF_H */
