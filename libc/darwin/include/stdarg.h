/*
 * <stdarg.h> — variable arguments (C11 §7.16), hosted macOS on Apple silicon.
 *
 * Under Apple's arm64 ABI the caller puts every variable argument on the stack, in
 * 8-byte slots after the named ones there, so va_list is a plain pointer (Apple's).
 * va_start is __va_start(&ap), which the code generator expands in place: it points
 * ap at the first variable slot.  A value sits at the start of its slot
 * (little-endian); an aggregate takes as many slots as it needs, but one over 16 bytes
 * that is no homogeneous float aggregate comes by reference, as its address: argument
 * class 1, __builtin_va_class(T).  A char or short comes promoted to int, a float to
 * double.  An argument aligned to 16 bytes starts at a 16-byte boundary.
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

void __va_start(va_list *ap);

#define va_start(ap, last) __va_start(&(ap))

#define __va_align(ap, T) \
    ((ap) = (char *)(((unsigned long)(ap) + _Alignof(T) - 1) & -(unsigned long)_Alignof(T)))

#define va_arg(ap, T)                                                                  \
    (*(__builtin_va_class(T) == 1 ? *(T **)(((ap) += 8) - 8)                           \
                      : (__va_align(ap, T), (T *)(((ap) += (sizeof(T) + 7) & ~7UL) -    \
                                                  ((sizeof(T) + 7) & ~7UL)))))

#define va_end(ap) ((void)(ap))

#define va_copy(dest, src) ((dest) = (src))

#endif /* _STDARG_H */
