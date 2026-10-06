/*
 * <setjmp.h> — non-local jumps (C11 §7.13), hosted macOS on Apple silicon:
 * libSystem's jmp_buf of 48 ints.
 */
#ifndef _SETJMP_H
#define _SETJMP_H

typedef int jmp_buf[48];

int            setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
