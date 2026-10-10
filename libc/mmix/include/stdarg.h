/*
 * <stdarg.h> — variable arguments (C11 §7.16), MMIX.
 *
 * The caller passes variable arguments exactly as named ones: in registers, then on the
 * stack, 8 bytes each.  A variadic function stores the argument registers after its
 * named ones into the top of its frame, directly below the incoming stack arguments, so
 * every variable argument is one 8-byte slot of a contiguous run, and va_list is a plain
 * pointer (GCC's).  va_start is __va_start(&ap), which the code generator expands in
 * place: it points ap at the first variable slot.
 *
 * A slot holds a register's value, so a value of 8 bytes or less sits right-justified
 * in it (big-endian: an int at offset 4, a 3-byte structure at 5); a larger structure
 * comes by reference, as its address.  A char or short comes promoted to int, a float
 * to double.
 */
#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

void __va_start(va_list *ap);

#define va_start(ap, last) __va_start(&(ap))

#define va_arg(ap, T) (*(sizeof(T) > 8 ? *(T **)(((ap) += 8) - 8) : (T *)(((ap) += 8) - sizeof(T))))

#define va_end(ap) ((void)(ap))

#define va_copy(dest, src) ((dest) = (src))

#endif /* _STDARG_H */
