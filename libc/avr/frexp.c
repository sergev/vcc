/*
 * frexp — break a double into a fraction in [0.5, 1) and a power of two (C11
 * §7.12.6.4), on the IEEE-754 bits: on AVR a double is binary32, one 32-bit long.
 */
#include <math.h>

double frexp(double x, int *e)
{
    unsigned long *w = (unsigned long *)&x;
    int be           = (int)(*w >> 23) & 0xff;

    *e = 0;
    if (be == 0xff || x == 0)
        return x;
    if (be == 0) {
        /* subnormal: scale by 2^32 to make it normal */
        x = frexp(x * 4294967296.0, e);
        *e -= 32;
        return x;
    }
    *e = be - 126;
    *w = (*w & 0x807fffffUL) | 0x3f000000UL;
    return x;
}
