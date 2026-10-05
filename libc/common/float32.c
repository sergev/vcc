/*
 * IEEE 754 binary32 arithmetic in software, under the libgcc names the code generator
 * calls (and clang, for a target with no FP hardware, as AVR): add, subtract, multiply,
 * divide, square root, the comparisons and the conversions between float and 32-bit
 * integers.  Correctly rounded to nearest-even, with subnormals, infinities and NaNs, so
 * it agrees with compiler-rt and with the constant folder bit for bit.  Written with
 * integer operations only; a float's bits are reached through a pointer.
 *
 * Inside, a finite value is a sign, a biased exponent e and a significand `sig` with
 * its leading one at bit 26: sig * 2^(e - 127 - 26).  The three bits below the 24 of
 * the result are the guard, the round and the sticky bit (the OR of everything below).
 */
#include <stdint.h>

#define SIGN     0x80000000UL
#define EXP_MASK 0x7f800000UL
#define FRAC     0x007fffffUL
#define IMPLICIT 0x00800000UL
#define QUIET    0x00400000UL
#define QNAN     0x7fc00000UL
#define LEAD     0x04000000UL /* bit 26 */

static uint32_t bits(float f)
{
    return *(uint32_t *)&f;
}

static float from_bits(uint32_t u)
{
    return *(float *)&u;
}

static int is_nan(uint32_t a)
{
    return (a & ~SIGN) > EXP_MASK;
}

static int is_inf(uint32_t a)
{
    return (a & ~SIGN) == EXP_MASK;
}

static int is_zero(uint32_t a)
{
    return (a & ~SIGN) == 0;
}

/* sig >> n, with the bits shifted out ORed into bit 0. */
static uint32_t shift_right_jam(uint32_t sig, int n)
{
    if (n <= 0)
        return sig;
    if (n >= 32)
        return sig != 0;
    return (sig >> n) | ((sig << (32 - n)) != 0);
}

/* The exponent and significand of finite nonzero `a`, normalized: a subnormal has its
 * leading one moved up to bit 26 and the exponent lowered to match. */
static int unpack(uint32_t a, uint32_t *sig)
{
    int exp    = (int)((a >> 23) & 0xff);
    uint32_t s = a & FRAC;
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

/* The float of sign, exponent and significand (leading one at bit 26, or below it for
 * a value that will be subnormal), rounded to nearest-even. */
static float round_pack(uint32_t sign, int exp, uint32_t sig)
{
    if (exp <= 0) {
        sig = shift_right_jam(sig, 1 - exp);
        exp = 0;
    }
    uint32_t low = sig & 7;
    sig >>= 3;
    if (low > 4 || (low == 4 && (sig & 1)))
        sig++;
    /* A normal value's leading one adds one to the exponent field, and a carry out of
     * the rounding one more; a subnormal rounded up to 2^-126 becomes normal. */
    uint32_t r = exp > 0 ? ((uint32_t)(exp - 1) << 23) + sig : sig;
    if (r >= EXP_MASK)
        return from_bits(sign | EXP_MASK);
    return from_bits(sign | r);
}

/* A NaN operand, quieted. */
static float nan_result(uint32_t a, uint32_t b)
{
    return from_bits((is_nan(a) ? a : b) | QUIET);
}

float __addsf3(float fa, float fb)
{
    uint32_t a = bits(fa), b = bits(fb);
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
        uint32_t t = a;
        a          = b;
        b          = t;
    }
    uint32_t sa, sb;
    int ea = unpack(a, &sa);
    int eb = unpack(b, &sb);
    sb     = shift_right_jam(sb, ea - eb);
    uint32_t sig;
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

float __subsf3(float a, float b)
{
    return __addsf3(a, from_bits(bits(b) ^ SIGN));
}

/* hi:lo = a * b, two significands of 24 bits, from 16-bit halves. */
static void mul24(uint32_t a, uint32_t b, uint32_t *hi, uint32_t *lo)
{
    uint32_t al = a & 0xffff, ah = a >> 16;
    uint32_t bl = b & 0xffff, bh = b >> 16;
    uint32_t ll = al * bl, mid = ah * bl + al * bh, hh = ah * bh;
    uint32_t l = ll + (mid << 16);
    *hi        = hh + (mid >> 16) + (l < ll);
    *lo        = l;
}

float __mulsf3(float fa, float fb)
{
    uint32_t a = bits(fa), b = bits(fb);
    uint32_t sign = (a ^ b) & SIGN;
    if (is_nan(a) || is_nan(b))
        return nan_result(a, b);
    if (is_inf(a) || is_inf(b)) {
        if (is_zero(a) || is_zero(b))
            return from_bits(QNAN);
        return from_bits(sign | EXP_MASK);
    }
    if (is_zero(a) || is_zero(b))
        return from_bits(sign);

    uint32_t sa, sb, hi, lo;
    int exp = unpack(a, &sa) + unpack(b, &sb) - 127;
    mul24(sa >> 3, sb >> 3, &hi, &lo);
    /* The product has its leading one at bit 46 or 47 of hi:lo; bring it to 26. */
    uint32_t sig = (hi << 12) | (lo >> 20) | ((lo & 0xfffff) != 0);
    if (sig & (LEAD << 1)) {
        sig = (sig >> 1) | (sig & 1);
        exp++;
    }
    return round_pack(sign, exp, sig);
}

float __divsf3(float fa, float fb)
{
    uint32_t a = bits(fa), b = bits(fb);
    uint32_t sign = (a ^ b) & SIGN;
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

    uint32_t sa, sb;
    int ea = unpack(a, &sa);
    int eb = unpack(b, &sb);
    sa >>= 3;
    sb >>= 3;
    if (sa < sb) {
        sa <<= 1;
        ea--;
    }
    /* 27 quotient bits, the first one, by restoring division; the remainder sticky. */
    uint32_t q = 0, rem = sa;
    for (int i = 0; i < 27; i++) {
        q <<= 1;
        if (rem >= sb) {
            rem -= sb;
            q |= 1;
        }
        rem <<= 1;
    }
    q |= rem != 0;
    return round_pack(sign, ea - eb + 127, q);
}

/* The square root, correctly rounded: the significand m (made even-scaled) is the top of
 * a radicand m * 2^28, whose integer root, digit by digit, has its leading one at bit
 * 26; the remainder is the sticky bit. */
float sqrtf(float x)
{
    uint32_t a = bits(x);
    if (is_nan(a))
        return from_bits(a | QUIET);
    if (is_zero(a))
        return x;
    if (a & SIGN)
        return from_bits(QNAN);
    if (is_inf(a))
        return x;

    uint32_t sig;
    int e      = unpack(a, &sig) - 127;
    uint32_t m = sig >> 2; /* leading one at bit 24; unpack's low three bits are zero */
    int k      = e - 24;
    if (k & 1) {
        m <<= 1;
        k--;
    }
    uint32_t q = 0, r = 0;
    for (int i = 26; i >= 0; i--) {
        int j       = 2 * i - 28; /* the radicand's bits 2i+1, 2i are m's bits j+1, j */
        unsigned d2 = 0;
        if (j + 1 >= 0)
            d2 = (unsigned)((m >> (j + 1)) & 1) << 1;
        if (j >= 0)
            d2 |= (unsigned)((m >> j) & 1);
        r            = (r << 2) | d2;
        uint32_t t   = (q << 2) | 1;
        if (r >= t) {
            r -= t;
            q = (q << 1) | 1;
        } else {
            q <<= 1;
        }
    }
    return round_pack(0, k / 2 + 139, q | (r != 0));
}

/* -1, 0 or 1 as a <, == or > b; neither is a NaN. */
static signed char compare(uint32_t a, uint32_t b)
{
    if (is_zero(a) && is_zero(b))
        return 0;
    int32_t ka = (a & SIGN) ? -(int32_t)(a & ~SIGN) : (int32_t)a;
    int32_t kb = (b & SIGN) ? -(int32_t)(b & ~SIGN) : (int32_t)b;
    return ka < kb ? -1 : ka > kb;
}

/* The comparisons return a value whose relation to zero is the answer; an unordered
 * pair gives the value that makes the comparison false. */
signed char __eqsf2(float a, float b)
{
    return is_nan(bits(a)) || is_nan(bits(b)) ? 1 : compare(bits(a), bits(b));
}

signed char __nesf2(float a, float b)
{
    return __eqsf2(a, b);
}

signed char __ltsf2(float a, float b)
{
    return is_nan(bits(a)) || is_nan(bits(b)) ? 1 : compare(bits(a), bits(b));
}

signed char __lesf2(float a, float b)
{
    return __ltsf2(a, b);
}

signed char __gtsf2(float a, float b)
{
    return is_nan(bits(a)) || is_nan(bits(b)) ? -1 : compare(bits(a), bits(b));
}

signed char __gesf2(float a, float b)
{
    return __gtsf2(a, b);
}

signed char __unordsf2(float a, float b)
{
    return is_nan(bits(a)) || is_nan(bits(b));
}

/* Toward zero; out of range, the extreme of the sign (NaN by its sign bit too). */
long __fixsfsi(float f)
{
    uint32_t a = bits(f);
    int exp    = (int)((a >> 23) & 0xff) - 127;
    if (exp < 0)
        return 0;
    if (exp >= 31)
        return (a & SIGN) ? (long)(-2147483647L - 1) : 2147483647L;
    uint32_t sig = (a & FRAC) | IMPLICIT;
    uint32_t m   = exp >= 23 ? sig << (exp - 23) : sig >> (23 - exp);
    return (a & SIGN) ? -(long)m : (long)m;
}

unsigned long __fixunssfsi(float f)
{
    uint32_t a = bits(f);
    int exp    = (int)((a >> 23) & 0xff) - 127;
    if ((a & SIGN) || exp < 0)
        return 0;
    if (exp >= 32)
        return 0xffffffffUL;
    uint32_t sig = (a & FRAC) | IMPLICIT;
    return exp >= 23 ? sig << (exp - 23) : sig >> (23 - exp);
}

/* The float of magnitude `u` with sign `sign`, rounded. */
static float from_magnitude(uint32_t sign, uint32_t u)
{
    if (u == 0)
        return from_bits(0);
    int p = 31;
    while (!(u & (1UL << p)))
        p--;
    uint32_t sig = p <= 26 ? u << (26 - p) : shift_right_jam(u, p - 26);
    return round_pack(sign, 127 + p, sig);
}

float __floatsisf(long i)
{
    return i < 0 ? from_magnitude(SIGN, 0 - (uint32_t)i) : from_magnitude(0, (uint32_t)i);
}

float __floatunsisf(unsigned long u)
{
    return from_magnitude(0, u);
}
