/*
 * <setjmp.h> — non-local jumps (C11 §7.13), AArch64 LP64 target.
 *
 * TODO: implement setjmp/longjmp.  jmp_buf holds x19-x30, sp and d8-d15.
 */
#ifndef _SETJMP_H
#define _SETJMP_H

typedef long jmp_buf[22];

int setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
