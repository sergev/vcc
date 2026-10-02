//
// Binary128 constants (float128.c): exact parsing, formatting, folding helpers.
// The expected bits were computed by exact rational arithmetic (see
// backend/riscv/test/gen_float128_cases.py; the arithmetic itself is tested there).
//
#include <gtest/gtest.h>

#include <cstring>

extern "C" {
#include "float128.h"
#include "xalloc.h"
}

class Float128Test : public ::testing::Test {
protected:
    void TearDown() override
    {
        xreport_lost_memory();
        EXPECT_EQ(xtotal_allocated_size(), 0);
        xfree_all();
    }
};

static const struct {
    const char *text;
    uint64_t hi, lo;
} parse_cases[] = {
    { "0.1", 0x3ffb999999999999ULL, 0x999999999999999aULL },
    { "1.5", 0x3fff800000000000ULL, 0x0000000000000000ULL },
    { "3.14159265358979323846264338327950288", 0x4000921fb54442d1ULL, 0x8469898cc51701b8ULL },
    { "1e4932", 0x7ffeae596552b8fdULL, 0xed99d037e3d04b75ULL },
    { "1.18973149535723176508575932662800702e+4932", 0x7ffeffffffffffffULL, 0xffffffffffffffffULL },
    { "1.2e4932", 0x7fff000000000000ULL, 0x0000000000000000ULL },
    { "3.36210314311209350626267781732175260e-4932", 0x0001000000000000ULL, 0x0000000000000000ULL },
    { "6.47517511943802511092443895822764655e-4966", 0x0000000000000000ULL, 0x0000000000000001ULL },
    { "3.2e-4966", 0x0000000000000000ULL, 0x0000000000000000ULL },
    { "1e-5000", 0x0000000000000000ULL, 0x0000000000000000ULL },
    { "123456789012345678901234567890123456789", 0x407d7383a6958057ULL, 0xfb16ab7e8ca2b8e6ULL },
    { "0x1.8p+0", 0x3fff800000000000ULL, 0x0000000000000000ULL },
    { "0x1.fffffffffffffffffffffffffffffp+0", 0x4000000000000000ULL, 0x0000000000000000ULL },
    { "0x.8p1", 0x3fff000000000000ULL, 0x0000000000000000ULL },
    { "1.0000000000000000000000000000000000963", 0x3fff000000000000ULL, 0x0000000000000001ULL },
    { "2.5e-1L", 0x3ffd000000000000ULL, 0x0000000000000000ULL },
    { "0.000001", 0x3feb0c6f7a0b5ed8ULL, 0xd36b4c7f34938583ULL },
    { "1e300", 0x43e37e43c8800759ULL, 0xba59c08e14c7cd7bULL },
};

// Decimal and hexadecimal constants round to nearest even, over the whole range:
// subnormals, overflow to infinity, underflow to zero.
TEST_F(Float128Test, Parse)
{
    for (const auto &c : parse_cases) {
        const char *end;
        Float128 f = f128_from_string(c.text, &end);
        EXPECT_EQ(c.hi, f.hi) << c.text;
        EXPECT_EQ(c.lo, f.lo) << c.text;
        EXPECT_TRUE(*end == 0 || *end == 'L') << c.text;
    }
}

TEST_F(Float128Test, Format)
{
    char buf[F128_BUFSIZE];
    EXPECT_STREQ("0x1.8p+0", f128_format(f128_from_string("1.5", nullptr), buf));
    EXPECT_STREQ("-0x1.999999999999999999999999999ap-4",
                 f128_format(f128_neg(f128_from_string("0.1", nullptr)), buf));
    EXPECT_STREQ("0x0.0000000000000000000000000001p-16382",
                 f128_format(Float128{ 1, 0 }, buf));
    EXPECT_STREQ("-0x0p+0", f128_format(f128_neg(f128_from_i64(0)), buf));
    EXPECT_STREQ("inf", f128_format(f128_from_string("1e5000", nullptr), buf));
    EXPECT_STREQ("nan", f128_format(f128_div(f128_from_i64(0), f128_from_i64(0)), buf));
}

// Conversions between host types and binary128.
TEST_F(Float128Test, Convert)
{
    Float128 third = f128_div(f128_from_i64(1), f128_from_i64(3));
    EXPECT_EQ(1.0 / 3, f128_to_double(third));
    EXPECT_EQ(1.0f / 3, f128_to_float(third));
    EXPECT_EQ(0, f128_cmp(f128_from_double(0.1), f128_from_string("0x1.999999999999ap-4", nullptr)));
    EXPECT_EQ(-7, f128_to_i64(f128_from_string("-7.9", nullptr), 64));
    EXPECT_EQ(INT32_MAX, f128_to_i64(f128_from_string("1e10", nullptr), 32));
    EXPECT_EQ(UINT64_MAX, f128_to_u64(f128_from_string("1e30", nullptr), 64));
    EXPECT_EQ(0u, f128_to_u64(f128_from_i64(-5), 64));
    Float128 big = f128_from_u64(UINT64_MAX);
    EXPECT_EQ(UINT64_MAX, f128_to_u64(big, 64));
    EXPECT_EQ(1, f128_cmp(third, f128_from_double(1.0 / 3))); // the double rounds down
    EXPECT_EQ(2, f128_cmp(f128_div(f128_from_i64(0), f128_from_i64(0)), third));
}
