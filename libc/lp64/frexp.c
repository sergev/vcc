/*
 * frexp — split x into a fraction in [0.5, 1) and a power of two (C11 §7.12.6.4),
 * for IEEE-754 binary64.  Zero, infinity and NaN are returned unchanged, with
 * exponent 0.
 */
#include <math.h>

double frexp(double x, int *e)
{
    unsigned long bits = *(unsigned long *)&x;
    int be             = (int)(bits >> 52) & 0x7ff;

    *e = 0;
    if (be == 0x7ff || x == 0)
        return x;
    if (be == 0) {
        /* subnormal: scale by 2^64 to make it normal */
        x = frexp(x * 18446744073709551616.0, e);
        *e -= 64;
        return x;
    }
    *e   = be - 1022;
    bits = (bits & 0x800fffffffffffffUL) | 0x3fe0000000000000UL;
    return *(double *)&bits;
}
