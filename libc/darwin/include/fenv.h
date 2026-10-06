/*
 * <fenv.h> — floating-point environment (C11 §7.6), hosted macOS on Apple silicon:
 * libSystem's values; fenv_t is FPSR and FPCR.
 */
#ifndef _FENV_H
#define _FENV_H

typedef unsigned short fexcept_t;

typedef struct {
    unsigned long long __fpsr;
    unsigned long long __fpcr;
} fenv_t;

#define FE_INVALID     0x01
#define FE_DIVBYZERO   0x02
#define FE_OVERFLOW    0x04
#define FE_UNDERFLOW   0x08
#define FE_INEXACT     0x10
#define FE_FLUSHTOZERO 0x80
#define FE_ALL_EXCEPT  0x9f

#define FE_TONEAREST  0
#define FE_UPWARD     0x400000
#define FE_DOWNWARD   0x800000
#define FE_TOWARDZERO 0xc00000

extern const fenv_t _FE_DFL_ENV;
#define FE_DFL_ENV (&_FE_DFL_ENV)

int feclearexcept(int excepts);
int fegetexceptflag(fexcept_t *flagp, int excepts);
int feraiseexcept(int excepts);
int fesetexceptflag(const fexcept_t *flagp, int excepts);
int fetestexcept(int excepts);

int fegetround(void);
int fesetround(int round);

int fegetenv(fenv_t *envp);
int feholdexcept(fenv_t *envp);
int fesetenv(const fenv_t *envp);
int feupdateenv(const fenv_t *envp);

#endif /* _FENV_H */
