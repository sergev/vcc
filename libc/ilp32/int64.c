/*
 * 64-bit integer runtime for ILP32 targets, under the libgcc names the code generator calls
 * (backend/riscv/llong.c): division and remainder, and the conversions between long
 * long and float or double.  Everything else on long long is inline.
 */

/* n / d, with the remainder in *rem. */
static unsigned long long udivmod(unsigned long long n, unsigned long long d,
                                  unsigned long long *rem)
{
    if ((n >> 32) == 0 && (d >> 32) == 0) {
        unsigned long a = (unsigned long)n, b = (unsigned long)d;
        *rem            = a % b;
        return a / b;
    }
    if (d >> 63) {
        /* The quotient is 0 or 1, and the shifting below would lose the top bit. */
        unsigned long long q = n >= d;
        *rem                 = n - (q ? d : 0);
        return q;
    }
    unsigned long long q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= 1ULL << i;
        }
    }
    *rem = r;
    return q;
}

unsigned long long __udivdi3(unsigned long long n, unsigned long long d)
{
    unsigned long long r;
    return udivmod(n, d, &r);
}

unsigned long long __umoddi3(unsigned long long n, unsigned long long d)
{
    unsigned long long r;
    udivmod(n, d, &r);
    return r;
}

static unsigned long long magnitude(long long x)
{
    return x < 0 ? 0 - (unsigned long long)x : (unsigned long long)x;
}

/* Truncating toward zero: the quotient is negative when the signs differ. */
long long __divdi3(long long n, long long d)
{
    unsigned long long r, q = udivmod(magnitude(n), magnitude(d), &r);
    return (n < 0) != (d < 0) ? (long long)(0 - q) : (long long)q;
}

/* The remainder has the sign of the dividend. */
long long __moddi3(long long n, long long d)
{
    unsigned long long r;
    udivmod(magnitude(n), magnitude(d), &r);
    return n < 0 ? (long long)(0 - r) : (long long)r;
}

/* Both halves are exact in a double, and so is their sum but for one rounding. */
double __floatundidf(unsigned long long u)
{
    return (double)(unsigned long)(u >> 32) * 4294967296.0 + (double)(unsigned long)u;
}

double __floatdidf(long long x)
{
    return x < 0 ? -__floatundidf(magnitude(x)) : __floatundidf((unsigned long long)x);
}

/* Through double, rounded once: above 2^53 the bits a float cannot keep are first
   folded into a sticky bit, so that the double is exact. */
float __floatundisf(unsigned long long u)
{
    if (u >> 53)
        return (float)((double)((u >> 11) | ((u & 0x7ff) != 0)) * 2048.0);
    return (float)__floatundidf(u);
}

float __floatdisf(long long x)
{
    return x < 0 ? -__floatundisf(magnitude(x)) : __floatundisf((unsigned long long)x);
}

/* Truncating toward zero; out of range is undefined, as in C. */
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
