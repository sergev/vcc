/*
 * <fenv.h> — floating-point environment (C11 §7.6), hosted Linux on RISC-V: glibc's
 * values; fenv_t is fcsr.
 */
#ifndef _FENV_H
#define _FENV_H

typedef unsigned int fexcept_t;
typedef unsigned int fenv_t;

#define FE_INEXACT    0x01
#define FE_UNDERFLOW  0x02
#define FE_OVERFLOW   0x04
#define FE_DIVBYZERO  0x08
#define FE_INVALID    0x10
#define FE_ALL_EXCEPT 0x1f

#define FE_TONEAREST  0
#define FE_TOWARDZERO 1
#define FE_DOWNWARD   2
#define FE_UPWARD     3

#define FE_DFL_ENV ((const fenv_t *)-1)

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
