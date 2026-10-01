/*
 * <stdarg.h> — variable arguments (C11 §7.16), RISC-V LP64 target.
 *
 * The compiler has no va_* builtins.  A variadic function saves a0-a7 just below
 * its incoming stack arguments, so every argument passed in integer registers or
 * on the stack sits in consecutive 8-byte slots, and va_list is a plain pointer.
 * A value takes its size rounded up to 8 bytes; a struct wider than 16 bytes is
 * passed as a pointer to a copy.  va_start steps on from the last named
 * parameter, which must not be one passed in an FP register or by reference.
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

#define __va_size(n) (((n) + 7) & ~7UL)

#define va_start(ap, last) ((ap) = (char *)&(last) + __va_size(sizeof(last)))

#define va_arg(ap, T)                                                                  \
    (*(sizeof(T) > 16 ? *(T **)(((ap) += 8) - 8)                                        \
                      : (T *)(((ap) += __va_size(sizeof(T))) - __va_size(sizeof(T)))))

#define va_end(ap) ((void)(ap))

#define va_copy(dest, src) ((dest) = (src))

#endif /* _STDARG_H */
