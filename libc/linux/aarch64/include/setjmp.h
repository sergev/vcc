/*
 * <setjmp.h> — non-local jumps (C11 §7.13), hosted Linux on AArch64: glibc's 312-byte
 * jmp_buf.  setjmp is glibc's _setjmp, which leaves the signal mask alone, as its
 * own header has it.
 */
#ifndef _SETJMP_H
#define _SETJMP_H

typedef struct __jmp_buf_tag {
    unsigned long long __jmpbuf[22];
    int __mask_was_saved;
    unsigned long __saved_mask[16];
} jmp_buf[1];

int _setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int val);
#define setjmp(env) _setjmp(env)

#endif /* _SETJMP_H */
