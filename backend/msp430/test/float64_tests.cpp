//
// The binary64 soft-float runtime (libc/common/float64.c): compiled for the host and
// checked bit for bit against the host's own double over edge cases and random
// operands; then, compiled by genmsp430, run on mspsim over a table.
//
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "msp430_test.h"

extern "C" {
double f64_adddf3(double, double);
double f64_subdf3(double, double);
double f64_muldf3(double, double);
double f64_divdf3(double, double);
double f64_sqrt(double);
int f64_eqdf2(double, double);
int f64_nedf2(double, double);
int f64_ltdf2(double, double);
int f64_ledf2(double, double);
int f64_gtdf2(double, double);
int f64_gedf2(double, double);
int f64_unorddf2(double, double);
long f64_fixdfsi(double);
unsigned long f64_fixunsdfsi(double);
double f64_floatsidf(long);
double f64_floatunsidf(unsigned long);
double f64_extendsfdf2(float);
float f64_truncdfsf2(double);
}

namespace {

uint64_t Bits(double d)
{
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

double FromBits(uint64_t u)
{
    double d;
    memcpy(&d, &u, 8);
    return d;
}

uint32_t Bits32(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

// Equal bit for bit; any NaN matches any NaN (payloads and their signs vary by host).
::testing::AssertionResult Same(double got, double want)
{
    if (std::isnan(want) ? std::isnan(got) : Bits(got) == Bits(want))
        return ::testing::AssertionSuccess();
    char buf[96];
    snprintf(buf, sizeof buf, "got %016llx, want %016llx", (unsigned long long)Bits(got),
             (unsigned long long)Bits(want));
    return ::testing::AssertionFailure() << buf;
}

// Operands: zeros, subnormals and their edge, normals around 1, halfway cases, the
// extremes, infinities, NaN, each with both signs; then random bit patterns, and
// random values of nearby exponents (where add and subtract cancel).
std::vector<double> Operands()
{
    std::vector<double> v;
    const uint64_t specials[] = {
        0,
        1,                     // the smallest subnormal
        0x000fffffffffffffULL, // the largest subnormal
        0x0010000000000000ULL, // the smallest normal
        0x3ff0000000000000ULL, // 1
        0x3ff0000000000001ULL, // 1 + ulp
        0x3fefffffffffffffULL, // 1 - ulp/2
        0x3ff8000000000000ULL, // 1.5
        0x4000000000000000ULL, // 2
        0x3fb999999999999aULL, // 0.1
        0x400921fb54442d18ULL, // pi
        0x7fefffffffffffffULL, // the largest normal
        0x7ff0000000000000ULL, // infinity
        0x7ff8000000000000ULL, // NaN
        0x4340000000000000ULL, // 2^53
        0x41dfffffffc00000ULL, // 2^31 - 1
        0x41e0000000000000ULL, // 2^31
        0x41efffffffe00000ULL, // 2^32 - 1
    };
    for (uint64_t s : specials) {
        v.push_back(FromBits(s));
        v.push_back(FromBits(s | 0x8000000000000000ULL));
    }
    std::mt19937_64 rng(430);
    for (int i = 0; i < 300; i++)
        v.push_back(FromBits(rng()));
    std::uniform_real_distribution<double> near(-4.0, 4.0);
    for (int i = 0; i < 300; i++)
        v.push_back(near(rng));
    return v;
}

} // namespace

TEST(Float64Host, Arithmetic)
{
    std::vector<double> ops = Operands();
    for (double a : ops)
        for (double b : ops) {
            ASSERT_TRUE(Same(f64_adddf3(a, b), a + b)) << a << " + " << b;
            ASSERT_TRUE(Same(f64_subdf3(a, b), a - b)) << a << " - " << b;
            ASSERT_TRUE(Same(f64_muldf3(a, b), a * b)) << a << " * " << b;
            ASSERT_TRUE(Same(f64_divdf3(a, b), a / b)) << a << " / " << b;
        }
}

// Products and quotients whose results are subnormal or overflow: the rounding at the
// bottom and the top of the range.
TEST(Float64Host, ArithmeticAtTheEdges)
{
    std::mt19937_64 rng(1);
    std::uniform_real_distribution<double> mant(1.0, 2.0);
    for (int i = 0; i < 20000; i++) {
        double a = std::ldexp(mant(rng), -(int)(rng() % 600));
        double b = std::ldexp(mant(rng), -(int)(rng() % 600));
        double c = std::ldexp(mant(rng), (int)(rng() % 600));
        ASSERT_TRUE(Same(f64_muldf3(a, b), a * b)) << a << " * " << b;
        ASSERT_TRUE(Same(f64_divdf3(a, c), a / c)) << a << " / " << c;
        ASSERT_TRUE(Same(f64_muldf3(c, c), c * c)) << c << " * " << c;
        ASSERT_TRUE(Same(f64_divdf3(c, a), c / a)) << c << " / " << a;
    }
}

TEST(Float64Host, SquareRoot)
{
    for (double a : Operands())
        ASSERT_TRUE(Same(f64_sqrt(a), std::sqrt(a))) << a;
    std::mt19937_64 rng(2);
    for (int i = 0; i < 100000; i++) {
        double a = FromBits(rng() & 0x7fffffffffffffffULL);
        ASSERT_TRUE(Same(f64_sqrt(a), std::sqrt(a))) << a;
    }
}

// Each comparison's helper answers it by its relation to zero, false for NaN.
TEST(Float64Host, Comparisons)
{
    std::vector<double> ops = Operands();
    for (double a : ops)
        for (double b : ops) {
            ASSERT_EQ(f64_eqdf2(a, b) == 0, a == b) << a << " == " << b;
            ASSERT_EQ(f64_nedf2(a, b) != 0, a != b) << a << " != " << b;
            ASSERT_EQ(f64_ltdf2(a, b) < 0, a < b) << a << " < " << b;
            ASSERT_EQ(f64_ledf2(a, b) <= 0, a <= b) << a << " <= " << b;
            ASSERT_EQ(f64_gtdf2(a, b) > 0, a > b) << a << " > " << b;
            ASSERT_EQ(f64_gedf2(a, b) >= 0, a >= b) << a << " >= " << b;
            ASSERT_EQ(f64_unorddf2(a, b) != 0, std::isnan(a) || std::isnan(b));
        }
}

// The conversions with 32-bit integers (a long is 32 bits on MSP430, 64 here: the
// values stay in 32).
TEST(Float64Host, IntegerConversions)
{
    for (double a : Operands()) {
        if (std::isnan(a))
            continue;
        if (a > -2147483649.0 && a < 2147483648.0)
            ASSERT_EQ(f64_fixdfsi(a), (long)(int32_t)a) << a;
        if (a > -1.0 && a < 4294967296.0)
            ASSERT_EQ(f64_fixunsdfsi(a), (unsigned long)(uint32_t)a) << a;
    }
    EXPECT_EQ(f64_fixdfsi(1e300), 2147483647L);
    EXPECT_EQ(f64_fixdfsi(-1e300), -2147483648L);
    EXPECT_EQ(f64_fixunsdfsi(1e300), 0xffffffffUL);
    EXPECT_EQ(f64_fixunsdfsi(-5.0), 0UL);
    std::mt19937 rng(3);
    for (int i = 0; i < 100000; i++) {
        int32_t s  = (int32_t)rng();
        uint32_t u = rng();
        ASSERT_TRUE(Same(f64_floatsidf(s), (double)s)) << s;
        ASSERT_TRUE(Same(f64_floatunsidf(u), (double)u)) << u;
    }
    for (int32_t s : { 0, 1, -1, INT32_MAX, INT32_MIN })
        EXPECT_TRUE(Same(f64_floatsidf(s), (double)s)) << s;
}

// float ↔ double: widening is exact, narrowing rounds, subnormals and NaNs included.
TEST(Float64Host, FloatConversions)
{
    std::mt19937 rng(4);
    for (int i = 0; i < 200000; i++) {
        uint32_t u = rng();
        float f;
        memcpy(&f, &u, 4);
        ASSERT_TRUE(Same(f64_extendsfdf2(f), (double)f)) << std::hex << u;
    }
    for (double a : Operands()) {
        float want = (float)a, got = f64_truncdfsf2(a);
        if (std::isnan(want))
            ASSERT_TRUE(std::isnan(got)) << a;
        else
            ASSERT_EQ(Bits32(got), Bits32(want)) << a;
    }
    std::mt19937_64 rng64(5);
    std::uniform_real_distribution<double> mant(1.0, 2.0);
    for (int i = 0; i < 200000; i++) {
        double a = std::ldexp(mant(rng64), (int)(rng64() % 320) - 160); // around float's range
        if (rng64() & 1)
            a = -a;
        ASSERT_EQ(Bits32(f64_truncdfsf2(a)), Bits32((float)a)) << a;
    }
}

// Exact halfway cases, which random operands almost never hit: each must round to even.
TEST(Float64Host, TiesToEven)
{
    std::mt19937_64 rng(6);
    for (int i = 0; i < 100000; i++) {
        // a in [1, 2) and half its ulp: a + h and a - h are ties.
        double a = FromBits(0x3ff0000000000000ULL | (rng() & 0x000fffffffffffffULL));
        double h = std::ldexp(1.0, -53);
        ASSERT_TRUE(Same(f64_adddf3(a, h), a + h)) << a;
        ASSERT_TRUE(Same(f64_subdf3(a, h), a - h)) << a;
        // An odd significand times 1.5 needs one bit more: a tie.
        double o = FromBits(Bits(a) | 1);
        ASSERT_TRUE(Same(f64_muldf3(o, 1.5), o * 1.5)) << o;
        ASSERT_TRUE(Same(f64_muldf3(-o, 1.5), -o * 1.5)) << o;
        // The same, landing among the subnormals.
        double s = std::ldexp(o, -1030);
        ASSERT_TRUE(Same(f64_muldf3(s, 0.75), s * 0.75)) << s;
        // A double halfway between two floats.
        float f      = (float)a;
        double tie   = (double)f + std::ldexp(1.0, -24);
        ASSERT_EQ(Bits32(f64_truncdfsf2(tie)), Bits32((float)tie)) << tie;
    }
}
