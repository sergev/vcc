/*
 * <setjmp.h> — non-local jumps (C11 §7.13), MSP430.
 *
 * jmp_buf holds the call-saved R4-R10, the stack pointer after the return and the
 * return address (libc/msp430/setjmp.s): newlib's nine words, in another order.
 */
#ifndef _SETJMP_H
#define _SETJMP_H

typedef unsigned int jmp_buf[9];

int  setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
