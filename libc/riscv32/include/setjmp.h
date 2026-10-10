/*
 * <setjmp.h> — non-local jumps (C11 §7.13), RISC-V ILP32 target.
 *
 * TODO: implement setjmp/longjmp.  jmp_buf holds ra, sp, s0-s11 (4 bytes each) and
 * fs0-fs11 (8 bytes each).
 */
#ifndef _SETJMP_H
#define _SETJMP_H

typedef long jmp_buf[38];

int setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
