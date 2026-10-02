/*
 * modf — split x into integral and fractional parts, both with the sign of x
 * (C11 §7.12.6.12), for IEEE-754 binary64.  At or above 2^52 every double is
 * integral; below that the integral part fits a long.
 */
#include <math.h>

double modf(double x, double *iptr)
{
    double frac;

    if (x != x) { /* NaN */
        *iptr = x;
        return x;
    }
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) {
        *iptr = x; /* includes infinities */
        frac  = 0.0;
    } else {
        *iptr = (double)(long)x;
        frac  = x - *iptr;
    }
    if (frac == 0 && x < 0)
        frac = -0.0;
    return frac;
}
