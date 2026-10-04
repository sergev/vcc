/*
 * <setjmp.h> — non-local jumps (C11 §7.13), x86-64 System V target.
 *
 * jmp_buf holds the callee-saved rbx, rbp and r12-r15, the stack pointer after the
 * return, the return address, and the MXCSR and x87 control words (libc/x86/setjmp.s).
 */
#ifndef _SETJMP_H
#define _SETJMP_H

typedef long jmp_buf[9];

int  setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
