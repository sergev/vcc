//
// ARM32 calls under AAPCS-VFP: core registers, even pairs, VFP registers with
// back-filling, stack arguments, results, and calls through pointers.
//
#include "arm32_test.h"

#define EXPECT_SELECTS(name, expected, src)                        \
    TEST_F(Arm32Test, name)                                        \
    {                                                              \
        DisableOptimization();                                     \
        std::string code = Code(CompileToArm32(src));              \
        EXPECT_NE(std::string::npos, code.find(expected)) << code; \
    }

// A long long skips r1 for the even pair r2:r3; the next int goes on the stack.
EXPECT_SELECTS(CallEvenPair, R"(mov r12, #9
str r12, [sp]
mov r0, #1
ldr r2, [r11, #-8]
ldr r3, [r11, #-4]
bl g
)",
               "int g(int a, long long b, int c); int f(void) { return g(1, 2, 9); }")
// A float back-fills s1, which the double's alignment skipped.
EXPECT_SELECTS(CallBackFill, R"(vmov s0, r12
mov r12, #0
mov lr, #1073741824
vmov d1, r12, lr
movw r12, #0
movt r12, #16448
vmov s1, r12
bl g
)",
               "void g(float a, double b, float c); void f(void) { g(1.0f, 2.0, 3.0f); }")
// Once an FP value is on the stack, no later one takes a VFP register.
TEST_F(Arm32Test, CallVfpClosed)
{
    std::string src = "void g(double, double, double, double, double, double, double, double, "
                      "double, float);\nvoid f(void) { g(1, 2, 3, 4, 5, 6, 7, 8, 9, 10.0f); }";
    std::string code = Code(CompileToArm32(src.c_str()));
    EXPECT_NE(std::string::npos, code.find("str r12, [sp]\nstr lr, [sp, #4]\n")) << code;
    EXPECT_NE(std::string::npos, code.find("str r12, [sp, #8]\n")) << code;
    EXPECT_NE(std::string::npos, code.find("vmov d7, r12, lr\nbl g\n")) << code;
}
// A narrow argument goes extended.
EXPECT_SELECTS(CallNarrowArgument, "ldrsb r0, [r11, #-5]\nbl g\n",
               "void g(signed char c); void f(void) { g(-1); }")
// A variadic callee takes a double in a core pair, under the base standard.
EXPECT_SELECTS(CallVariadicDouble, "mov r2, #0\nmov r3, #1073741824\nbl g\n",
               "void g(int n, ...); void f(void) { g(1, 2.0); }")
EXPECT_SELECTS(CallResults, "bl g\nvstr d0, [r11, #-",
               "double g(void); double f(void) { return g(); }")
// A constant argument goes as its parameter type.
TEST_F(Arm32Test, CallNarrowConstant)
{
    std::string code = Code(CompileToArm32("void g(signed char c); void f(void) { g(-1); }"));
    EXPECT_NE(std::string::npos, code.find("mvn r0, #0\nbl g\n")) << code;
}
// A parameter in a register is stored to its slot; one on the stack read where it is.
EXPECT_SELECTS(Parameters, R"(str r0, [r11, #-4]
str r2, [r11, #-16]
str r3, [r11, #-12]
vstr s0, [r11, #-20]
)",
               "int f(int a, long long b, float c, int d) { return d; }")
EXPECT_SELECTS(StackParameter, "ldr r0, [r11, #8]\n",
               "int f(int a, long long b, float c, int d) { return d; }")
EXPECT_SELECTS(IndirectCall, "ldr r12, [r11, #-4]\nblx r12\n",
               "int f(int (*p)(int)) { return p(3); }")

TEST_F(Arm32Test, RunCalls)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", CompileAndRunArm32(R"(
long long add3(int a, long long b, int c) { return a + b + c; }
int many(int a, int b, int c, int d, int e, long long f, int g, int h)
{
    return a + b + c + d + e + (int)f + g + h;
}
int narrow(signed char c, unsigned short s) { return c + s; }
int twice(int x) { return 2 * x; }
int apply(int (*fn)(int), int x) { return fn(x); }
int main(void) {
    return (add3(1, 0x100000000LL, 2) == 0x100000003LL)
         + 2 * (many(1, 2, 3, 4, 5, 6, 7, 8) == 36)
         + 4 * (narrow(-1, 65535) == 65534) + 8 * (apply(twice, 21) == 42);
})"));
    EXPECT_EQ(15, exit_status);
}

// Ours forwards its parameters to clang's, which checks them, and returns clang's
// results: FP values in VFP registers with back-filling and on the stack, a long long
// in an even pair and on the stack, narrow values extended both ways.
TEST_F(Arm32Test, RunCallsWithClang)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWithClang(R"(
int c_mix(float a, double b, float c, double d, float e);
int c_many(double a, double b, double c, double d, double e, double f, double g,
           double h, float i, int j, long long k, int l, long long m);
int c_narrow(signed char c, unsigned short s);
double c_half(void);
float c_third(void);
int c_check(double d, float f);
int mix(float a, double b, float c, double d, float e) { return c_mix(a, b, c, d, e); }
int many(double a, double b, double c, double d, double e, double f, double g,
         double h, float i, int j, long long k, int l, long long m)
{
    return c_many(a, b, c, d, e, f, g, h, i, j, k, l, m);
}
int main(void) {
    double h = c_half();
    float t = c_third();
    return mix(1.0f, 2.0, 3.0f, 4.0, 5.0f) + 2 * c_narrow(-1, 65535)
         + 4 * many(1, 2, 3, 4, 5, 6, 7, 8, 9.0f, 10, 11, 12, 0x100000000LL)
         + 8 * c_check(h, t);
})",
                                         R"(
int c_mix(float a, double b, float c, double d, float e)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5;
}
int c_many(double a, double b, double c, double d, double e, double f, double g,
           double h, float i, int j, long long k, int l, long long m)
{
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 && g == 7 && h == 8 &&
           i == 9 && j == 10 && k == 11 && l == 12 && m == 0x100000000LL;
}
int c_narrow(signed char c, unsigned short s) { return c + s == 65534; }
double c_half(void) { return 0.5; }
float c_third(void) { return 1.0f / 3; }
int c_check(double d, float f) { return d == 0.5 && f == 1.0f / 3; }
int mix(float a, double b, float c, double d, float e);
int many(double a, double b, double c, double d, double e, double f, double g,
         double h, float i, int j, long long k, int l, long long m);
)"));
    EXPECT_EQ(15, exit_status);
}
