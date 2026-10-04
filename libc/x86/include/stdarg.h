/*
 * <stdarg.h> — variable arguments (C11 §7.16), x86-64 System V target.
 *
 * va_list is clang's: an array of one __va_list_tag, so it passes as a pointer, and
 * one can be handed to and from clang-compiled code.  A variadic function saves rdi-r9
 * and xmm0-xmm7 into a 176-byte register save area; gp_offset (0-48) and fp_offset
 * (48-176) step through it, and then the arguments continue at overflow_arg_area.
 * va_start is __va_start(ap), which the code generator expands in place; va_arg calls
 * __va_arg with the type's argument class, __builtin_va_class(T): INTEGER or SSE per
 * eightbyte, or MEMORY (a struct whose eightbytes mix the classes is put together in
 * the last argument, a temporary of type T).
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef struct __va_list_tag {
    unsigned int gp_offset;
    unsigned int fp_offset;
    void *overflow_arg_area;
    void *reg_save_area;
} va_list[1];

void __va_start(va_list ap);
void *__va_arg(va_list ap, unsigned long size, unsigned long align, int cls, void *tmp);

#define va_start(ap, last) __va_start(ap)

#define va_arg(ap, T)                                                                  \
    (*(T *)__va_arg(ap, sizeof(T), _Alignof(T), __builtin_va_class(T), &(T){ 0 }))

#define va_end(ap) ((void)(ap))

#define va_copy(dest, src) (*(dest) = *(src))

#endif /* _STDARG_H */
