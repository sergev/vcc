/*
 * <stdarg.h> — variable arguments (C11 §7.16), ARM32 AAPCS target.
 *
 * The compiler has no va_* builtins.  A variadic function takes all its arguments
 * under the base standard of the AAPCS, the FP ones in core registers too, and
 * saves r0-r3 just below its incoming stack arguments, so every argument sits in
 * consecutive 4-byte slots.  A value takes its size rounded up to 4 bytes, at an
 * 8-byte boundary when so aligned (double, long double, long long: an even register
 * pair).  Every composite is passed by value, so no argument is a pointer to a copy.
 * va_list is clang's type: a structure of one pointer, passed as a pointer is.
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef struct __va_list {
    char *__ap;
} va_list;

#define __va_size(n) (((n) + 3) & ~3U)

#define va_start(ap, last) ((ap).__ap = (char *)&(last) + __va_size(sizeof(last)))

#define __va_align(ap, T)                                                              \
    (_Alignof(T) > 4 ? ((ap).__ap = (char *)(((unsigned)(ap).__ap + 7) & ~7U)) : (ap).__ap)

#define va_arg(ap, T)                                                                  \
    (*(__va_align(ap, T), (T *)(((ap).__ap += __va_size(sizeof(T))) - __va_size(sizeof(T)))))

#define va_end(ap) ((void)(ap))

#define va_copy(dest, src) ((dest) = (src))

#endif /* _STDARG_H */
