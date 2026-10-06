//
// ILP32D interoperation with clang, as interop_tests.cpp: what differs from LP64D.  A
// long long or a double where an integer goes is a register pair (an even one when
// variadic, split between a7 and the stack when only a7 is left); a long double goes
// by reference; a result that would is written through a hidden pointer in a0; a
// struct of two doubles is 16 bytes and still goes in FP registers.  Built into
// riscv32-tests only.
//
#include "riscv_test.h"

static_assert(RISCV_TEST_XLEN == 32, "interop32_tests.cpp belongs in riscv32-tests");

static const std::string kDecls = R"(
typedef long long i64;
struct L1 { i64 x; };
struct DD { double a, b; };
struct DF { double d; float f; };
struct I3 { int a, b, c; };
struct C9 { char c[9]; };
struct LD { long double x; };
i64 ll_add(i64 a, int b, i64 c);
i64 ll_split(int a, int b, int c, int d, int e, int f, int g, i64 x);
i64 ll_stack(int a, int b, int c, int d, int e, int f, int g, int h, int k, i64 x);
double d_ints(double a, double b, double c, double d, double e, double f, double g, double h,
              double x, int i, double y);
double d_split(int a, int b, int c, int d, int e, int f, int g, double p, double q, double r,
               double s, double t, double u, double v, double w, double x);
i64 v_ll(int n, ...);
double v_d(int n, ...);
long double ld_add(long double a, long double b);
long double ld_mix(int a, long double b, double c, i64 d);
struct LD ld_struct(struct LD s, int k);
long double v_ld(int n, ...);
struct DD r_dd(double a);
struct DF r_df(double d, float f);
struct I3 r_i3(int a);
struct C9 r_c9(char c);
struct L1 r_l1(i64 x);
i64 s_l1(struct L1 s, int i, struct L1 t);
double s_dd(struct DD s, struct DF t);
int s_i3(struct I3 s, struct C9 c);
)";

static const std::string kCallee = kDecls + R"(
#include <stdarg.h>
i64 ll_add(i64 a, int b, i64 c) { return a + b + c; }
i64 ll_split(int a, int b, int c, int d, int e, int f, int g, i64 x)
{
    return a + b + c + d + e + f + g + x;
}
i64 ll_stack(int a, int b, int c, int d, int e, int f, int g, int h, int k, i64 x)
{
    return a + b + c + d + e + f + g + h + k * 100 + x;
}
double d_ints(double a, double b, double c, double d, double e, double f, double g, double h,
              double x, int i, double y)
{
    return a + b + c + d + e + f + g + h + x * 10 + i * 100 + y * 1000;
}
double d_split(int a, int b, int c, int d, int e, int f, int g, double p, double q, double r,
               double s, double t, double u, double v, double w, double x)
{
    return a + b + c + d + e + f + g + p + q + r + s + t + u + v + w + x * 100;
}
i64 v_ll(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    i64 s = 0;
    for (int i = 0; i < n; i++)
        s = s * 10 + va_arg(ap, int) + va_arg(ap, i64);
    va_end(ap);
    return s;
}
double v_d(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    double s = 0;
    for (int i = 0; i < n; i++)
        s = s * 10 + va_arg(ap, double) + va_arg(ap, int);
    va_end(ap);
    return s;
}
long double ld_add(long double a, long double b) { return a + b; }
long double ld_mix(int a, long double b, double c, i64 d) { return a + b * 10 + c * 100 + d * 1000; }
struct LD ld_struct(struct LD s, int k) { s.x = s.x * k; return s; }
long double v_ld(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long double s = 0;
    for (int i = 0; i < n; i++)
        s = s * 10 + va_arg(ap, long double);
    va_end(ap);
    return s;
}
struct DD r_dd(double a) { struct DD r = { a, a + 1 }; return r; }
struct DF r_df(double d, float f) { struct DF r = { d, f }; return r; }
struct I3 r_i3(int a) { struct I3 r = { a, a + 1, a + 2 }; return r; }
struct C9 r_c9(char c) { struct C9 r; for (int i = 0; i < 9; i++) r.c[i] = c + i; return r; }
struct L1 r_l1(i64 x) { struct L1 r = { x * 2 }; return r; }
i64 s_l1(struct L1 s, int i, struct L1 t) { return s.x + i + t.x * 10; }
double s_dd(struct DD s, struct DF t) { return s.a + s.b * 10 + t.d * 100 + t.f * 1000; }
int s_i3(struct I3 s, struct C9 c) { return s.a + s.b * 10 + s.c * 100 + c.c[8] * 1000; }
)";

static const std::string kCaller = kDecls + R"(
int main(void)
{
    i64 big = 0x100000000LL;
    if (ll_add(big, 2, 3 * big) != 4 * big + 2) return 1;
    if (ll_split(1, 1, 1, 1, 1, 1, 1, big) != big + 7) return 2;
    if (ll_stack(1, 1, 1, 1, 1, 1, 1, 1, 2, big) != big + 208) return 3;
    if (d_ints(1, 1, 1, 1, 1, 1, 1, 1, 2.5, 3, 4.5) != 4833) return 4;
    if (d_split(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0.5) != 65) return 5;
    if (v_ll(2, 1, big, 2, -1LL) != big * 10 + 11) return 6;
    if (v_ll(3, 1, 1LL, 2, 2LL, 3, 3LL) != 246) return 7;
    if (v_d(3, 0.5, 1, 1.5, 2, 2.5, 3) != 190.5) return 8;
    if (ld_add(1.25L, 2.5L) != 3.75L) return 9;
    if (ld_mix(1, 2.0L, 3.0, 4) != 4321) return 10;
    struct LD s = { 1.5L };
    struct LD t = ld_struct(s, 4);
    if (t.x != 6 || s.x != 1.5L) return 11;
    if (v_ld(3, 1.0L, 2.0L, 3.5L) != 123.5L) return 12;
    struct DD dd = r_dd(1.5);
    if (dd.a != 1.5 || dd.b != 2.5) return 13;
    struct DF df = r_df(2.5, 0.25f);
    if (df.d != 2.5 || df.f != 0.25f) return 14;
    struct I3 i3 = r_i3(5);
    if (i3.a != 5 || i3.b != 6 || i3.c != 7) return 15;
    struct C9 c9 = r_c9('a');
    if (c9.c[0] != 'a' || c9.c[8] != 'i') return 16;
    struct L1 l1 = r_l1(big);
    if (l1.x != 2 * big) return 17;
    if (s_l1(l1, 3, l1) != 22 * big + 3) return 18;
    if (s_dd(dd, df) != 526.5) return 19;
    if (s_i3(i3, c9) != 105765) return 20;
    r_i3(1); // the result unused
    return 0;
}
)";

TEST_F(RiscvTest, Interop32WeCallClang)
{
    SKIP_IF_NO_RISCV_TOOLS();
    SKIP_IF_NO_RISCV_CLANG();
    EXPECT_EQ("", CompileAndRunWithClang(kCaller, kCallee));
    EXPECT_EQ(0, exit_status);
}

TEST_F(RiscvTest, Interop32ClangCallsUs)
{
    SKIP_IF_NO_RISCV_TOOLS();
    SKIP_IF_NO_RISCV_CLANG();
    EXPECT_EQ("", CompileAndRunWithClang(kCallee, kCaller));
    EXPECT_EQ(0, exit_status);
}

TEST_F(RiscvTest, Interop32Ours)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(kCallee + kCaller.substr(kDecls.size())));
    EXPECT_EQ(0, exit_status);
}
