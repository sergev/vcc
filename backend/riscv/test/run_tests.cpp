//
// RISC-V programs run on bare-metal qemu.
//
#include "riscv_test.h"

TEST_F(RiscvTest, RunReturn2)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv("int main(void) { return 2; }"));
    EXPECT_EQ(2, exit_status);
}

TEST_F(RiscvTest, RunBookStatus)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("-7\n", CompileAndRunBook("int main(void) { return -7; }"));
}

TEST_F(RiscvTest, RunLargeFrame)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = "int main(void) {\n";
    for (int i = 0; i < 600; i++)
        src += "    int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    src += "    int t = v599;\n    v0 = t;\n    return v0;\n}\n";
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunRiscv(src));
    EXPECT_EQ(599 & 255, exit_status);
}

// 32-bit values stay sign-extended; unsigned division and widening are exact.
TEST_F(RiscvTest, RunIntegerWidths)
{
    SKIP_IF_NO_RISCV_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
int main(void) {
    unsigned u = 4000000000u;
    unsigned long w = u;
    long l = -3;
    int i = -7;
    unsigned char c = 200;
    return (w != 4000000000ul) | (u / 3u != 1333333333u) << 1 |
           (l * 1000000000000l != -3000000000000l) << 2 | (i % 3 != -1) << 3 |
           ((unsigned)i >> 28 != 15u) << 4 | (c + 100 != 300) << 5 | !(u > 3000000000u) << 6;
})"));
    EXPECT_EQ(0, exit_status);
}

// Ten arguments: a0-a7, then two on the stack; narrow ones arrive extended.
TEST_F(RiscvTest, RunCalls)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
long sum(signed char a, int b, long c, unsigned d, short e, long f, int g, long h, int i, long j)
{
    return a + b + c + d + e + f + g + h + i * 1000 + j * 100000;
}
int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
int twice(int (*f)(int), int x) { return f(f(x)); }
int inc(int x) { return x + 1; }
int main(void) {
    if (sum(-1, 2, 3, 4u, -5, 6, 7, 8, 9, 10) != 1009024)
        return 1;
    if (fact(10) != 3628800)
        return 2;
    if (twice(inc, 40) != 42)
        return 3;
    return 0;
})"));
    EXPECT_EQ(0, exit_status);
}

// A constant truncated to a 16-bit short is not folded as a char.
TEST_F(RiscvTest, RunShortTruncation)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
int main(void) {
    short s = 70000;
    short t = -5;
    unsigned short u = -1;
    return (s != 4464) | (t != -5) << 1 | (u != 65535) << 2;
})"));
    EXPECT_EQ(0, exit_status);
}

// A case constant converts to the controlling type: with an int controlling
// expression, `case 8589934592l` is `case 0`.
TEST_F(RiscvTest, RunSwitchCaseConversion)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
int f(int i) {
    switch (i) {
    case 8589934592l:
        return 1;
    case 4294967295u:
        return 2;
    default:
        return 3;
    }
}
int g(unsigned long u) {
    switch (u) {
    case 4294967295u:
        return 1;
    default:
        return 2;
    }
}
int main(void) {
    return (f(0) != 1) | (f(-1) != 2) << 1 | (g(4294967295ul) != 1) << 2 | (g(-1) != 2) << 3;
})"));
    EXPECT_EQ(0, exit_status);
}

// E1 op= E2 is E1 = E1 op E2 in the common type (C11 6.5.16.2p3).
TEST_F(RiscvTest, RunCompoundAssignCommonType)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
int main(void) {
    int x = 1, b = 2147483647, i = -50;
    x += -0.5;
    b /= -34359738367l;
    i %= 4294967200u;
    return (x != 0) | (b != 0) << 1 | (i != 46) << 2;
})"));
    EXPECT_EQ(0, exit_status);
}

// Globals of each width, and a static local.
TEST_F(RiscvTest, RunGlobals)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
signed char sc = -3;
unsigned short us = 65535;
long big = -8589934592l;
int counter(void) { static int n; return ++n; }
int main(void) {
    counter();
    counter();
    return (sc != -3) | (us != 65535) << 1 | (big / 2 != -4294967296l) << 2 |
           (counter() != 3) << 3;
})"));
    EXPECT_EQ(0, exit_status);
}

// Float and double arithmetic, conversions both ways, FP arguments past fa7 and
// mixed with integers, NaN comparisons.
TEST_F(RiscvTest, RunFloatingPoint)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
double sum(double a, int i, double b, double c, double d, double e, double f, double g,
           double h, double k, float x)
{
    return a + i + b + c + d + e + f + g + h + k + x;
}
int main(void) {
    double zero = 0.0;
    double nan = zero / zero;
    float f = 1.5f;
    unsigned long big = 18446744073709551615ul;
    double d = big;
    return (sum(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0.5f) != 55.5) |
           ((int)-2.7 != -2) << 1 | ((unsigned)3e9 != 3000000000u) << 2 |
           (d != 18446744073709551616.0) << 3 | (f * 2 != 3.0f) << 4 |
           (nan == nan) << 5 | !(nan != nan) << 6 | (nan < 1.0) << 7 |
           ((unsigned long)1e19 != 10000000000000000000ul) << 8;
})"));
    EXPECT_EQ(0, exit_status);
}
