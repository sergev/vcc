/*
 * <stdarg.h> — variable arguments (C11 §7.16), MSP430.
 *
 * The compiler has no va_* builtins.  A variadic function takes its last named argument
 * and all the variable ones on the stack, in order, each in a whole number of 2-byte
 * words (the named ones before it go in registers, by the ordinary rules, as GCC passes
 * them): va_list is a plain pointer (as GCC's __builtin_va_list for the target),
 * va_start steps over the last named parameter, and va_arg over each value.  A char or short
 * argument comes promoted to int, a float to double.  A structure or union comes by reference, as
 * its address
 * (__builtin_va_class(T) is TAC_MSP430_BY_REF, 1): va_arg takes the address and reads
 * the object through it.  The last named parameter must not be a structure, which the
 * callee keeps a copy of elsewhere.
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

/* The stack size of an argument of type T: its size rounded up to a word. */
#define __va_size(T) ((sizeof(T) + 1) / 2 * 2)

#define va_start(ap, last) ((ap) = (char *)&(last) + __va_size(last))

#define va_arg(ap, T)                                           \
    (*(T *)(__builtin_va_class(T) ? *(char **)(((ap) += 2) - 2) \
                                  : (((ap) += __va_size(T)) - __va_size(T))))

#define va_end(ap) ((void)(ap))

#define va_copy(dest, src) ((dest) = (src))

#endif /* _STDARG_H */
