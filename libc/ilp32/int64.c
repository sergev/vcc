/*
 * 64-bit integer runtime for ILP32 targets, under the libgcc names the code generator calls
 * (backend/riscv/llong.c): division and remainder.  Everything else on long long is
 * inline; the conversions to and from float and double are in int64conv.c, so that a
 * program dividing long longs does not take in the floating-point runtime.
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
