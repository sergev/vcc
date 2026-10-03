//
// Run tests for the <math.h> routines in the AArch64 libc, ported from RISC-V's
// math_tests.cpp (itself from BESM-6's).
// The expected output is what the host C library prints for the same program.
//
#include "aarch64_test.h"

TEST_F(Aarch64Test, FrexpBasic)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("M=0.750000 E=4\n", CompileAndRunAarch64(R"PROG(
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

TEST_F(Aarch64Test, FrexpPowersOfTwo)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("0.500000 4 0.500000 1 0.500000 -1\n", CompileAndRunAarch64(R"PROG(
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

TEST_F(Aarch64Test, FrexpNegativePreservesSign)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("-0.750000 4\n", CompileAndRunAarch64(R"PROG(
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

TEST_F(Aarch64Test, FrexpZero)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("0.000000 0\n", CompileAndRunAarch64(R"PROG(
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

TEST_F(Aarch64Test, LdexpBasic)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("8.000000 12.000000 0.250000\n", CompileAndRunAarch64(R"PROG(
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

TEST_F(Aarch64Test, LdexpZero)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("0.000000\n", CompileAndRunAarch64(R"PROG(
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

TEST_F(Aarch64Test, FrexpLdexpRoundTrip)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("M=0.750000 E=4\nBACK=12.000000\nBACK=100.000000\n", CompileAndRunAarch64(R"PROG(
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

TEST_F(Aarch64Test, FrexpLdexpExactRoundTrip)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("OK\n", CompileAndRunAarch64(R"PROG(
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
