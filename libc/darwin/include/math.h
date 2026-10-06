/*
 * <math.h> — mathematics (C11 §7.12), hosted macOS: libSystem's libm (-lm links
 * nothing more).  The classification macros call libSystem's exported helpers; long
 * double is double.
 */
#ifndef _MATH_H
#define _MATH_H

#include <float.h>

typedef float  float_t;
typedef double double_t;

#define INFINITY  (1e30f * 1e30f)
#define NAN       (INFINITY * 0.0f)
#define HUGE_VAL  ((double)INFINITY)
#define HUGE_VALF INFINITY
#define HUGE_VALL ((long double)INFINITY)

#define FP_NAN       1
#define FP_INFINITE  2
#define FP_ZERO      3
#define FP_NORMAL    4
#define FP_SUBNORMAL 5

#define FP_ILOGB0   (-2147483647 - 1)
#define FP_ILOGBNAN (-2147483647 - 1)

#define MATH_ERRNO       1
#define MATH_ERREXCEPT   2
int __math_errhandling(void);
#define math_errhandling (__math_errhandling())

#define M_PI 3.14159265358979323846
#define M_E  2.7182818284590452354

int __fpclassifyd(double x);
int __fpclassifyf(float x);
int __fpclassifyl(long double x);
int __signbitd(double x);
int __signbitf(float x);
int __signbitl(long double x);
int __isinfd(double x);
int __isinff(float x);
int __isinfl(long double x);
int __isnand(double x);
int __isnanf(float x);
int __isnanl(long double x);
int __isfinited(double x);
int __isfinitef(float x);
int __isfinitel(long double x);

#define __MATH_GENERIC(fn, x) _Generic((x), float: fn##f, long double: fn##l, default: fn##d)(x)
#define fpclassify(x) __MATH_GENERIC(__fpclassify, x)
#define signbit(x)    __MATH_GENERIC(__signbit, x)
#define isinf(x)      __MATH_GENERIC(__isinf, x)
#define isnan(x)      __MATH_GENERIC(__isnan, x)
#define isfinite(x)   __MATH_GENERIC(__isfinite, x)
#define isnormal(x)   (fpclassify(x) == FP_NORMAL)

#define isgreater(x, y)      (!isunordered(x, y) && (x) > (y))
#define isgreaterequal(x, y) (!isunordered(x, y) && (x) >= (y))
#define isless(x, y)         (!isunordered(x, y) && (x) < (y))
#define islessequal(x, y)    (!isunordered(x, y) && (x) <= (y))
#define islessgreater(x, y)  (!isunordered(x, y) && ((x) < (y) || (x) > (y)))
#define isunordered(x, y)    (isnan(x) || isnan(y))

double      acos(double x);
float       acosf(float x);
long double acosl(long double x);
double      asin(double x);
float       asinf(float x);
long double asinl(long double x);
double      atan(double x);
float       atanf(float x);
long double atanl(long double x);
double      atan2(double y, double x);
float       atan2f(float y, float x);
long double atan2l(long double y, long double x);
double      cos(double x);
float       cosf(float x);
long double cosl(long double x);
double      sin(double x);
float       sinf(float x);
long double sinl(long double x);
double      tan(double x);
float       tanf(float x);
long double tanl(long double x);

double      acosh(double x);
double      asinh(double x);
double      atanh(double x);
double      cosh(double x);
float       coshf(float x);
double      sinh(double x);
float       sinhf(float x);
double      tanh(double x);
float       tanhf(float x);

double      exp(double x);
float       expf(float x);
long double expl(long double x);
double      exp2(double x);
double      expm1(double x);
double      frexp(double x, int *exp);
float       frexpf(float x, int *exp);
long double frexpl(long double x, int *exp);
int         ilogb(double x);
double      ldexp(double x, int exp);
float       ldexpf(float x, int exp);
long double ldexpl(long double x, int exp);
double      log(double x);
float       logf(float x);
long double logl(long double x);
double      log10(double x);
float       log10f(float x);
double      log1p(double x);
double      log2(double x);
float       log2f(float x);
double      logb(double x);
double      modf(double x, double *iptr);
float       modff(float x, float *iptr);
long double modfl(long double x, long double *iptr);
double      scalbn(double x, int n);
double      scalbln(double x, long n);

double      cbrt(double x);
double      fabs(double x);
float       fabsf(float x);
long double fabsl(long double x);
double      hypot(double x, double y);
float       hypotf(float x, float y);
double      pow(double x, double y);
float       powf(float x, float y);
long double powl(long double x, long double y);
double      sqrt(double x);
float       sqrtf(float x);
long double sqrtl(long double x);

double      erf(double x);
double      erfc(double x);
double      lgamma(double x);
double      tgamma(double x);

double      ceil(double x);
float       ceilf(float x);
long double ceill(long double x);
double      floor(double x);
float       floorf(float x);
long double floorl(long double x);
double      nearbyint(double x);
double      rint(double x);
float       rintf(float x);
long        lrint(double x);
long long   llrint(double x);
double      round(double x);
float       roundf(float x);
long double roundl(long double x);
long        lround(double x);
long long   llround(double x);
double      trunc(double x);
float       truncf(float x);
long double truncl(long double x);

double      fmod(double x, double y);
float       fmodf(float x, float y);
long double fmodl(long double x, long double y);
double      remainder(double x, double y);
double      remquo(double x, double y, int *quo);

double      copysign(double x, double y);
float       copysignf(float x, float y);
long double copysignl(long double x, long double y);
double      nan(const char *tag);
float       nanf(const char *tag);
double      nextafter(double x, double y);
double      nexttoward(double x, long double y);

double      fdim(double x, double y);
double      fmax(double x, double y);
float       fmaxf(float x, float y);
double      fmin(double x, double y);
float       fminf(float x, float y);
double      fma(double x, double y, double z);
float       fmaf(float x, float y, float z);

#endif /* _MATH_H */
