//
// ARM32 floating point: VFP arithmetic, comparisons (NaN included), conversions,
// constants, conditions, and FP values across calls.
//
#include "arm32_test.h"

TEST_F(Arm32Test, DoubleArithmetic)
{
    DisableOptimization();
    std::string code = Code(CompileToArm32(R"(
double f(void) { double a = 1.5; double b = 2.0; return a * b + a; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(vldr d14, [r11, #-8]
vldr d15, [r11, #-16]
vmul.f64 d14, d14, d15
)")) << code;
    EXPECT_NE(std::string::npos, code.find("vldr d0, [r11, #")) << code;
    // d14 and d15 are callee-saved.
    EXPECT_NE(std::string::npos, code.find("vpush {d14, d15}\n")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(vpop {d14, d15}
mov sp, r11
pop {r11, pc}
)"))
        << code;
}

// vcmp and vmrs; a NaN sets C and V, so < and <= use mi and ls.
TEST_F(Arm32Test, FloatCompare)
{
    DisableOptimization();
    std::string code = Code(CompileToArm32(R"(
int f(void) { float a = 1.0f; float b = 2.0f; return (a < b) + (a <= b); }
)"));
    EXPECT_NE(std::string::npos,
              code.find(R"(vcmp.f32 s28, s30
vmrs APSR_nzcv, fpscr
mov r12, #0
movmi r12, #1
)"))
        << code;
    EXPECT_NE(std::string::npos, code.find("movls r12, #1\n")) << code;
}

TEST_F(Arm32Test, FloatingPointCondition)
{
    DisableOptimization();
    std::string code = Code(CompileToArm32(R"(
int f(void) { double d = 0.5; if (d) return 1; return 2; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(vcmp.f64 d14, #0
vmrs APSR_nzcv, fpscr
beq .L)"))
        << code;
}

TEST_F(Arm32Test, FloatingPointConversions)
{
    DisableOptimization();
    std::string code = Code(CompileToArm32(R"(
int f(void) {
    unsigned u = 7; double d = u; float x = d; int i = x; unsigned v = d;
    return i + v;
})"));
    static const char *const expected[] = {
        R"(vmov s28, r12
vcvt.f64.u32 d14, s28
)",
        "vcvt.f32.f64 s28, d14\n",
        R"(vcvt.s32.f32 s28, s28
vmov r12, s28
)",
        "vcvt.u32.f64 s28, d14\n",
    };
    for (const char *s : expected)
        EXPECT_NE(std::string::npos, code.find(s)) << s << " in\n" << code;
}

// A constant that is a VFP immediate takes one vmov; another its bits through core
// registers.  A long double is a double, its constant read from its binary128 bits.
EXPECT_CODE(FloatImmediate, R"(vmov.f32 s0, #-1.5
bx lr
)",
            "float f(void) { return -1.5f; }")
EXPECT_CODE(DoubleImmediate, R"(vmov.f64 d0, #0.1328125
bx lr
)",
            "double f(void) { return 0.1328125; }")
EXPECT_CODE(LongDoubleImmediate, R"(vmov.f64 d0, #31.0
bx lr
)",
            "long double f(void) { return 31.0L; }")
EXPECT_CODE(DoubleBits, R"(movw r0, #39322
movt r0, #39321
movw r1, #39321
movt r1, #16313
vmov d0, r0, r1
bx lr
)",
            "double f(void) { return 0.1; }")
EXPECT_CODE(LongDoubleBits, R"(movw r0, #39322
movt r0, #39321
movw r1, #39321
movt r1, #16313
vmov d0, r0, r1
bx lr
)",
            "long double f(void) { return 0.1L; }")

TEST_F(Arm32Test, RunArithmeticAndComparisons)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    double a = 1.5; double b = -0.25; float f = 3.0f; double zero = 0.0;
    double nan = zero / zero;
    return (a * 4 + b == 5.75) + 2 * (a / b == -6.0) + 4 * (f - 1.0f == 2.0f)
         + 8 * (-a < b) + 16 * !(nan < a) + 32 * !(nan <= a) + 64 * (nan != nan)
         + 128 * !(nan == nan);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Arm32Test, RunFloatingPointConversions)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    double d = -7.9; unsigned u = 4000000000u; signed char c = -3; float f = 1e9f;
    long double ld = 0.1L; double big = 3e9;
    return ((int)d == -7) + 2 * ((unsigned)big == 3000000000u)
         + 4 * ((double)u == 4000000000.0) + 8 * ((double)c == -3.0)
         + 16 * ((float)u == 4e9f) + 32 * ((int)f == 1000000000) + 64 * ((float)d == -7.9f)
         + 128 * ((unsigned char)(double)200.7 == 200 && ld == 0.1);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Arm32Test, RunCallsAndConditions)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", CompileAndRunArm32(R"(
double mix(int a, double b, float c, int d, double e, double f, double g, double h,
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

// Doubles and floats in d0-d7 with back-filling, and past them on the stack, both
// ways with clang.
TEST_F(Arm32Test, RunFloatingPointInteropWithClang)
{
    SKIP_IF_NO_ARM32_TOOLS();
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
