//
// AArch64 floating point: arithmetic, comparisons (NaN included), conversions,
// conditions, and FP values across calls.
//
#include "aarch64_test.h"

TEST_F(Aarch64Test, DoubleArithmetic)
{
    NaiveSelection();
    DisableOptimization();
    std::string code = Code(CompileToAarch64(R"(
double f(void) { double a = 1.5; double b = 2.0; return a * b + a; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(ldr d16, [x29, #-8]
ldr d17, [x29, #-16]
fmul d16, d16, d17
)")) << code;
    EXPECT_NE(std::string::npos, code.find("ldr d0, [x29, #")) << code;
}

// fcmp sets C and V on a NaN, so < and <= use mi and ls.
TEST_F(Aarch64Test, FloatCompare)
{
    NaiveSelection();
    DisableOptimization();
    std::string code = Code(CompileToAarch64(R"(
int f(void) { float a = 1.0f; float b = 2.0f; return (a < b) + (a <= b); }
)"));
    EXPECT_NE(std::string::npos, code.find("fcmp s16, s17\ncset w9, mi\n")) << code;
    EXPECT_NE(std::string::npos, code.find("fcmp s16, s17\ncset w9, ls\n")) << code;
}

TEST_F(Aarch64Test, FloatingPointCondition)
{
    NaiveSelection();
    DisableOptimization();
    std::string code = Code(CompileToAarch64(R"(
int f(void) { double d = 0.5; if (d) return 1; return 2; }
)"));
    EXPECT_NE(std::string::npos, code.find("fcmp d16, #0.0\nb.eq .L")) << code;
}

TEST_F(Aarch64Test, FloatingPointConversions)
{
    NaiveSelection();
    DisableOptimization();
    std::string code = Code(CompileToAarch64(R"(
long f(void) {
    unsigned u = 7; double d = u; float x = d; long l = x; unsigned long ul = d;
    return l + ul;
})"));
    for (const char *s :
         { "ucvtf d17, w9\n", "fcvt s17, d16\n", "fcvtzs x10, s16\n", "fcvtzu x10, d16\n" })
        EXPECT_NE(std::string::npos, code.find(s)) << s << " in\n" << code;
}

TEST_F(Aarch64Test, RunArithmeticAndComparisons)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
int main(void) {
    double a = 1.5; double b = -0.25; float f = 3.0f; double zero = 0.0;
    double nan = zero / zero;
    return (a * 4 + b == 5.75) + 2 * (a / b == -6.0) + 4 * (f - 1.0f == 2.0f)
         + 8 * (-a < b) + 16 * !(nan < a) + 32 * !(nan <= a) + 64 * (nan != nan)
         + 128 * !(nan == nan);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Aarch64Test, RunFloatingPointConversions)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
int main(void) {
    double d = -7.9; unsigned long big = 18446744073709551615ul; unsigned u = 4000000000u;
    signed char c = -3; float f = 1e10f;
    return ((int)d == -7) + 2 * ((double)big == 18446744073709551615.0)
         + 4 * ((double)u == 4000000000.0) + 8 * ((double)c == -3.0)
         + 16 * ((unsigned long)1e19 == 10000000000000000000ul) + 32 * ((long)f == 10000000000l)
         + 64 * ((float)d == -7.9f) + 128 * ((unsigned char)(double)200.7 == 200);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Aarch64Test, RunCallsAndConditions)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
double mix(int a, double b, float c, long d, double e, double f, double g, double h,
           double i, double j, double k)
{
    return a + b + c + d + e + f + g + h + i + j + k * 100;
}
int truth(double d) { if (d) return 1; return 0; }
int main(void) {
    double zero = 0.0;
    return (mix(1, 2.0, 3.0f, 4, 5, 6, 7, 8, 9, 10, 11) == 1155.0) + 2 * truth(-0.5)
         + 4 * !truth(zero) + 8 * truth(zero / zero);
})"));
    EXPECT_EQ(15, exit_status);
}

// Doubles and floats in v0-v7, and past them on the stack, both ways with clang.
TEST_F(Aarch64Test, RunFloatingPointInteropWithClang)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunWithClang(R"(
double theirs(float a, double b, int c, double d, double e, double f, double g, double h,
              double i, float j, double k);
int call_ours(void);
double ours(double a, float b, double c, double d, double e, double f, double g, double h,
            float i, double j)
{
    return a + b + c + d + e + f + g + h + i * 100 + j * 1000;
}
int main(void) {
    return (theirs(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11) == 11145.0) + 2 * call_ours();
})",
                                         R"(
double ours(double a, float b, double c, double d, double e, double f, double g, double h,
            float i, double j);
double theirs(float a, double b, int c, double d, double e, double f, double g, double h,
              double i, float j, double k)
{
    return a + b + c + d + e + f + g + h + i + j * 10 + k * 1000;
}
int call_ours(void) { return ours(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 10936.0; }
)"));
    EXPECT_EQ(3, exit_status);
}
