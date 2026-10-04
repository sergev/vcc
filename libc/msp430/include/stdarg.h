/*
 * <stdarg.h> — variable arguments (C11 §7.16), MSP430.
 *
 * The compiler has no va_* builtins.  A variadic function takes all its arguments on
 * the stack, named ones included, in order, each in a whole number of 2-byte words, and
 * its parameters live where they came in: va_list is a plain pointer (as clang's
 * __builtin_va_list for the target), va_start steps over the last named parameter, and
 * va_arg over each value.  A char or short argument comes promoted to int, a float to
 * double.
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

/* The stack size of an argument of type T: its size rounded up to a word. */
#define __va_size(T) ((sizeof(T) + 1) / 2 * 2)

#define va_start(ap, last) ((ap) = (char *)&(last) + __va_size(last))

#define va_arg(ap, T) (*(T *)(((ap) += __va_size(T)) - __va_size(T)))

#define va_end(ap) ((void)(ap))

#define va_copy(dest, src) ((dest) = (src))

#endif /* _STDARG_H */
