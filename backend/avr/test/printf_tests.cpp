//
// Run tests for printf / sprintf / snprintf in the AVR libc, ported from x86-64's (and
// so from AArch64's, RISC-V's and BESM-6's).
// The expected output is what the host C library prints for the same program.
//
#include "avr_test.h"

TEST_F(AvrTest, PrintfDecimal)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0 42 -2345\n", CompileAndRunAvr(R"PROG(
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

// The length modifiers: ll and j read a long long.  70000 is a long here, so it is
// cast to the int that %hd reads.
TEST_F(AvrTest, PrintfLengthModifiers)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("-9223372036854775808 18446744073709551615 123456789abcdef -5 44 4464 7 -3\n",
              CompileAndRunAvr(R"PROG(
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
int main(void) {
    printf("%lld %llu %llx %jd %hhd %hd %zu %td\n", (long long)(-9223372036854775807LL - 1),
           18446744073709551615ULL, 0x123456789abcdefULL, (intmax_t)-5, 300, (int)70000L,
           (size_t)7, (ptrdiff_t)-3);
    return 0;
}
)PROG"));
}

TEST_F(AvrTest, PrintfUnsignedOctalHex)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("100 100 ff FF\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfSharpFlag)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0xff 0100\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfSignFlags)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("+42  42 -42\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfIntPrecision)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("00042   042\n", CompileAndRunAvr(R"PROG(
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

// int is 16 bits.
TEST_F(AvrTest, PrintfNegativeHex)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("ffff\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfWidth)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("[   42][42   ][00042]\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfStarWidthPrecision)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("[    42][3.14]\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfChar)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("ABC\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfPercent)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("100%\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfString)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("[hello]\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfStringWidthPrecision)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("[        hi][hi        ][hel]\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfNullString)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("[(null)]\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfFloatF)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("1.500000 0.000000 -2.500000\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfFloatPrecisionWidth)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("[3.14][     3.142][3.142   ]\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfFloatExp)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("1.234000e+01\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfFloatG)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("12.34 100000\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfFloatRoundCarry)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("0 10\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, Snprintf)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("[n=7 s=abc](9)\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, Sprintf)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("[5-fa-Z](6)\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, SnprintfTruncation)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("[abcd](7)\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, MutatedParameterInLoop)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("321\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, CharPtrRelationalCompare)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("ABC\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, EnumArrayDimension)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("4\n", CompileAndRunAvr(R"PROG(
#include <stdio.h>
enum { N = 4 };
static void body(void) { printf("%d\n", (int)sizeof(char[N])); }
int main(void) {
    body();
    return 0;
}
)PROG"));
}

TEST_F(AvrTest, PrintfStaticStringPointer)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("[ABC]\n", CompileAndRunAvr(R"PROG(
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

TEST_F(AvrTest, PrintfStaticVoidStringPointer)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("[AB][CD][EF]\n", CompileAndRunAvr(R"PROG(
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
TEST_F(AvrTest, PrintfInfNan)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ(R"([inf] [INF] [inf] [INF] [inf] [INF]
[-inf] [-INF] [-inf] [-INF] [-inf] [-INF]
[nan] [NAN] [nan] [NAN] [nan] [NAN]
[     inf] [inf     ] [     inf] [+inf] [ inf] [    +inf]
[     nan] [NAN     ] [     nan] [nan]
[-inf] [      -inf] [-INF      ] [-INF]
[inf] [nan]
-inf|NAN|inf
)",
              CompileAndRunAvr(R"PROG(
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
TEST_F(AvrTest, PrintfRoundHalfEven)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ(R"(0 2 2 4 -2
0.2 0.8 1.12 0.35
2e+01 4e+01 1.2e+00 1.12e+00
2 0.12 0.5 1e-05
)",
              CompileAndRunAvr(R"PROG(
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
