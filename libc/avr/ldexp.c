/*
 * ldexp — multiply a double by a power of two (C11 §7.12.6.6): at most three
 * multiplications by powers built from their IEEE-754 bits; on AVR a double is
 * binary32.
 */
#include <math.h>

static double pow2(int n)
{
    unsigned long w = (unsigned long)(n + 127) << 23;
    return *(double *)&w;
}

double ldexp(double x, int n)
{
    if (n > 127) {
        x *= pow2(127);
        n -= 127;
        if (n > 127) {
            x *= pow2(127);
            n -= 127;
            if (n > 127)
                n = 127;
        }
    } else if (n < -126) {
        /* 2^-102 = 2^-126 * 2^24: stays normal, loses no bits */
        x *= pow2(-102);
        n += 102;
        if (n < -126) {
            x *= pow2(-102);
            n += 102;
            if (n < -126)
                n = -126;
        }
    }
    return x * pow2(n);
}
