//
// Run tests for printf / sprintf / snprintf in the MMIX libc, ported from x86-64's (and so
// from AArch64's, RISC-V's and BESM-6's).
// The expected output is what the host C library prints for the same program.  Each
// case is also built by GCC with newlib and run on mmix (RunAgainstNewlib): the two
// outputs must agree.
//
#include "mmix_test.h"

TEST_F(MmixTest, PrintfDecimal)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("0 42 -2345\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("%d %d %d\n", 0, 42, -2345);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

// The length modifiers: ll and j read a long long, 64 bits on both widths.
// Not against newlib: newlib is built without the C99 formats: j, z, t and hh.
TEST_F(MmixTest, PrintfLengthModifiers)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("-9223372036854775808 18446744073709551615 123456789abcdef -5 44 4464 7 -3\n",
              CompileAndRunMmix(R"PROG(
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
int main(void) {
    printf("%lld %llu %llx %jd %hhd %hd %zu %td\n", (long long)(-9223372036854775807LL - 1),
           18446744073709551615ULL, 0x123456789abcdefULL, (intmax_t)-5, 300, 70000,
           (size_t)7, (ptrdiff_t)-3);
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfUnsignedOctalHex)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("100 100 ff FF\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("%u %o %x %X\n", 100, 64, 255, 255);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfSharpFlag)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("0xff 0100\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("%#x %#o\n", 255, 64);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfSignFlags)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("+42  42 -42\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("%+d % d %+d\n", 42, 42, -42);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfIntPrecision)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("00042   042\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("%.5d %05.3d\n", 42, 42);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfNegativeHex)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("ffffffff\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("%x\n", -1);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfWidth)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("[   42][42   ][00042]\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("[%5d][%-5d][%05d]\n", 42, 42, 42);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfStarWidthPrecision)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("[    42][3.14]\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("[%*d][%.*f]\n", 6, 42, 2, 3.14159);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfChar)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("ABC\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("%c%c%c\n", 'A', 'B', 'C');
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfPercent)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("100%\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("100%%\n");
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfString)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("[hello]\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("[%s]\n", "hello");
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfStringWidthPrecision)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("[        hi][hi        ][hel]\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("[%10s][%-10s][%.3s]\n", "hi", "hi", "hello");
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfNullString)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("[(null)]\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("[%s]\n", (char *)0);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfFloatF)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("1.500000 0.000000 -2.500000\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("%f %f %f\n", 1.5, 0.0, -2.5);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfFloatPrecisionWidth)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("[3.14][     3.142][3.142   ]\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("[%.2f][%10.3f][%-8.3f]\n", 3.14159, 3.14159, 3.14159);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfFloatExp)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("1.234000e+01\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("%e\n", 12.34);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfFloatG)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("12.34 100000\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("%g %g\n", 12.34, 100000.0);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfFloatRoundCarry)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("0 10\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    printf("%.0f %.0f\n", 0.5, 9.5);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, Snprintf)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("[n=7 s=abc](9)\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    char buf[32];
    int n = snprintf(buf, 32, "n=%d s=%s", 7, "abc");
    printf("[%s](%d)\n", buf, n);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, Sprintf)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("[5-fa-Z](6)\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    char buf[40];
    int n = sprintf(buf, "%d-%x-%c", 5, 250, 'Z');
    printf("[%s](%d)\n", buf, n);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, SnprintfTruncation)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("[abcd](7)\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    char buf[5];
    int n = snprintf(buf, 5, "%s", "abcdefg");
    printf("[%s](%d)\n", buf, n);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

// putbyte is ours, not newlib's.
TEST_F(MmixTest, MutatedParameterInLoop)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("321\n", CompileAndRunMmix(R"PROG(
#include <stdio.h>
void countdown(int n) {
    while (n > 0) {
        putbyte('0' + n);
        --n;
    }
}
static void body(void) { countdown(3); putbyte('\n'); }
int main(void) {
    body();
    return 0;
}
)PROG"));
}

// putbyte is ours, not newlib's.
TEST_F(MmixTest, CharPtrRelationalCompare)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("ABC\n", CompileAndRunMmix(R"PROG(
#include <stdio.h>
static void body(void) {
    char b[4];
    b[0] = 'C'; b[1] = 'B'; b[2] = 'A'; b[3] = 0;
    char *p = b + 2;
    while (p >= b) {
        putbyte(*p);
        --p;
    }
    putbyte('\n');
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, EnumArrayDimension)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("4\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
enum { N = 4 };
static void body(void) { printf("%d\n", (int)sizeof(char[N])); }
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfStaticStringPointer)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("[ABC]\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
static void body(void) {
    static char *p = "ABC";
    printf("[%s]\n", p);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(MmixTest, PrintfStaticVoidStringPointer)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("[AB][CD][EF]\n", RunAgainstNewlib(R"PROG(
#include <stdio.h>
void *g = "AB";
struct s { const void *p; } sg = { "CD" };
static void body(void) {
    static void *q = "EF";
    printf("[%s][%s][%s]\n", (char *)g, (const char *)sg.p, (char *)q);
}
int main(void) {
    body();
    return 0;
}
)PROG"));
}

// Infinities and NaNs under every floating conversion, with widths and flags; a NaN
// gets no sign flag here, since C libraries differ on "%+f" of NaN.
// Not against newlib: newlib is built without the C99 formats: it prints %F as F.
TEST_F(MmixTest, PrintfInfNan)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ(R"([inf] [INF] [inf] [INF] [inf] [INF]
[-inf] [-INF] [-inf] [-INF] [-inf] [-INF]
[nan] [NAN] [nan] [NAN] [nan] [NAN]
[     inf] [inf     ] [     inf] [+inf] [ inf] [    +inf]
[     nan] [NAN     ] [     nan] [nan]
[-inf] [      -inf] [-INF      ] [-INF]
[inf] [nan]
-inf|NAN|inf
)",
              CompileAndRunMmix(R"PROG(
#include <math.h>
#include <stdio.h>
int main(void) {
    double pinf = INFINITY;
    double qnan = NAN;
    double zero = 0.0;
    char buf[32];
    printf("[%f] [%F] [%e] [%E] [%g] [%G]\n", pinf, pinf, pinf, pinf, pinf, pinf);
    printf("[%f] [%F] [%e] [%E] [%g] [%G]\n", -pinf, -pinf, -pinf, -pinf, -pinf, -pinf);
    printf("[%f] [%F] [%e] [%E] [%g] [%G]\n", qnan, qnan, qnan, qnan, qnan, qnan);
    printf("[%8f] [%-8f] [%08f] [%+f] [% f] [%+08.3e]\n", pinf, pinf, pinf, pinf, pinf, pinf);
    printf("[%8f] [%-8F] [%08g] [%.0e]\n", qnan, qnan, qnan, qnan);
    printf("[%.3f] [%10.2e] [%-10G] [%+F]\n", -pinf, -pinf, -pinf, -pinf);
    printf("[%f] [%g]\n", 1.0 / zero, zero / zero);
    snprintf(buf, sizeof buf, "%f|%F|%e", -pinf, qnan, pinf);
    puts(buf);
    return 0;
}
)PROG"));
}

// An exact tie rounds to even; anything above it rounds up.
TEST_F(MmixTest, PrintfRoundHalfEven)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ(R"(0 2 2 4 -2
0.2 0.8 1.12 0.35
2e+01 4e+01 1.2e+00 1.12e+00
2 0.12 0.5 1e-05
)",
              RunAgainstNewlib(R"PROG(
#include <stdio.h>
int main(void) {
    printf("%.0f %.0f %.0f %.0f %.0f\n", 0.5, 1.5, 2.5, 3.5, -2.5);
    printf("%.1f %.1f %.2f %.2f\n", 0.25, 0.75, 1.125, 0.35);
    printf("%.0e %.0e %.1e %.2e\n", 25.0, 35.0, 1.25, 1.125);
    printf("%.1g %.2g %g %g\n", 2.5, 0.125, 0.5, 1e-5);
    return 0;
}
)PROG"));
}

// long double, which is double here, through va_arg: %Lf, %Le and %Lg, interleaved
// with other arguments, past the 16 registers onto the stack, and into a buffer with a
// width and precision.  Also %f of DBL_MAX, the longest output FBUFSIZE allows for:
// its 309 integer digits, of which doprnt gets the first 17 right and prints the rest
// as 0 (the host and newlib print them exactly).
TEST_F(MmixTest, PrintfLongDouble)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ(R"(2.500000 1.234e+03 0.0001 1.250000
1 2.500000 3 0.3333333333 x 1.000000 2.000000 3.000000 4.000000 5.000000 6.000000 -0.125000
[      3.14|-2.3    ]
316 179769313486 .000000
)",
              RunAgainstNewlib(R"PROG(
#include <stdio.h>
int main(void) {
    long double third = 1.0L / 3;
    printf("%Lf %.3Le %Lg %f\n", 2.5L, 1234.5L, 0.0001L, 1.25);
    printf("%d %Lf %d %.10Lf %s %Lf %Lf %Lf %Lf %Lf %Lf %Lf\n", 1, 2.5L, 3, third, "x", 1.0L,
           2.0L, 3.0L, 4.0L, 5.0L, 6.0L, -0.125L);
    char buf[64];
    snprintf(buf, sizeof buf, "[%10.2Lf|%-8.1Lf]", 3.14159L, -2.26L);
    puts(buf);
    char big[400];
    int n = sprintf(big, "%f", 1.7976931348623157e308);
    printf("%d %.12s %s\n", n, big, big + n - 7);
    return 0;
}
)PROG"));
}
