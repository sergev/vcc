/*
 * modf — split a double into integer and fractional parts (C11 §7.12.6.12).  The
 * integer part clears the fraction bits of the IEEE-754 representation; on AVR a
 * double is binary32, one 32-bit long.
 */
#include <math.h>

double modf(double x, double *iptr)
{
    double i = x, frac;
    unsigned long *w = (unsigned long *)&i;
    int e            = (int)((*w >> 23) & 0xff) - 127; /* unbiased exponent */

    if (x != x) { /* NaN */
        *iptr = x;
        return x;
    }
    if (e >= 23) {
        *iptr = x; /* includes infinities */
        frac  = 0.0;
    } else {
        if (e < 0)
            *w &= 0x80000000UL;
        else
            *w &= ~(0x007fffffUL >> e);
        *iptr = i;
        frac  = x - i;
    }
    if (frac == 0 && x < 0)
        frac = -0.0;
    return frac;
}
