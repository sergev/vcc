//
// AArch64 integer operations and width conversions.  The optimizer is off, so each
// operation reaches the code generator; a run test checks several results at once,
// one bit of the exit status each.
//
#include "aarch64_test.h"

// Each test compiles one translation unit: the fixture's symbol table lives per test.
#define EXPECT_SELECTS(name, expected, src)                        \
    TEST_F(Aarch64Test, name)                                      \
    {                                                              \
        DisableOptimization();                                     \
        NaiveSelection();                                          \
        std::string code = Code(CompileToAarch64(src));            \
        EXPECT_NE(std::string::npos, code.find(expected)) << code; \
    }

EXPECT_SELECTS(AddInt, R"(ldr w9, [x29, #-4]
ldr w10, [x29, #-8]
add w9, w9, w10
)",
               "int f(void) { int a = 1; int b = 2; return a + b; }")
EXPECT_SELECTS(MultiplyLong, "mul x9, x9, x10\n",
               "long f(void) { long a = 3; long b = 4; return a * b; }")
EXPECT_SELECTS(Remainder, R"(sdiv w11, w9, w10
msub w9, w11, w10, w9
)",
               "int f(void) { int a = 7; int b = 2; return a % b; }")
EXPECT_SELECTS(UnsignedDivide, "udiv w9, w9, w10\n",
               "unsigned f(void) { unsigned a = 7; unsigned b = 2; return a / b; }")
EXPECT_SELECTS(SignedCompare, R"(cmp w9, w10
cset w9, lt
)",
               "int f(void) { int a = 7; int b = 2; return a < b; }")
EXPECT_SELECTS(UnsignedCompare, R"(cmp x9, x10
cset w9, hs
)",
               "int f(void) { unsigned long a = 7; unsigned long b = 2; return a >= b; }")
EXPECT_SELECTS(ArithmeticShift, "asr w9, w9, w10\n",
               "int f(void) { int a = -8; int b = 1; return a >> b; }")
EXPECT_SELECTS(LogicalShift, "lsr w9, w9, w10\n",
               "unsigned f(void) { unsigned a = 8; int b = 1; return a >> b; }")
EXPECT_SELECTS(Not, R"(cmp w9, #0
cset w9, eq
)",
               "int f(void) { int a = 3; return !a; }")
EXPECT_SELECTS(Complement, "mvn x9, x9\n", "long f(void) { long a = 3; return ~a; }")
EXPECT_SELECTS(SignExtendIntToLong, R"(ldr w9, [x29, #-4]
sxtw x9, w9
)",
               "long f(void) { int a = -3; return a; }")
EXPECT_SELECTS(SignExtendCharToLong, R"(ldrsb w9, [x29, #-1]
sxtb x9, w9
)",
               "long f(void) { signed char a = -3; return a; }")
EXPECT_SELECTS(ZeroExtendUnsignedToLong, R"(ldr w9, [x29, #-4]
str x9,)",
               "unsigned long f(void) { unsigned a = 3; return a; }")
EXPECT_SELECTS(TruncateLongToChar, R"(ldr x9, [x29, #-8]
strb w9,)",
               "char f(void) { long a = 300; char c = a; return c; }")

TEST_F(Aarch64Test, RunDivisionAndRemainder)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
int main(void) {
    int a = -7; int b = 2; unsigned u = -7; unsigned v = 2;
    long la = -7; long lb = 2; unsigned long ul = -7; unsigned long ub = 2;
    return (a / b == -3) + 2 * (a % b == -1) + 4 * (u / v == 2147483644u)
         + 8 * (u % v == 1) + 16 * (la / lb == -3) + 32 * (la % lb == -1)
         + 64 * (ul / ub == 9223372036854775804ul) + 128 * (ul % ub == 1);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Aarch64Test, RunShifts)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
int main(void) {
    int a = -16; int two = 2; unsigned u = 0x80000000u; int n = 31;
    long l = 1; long forty = 40; unsigned long ul = 0x8000000000000000ul;
    long m = -1;
    return (a >> two == -4) + 2 * (u >> n == 1) + 4 * (l << forty == 1099511627776l)
         + 8 * (ul >> 63 == 1) + 16 * (m >> 63 == -1) + 32 * (a << two == -64);
})"));
    EXPECT_EQ(63, exit_status);
}

TEST_F(Aarch64Test, RunComparisons)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
int main(void) {
    int a = -1; int b = 1; unsigned u = -1; unsigned v = 1;
    long la = -1; unsigned long ua = -1; unsigned long ub = 1;
    return (a < b) + 2 * (u > v) + 4 * (la <= a) + 8 * (ua >= ub)
         + 16 * (a != b) + 32 * !(u < v) + 64 * (la == -1) + 128 * (b > a);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Aarch64Test, RunConversions)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
int main(void) {
    signed char c = 200; unsigned char u = 200; short s = 70000;
    long l = -1; int i = -5; unsigned short us = 65535; unsigned w = -1;
    return (c == -56) + 2 * (u == 200) + 4 * (s == 4464)
         + 8 * ((unsigned)l == 4294967295u) + 16 * ((long)i == -5l)
         + 32 * ((long)(unsigned)l == 4294967295l) + 64 * (us + 1 == 65536)
         + 128 * ((long)w == 4294967295l);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Aarch64Test, RunUnary)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
int main(void) {
    int a = 5; long l = 0; unsigned u = 0;
    return (-a == -5) + 2 * (~a == -6) + 4 * (!a == 0)
         + 8 * (!l == 1) + 16 * (-u == 0) + 32 * (~u == 4294967295u);
})"));
    EXPECT_EQ(63, exit_status);
}
