/*
 * <stdarg.h> — variable arguments (C11 §7.16), AVR.
 *
 * The compiler has no va_* builtins.  A variadic function takes all its arguments on
 * the stack, named ones included, in order and unaligned, and its parameters live
 * where they came in: va_list is a plain pointer (as clang's __builtin_va_list for
 * the target), va_start steps over the last named parameter, and va_arg over each
 * value.  A char or short argument comes promoted to int, a float to double.
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

#define va_start(ap, last) ((ap) = (char *)&(last) + sizeof(last))

#define va_arg(ap, T) (*(T *)(((ap) += sizeof(T)) - sizeof(T)))

#define va_end(ap) ((void)(ap))

#define va_copy(dest, src) ((dest) = (src))

#endif /* _STDARG_H */
