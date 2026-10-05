/*
 * The conversions between long long and float or double for ILP32 targets, under the
 * libgcc names the code generator calls (backend/riscv/llong.c); the 64-bit division is
 * in int64.c.
 */

static unsigned long long magnitude(long long x)
{
    return x < 0 ? 0 - (unsigned long long)x : (unsigned long long)x;
}

float __floatundisf(unsigned long long u);

/* Both halves are exact in a binary64 double, and so is their sum but for one
   rounding; a binary32 one (AVR) is a float. */
double __floatundidf(unsigned long long u)
{
    if (sizeof(double) == sizeof(float))
        return __floatundisf(u);
    return (double)(unsigned long)(u >> 32) * 4294967296.0 + (double)(unsigned long)u;
}

double __floatdidf(long long x)
{
    return x < 0 ? -__floatundidf(magnitude(x)) : __floatundidf((unsigned long long)x);
}

/* Rounded once, whatever the width of double (binary32 on AVR): the bits beyond 32
   are shifted out into a sticky bit, which rounds as they would, the 32-bit value is
   converted, and the result scaled back exactly. */
float __floatundisf(unsigned long long u)
{
    int s = 0;
    while (u >> 32) {
        u = (u >> 1) | (u & 1);
        s++;
    }
    float f = (float)(unsigned long)u;
    if (s)
        f = f * (float)(1UL << (s / 2)) * (float)(1UL << (s - s / 2));
    return f;
}

float __floatdisf(long long x)
{
    return x < 0 ? -__floatundisf(magnitude(x)) : __floatundisf((unsigned long long)x);
}

/* Truncating toward zero; out of range is undefined, as in C.  Exact for a binary32
   double too: above 2^32 it has no bits below 2^9. */
unsigned long long __fixunsdfdi(double d)
{
    if (d < 1.0)
        return 0;
    unsigned long hi = (unsigned long)(d / 4294967296.0);
    unsigned long lo = (unsigned long)(d - (double)hi * 4294967296.0);
    return (unsigned long long)hi << 32 | lo;
}

long long __fixdfdi(double d)
{
    return d < 0 ? (long long)(0 - __fixunsdfdi(-d)) : (long long)__fixunsdfdi(d);
}

unsigned long long __fixunssfdi(float f)
{
    return __fixunsdfdi(f);
}

long long __fixsfdi(float f)
{
    return __fixdfdi(f);
}
