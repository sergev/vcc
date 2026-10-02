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
        std::string code = Code(CompileToAarch64(src));            \
        EXPECT_NE(std::string::npos, code.find(expected)) << code; \
    }

EXPECT_SELECTS(AddInt, "ldr w9, [x29, #-4]\nldr w10, [x29, #-8]\nadd w9, w9, w10\n",
               "int f(void) { int a = 1; int b = 2; return a + b; }")
EXPECT_SELECTS(MultiplyLong, "mul x9, x9, x10\n",
               "long f(void) { long a = 3; long b = 4; return a * b; }")
EXPECT_SELECTS(Remainder, "sdiv w11, w9, w10\nmsub w9, w11, w10, w9\n",
               "int f(void) { int a = 7; int b = 2; return a % b; }")
EXPECT_SELECTS(UnsignedDivide, "udiv w9, w9, w10\n",
               "unsigned f(void) { unsigned a = 7; unsigned b = 2; return a / b; }")
EXPECT_SELECTS(SignedCompare, "cmp w9, w10\ncset w9, lt\n",
               "int f(void) { int a = 7; int b = 2; return a < b; }")
EXPECT_SELECTS(UnsignedCompare, "cmp x9, x10\ncset w9, hs\n",
               "int f(void) { unsigned long a = 7; unsigned long b = 2; return a >= b; }")
EXPECT_SELECTS(ArithmeticShift, "asr w9, w9, w10\n",
               "int f(void) { int a = -8; int b = 1; return a >> b; }")
EXPECT_SELECTS(LogicalShift, "lsr w9, w9, w10\n",
               "unsigned f(void) { unsigned a = 8; int b = 1; return a >> b; }")
EXPECT_SELECTS(Not, "cmp w9, #0\ncset w9, eq\n", "int f(void) { int a = 3; return !a; }")
EXPECT_SELECTS(Complement, "mvn x9, x9\n", "long f(void) { long a = 3; return ~a; }")
EXPECT_SELECTS(SignExtendIntToLong, "ldr w9, [x29, #-4]\nsxtw x9, w9\n",
               "long f(void) { int a = -3; return a; }")
EXPECT_SELECTS(SignExtendCharToLong, "ldrsb w9, [x29, #-1]\nsxtb x9, w9\n",
               "long f(void) { signed char a = -3; return a; }")
EXPECT_SELECTS(ZeroExtendUnsignedToLong, "ldr w9, [x29, #-4]\nstr x9,",
               "unsigned long f(void) { unsigned a = 3; return a; }")
EXPECT_SELECTS(TruncateLongToChar, "ldr x9, [x29, #-8]\nstrb w9,",
               "char f(void) { long a = 300; char c = a; return c; }")

TEST_F(Aarch64Test, RunDivisionAndRemainder)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("",
              CompileAndRunAarch64(
                  "int main(void) {\n"
                  "    int a = -7; int b = 2; unsigned u = -7; unsigned v = 2;\n"
                  "    long la = -7; long lb = 2; unsigned long ul = -7; unsigned long ub = 2;\n"
                  "    return (a / b == -3) + 2 * (a % b == -1) + 4 * (u / v == 2147483644u)\n"
                  "         + 8 * (u % v == 1) + 16 * (la / lb == -3) + 32 * (la % lb == -1)\n"
                  "         + 64 * (ul / ub == 9223372036854775804ul) + 128 * (ul % ub == 1);\n"
                  "}\n"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Aarch64Test, RunShifts)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ(
        "",
        CompileAndRunAarch64(
            "int main(void) {\n"
            "    int a = -16; int two = 2; unsigned u = 0x80000000u; int n = 31;\n"
            "    long l = 1; long forty = 40; unsigned long ul = 0x8000000000000000ul;\n"
            "    long m = -1;\n"
            "    return (a >> two == -4) + 2 * (u >> n == 1) + 4 * (l << forty == 1099511627776l)\n"
            "         + 8 * (ul >> 63 == 1) + 16 * (m >> 63 == -1) + 32 * (a << two == -64);\n"
            "}\n"));
    EXPECT_EQ(63, exit_status);
}

TEST_F(Aarch64Test, RunComparisons)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("",
              CompileAndRunAarch64(
                  "int main(void) {\n"
                  "    int a = -1; int b = 1; unsigned u = -1; unsigned v = 1;\n"
                  "    long la = -1; unsigned long ua = -1; unsigned long ub = 1;\n"
                  "    return (a < b) + 2 * (u > v) + 4 * (la <= a) + 8 * (ua >= ub)\n"
                  "         + 16 * (a != b) + 32 * !(u < v) + 64 * (la == -1) + 128 * (b > a);\n"
                  "}\n"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Aarch64Test, RunConversions)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("",
              CompileAndRunAarch64(
                  "int main(void) {\n"
                  "    signed char c = 200; unsigned char u = 200; short s = 70000;\n"
                  "    long l = -1; int i = -5; unsigned short us = 65535; unsigned w = -1;\n"
                  "    return (c == -56) + 2 * (u == 200) + 4 * (s == 4464)\n"
                  "         + 8 * ((unsigned)l == 4294967295u) + 16 * ((long)i == -5l)\n"
                  "         + 32 * ((long)(unsigned)l == 4294967295l) + 64 * (us + 1 == 65536)\n"
                  "         + 128 * ((long)w == 4294967295l);\n"
                  "}\n"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Aarch64Test, RunUnary)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunAarch64(
                      "int main(void) {\n"
                      "    int a = 5; long l = 0; unsigned u = 0;\n"
                      "    return (-a == -5) + 2 * (~a == -6) + 4 * (!a == 0)\n"
                      "         + 8 * (!l == 1) + 16 * (-u == 0) + 32 * (~u == 4294967295u);\n"
                      "}\n"));
    EXPECT_EQ(63, exit_status);
}
