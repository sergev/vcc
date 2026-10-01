/*
 * <fenv.h> — floating-point environment (C11 §7.6), BESM-6 target.
 *
 * BESM-6 floating point has no IEEE exception flags, rounding-mode register, or
 * traps that this library exposes, so the floating-point environment is
 * degenerate: there is a single mode and no sticky flags.  The surface is
 * provided for source portability; a future implementation may map FE_* onto
 * the hardware's arithmetic-condition register if useful (TODO).
 */
#ifndef _FENV_H
#define _FENV_H

typedef int fexcept_t;

typedef struct {
    int __mode;
} fenv_t;

/* No exception flags are raised on BESM-6. */
#define FE_ALL_EXCEPT 0

/* Single rounding mode: round to nearest. */
#define FE_TONEAREST 0

#define FE_DFL_ENV ((const fenv_t *)0)

int feclearexcept(int excepts);
int fetestexcept(int excepts);
int feraiseexcept(int excepts);
int fegetexceptflag(fexcept_t *flagp, int excepts);
int fesetexceptflag(const fexcept_t *flagp, int excepts);

int fegetround(void);
int fesetround(int round);

int fegetenv(fenv_t *envp);
int fesetenv(const fenv_t *envp);
int feholdexcept(fenv_t *envp);
int feupdateenv(const fenv_t *envp);

#endif /* _FENV_H */
