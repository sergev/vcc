/*
 * <stdarg.h> — variable arguments (C11 §7.16), AArch64 AAPCS64 target.
 *
 * va_list is the AAPCS64 structure, so one can be handed to and from clang-compiled
 * code.  A variadic function saves the argument registers its named parameters left
 * in two save areas, the general ones (x0-x7, 8 bytes each) and the vector ones
 * (q0-q7, 16 bytes each); __gr_offs and __vr_offs count up to zero through them, and
 * then the arguments continue at __stack.  va_start is __va_start(&ap), which the code
 * generator expands in place; va_arg calls __va_arg with the type's argument class,
 * __builtin_va_class(T): general registers, by reference, or in vector registers as
 * 1-4 elements (an HFA's members, one per q register, are gathered into the last
 * argument, a temporary of type T).
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef struct __va_list {
    void *__stack;
    void *__gr_top;
    void *__vr_top;
    int __gr_offs;
    int __vr_offs;
} va_list;

void __va_start(va_list *ap);
void *__va_arg(va_list *ap, unsigned long size, unsigned long align, int cls, void *tmp);

#define va_start(ap, last) __va_start(&(ap))

#define va_arg(ap, T)                                                                  \
    (*(T *)__va_arg(&(ap), sizeof(T), _Alignof(T), __builtin_va_class(T), &(T){ 0 }))

#define va_end(ap) ((void)(ap))

#define va_copy(dest, src) ((dest) = (src))

#endif /* _STDARG_H */
