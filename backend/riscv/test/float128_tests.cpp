//
// long double (binary128): the runtime routines in libc/riscv64/float128.c, reached
// through C operators compiled by us, against exact results (gen_float128_cases.py);
// and long double values passed to and from clang-compiled code.  Built for both
// widths: on rv32 a long double goes by reference, and 64-bit values are long long.
//
#include "riscv_test.h"

class Float128Test : public RiscvTest {};

static const std::string kCases =
#include "float128_cases.inc"
    ;

static const std::string kChecks = R"(
#include <stdio.h>

typedef union {
    long double f;
    double d;
    float s;
    unsigned long long w[2];
} B;

static int fails;

static long double ld(const unsigned long long *p)
{
    B b;
    b.w[0] = p[0];
    b.w[1] = p[1];
    return b.f;
}

static int nan_bits(const unsigned long long *p)
{
    unsigned long long hi = p[1] & 0x7fffffffffffffffULL;
    return hi > 0x7fff000000000000ULL || (hi == 0x7fff000000000000ULL && p[0]);
}

// x has the bits at r, or is a NaN when r is one.
static int same(long double x, const unsigned long long *r)
{
    B b;
    b.f = x;
    if (nan_bits(r))
        return nan_bits(b.w);
    return b.w[0] == r[0] && b.w[1] == r[1];
}

static void check(const char *what, int i, int ok)
{
    if (!ok && ++fails <= 10)
        printf("%s %d\n", what, i);
}

#define N(t) (int)(sizeof(t) / sizeof(t[0]))

int main(void)
{
    int i;
    for (i = 0; i < N(t_add); i++)
        check("add", i, same(ld(t_add[i]) + ld(t_add[i] + 2), t_add[i] + 4));
    for (i = 0; i < N(t_sub); i++)
        check("sub", i, same(ld(t_sub[i]) - ld(t_sub[i] + 2), t_sub[i] + 4));
    for (i = 0; i < N(t_mul); i++)
        check("mul", i, same(ld(t_mul[i]) * ld(t_mul[i] + 2), t_mul[i] + 4));
    for (i = 0; i < N(t_div); i++)
        check("div", i, same(ld(t_div[i]) / ld(t_div[i] + 2), t_div[i] + 4));
    for (i = 0; i < N(t_cmp); i++) {
        long double x = ld(t_cmp[i]), y = ld(t_cmp[i] + 2);
        unsigned long long m = (x < y) | (x <= y) << 1 | (x > y) << 2 | (x >= y) << 3 |
                               (x == y) << 4 | (x != y) << 5;
        check("cmp", i, m == t_cmp[i][4]);
    }
    for (i = 0; i < N(t_narrow); i++) {
        B d, s;
        d.d = (double)ld(t_narrow[i]);
        s.w[0] = 0;
        s.s = (float)ld(t_narrow[i]);
        if (nan_bits(t_narrow[i]))
            check("narrow", i, d.d != d.d && s.s != s.s);
        else
            check("narrow", i, d.w[0] == t_narrow[i][2] && (unsigned)s.w[0] == t_narrow[i][3]);
    }
    for (i = 0; i < N(t_fix); i++)
        check("fix", i, (long long)ld(t_fix[i]) == (long long)t_fix[i][2]);
    for (i = 0; i < N(t_float); i++)
        check("float", i, same((long double)(long long)t_float[i][0], t_float[i] + 1));
    for (i = 0; i < N(t_extend); i++) {
        B d;
        d.w[0] = t_extend[i][0];
        check("extend", i, same((long double)d.d, t_extend[i] + 1));
    }
    printf("%d failures\n", fails);
    return 0;
}
)";

// Every routine on random and special operands, rounded to nearest even.
TEST_F(Float128Test, Routines)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("0 failures\n", CompileAndRunRiscv(kCases + kChecks));
}

static const std::string kDecls = R"(
struct LD { long double x; };
struct A16 { _Alignas(16) long a; long b; };
long double ld_sub(long double a, long double b);
long double ld_split(long a0, long a1, long a2, long a3, long a4, long a5, long a6,
                     long double x);
long double ld_stack(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7,
                     int k, long double x);
long double ld_vsum(int n, ...);
long ld_vmix(int a, int b, int c, ...);
struct LD ld_struct(struct LD s, long double y);
long a16_stack(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7, int k,
               struct A16 t);
long a16_va(int n, ...);
long double ld_conv(double d, float f, int i, unsigned long long u);
long long ld_back(long double x, int which);
)";

static const std::string kLdCallee = kDecls + R"(
#include <stdarg.h>
long double ld_sub(long double a, long double b) { return a - b; }
long double ld_split(long a0, long a1, long a2, long a3, long a4, long a5, long a6,
                     long double x)
{
    return x * a6 + a0;
}
long double ld_stack(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7,
                     int k, long double x)
{
    return x / k;
}
long double ld_vsum(int n, ...)
{
    va_list ap;
    long double s = 0;
    va_start(ap, n);
    for (int i = 0; i < n; i++)
        s += va_arg(ap, long double);
    va_end(ap);
    return s;
}
long ld_vmix(int a, int b, int c, ...)
{
    va_list ap;
    va_start(ap, c);
    long double x = va_arg(ap, long double);
    int d         = va_arg(ap, int);
    long double y = va_arg(ap, long double);
    va_end(ap);
    return (long)(x * 10) * 1000 + d * 100 + (long)y + a + b + c;
}
struct LD ld_struct(struct LD s, long double y)
{
    s.x = s.x * y;
    return s;
}
long a16_stack(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7, int k,
               struct A16 t)
{
    return t.a * 10 + t.b + k * 100;
}
long a16_va(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    struct A16 t = va_arg(ap, struct A16);
    va_end(ap);
    return t.a * 10 + t.b + n * 100;
}
long double ld_conv(double d, float f, int i, unsigned long long u)
{
    return (long double)d + f + i + u;
}
long long ld_back(long double x, int which)
{
    if (which == 0)
        return (long long)x;
    if (which == 1)
        return (long long)(double)x;
    return (long long)((float)x * 4);
}
)";

static const std::string kLdCaller = kDecls + R"(
long double third(void) { return 1; }
int main(void)
{
    long double tiny = 1;
    for (int i = 0; i < 100; i++)
        tiny /= 2;
    // 113 bits of precision: 1 + 2^-100 differs from 1.
    if (ld_sub(1 + tiny, 1) != tiny) return 1;
    if (ld_sub(2.5L, 0.25L) != 2.25L) return 2;
    if (ld_split(3, 0, 0, 0, 0, 0, 4, 1.5L) != 9) return 3;
    if (ld_stack(0, 0, 0, 0, 0, 0, 0, 0, 4, 10) != 2.5L) return 4;
    if (ld_vsum(3, 1.5L, tiny, 2.0L) != 3.5L + tiny) return 5;
    if (ld_vmix(1, 2, 3, 0.5L, 7, 9.0L) != 5715) return 6;
    struct LD s = { 1.25L };
    s = ld_struct(s, 4);
    if (s.x != 5) return 7;
    struct A16 t = { 3, 4 };
    if (a16_stack(0, 0, 0, 0, 0, 0, 0, 0, 5, t) != 534) return 8;
    if (a16_va(2, t) != 234) return 9;
    if (ld_conv(0.5, 0.25f, -3, 1ULL << 63) != (long double)(1ULL << 63) - 2.25L) return 10;
    if (ld_back(-7.75L, 0) != -7) return 11;
    if (ld_back(1e18L, 1) != 1000000000000000000) return 12;
    if (ld_back(2.5L, 2) != 10) return 13;
    long double x = third() / 3;
    if (x * 3 != 1 || -x >= 0 || !(x > 0.333L) || x == (double)x) return 14;
    return 0;
}
)";

// long double arguments in a register pair, split between a7 and the stack, aligned on
// the stack and variadic in an aligned pair; so is a struct aligned to 16.
TEST_F(Float128Test, InteropWeCallClang)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunWithClang(kLdCaller, kLdCallee));
    EXPECT_EQ(0, exit_status);
}

TEST_F(Float128Test, InteropClangCallsUs)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunWithClang(kLdCallee, kLdCaller));
    EXPECT_EQ(0, exit_status);
}

// An operation is a call to the runtime, with operands in register pairs.  A long
// double lives in a 16-byte slot; the reloads of what was just stored go.
#if RISCV_TEST_XLEN == 64
TEST_F(Float128Test, CallsRuntime)
{
    EXPECT_EQ(R"(addi sp, sp, -64
sd ra, 56(sp)
sd a0, 32(sp)
sd a1, 40(sp)
sd a2, 16(sp)
sd a3, 24(sp)
call __addtf3
sd a0, 0(sp)
sd a1, 8(sp)
ld ra, 56(sp)
addi sp, sp, 64
ret
)",
              Code(CompileToRiscv("long double f(long double a, long double b) { return a + b; }")));
}
#else
// On rv32 the operands go by reference to copies, the result through a hidden pointer.
TEST_F(Float128Test, CallsRuntime)
{
    std::string s = Code(CompileToRiscv("long double f(long double a, long double b) { return a + b; }"));
    EXPECT_NE(std::string::npos, s.find("call __addtf3\n")) << s;
    EXPECT_EQ(std::string::npos, s.find("ld ")) << s;
}
#endif

// printf's L modifier, printed with double precision; a static long double.
TEST_F(Float128Test, Printf)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("2.500 -0.125 1e+100 7\n", CompileAndRunRiscv(R"(
#include <stdio.h>
static long double big = 1e100L;
int main(void)
{
    long double x = -1;
    printf("%.3Lf %Lg %Lg %d\n", 2.5L, x / 8, big, 7);
    return 0;
}
)"));
}

// A conversion is signed or unsigned by its kind, though copy propagation leaves an
// unsigned operand where a cast to long was.
TEST_F(Float128Test, ConvertSignedness)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("-3 -3\n", CompileAndRunRiscv(R"(
#include <stdio.h>
double d(unsigned long u) { return (double)(long)u; }
long double ld(unsigned long u) { return (long double)(long)u; }
int main(void)
{
    printf("%g %Lg\n", d(-3UL), ld(-3UL));
    return 0;
}
)"));
}

// Constants: each side checks the other's literals, folded expressions and static
// initializers against its own, bit for bit.
static const std::string kConstDecls = R"(
int ld_consts(long double tenth, long double max, long double min, long double tmin,
              long double eps, long double third, long double neg, long double big);
)";

static const std::string kConstCheck = kConstDecls + R"(
#include <float.h>
static long double s_third = 1.0L / 3;
static long double s_neg   = -0.1L * 7;
int ld_consts(long double tenth, long double max, long double min, long double tmin,
              long double eps, long double third, long double neg, long double big)
{
    if (tenth != 0.1L) return 1;
    if (max != LDBL_MAX) return 2;
    if (min != LDBL_MIN) return 3;
    if (tmin != LDBL_TRUE_MIN) return 4;
    if (eps != LDBL_EPSILON || 1 + eps == 1) return 5;
    if (third != s_third) return 6;
    if (neg != s_neg) return 7;
    if (big != 123456789012345678901234567890.0L) return 8;
    return 0;
}
)";

static const std::string kConstCall = kConstDecls + R"(
#include <float.h>
int main(void)
{
    return ld_consts(0.1L, LDBL_MAX, LDBL_MIN, LDBL_TRUE_MIN, LDBL_EPSILON, 1.0L / 3,
                     -0.1L * 7, 123456789012345678901234567890.0L);
}
)";

TEST_F(Float128Test, ConstantsWeCallClang)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunWithClang(kConstCall, kConstCheck));
    EXPECT_EQ(0, exit_status);
}

TEST_F(Float128Test, ConstantsClangCallsUs)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunWithClang(kConstCheck, kConstCall));
    EXPECT_EQ(0, exit_status);
}
