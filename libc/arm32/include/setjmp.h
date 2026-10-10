/*
 * <setjmp.h> — non-local jumps (C11 §7.13), ARM32 AAPCS target.
 *
 * TODO: implement setjmp/longjmp.  jmp_buf holds r4-r11, sp and lr (4 bytes each) and
 * d8-d15 (8 bytes each).
 */
#ifndef _SETJMP_H
#define _SETJMP_H

typedef long long jmp_buf[13];

int setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
