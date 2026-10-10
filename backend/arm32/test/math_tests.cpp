//
// Run tests for the <math.h> routines in the ARM32 libc, ported from AArch64's
// math_tests.cpp (itself from RISC-V's, and that from BESM-6's).
// The expected output is what the host C library prints for the same program.
//
#include "arm32_test.h"

TEST_F(Arm32Test, FrexpBasic)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("M=0.750000 E=4\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <math.h>
static void body(void) {
    int e;
    double m = frexp(12.0, &e);   /* 12 = 0.75 * 2^4 */
    printf("M=%f E=%d\n", m, e);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, FrexpPowersOfTwo)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("0.500000 4 0.500000 1 0.500000 -1\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <math.h>
static void body(void) {
    int e1, e2, e3;
    double m1 = frexp(8.0, &e1);    /* 0.5 * 2^4  */
    double m2 = frexp(1.0, &e2);    /* 0.5 * 2^1  */
    double m3 = frexp(0.25, &e3);   /* 0.5 * 2^-1 */
    printf("%f %d %f %d %f %d\n", m1, e1, m2, e2, m3, e3);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, FrexpNegativePreservesSign)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("-0.750000 4\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <math.h>
static void body(void) {
    int e;
    double m = frexp(-12.0, &e);   /* fraction in (-1, -0.5] */
    printf("%f %d\n", m, e);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, FrexpZero)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("0.000000 0\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <math.h>
static void body(void) {
    int e;
    double m = frexp(0.0, &e);   /* returns 0.0, *e = 0 (branchless path) */
    printf("%f %d\n", m, e);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, LdexpBasic)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("8.000000 12.000000 0.250000\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <math.h>
static void body(void) {
    printf("%f %f %f\n", ldexp(1.0, 3), ldexp(0.75, 4), ldexp(1.0, -2));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, LdexpZero)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("0.000000\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <math.h>
static void body(void) {
    printf("%f\n", ldexp(0.0, 5));   /* scaling zero stays zero */
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, FrexpLdexpRoundTrip)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("M=0.750000 E=4\nBACK=12.000000\nBACK=100.000000\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <math.h>
static void body(void) {
    int e;
    double m = frexp(12.0, &e);
    printf("M=%f E=%d\n", m, e);
    printf("BACK=%f\n", ldexp(m, e));
    double n = frexp(100.0, &e);       /* not a power of two */
    printf("BACK=%f\n", ldexp(n, e));
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(Arm32Test, FrexpLdexpExactRoundTrip)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("OK\n", CompileAndRunArm32(R"PROG(
#include <stdio.h>
#include <math.h>
static void body(void) {
    double xs[6];
    xs[0] = 12.0;
    xs[1] = -12.0;
    xs[2] = 0.25;
    xs[3] = 100.0;
    xs[4] = 1.0;
    xs[5] = 0.0;
    int i, e;
    for (i = 0; i < 6; i = i + 1) {
        double x = xs[i];
        double y = ldexp(frexp(x, &e), e);
        if (y != x) {
            printf("FAIL %d\n", i);
            return;
        }
    }
    printf("OK\n");
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

// A call of sqrt is the vsqrt.f64 instruction, not a call.
TEST_F(Arm32Test, SqrtInstruction)
{
    EXPECT_EQ("vsqrt.f64 d0, d0\nbx lr\n", Code(CompileToArm32(R"(
#include <math.h>
double f(double x) { return sqrt(x); }
)")));
}

// sqrt is IEEE, correctly rounded: the bits are the host's, for zeros of both signs,
// denormals, the largest double and infinity; a negative operand or a NaN gives a NaN.
// The operand may come from memory; a constant operand is folded.
TEST_F(Arm32Test, SqrtRun)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ(
        "0000000000000000\n8000000000000000\n3ff0000000000000\n3ff6a09e667f3bcd\n3ffbb67ae8584caa\n"
        "3fe0000000000000\n1fc1297872d9cbae\n5fefffffffffffff\n5f138d352e5096af\n1e60000000000000\n"
        "7ff0000000000000\n3ffbb67ae8584caa\n1 1 1\n4.5\n",
        CompileAndRunArm32(R"(
#include <stdio.h>
#include <string.h>
#include <math.h>
double root(double x) { return sqrt(x); }
double rootp(const double *p) { return sqrt(*p); }
static void bits(double r)
{
    unsigned long long b;
    memcpy(&b, &r, sizeof b);
    printf("%08x%08x\n", (unsigned)(b >> 32), (unsigned)b);
}
int main(void)
{
    static const double v[] = { 0.0, -0.0, 1.0, 2.0, 3.0, 0.25, 1e-310,
                                1.7976931348623157e308, 1e300, 5e-324 };
    for (int i = 0; i < 10; i++)
        bits(root(v[i]));
    double inf = v[7] * 2, nan = inf - inf;
    bits(root(inf));
    bits(rootp(&v[4]));
    double r1 = root(-1.0), r2 = root(nan), r3 = root(-inf);
    printf("%d %d %d\n", r1 != r1, r2 != r2, r3 != r3);
    printf("%g\n", sqrt(16.0) + sqrt(0.25));
    return 0;
}
)"));
}

// sqrt and sqrtf in libc.a: called through a pointer, and by clang's code (which
// -fno-builtin keeps a call).
TEST_F(Arm32Test, SqrtLibrary)
{
    SKIP_IF_NO_ARM32_TOOLS();
    SKIP_IF_NO_ARM32_CLANG();
    EXPECT_EQ("3ff6a09e667f3bcd\n3ffbb67ae8584caa\n3fb504f3\n3f3504f3\n3fddb3d7\n80000000\n1\n",
              CompileAndRunWithClang(R"(
#include <stdio.h>
#include <string.h>
#include <math.h>
double croot(double x);
float crootf(float x);
static void bits(double r)
{
    unsigned long long b;
    memcpy(&b, &r, sizeof b);
    printf("%08x%08x\n", (unsigned)(b >> 32), (unsigned)b);
}
static void fbits(float r)
{
    unsigned b;
    memcpy(&b, &r, sizeof b);
    printf("%08x\n", b);
}
int main(void)
{
    double (*f)(double) = sqrt;
    float (*g)(float)   = sqrtf;
    bits(f(2.0));
    bits(croot(3.0));
    fbits(sqrtf(2.0f));
    fbits(g(0.5f));
    fbits(crootf(3.0f));
    fbits(sqrtf(-0.0f));
    double m = f(-1.0);
    printf("%d\n", m != m);
    return 0;
}
)",
                                     R"(
double sqrt(double);
float sqrtf(float);
double croot(double x) { return sqrt(x); }
float crootf(float x) { return sqrtf(x); }
)"));
}
