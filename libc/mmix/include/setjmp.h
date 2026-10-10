/*
 * <setjmp.h> — non-local jumps (C11 §7.13), MMIX.
 *
 * jmp_buf is newlib's, the layout of GCC's built-in too: the frame pointer $253, rJ (the
 * return address), the stack pointer $254, rO as it was before the call to setjmp (the
 * register stack's top, to which longjmp pops back), and the value longjmp hands over
 * (libc/mmix/setjmp.s).
 */
#ifndef _SETJMP_H
#define _SETJMP_H

typedef unsigned long jmp_buf[5];

int setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
