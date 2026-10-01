/*
 * ldexp — x * 2^n (C11 §7.12.6.6), for IEEE-754 binary64.
 *
 * Scales by exact powers of two built from their bits.  A large |n| is applied in
 * steps that keep each intermediate normal, so the result is rounded once.
 */
#include <math.h>

/* 2^n, for -1022 <= n <= 1023. */
static double pow2(int n)
{
    unsigned long bits = (unsigned long)(n + 1023) << 52;
    return *(double *)&bits;
}

double ldexp(double x, int n)
{
    if (n > 1023) {
        x *= pow2(1023);
        n -= 1023;
        if (n > 1023) {
            x *= pow2(1023);
            n -= 1023;
            if (n > 1023)
                n = 1023;
        }
    } else if (n < -1022) {
        /* 2^-969 = 2^-1022 * 2^53: stays normal, loses no bits */
        x *= pow2(-969);
        n += 969;
        if (n < -1022) {
            x *= pow2(-969);
            n += 969;
            if (n < -1022)
                n = -1022;
        }
    }
    return x * pow2(n);
}
