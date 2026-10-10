/*
 * <setjmp.h> — non-local jumps (C11 §7.13), AVR.
 *
 * jmp_buf holds the call-saved r2-r17, Y (r29:r28), the stack pointer after the
 * return, SREG and the return address (libc/avr/setjmp.s).
 */
#ifndef _SETJMP_H
#define _SETJMP_H

typedef unsigned char jmp_buf[23];

int setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
