/*
 * <math.h> — mathematics (C11 §7.12), AVR.
 *
 * float, double and long double are all binary32, so each double function serves as
 * its float and long double forms too.  Status: modf(), frexp(), ldexp(), fabs(),
 * fmin(), fmax() and fma() are implemented in libc.a; the rest are declared for future
 * implementation (TODO).
 */
#ifndef _MATH_H
#define _MATH_H

#include <float.h>

/* Overflowing constant expressions: the compiler has no builtins for these. */
#define INFINITY  (1e30f * 1e30f)
#define NAN       (INFINITY * 0.0f)
#define HUGE_VAL  ((double)INFINITY)
#define HUGE_VALF INFINITY
#define HUGE_VALL ((long double)INFINITY)

#define M_PI 3.14159265358979
#define M_E  2.71828182845905

/* ---- implemented in libc.a ---- */
double modf(double x, double *iptr);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double fabs(double x);
double fmin(double x, double y);
double fmax(double x, double y);
double fma(double x, double y, double z);

#define frexpf frexp
#define frexpl frexp
#define ldexpf ldexp
#define ldexpl ldexp
#define fabsf  fabs
#define fabsl  fabs
#define fminf  fmin
#define fminl  fmin
#define fmaxf  fmax
#define fmaxl  fmax
#define fmaf   fma
#define fmal   fma

/* ---- declared for future implementation (TODO) ---- */
double sqrt(double x);
double floor(double x);
double ceil(double x);
double round(double x);
double trunc(double x);
double fmod(double x, double y);

double pow(double x, double y);
double exp(double x);
double log(double x);
double log10(double x);

double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);

double hypot(double x, double y);
double copysign(double x, double y);

#endif /* _MATH_H */
