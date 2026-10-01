/*
 * long double (IEEE-754 binary128) arithmetic, comparisons and conversions: the
 * libgcc/compiler-rt routines that our code generator and clang call.  Results are
 * rounded to nearest, ties to even; exceptions are not raised.  No long double
 * arithmetic is used here, only its bits.
 */
typedef unsigned long u64;

typedef union {
    long double f;
    struct {
        u64 lo, hi;
    } w;
} Bits;

/* A 128-bit unsigned integer. */
typedef struct {
    u64 hi, lo;
} U128;

enum { ZERO, FINITE, INF, NAN };

/* An unpacked number.  A finite one is normalized: the significand's leading bit is
 * bit 112, and its value is m * 2^(exp - 16383 - 112); a subnormal gets exp <= 0. */
typedef struct {
    int cls;
    int sign;
    int exp;
    U128 m;
    u64 hi, lo; /* the bits */
} Num;

#define SIGN  0x8000000000000000UL
#define FRAC  0x0000ffffffffffffUL /* the fraction bits of the high doubleword */
#define EXPS  0x7fff000000000000UL
#define QUIET 0x0000800000000000UL
#define BIAS  16383

static int top64(u64 x)
{
    int n = 0;
    if (x >> 32) {
        n += 32;
        x >>= 32;
    }
    if (x >> 16) {
        n += 16;
        x >>= 16;
    }
    if (x >> 8) {
        n += 8;
        x >>= 8;
    }
    if (x >> 4) {
        n += 4;
        x >>= 4;
    }
    if (x >> 2) {
        n += 2;
        x >>= 2;
    }
    if (x >> 1)
        n += 1;
    return n;
}

/* The position of the leading one bit, or -1. */
static int u128_top(U128 a)
{
    if (a.hi)
        return 64 + top64(a.hi);
    return a.lo ? top64(a.lo) : -1;
}

static U128 u128_shl(U128 a, int n)
{
    U128 r;
    if (n == 0)
        return a;
    if (n >= 64) {
        r.hi = a.lo << (n - 64);
        r.lo = 0;
    } else {
        r.hi = a.hi << n | a.lo >> (64 - n);
        r.lo = a.lo << n;
    }
    return r;
}

static U128 u128_shr(U128 a, int n)
{
    U128 r;
    if (n == 0)
        return a;
    if (n >= 128) {
        r.hi = 0;
        r.lo = 0;
    } else if (n >= 64) {
        r.hi = 0;
        r.lo = a.hi >> (n - 64);
    } else {
        r.hi = a.hi >> n;
        r.lo = a.lo >> n | a.hi << (64 - n);
    }
    return r;
}

/* Shift right, or-ing the bits shifted out into bit 0. */
static U128 u128_shr_sticky(U128 a, int n)
{
    U128 r = u128_shr(a, n);
    if (n > 0 && (n >= 128 || u128_shl(r, n).hi != a.hi || u128_shl(r, n).lo != a.lo))
        r.lo |= 1;
    return r;
}

static U128 u128_add(U128 a, U128 b)
{
    U128 r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo);
    return r;
}

static U128 u128_sub(U128 a, U128 b)
{
    U128 r;
    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - (a.lo < b.lo);
    return r;
}

static int u128_cmp(U128 a, U128 b)
{
    if (a.hi != b.hi)
        return a.hi < b.hi ? -1 : 1;
    if (a.lo != b.lo)
        return a.lo < b.lo ? -1 : 1;
    return 0;
}

/* The 128-bit product of two doublewords. */
static U128 mul64(u64 a, u64 b)
{
    u64 a0 = a & 0xffffffff, a1 = a >> 32, b0 = b & 0xffffffff, b1 = b >> 32;
    u64 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    u64 mid = (p00 >> 32) + (p01 & 0xffffffff) + (p10 & 0xffffffff);
    U128 r;
    r.lo = mid << 32 | (p00 & 0xffffffff);
    r.hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    return r;
}

static long double pack(u64 hi, u64 lo)
{
    Bits b;
    b.w.hi = hi;
    b.w.lo = lo;
    return b.f;
}

static Num unpack(long double f)
{
    Bits b;
    Num n;
    b.f    = f;
    n.hi   = b.w.hi;
    n.lo   = b.w.lo;
    n.sign = b.w.hi >> 63;
    n.exp  = (b.w.hi >> 48) & 0x7fff;
    n.m.hi = b.w.hi & FRAC;
    n.m.lo = b.w.lo;
    if (n.exp == 0x7fff) {
        n.cls = (n.m.hi | n.m.lo) ? NAN : INF;
    } else if (n.exp) {
        n.cls = FINITE;
        n.m.hi |= FRAC + 1;
    } else if (n.m.hi | n.m.lo) {
        int shift = 112 - u128_top(n.m);
        n.cls     = FINITE;
        n.m       = u128_shl(n.m, shift);
        n.exp     = 1 - shift;
    } else {
        n.cls = ZERO;
    }
    return n;
}

static long double quiet(Num a)
{
    return pack(a.hi | QUIET, a.lo);
}

static long double default_nan(void)
{
    return pack(EXPS | QUIET, 0);
}

static long double infinity(int sign)
{
    return pack((u64)sign << 63 | EXPS, 0);
}

static long double zero(int sign)
{
    return pack((u64)sign << 63, 0);
}

/* Round m * 2^(exp - 16383 - 125) and pack it. */
static long double round_pack(int sign, int exp, U128 m)
{
    u64 s    = (u64)sign << 63;
    int lead = u128_top(m);
    u64 rest;
    if (lead < 0)
        return pack(s, 0);
    if (lead > 125) {
        m = u128_shr_sticky(m, lead - 125);
        exp += lead - 125;
    } else if (lead < 125) {
        m = u128_shl(m, 125 - lead);
        exp -= 125 - lead;
    }
    if (exp <= 0) {
        /* Subnormal: scaled as exponent 1, without the leading bit. */
        m   = u128_shr_sticky(m, 1 - exp);
        exp = 1;
    }
    rest = m.lo & 0x1fff;
    m    = u128_shr(m, 13);
    if (rest > 0x1000 || (rest == 0x1000 && (m.lo & 1))) {
        U128 one = { 0, 1 };
        m        = u128_add(m, one);
    }
    if (m.hi >> 49) {
        m = u128_shr(m, 1);
        exp++;
    }
    if (!(m.hi >> 48))
        exp = 0;
    if (exp >= 0x7fff)
        return infinity(sign);
    return pack(s | (u64)exp << 48 | (m.hi & FRAC), m.lo);
}

/* A finite unpacked number, packed again. */
static long double repack(Num a)
{
    return round_pack(a.sign, a.exp, u128_shl(a.m, 13));
}

static long double add(long double x, long double y, int negate)
{
    Num a = unpack(x), b = unpack(y);
    U128 ma, mb, m;
    b.sign ^= negate;
    if (a.cls == NAN)
        return quiet(a);
    if (b.cls == NAN)
        return quiet(b);
    if (a.cls == INF)
        return b.cls == INF && a.sign != b.sign ? default_nan() : infinity(a.sign);
    if (b.cls == INF)
        return infinity(b.sign);
    if (a.cls == ZERO && b.cls == ZERO)
        return zero(a.sign & b.sign);
    if (a.cls == ZERO)
        return repack(b);
    if (b.cls == ZERO)
        return repack(a);
    if (a.exp < b.exp || (a.exp == b.exp && u128_cmp(a.m, b.m) < 0)) {
        Num t = a;
        a     = b;
        b     = t;
    }
    ma = u128_shl(a.m, 13);
    mb = u128_shr_sticky(u128_shl(b.m, 13), a.exp - b.exp);
    if (a.sign == b.sign) {
        m = u128_add(ma, mb);
    } else {
        m = u128_sub(ma, mb);
        if (!(m.hi | m.lo))
            return zero(0);
    }
    return round_pack(a.sign, a.exp, m);
}

long double __addtf3(long double x, long double y)
{
    return add(x, y, 0);
}

long double __subtf3(long double x, long double y)
{
    return add(x, y, 1);
}

long double __multf3(long double x, long double y)
{
    Num a = unpack(x), b = unpack(y);
    int sign = a.sign ^ b.sign;
    U128 ll, lh, hl, hh, m;
    u64 p1, p2, p3, c1, c2;
    if (a.cls == NAN)
        return quiet(a);
    if (b.cls == NAN)
        return quiet(b);
    if (a.cls == INF || b.cls == INF)
        return a.cls == ZERO || b.cls == ZERO ? default_nan() : infinity(sign);
    if (a.cls == ZERO || b.cls == ZERO)
        return zero(sign);

    /* The 226-bit product, in doublewords p3..p0. */
    ll = mul64(a.m.lo, b.m.lo);
    lh = mul64(a.m.lo, b.m.hi);
    hl = mul64(a.m.hi, b.m.lo);
    hh = mul64(a.m.hi, b.m.hi);
    p1 = ll.hi + lh.lo;
    c1 = p1 < ll.hi;
    p1 += hl.lo;
    c1 += p1 < hl.lo;
    p2 = lh.hi + hl.hi;
    c2 = p2 < lh.hi;
    p2 += hh.lo;
    c2 += p2 < hh.lo;
    p2 += c1;
    c2 += p2 < c1;
    p3 = hh.hi + c2;

    /* Bits 227..100, the rest sticky. */
    m.hi = p2 >> 36 | p3 << 28;
    m.lo = p1 >> 36 | p2 << 28;
    if (ll.lo || (p1 & 0xfffffffff))
        m.lo |= 1;
    return round_pack(sign, a.exp + b.exp - (BIAS - 1), m);
}

long double __divtf3(long double x, long double y)
{
    Num a = unpack(x), b = unpack(y);
    int sign = a.sign ^ b.sign;
    U128 q = { 0, 0 }, r;
    int i;
    if (a.cls == NAN)
        return quiet(a);
    if (b.cls == NAN)
        return quiet(b);
    if (a.cls == INF)
        return b.cls == INF ? default_nan() : infinity(sign);
    if (b.cls == INF)
        return zero(sign);
    if (b.cls == ZERO)
        return a.cls == ZERO ? default_nan() : infinity(sign);
    if (a.cls == ZERO)
        return zero(sign);

    /* q = a.m * 2^125 / b.m, one bit at a time; a remainder is sticky. */
    r = a.m;
    for (i = 0; i < 126; i++) {
        q = u128_shl(q, 1);
        if (u128_cmp(r, b.m) >= 0) {
            r = u128_sub(r, b.m);
            q.lo |= 1;
        }
        r = u128_shl(r, 1);
    }
    if (r.hi | r.lo)
        q.lo |= 1;
    return round_pack(sign, a.exp - b.exp + BIAS, q);
}

/* -1, 0 or 1 as x is less than, equal to or greater than y; `unordered` when either
 * is a NaN. */
static int compare(long double x, long double y, int unordered)
{
    Bits a, b;
    u64 ah, bh;
    int c;
    a.f = x;
    b.f = y;
    ah  = a.w.hi & ~SIGN;
    bh  = b.w.hi & ~SIGN;
    if (ah > EXPS || (ah == EXPS && a.w.lo) || bh > EXPS || (bh == EXPS && b.w.lo))
        return unordered;
    if (!(ah | a.w.lo | bh | b.w.lo))
        return 0; /* +0 == -0 */
    if ((a.w.hi ^ b.w.hi) & SIGN)
        return a.w.hi & SIGN ? -1 : 1;
    if (ah != bh)
        c = ah < bh ? -1 : 1;
    else if (a.w.lo != b.w.lo)
        c = a.w.lo < b.w.lo ? -1 : 1;
    else
        c = 0;
    return a.w.hi & SIGN ? -c : c;
}

int __eqtf2(long double x, long double y)
{
    return compare(x, y, 1);
}

int __netf2(long double x, long double y)
{
    return compare(x, y, 1);
}

int __lttf2(long double x, long double y)
{
    return compare(x, y, 1);
}

int __letf2(long double x, long double y)
{
    return compare(x, y, 1);
}

int __cmptf2(long double x, long double y)
{
    return compare(x, y, 1);
}

int __gttf2(long double x, long double y)
{
    return compare(x, y, -1);
}

int __getf2(long double x, long double y)
{
    return compare(x, y, -1);
}

int __unordtf2(long double x, long double y)
{
    return compare(x, y, 2) == 2;
}

/* The integer part of |f|, when below 2^bits; else saturated to the high bit set. */
static u64 int_part(Num a, int bits, int *big)
{
    int e = a.exp - BIAS;
    *big  = 0;
    if (a.cls == ZERO || (a.cls == FINITE && e < 0))
        return 0;
    if (a.cls != FINITE || e >= bits) {
        *big = 1;
        return 0;
    }
    return u128_shr(a.m, 112 - e).lo;
}

long __fixtfdi(long double f)
{
    Num a = unpack(f);
    int big;
    u64 v = int_part(a, 63, &big);
    if (big)
        return a.sign ? (long)SIGN : (long)~SIGN;
    return a.sign ? -(long)v : (long)v;
}

int __fixtfsi(long double f)
{
    Num a = unpack(f);
    int big;
    u64 v = int_part(a, 31, &big);
    if (big)
        return a.sign ? -2147483647 - 1 : 2147483647;
    return a.sign ? -(int)v : (int)v;
}

unsigned long __fixunstfdi(long double f)
{
    Num a = unpack(f);
    int big;
    u64 v = int_part(a, 64, &big);
    if (a.sign)
        return 0;
    return big ? ~0UL : v;
}

unsigned __fixunstfsi(long double f)
{
    Num a = unpack(f);
    int big;
    u64 v = int_part(a, 32, &big);
    if (a.sign)
        return 0;
    return big ? ~0U : (unsigned)v;
}

static long double from_u64(int sign, u64 v)
{
    int lead;
    U128 m;
    if (!v)
        return zero(sign);
    lead = top64(v);
    m.hi = 0;
    m.lo = v;
    m    = u128_shl(m, 112 - lead);
    return pack((u64)sign << 63 | (u64)(BIAS + lead) << 48 | (m.hi & FRAC), m.lo);
}

long double __floatditf(long v)
{
    return v < 0 ? from_u64(1, -(u64)v) : from_u64(0, (u64)v);
}

long double __floatunditf(unsigned long v)
{
    return from_u64(0, v);
}

long double __floatsitf(int v)
{
    return __floatditf(v);
}

long double __floatunsitf(unsigned v)
{
    return from_u64(0, v);
}

/* From a binary format of `fbits` fraction and `ebits` exponent bits: exact. */
static long double widen(u64 bits, int fbits, int ebits)
{
    int emax = (1 << ebits) - 1, bias = emax >> 1;
    u64 s    = bits >> (fbits + ebits) << 63;
    int e    = (int)(bits >> fbits) & emax;
    U128 m;
    m.hi = 0;
    m.lo = bits & ((1UL << fbits) - 1);
    if (e == emax) {
        m = u128_shl(m, 112 - fbits);
        return pack(s | EXPS | m.hi | (m.hi | m.lo ? QUIET : 0), m.lo);
    }
    if (e == 0) {
        int shift;
        if (!m.lo)
            return pack(s, 0);
        shift = fbits - top64(m.lo);
        m.lo <<= shift;
        e = 1 - shift;
    }
    m = u128_shl(m, 112 - fbits);
    return pack(s | (u64)(e - bias + BIAS) << 48 | (m.hi & FRAC), m.lo);
}

/* To a binary format of `fbits` fraction and `ebits` exponent bits, rounded. */
static u64 narrow(long double f, int fbits, int ebits)
{
    Num a    = unpack(f);
    int emax = (1 << ebits) - 1, bias = emax >> 1;
    u64 s    = (u64)a.sign << (fbits + ebits);
    u64 mask = (1UL << fbits) - 1;
    int e, shift, c;
    U128 q, rem, half;
    u64 r;
    if (a.cls == NAN)
        return s | (u64)emax << fbits | 1UL << (fbits - 1) | (u128_shr(a.m, 112 - fbits).lo & mask);
    if (a.cls == INF)
        return s | (u64)emax << fbits;
    if (a.cls == ZERO)
        return s;
    e     = a.exp - BIAS + bias;
    shift = 112 - fbits;
    if (e <= 0) {
        /* Subnormal: scaled as exponent 1, without the leading bit. */
        shift += 1 - e;
        e = 1;
    }
    if (shift > 113)
        return s; /* below half the least subnormal */
    q       = u128_shr(a.m, shift);
    rem     = u128_sub(a.m, u128_shl(q, shift));
    half.hi = 0;
    half.lo = 1;
    half    = u128_shl(half, shift - 1);
    r       = q.lo;
    c       = u128_cmp(rem, half);
    if (c > 0 || (c == 0 && (r & 1)))
        r++;
    if (r >> (fbits + 1)) {
        r >>= 1;
        e++;
    }
    if (!(r >> fbits))
        e = 0;
    if (e >= emax)
        return s | (u64)emax << fbits;
    return s | (u64)e << fbits | (r & mask);
}

long double __extenddftf2(double d)
{
    union {
        double d;
        u64 u;
    } x;
    x.d = d;
    return widen(x.u, 52, 11);
}

long double __extendsftf2(float f)
{
    union {
        float f;
        unsigned u;
    } x;
    x.f = f;
    return widen(x.u, 23, 8);
}

double __trunctfdf2(long double f)
{
    union {
        double d;
        u64 u;
    } x;
    x.u = narrow(f, 52, 11);
    return x.d;
}

float __trunctfsf2(long double f)
{
    union {
        float f;
        unsigned u;
    } x;
    x.u = (unsigned)narrow(f, 23, 8);
    return x.f;
}
