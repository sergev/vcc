//
// ARM32 long long: inline word-pair sequences joined by the carry flag, and the RTABI
// helpers for the rest.
//
#include <cinttypes>
#include <cstring>

#include "arm32_test.h"

#define EXPECT_SELECTS(name, expected, src)                        \
    TEST_F(Arm32Test, name)                                        \
    {                                                              \
        DisableOptimization();                                     \
        NaiveSelection();                                          \
        std::string code = Code(CompileToArm32(src));              \
        EXPECT_NE(std::string::npos, code.find(expected)) << code; \
    }

// Each word is stored as it is computed; the carry survives the loads and the store.
EXPECT_SELECTS(LongLongAdd, R"(ldr r12, [r11, #-8]
ldr lr, [r11, #-24]
adds r12, r12, lr
str r12, [r11, #-40]
ldr r12, [r11, #-4]
ldr lr, [r11, #-20]
adc r12, r12, lr
str r12, [r11, #-36]
)",
               "long long f(void) { long long a = 1; long long b = 2; return a + b; }")
EXPECT_SELECTS(LongLongSubtract, R"(subs r12, r12, lr
str r12, [r11, #-40]
)",
               "long long f(void) { long long a = 1; long long b = 2; return a - b; }")
EXPECT_SELECTS(LongLongNegate, R"(rsbs r12, r12, #0
rsc lr, lr, #0
)",
               "long long f(void) { long long a = 1; return -a; }")
// A compare subtracts with the borrow: cmp of the low words, sbcs of the high ones.
EXPECT_SELECTS(LongLongLess, R"(cmp r12, lr
ldr r12, [r11, #-4]
ldr lr, [r11, #-20]
sbcs r12, r12, lr
mov r12, #0
movlt r12, #1
)",
               "int f(void) { long long a = 1; long long b = 2; return a < b; }")
// Greater compares the other way round.
EXPECT_SELECTS(UnsignedLongLongGreater, R"(ldr r12, [r11, #-24]
ldr lr, [r11, #-8]
cmp r12, lr
)",
               "int f(void) { unsigned long long a = 1; unsigned long long b = 2; return a > b; }")
EXPECT_SELECTS(LongLongEqual, R"(cmp r12, lr
ldr r12, [r11, #-8]
ldr lr, [r11, #-24]
cmpeq r12, lr
mov r12, #0
moveq r12, #1
)",
               "int f(void) { long long a = 1; long long b = 2; return a == b; }")
EXPECT_SELECTS(LongLongShiftLeftConstant, R"(lsl lr, lr, #4
orr lr, lr, r12, lsr #28
lsl r12, r12, #4
)",
               "long long f(void) { long long a = 1; return a << 4; }")
EXPECT_SELECTS(LongLongShiftRightConstant40, R"(asr r12, lr, #8
asr lr, lr, #31
)",
               "long long f(void) { long long a = 1; return a >> 40; }")
EXPECT_SELECTS(LongLongShiftVariable, "bl __aeabi_lasr\n",
               "long long f(void) { long long a = 1; int n = 3; return a >> n; }")
EXPECT_SELECTS(LongLongMultiply, R"(ldr r0, [r11, #-8]
ldr r1, [r11, #-4]
ldr r2, [r11, #-24]
ldr r3, [r11, #-20]
bl __aeabi_lmul
str r0,)",
               "long long f(void) { long long a = 3; long long b = 4; return a * b; }")
// The quotient comes back in r0:r1, the remainder in r2:r3.
EXPECT_SELECTS(LongLongDivide, R"(bl __aeabi_ldivmod
str r0,)",
               "long long f(void) { long long a = 7; long long b = 2; return a / b; }")
EXPECT_SELECTS(UnsignedLongLongRemainder, R"(bl __aeabi_uldivmod
str r2,)",
               "unsigned long long f(void) { unsigned long long a = 7; unsigned long long b = 2; "
               "return a % b; }")
// The conversions with FP take and return a double in a core register pair.
EXPECT_SELECTS(LongLongToDouble, R"(bl __aeabi_l2d
str r0, [r11, #-)",
               "double f(void) { long long a = 7; return a; }")
EXPECT_SELECTS(FloatToUnsignedLongLong, R"(ldr r0, [r11, #-4]
bl __aeabi_f2ulz
)",
               "unsigned long long f(void) { float a = 7; return a; }")
EXPECT_SELECTS(SignExtendToLongLong, R"(ldr r12, [r11, #-4]
mov lr, r12, asr #31
)",
               "long long f(void) { int a = -3; return a; }")

TEST_F(Arm32Test, RunLongLongArithmetic)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    long long a = 0xffffffffLL; long long b = 1; unsigned long long m = 0x8000000000000000ULL;
    long long c = a + b; long long d = c - b; long long n = -c;
    long long p = 0x123456789LL; long long q = 0x100000001LL;
    return (c == 0x100000000LL) + 2 * (d == a) + 4 * (n == -4294967296LL)
         + 8 * ((p & q) == 0x100000001LL) + 16 * ((p ^ q) == 0x23456788LL)
         + 32 * (~b == -2) + 64 * (p * q == 0x2345678A23456789LL) + 128 * !(m == 0);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Arm32Test, RunLongLongShiftsAndCompares)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    long long p = 0x123456789LL; unsigned long long m = 0x8000000000000000ULL;
    long long s = (long long)m; long long a = 0xffffffffLL; int n = 40; long long k = 63;
    return (p << 4 == 0x1234567890LL) + 2 * (m >> 63 == 1) + 4 * (s >> 40 == -8388608)
         + 8 * (a << 32 == (long long)0xffffffff00000000ULL) + 16 * (s >> n == -8388608)
         + 32 * (m >> k == 1) + 64 * (s < 1 && m > 1 && s <= s && !(s > 1))
         + 128 * (p >= p && p != a && -1LL < 0 && 0xffffffffULL < 0x100000000ULL);
})"));
    EXPECT_EQ(255, exit_status);
}

// The inline sequences and the RTABI helpers (libc/ilp32/int64.c beneath division and
// the conversions), against the same operations done by the host, as on rv32.
// Output without printf: a 64-bit value in hex, or a decimal int.
static const char *const kPrelude = R"(
#include <stdio.h>
typedef unsigned long long u64;
typedef long long i64;
static void hex(u64 x)
{
    for (int i = 60; i >= 0; i -= 4)
        putchar("0123456789abcdef"[(x >> i) & 15]);
    putchar(' ');
}
static void nl(void) { putchar('\n'); }
)";

static const uint64_t kValues[] = {
    0,
    1,
    UINT64_MAX, // -1
    2,
    (uint64_t)-2,
    7,
    0x7fffffff,
    0x80000000,
    0xffffffff,
    0x100000000,
    0x123456789abcdef0,
    0x7fffffffffffffff,
    0x8000000000000000,
    (uint64_t)-0x123456789,
    1000000007,
    0x8000000000000001,
    0x20000000000001,
    0x1000001000000001,
};
static const int kNumValues = sizeof(kValues) / sizeof(kValues[0]);

static std::string Hex(uint64_t x)
{
    char buf[20];
    snprintf(buf, sizeof(buf), "%016" PRIx64 " ", x);
    return buf;
}

// The values as a C initializer.
static std::string ValuesInit()
{
    std::string s = "static u64 v[] = {";
    for (uint64_t x : kValues)
        s += "0x" + Hex(x).substr(0, 16) + "ULL, ";
    return s + "};\n#define N " + std::to_string(kNumValues) + "\n";
}

static uint64_t DoubleBits(double d)
{
    uint64_t b;
    memcpy(&b, &d, 8);
    return b;
}

static uint64_t FloatBits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

TEST_F(Arm32Test, LongLongArithmetic)
{
    SKIP_IF_NO_ARM32_TOOLS();
    std::string src = std::string(kPrelude) + ValuesInit() + R"(
int main(void)
{
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            u64 a = v[i], b = v[j];
            i64 sa = (i64)a, sb = (i64)b;
            hex(a + b);
            hex(a - b);
            hex(a * b);
            hex(a & b);
            hex(a | b);
            hex(a ^ b);
            if (b) {
                hex(a / b);
                hex(a % b);
                if (!(sb == -1 && sa == (i64)0x8000000000000000ULL)) {
                    hex(sa / sb);
                    hex(sa % sb);
                }
            }
            nl();
        }
        hex(-v[i]);
        hex(~v[i]);
        hex(!v[i]);
        nl();
    }
    return 0;
}
)";
    std::string expect;
    for (uint64_t a : kValues) {
        for (uint64_t b : kValues) {
            int64_t sa = (int64_t)a, sb = (int64_t)b;
            expect += Hex(a + b) + Hex(a - b) + Hex(a * b) + Hex(a & b) + Hex(a | b) + Hex(a ^ b);
            if (b) {
                expect += Hex(a / b) + Hex(a % b);
                if (!(sb == -1 && sa == INT64_MIN))
                    expect += Hex((uint64_t)(sa / sb)) + Hex((uint64_t)(sa % sb));
            }
            expect += "\n";
        }
        expect += Hex(0 - a) + Hex(~a) + Hex(!a) + "\n";
    }
    EXPECT_EQ(expect, CompileAndRunArm32(src));
}

TEST_F(Arm32Test, LongLongComparisons)
{
    SKIP_IF_NO_ARM32_TOOLS();
    std::string src = std::string(kPrelude) + ValuesInit() + R"(
int main(void)
{
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            u64 a = v[i], b = v[j];
            i64 sa = (i64)a, sb = (i64)b;
            int bits = (a == b) | (a != b) << 1 | (a < b) << 2 | (a <= b) << 3 | (a > b) << 4 |
                       (a >= b) << 5 | (sa < sb) << 6 | (sa <= sb) << 7 | (sa > sb) << 8 |
                       (sa >= sb) << 9;
            if (sa < sb)
                bits |= 1 << 10;
            if (a > b)
                bits |= 1 << 11;
            hex(bits);
        }
        nl();
    }
    return 0;
}
)";
    std::string expect;
    for (uint64_t a : kValues) {
        for (uint64_t b : kValues) {
            int64_t sa = (int64_t)a, sb = (int64_t)b;
            int bits = (a == b) | (a != b) << 1 | (a < b) << 2 | (a <= b) << 3 | (a > b) << 4 |
                       (a >= b) << 5 | (sa < sb) << 6 | (sa <= sb) << 7 | (sa > sb) << 8 |
                       (sa >= sb) << 9 | (sa < sb) << 10 | (a > b) << 11;
            expect += Hex(bits);
        }
        expect += "\n";
    }
    EXPECT_EQ(expect, CompileAndRunArm32(src));
}

static const int kCounts[] = { 0, 1, 5, 31, 32, 33, 40, 63 };

TEST_F(Arm32Test, LongLongShifts)
{
    SKIP_IF_NO_ARM32_TOOLS();
    // Variable counts, of int and of long long type, and each count as a constant.
    std::string src = std::string(kPrelude) + ValuesInit() + "static int c[] = {";
    for (int n : kCounts)
        src += std::to_string(n) + ", ";
    src += "};\nstatic void constant(u64 a)\n{\n    i64 s = (i64)a;\n";
    for (int n : kCounts) {
        std::string k = std::to_string(n);
        src += "    hex(a << " + k + ");\n    hex(a >> " + k + ");\n    hex(s >> " + k + ");\n";
    }
    src += R"(}
int main(void)
{
    for (int i = 0; i < N; i++) {
        u64 a = v[i];
        i64 s = (i64)a;
        for (int j = 0; j < 8; j++) {
            int n = c[j];
            u64 m = n;
            hex(a << n);
            hex(a >> n);
            hex(s >> n);
            hex(a << m);
            hex(s >> m);
            hex(1 << (m & 15));
        }
        nl();
        constant(a);
        nl();
    }
    return 0;
}
)";
    std::string expect;
    for (uint64_t a : kValues) {
        int64_t s = (int64_t)a;
        std::string constant;
        for (int n : kCounts) {
            expect += Hex(a << n) + Hex(a >> n) + Hex((uint64_t)(s >> n)) + Hex(a << n) +
                      Hex((uint64_t)(s >> n)) + Hex(1u << (n & 15));
            constant += Hex(a << n) + Hex(a >> n) + Hex((uint64_t)(s >> n));
        }
        expect += "\n" + constant + "\n";
    }
    EXPECT_EQ(expect, CompileAndRunArm32(src));
}

static const double kDoubles[] = {
    0.0,  0.5,  -0.5, 1.5,  -1.5, 4294967296.5, -4294967297.75, 123456789012.9,
    1e18, -1e18, 9.2e18, -9.2e18,
};

TEST_F(Arm32Test, LongLongConversions)
{
    SKIP_IF_NO_ARM32_TOOLS();
    std::string src = std::string(kPrelude) + ValuesInit() + "static double d[] = {";
    for (double x : kDoubles) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%.17g, ", x);
        src += buf;
    }
    src += R"(};
static u64 dbits(double x) { return *(u64 *)&x; }
static u64 fbits(float x) { return *(unsigned *)&x; }
int main(void)
{
    for (int i = 0; i < N; i++) {
        u64 a = v[i];
        i64 s = (i64)a;
        hex(dbits((double)a));
        hex(dbits((double)s));
        hex(fbits((float)a));
        hex(fbits((float)s));
        int x = (int)a;
        unsigned u = (unsigned)a;
        short h = (short)s;
        unsigned char c = (unsigned char)a;
        hex((i64)x);
        hex((u64)u);
        hex((i64)h);
        hex((u64)c);
        hex((i64)(signed char)c);
        nl();
    }
    for (int i = 0; i < 12; i++) {
        hex((u64)(i64)d[i]);
        hex((u64)(i64)(float)d[i]);
        if (d[i] >= 0) {
            hex((u64)d[i]);
            hex((u64)(d[i] * 2));
            hex((u64)(float)d[i]);
        }
        nl();
    }
    return 0;
}
)";
    std::string expect;
    for (uint64_t a : kValues) {
        int64_t s = (int64_t)a;
        expect += Hex(DoubleBits((double)a)) + Hex(DoubleBits((double)s)) + Hex(FloatBits((float)a)) +
                  Hex(FloatBits((float)s));
        expect += Hex((uint64_t)(int64_t)(int32_t)a) + Hex((uint32_t)a) +
                  Hex((uint64_t)(int64_t)(int16_t)a) + Hex((uint8_t)a) +
                  Hex((uint64_t)(int64_t)(int8_t)a) + "\n";
    }
    for (double x : kDoubles) {
        expect += Hex((uint64_t)(int64_t)x) + Hex((uint64_t)(int64_t)(float)x);
        if (x >= 0)
            expect += Hex((uint64_t)x) + Hex((uint64_t)(x * 2)) + Hex((uint64_t)(float)x);
        expect += "\n";
    }
    EXPECT_EQ(expect, CompileAndRunArm32(src));
}
