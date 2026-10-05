/*
 * IEEE 754 binary64 arithmetic in software, under the libgcc names the code generator
 * calls (MSP430, which has no FP hardware): add, subtract, multiply, divide, square
 * root, the comparisons, and the conversions between double, float and 32-bit integers
 * (the 64-bit ones are in libc/ilp32/int64.c).  Correctly rounded to nearest-even, with
 * subnormals, infinities and NaNs, so it agrees with the host's double and with the
 * constant folder bit for bit.  Written with integer operations only; a double's bits
 * are reached through a pointer.  The binary32 sibling is float32.c.
 *
 * Inside, a finite value is a sign, a biased exponent e and a significand `sig` with
 * its leading one at bit 55: sig * 2^(e - 1023 - 55).  The three bits below the 53 of
 * the result are the guard, the round and the sticky bit (the OR of everything below).
 */
#include <stdint.h>

#define SIGN     0x8000000000000000ULL
#define EXP_MASK 0x7ff0000000000000ULL
#define FRAC     0x000fffffffffffffULL
#define IMPLICIT 0x0010000000000000ULL
#define QUIET    0x0008000000000000ULL
#define QNAN     0x7ff8000000000000ULL
#define LEAD     0x0080000000000000ULL /* bit 55 */

static uint64_t bits(double d)
{
    return *(uint64_t *)&d;
}

static double from_bits(uint64_t u)
{
    return *(double *)&u;
}

static int is_nan(uint64_t a)
{
    return (a & ~SIGN) > EXP_MASK;
}

static int is_inf(uint64_t a)
{
    return (a & ~SIGN) == EXP_MASK;
}

static int is_zero(uint64_t a)
{
    return (a & ~SIGN) == 0;
}

/* sig >> n, with the bits shifted out ORed into bit 0. */
static uint64_t shift_right_jam(uint64_t sig, int n)
{
    if (n <= 0)
        return sig;
    if (n >= 64)
        return sig != 0;
    return (sig >> n) | ((sig << (64 - n)) != 0);
}

/* The exponent and significand of finite nonzero `a`, normalized: a subnormal has its
 * leading one moved up to bit 55 and the exponent lowered to match. */
static int unpack(uint64_t a, uint64_t *sig)
{
    int exp    = (int)((a >> 52) & 0x7ff);
    uint64_t s = a & FRAC;
    if (exp == 0)
        exp = 1;
    else
        s |= IMPLICIT;
    s <<= 3;
    while (!(s & LEAD)) {
        s <<= 1;
        exp--;
    }
    *sig = s;
    return exp;
}

/* The double of sign, exponent and significand (leading one at bit 55, or below it for
 * a value that will be subnormal), rounded to nearest-even. */
static double round_pack(uint64_t sign, int exp, uint64_t sig)
{
    if (exp <= 0) {
        sig = shift_right_jam(sig, 1 - exp);
        exp = 0;
    }
    unsigned low = (unsigned)(sig & 7);
    sig >>= 3;
    if (low > 4 || (low == 4 && (sig & 1)))
        sig++;
    /* A normal value's leading one adds one to the exponent field, and a carry out of
     * the rounding one more; a subnormal rounded up to 2^-1022 becomes normal. */
    uint64_t r = exp > 0 ? ((uint64_t)(exp - 1) << 52) + sig : sig;
    if (r >= EXP_MASK)
        return from_bits(sign | EXP_MASK);
    return from_bits(sign | r);
}

/* A NaN operand, quieted. */
static double nan_result(uint64_t a, uint64_t b)
{
    return from_bits((is_nan(a) ? a : b) | QUIET);
}

double __adddf3(double fa, double fb)
{
    uint64_t a = bits(fa), b = bits(fb);
    if (is_nan(a) || is_nan(b))
        return nan_result(a, b);
    if (is_inf(a)) {
        if (is_inf(b) && ((a ^ b) & SIGN))
            return from_bits(QNAN);
        return fa;
    }
    if (is_inf(b))
        return fb;
    if (is_zero(a))
        return is_zero(b) ? from_bits(a & b) : fb;
    if (is_zero(b))
        return fa;

    if ((a & ~SIGN) < (b & ~SIGN)) {
        uint64_t t = a;
        a          = b;
        b          = t;
    }
    uint64_t sa, sb;
    int ea = unpack(a, &sa);
    int eb = unpack(b, &sb);
    sb     = shift_right_jam(sb, ea - eb);
    uint64_t sig;
    if (!((a ^ b) & SIGN)) {
        sig = sa + sb;
        if (sig & (LEAD << 1)) {
            sig = (sig >> 1) | (sig & 1);
            ea++;
        }
    } else {
        sig = sa - sb;
        if (sig == 0)
            return from_bits(0); /* exact cancellation is +0 */
        while (!(sig & LEAD)) {
            sig <<= 1;
            ea--;
        }
    }
    return round_pack(a & SIGN, ea, sig);
}

double __subdf3(double a, double b)
{
    return __adddf3(a, from_bits(bits(b) ^ SIGN));
}

/* hi:lo = a * b, two significands of 53 bits, from 32-bit halves. */
static void mul53(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t al = a & 0xffffffffUL, ah = a >> 32;
    uint64_t bl = b & 0xffffffffUL, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = lh + (ll >> 32) + (hl & 0xffffffffUL); /* fits: each part < 2^32 */
    *lo          = (mid << 32) | (ll & 0xffffffffUL);
    *hi          = hh + (mid >> 32) + (hl >> 32);
}

double __muldf3(double fa, double fb)
{
    uint64_t a = bits(fa), b = bits(fb);
    uint64_t sign = (a ^ b) & SIGN;
    if (is_nan(a) || is_nan(b))
        return nan_result(a, b);
    if (is_inf(a) || is_inf(b)) {
        if (is_zero(a) || is_zero(b))
            return from_bits(QNAN);
        return from_bits(sign | EXP_MASK);
    }
    if (is_zero(a) || is_zero(b))
        return from_bits(sign);

    uint64_t sa, sb, hi, lo;
    int exp = unpack(a, &sa) + unpack(b, &sb) - 1023;
    mul53(sa >> 3, sb >> 3, &hi, &lo);
    /* The product has its leading one at bit 104 or 105 of hi:lo; bring it to 55. */
    uint64_t sig = (hi << 15) | (lo >> 49) | ((lo & ((1ULL << 49) - 1)) != 0);
    if (sig & (LEAD << 1)) {
        sig = (sig >> 1) | (sig & 1);
        exp++;
    }
    return round_pack(sign, exp, sig);
}

double __divdf3(double fa, double fb)
{
    uint64_t a = bits(fa), b = bits(fb);
    uint64_t sign = (a ^ b) & SIGN;
    if (is_nan(a) || is_nan(b))
        return nan_result(a, b);
    if (is_inf(a))
        return from_bits(is_inf(b) ? QNAN : sign | EXP_MASK);
    if (is_inf(b))
        return from_bits(sign);
    if (is_zero(b))
        return from_bits(is_zero(a) ? QNAN : sign | EXP_MASK);
    if (is_zero(a))
        return from_bits(sign);

    uint64_t sa, sb;
    int ea = unpack(a, &sa);
    int eb = unpack(b, &sb);
    sa >>= 3;
    sb >>= 3;
    if (sa < sb) {
        sa <<= 1;
        ea--;
    }
    /* 56 quotient bits, the first one, by restoring division; the remainder sticky. */
    uint64_t q = 0, rem = sa;
    for (int i = 0; i < 56; i++) {
        q <<= 1;
        if (rem >= sb) {
            rem -= sb;
            q |= 1;
        }
        rem <<= 1;
    }
    q |= rem != 0;
    return round_pack(sign, ea - eb + 1023, q);
}

/* The square root, correctly rounded: the significand m (made even-scaled) is the top of
 * a radicand m * 2^58, whose integer root, digit by digit, has its leading one at bit
 * 55; the remainder is the sticky bit. */
double sqrt(double x)
{
    uint64_t a = bits(x);
    if (is_nan(a))
        return from_bits(a | QUIET);
    if (is_zero(a))
        return x;
    if (a & SIGN)
        return from_bits(QNAN);
    if (is_inf(a))
        return x;

    uint64_t sig;
    int e      = unpack(a, &sig) - 1023;
    uint64_t m = sig >> 3; /* leading one at bit 52 */
    int k      = e - 52;
    if (k & 1) {
        m <<= 1;
        k--;
    }
    uint64_t q = 0, r = 0;
    for (int i = 55; i >= 0; i--) {
        int j       = 2 * i - 58; /* the radicand's bits 2i+1, 2i are m's bits j+1, j */
        unsigned d2 = 0;
        if (j + 1 >= 0)
            d2 = (unsigned)((m >> (j + 1)) & 1) << 1;
        if (j >= 0)
            d2 |= (unsigned)((m >> j) & 1);
        r            = (r << 2) | d2;
        uint64_t t   = (q << 2) | 1;
        if (r >= t) {
            r -= t;
            q = (q << 1) | 1;
        } else {
            q <<= 1;
        }
    }
    return round_pack(0, k / 2 + 1049, q | (r != 0));
}

/* -1, 0 or 1 as a <, == or > b; neither is a NaN. */
static int compare(uint64_t a, uint64_t b)
{
    if (is_zero(a) && is_zero(b))
        return 0;
    int64_t ka = (a & SIGN) ? -(int64_t)(a & ~SIGN) : (int64_t)a;
    int64_t kb = (b & SIGN) ? -(int64_t)(b & ~SIGN) : (int64_t)b;
    return ka < kb ? -1 : ka > kb;
}

/* The comparisons return a value whose relation to zero is the answer; an unordered
 * pair gives the value that makes the comparison false. */
int __eqdf2(double a, double b)
{
    return is_nan(bits(a)) || is_nan(bits(b)) ? 1 : compare(bits(a), bits(b));
}

int __nedf2(double a, double b)
{
    return __eqdf2(a, b);
}

int __ltdf2(double a, double b)
{
    return is_nan(bits(a)) || is_nan(bits(b)) ? 1 : compare(bits(a), bits(b));
}

int __ledf2(double a, double b)
{
    return __ltdf2(a, b);
}

int __gtdf2(double a, double b)
{
    return is_nan(bits(a)) || is_nan(bits(b)) ? -1 : compare(bits(a), bits(b));
}

int __gedf2(double a, double b)
{
    return __gtdf2(a, b);
}

int __unorddf2(double a, double b)
{
    return is_nan(bits(a)) || is_nan(bits(b));
}

/* Toward zero; out of range, the extreme of the sign (NaN by its sign bit too). */
long __fixdfsi(double d)
{
    uint64_t a = bits(d);
    int exp    = (int)((a >> 52) & 0x7ff) - 1023;
    if (exp < 0)
        return 0;
    if (exp >= 31)
        return (a & SIGN) ? (long)(-2147483647L - 1) : 2147483647L;
    uint64_t sig = (a & FRAC) | IMPLICIT;
    unsigned long m = (unsigned long)(sig >> (52 - exp));
    return (a & SIGN) ? -(long)m : (long)m;
}

unsigned long __fixunsdfsi(double d)
{
    uint64_t a = bits(d);
    int exp    = (int)((a >> 52) & 0x7ff) - 1023;
    if ((a & SIGN) || exp < 0)
        return 0;
    if (exp >= 32)
        return 0xffffffffUL;
    uint64_t sig = (a & FRAC) | IMPLICIT;
    return (unsigned long)(sig >> (52 - exp));
}

/* The double of magnitude `u` with sign `sign`: exact, 32 bits fit in 53. */
static double from_magnitude(uint64_t sign, unsigned long u)
{
    if (u == 0)
        return from_bits(0); /* +0 */
    int p = 31;
    while (!(u & (1UL << p)))
        p--;
    return round_pack(sign, 1023 + p, (uint64_t)u << (55 - p));
}

double __floatsidf(long i)
{
    return i < 0 ? from_magnitude(SIGN, 0 - (unsigned long)i) : from_magnitude(0, (unsigned long)i);
}

double __floatunsidf(unsigned long u)
{
    return from_magnitude(0, u);
}

/* float → double: exact. */
double __extendsfdf2(float f)
{
    uint32_t a    = *(uint32_t *)&f;
    uint64_t sign = (uint64_t)(a & 0x80000000UL) << 32;
    int exp       = (int)((a >> 23) & 0xff);
    uint64_t frac = a & 0x007fffffUL;
    if (exp == 0xff) /* infinity, or NaN with its payload, quieted */
        return from_bits(sign | EXP_MASK | (frac << 29) | (frac ? QUIET : 0));
    if (exp == 0) {
        if (frac == 0)
            return from_bits(sign);
        exp = 1; /* a subnormal: normalize */
        while (!(frac & 0x00800000UL)) {
            frac <<= 1;
            exp--;
        }
        frac &= 0x007fffffUL;
    }
    return from_bits(sign | ((uint64_t)(exp - 127 + 1023) << 52) | (frac << 29));
}

/* double → float, rounded to nearest-even: the significand jammed down to float32.c's
 * form, with its leading one at bit 26, and rounded as float32.c rounds. */
float __truncdfsf2(double d)
{
    uint64_t a    = bits(d);
    uint32_t sign = (uint32_t)(a >> 32) & 0x80000000UL;
    uint32_t u;
    if (is_nan(a)) {
        u = sign | 0x7fc00000UL | (uint32_t)((a & FRAC) >> 29);
    } else if (is_inf(a)) {
        u = sign | 0x7f800000UL;
    } else if (is_zero(a)) {
        u = sign;
    } else {
        uint64_t sig;
        int exp      = unpack(a, &sig) - 1023 + 127;
        uint32_t s32 = (uint32_t)shift_right_jam(sig, 29); /* leading one at bit 26 */
        if (exp <= 0) {
            s32 = (uint32_t)shift_right_jam(s32, 1 - exp);
            exp = 0;
        }
        unsigned low = s32 & 7;
        s32 >>= 3;
        if (low > 4 || (low == 4 && (s32 & 1)))
            s32++;
        u = exp > 0 ? ((uint32_t)(exp - 1) << 23) + s32 : s32;
        if (exp >= 0xff || u >= 0x7f800000UL)
            u = 0x7f800000UL;
        u |= sign;
    }
    return *(float *)&u;
}
