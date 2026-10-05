//
// Run tests for the <math.h> routines in the AVR libc, ported from x86-64's (and
// so from AArch64's, RISC-V's and BESM-6's).
// The expected output is what the host C library prints for the same program.
//
#include "avr_test.h"

TEST_F(AvrTest, FrexpBasic)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("M=0.750000 E=4\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, FrexpPowersOfTwo)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0.500000 4 0.500000 1 0.500000 -1\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, FrexpNegativePreservesSign)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("-0.750000 4\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, FrexpZero)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0.000000 0\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, LdexpBasic)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("8.000000 12.000000 0.250000\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, LdexpZero)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0.000000\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, FrexpLdexpRoundTrip)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ(R"(M=0.750000 E=4
BACK=12.000000
BACK=100.000000
)", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, FrexpLdexpExactRoundTrip)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("OK\n", CompileAndRunAvr(R"PROG(
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

// sqrt is float32.c's sqrtf (double is binary32), correctly rounded: the bits are the
// host's sqrtf, for zeros of both signs, subnormals, the largest float and infinity; a
// negative operand or a NaN gives a NaN.  sqrtf is the same function.
TEST_F(AvrTest, SqrtRun)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ(R"(00000000
80000000
3f800000
3fb504f3
3fddb3d7
3f000000
1e3ce4e7
5f7fffff
58635fa9
1a3504f3
7f800000
1 1 1
4.5
)",
              CompileAndRunAvr(R"(
#include <stdio.h>
#include <string.h>
#include <math.h>
static void bits(double r)
{
    unsigned long b;
    memcpy(&b, &r, sizeof b);
    printf("%08lx\n", b);
}
int main(void)
{
    static const double v[] = { 0.0, -0.0, 1.0, 2.0, 3.0, 0.25, 1e-40, 3.40282347e+38, 1e30,
                                1.4e-45 };
    for (int i = 0; i < 10; i++)
        bits(sqrt(v[i]));
    double inf = v[7] * 2, nan = inf - inf;
    bits(sqrtf(inf));
    double r1 = sqrt(-1.0), r2 = sqrt(nan), r3 = sqrtl(-inf);
    printf("%d %d %d\n", r1 != r1, r2 != r2, r3 != r3);
    printf("%g\n", sqrt(16.0) + sqrt(0.25));
    return 0;
}
)"));
}
