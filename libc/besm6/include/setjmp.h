/*
 * <setjmp.h> — non-local jumps (C11 §7.13), BESM-6 target.
 *
 * TODO: implement setjmp/longjmp in libc.bin.  A jmp_buf must save the registers
 * the calling convention requires to be preserved across the call — at least the
 * return address (r13), the parameter and auto pointers (r6, r7) and the stack
 * pointer (r15); see backend/besm6/abi.h.  Eight words leaves room for that set.
 */
#ifndef _SETJMP_H
#define _SETJMP_H

typedef long jmp_buf[8];

int setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
