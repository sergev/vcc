/*
 * <setjmp.h> — non-local jumps (C11 §7.13), RISC-V LP64 target.
 *
 * TODO: implement setjmp/longjmp.  jmp_buf holds ra, sp, s0-s11 and fs0-fs11.
 */
#ifndef _SETJMP_H
#define _SETJMP_H

typedef long jmp_buf[26];

int setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
