//
// Interoperation with clang: the same callee functions and caller are linked with
// one side compiled by us and the other by clang, in both directions.  The caller
// returns the number of the first check that failed, or 0.
//
#include "riscv_test.h"

static const std::string kDecls = R"(
struct I2 { long a, b; };
struct I3 { int a, b, c; };
struct B { long a, b, c; };
struct FF { float a, b; };
struct DI { double d; int i; };
struct CD { char c; double d; };
struct F3 { float a, b, c; };
struct D1 { double d; };
struct AL { char c; _Alignas(16) long x; };
long ext(unsigned char a, signed char b, unsigned short c, short d, unsigned e, int f);
unsigned char ruc(int x);
short rs(int x);
unsigned ru(long x);
double spill(int i1, double d1, int i2, double d2, int i3, double d3, int i4, double d4,
             int i5, double d5, int i6, double d6, int i7, double d7, int i8, double d8,
             int i9, double d9, int i10, double d10);
float fspill(float f1, float f2, float f3, float f4, float f5, float f6, float f7, float f8,
             float f9, float f10);
long s_i2(struct I2 s);
long s_i3(struct I3 s);
long s_b(struct B s);
double s_ff(struct FF s);
double s_di(struct DI s);
double s_cd(struct CD s);
double s_f3(struct F3 s);
double s_d1(struct D1 s);
long split(long a, long b, long c, long d, long e, long f, long g, struct I2 s);
double fpx(double d1, double d2, double d3, double d4, double d5, double d6, double d7,
           double d8, struct DI s);
double fpy(double d1, double d2, double d3, double d4, double d5, double d6, double d7,
           struct FF s);
struct I2 r_i2(long a);
struct I3 r_i3(int a);
struct B r_b(long a);
struct FF r_ff(float a);
struct DI r_di(double d, int i);
struct CD r_cd(char c, double d);
struct D1 r_d1(double d);
long apply(long (*f)(long), long x);
long twice(long x);
long vsum(int n, ...);
double vdsum(int n, ...);
long vmix(const char *s, ...);
long vstructs(int n, ...);
long s_al(struct AL s);
long p_al(struct AL *p);
)";

static const std::string kCallee = kDecls + R"(
#include <stdarg.h>
long ext(unsigned char a, signed char b, unsigned short c, short d, unsigned e, int f)
{
    return a + b + c + d + (long)e + f;
}
unsigned char ruc(int x) { return x; }
short rs(int x) { return x; }
unsigned ru(long x) { return x; }
double spill(int i1, double d1, int i2, double d2, int i3, double d3, int i4, double d4,
             int i5, double d5, int i6, double d6, int i7, double d7, int i8, double d8,
             int i9, double d9, int i10, double d10)
{
    return i1 + 2 * i2 + 3 * i3 + 4 * i4 + 5 * i5 + 6 * i6 + 7 * i7 + 8 * i8 + 9 * i9 +
           10 * i10 + d1 + 2 * d2 + 3 * d3 + 4 * d4 + 5 * d5 + 6 * d6 + 7 * d7 + 8 * d8 +
           9 * d9 + 10 * d10;
}
float fspill(float f1, float f2, float f3, float f4, float f5, float f6, float f7, float f8,
             float f9, float f10)
{
    return f1 + 2 * f2 + 3 * f3 + 4 * f4 + 5 * f5 + 6 * f6 + 7 * f7 + 8 * f8 + 9 * f9 + 10 * f10;
}
long s_i2(struct I2 s) { return s.a * 10 + s.b; }
long s_i3(struct I3 s) { return s.a * 100 + s.b * 10 + s.c; }
long s_b(struct B s) { return s.a * 100 + s.b * 10 + s.c; }
double s_ff(struct FF s) { return s.a * 10 + s.b; }
double s_di(struct DI s) { return s.d * 10 + s.i; }
double s_cd(struct CD s) { return s.c * 10 + s.d; }
double s_f3(struct F3 s) { return s.a * 100 + s.b * 10 + s.c; }
double s_d1(struct D1 s) { return s.d; }
long split(long a, long b, long c, long d, long e, long f, long g, struct I2 s)
{
    return a + b + c + d + e + f + g + s.a * 100 + s.b * 1000;
}
double fpx(double d1, double d2, double d3, double d4, double d5, double d6, double d7,
           double d8, struct DI s)
{
    return d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8 + s.d * 100 + s.i * 1000;
}
double fpy(double d1, double d2, double d3, double d4, double d5, double d6, double d7,
           struct FF s)
{
    return d1 + d2 + d3 + d4 + d5 + d6 + d7 + s.a * 100 + s.b * 1000;
}
struct I2 r_i2(long a) { struct I2 r = { a, a + 1 }; return r; }
struct I3 r_i3(int a) { struct I3 r = { a, a + 1, a + 2 }; return r; }
struct B r_b(long a) { struct B r = { a, a + 1, a + 2 }; return r; }
struct FF r_ff(float a) { struct FF r = { a, a + 1 }; return r; }
struct DI r_di(double d, int i) { struct DI r = { d, i }; return r; }
struct CD r_cd(char c, double d) { struct CD r = { c, d }; return r; }
struct D1 r_d1(double d) { struct D1 r = { d }; return r; }
long apply(long (*f)(long), long x) { return f(x) + 1; }
long vsum(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long s = 0;
    for (int i = 0; i < n; i++)
        s += va_arg(ap, long);
    va_end(ap);
    return s;
}
double vdsum(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    double s = 0;
    for (int i = 0; i < n; i++)
        s += va_arg(ap, double);
    va_end(ap);
    return s;
}
long vmix(const char *s, ...)
{
    va_list ap;
    va_start(ap, s);
    long r = 0;
    for (; *s; s++) {
        if (*s == 'i')
            r = r * 10 + va_arg(ap, int);
        else if (*s == 'd')
            r = r * 10 + (long)va_arg(ap, double);
        else
            r = r * 10 + *va_arg(ap, char *) - '0';
    }
    va_end(ap);
    return r;
}
long s_al(struct AL s) { return s.c * 10 + s.x; }
long p_al(struct AL *p) { return (char *)&p->x - (char *)p + sizeof(struct AL) * 100; }
long vstructs(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long r = 0;
    for (int i = 0; i < n; i++) {
        struct I3 small = va_arg(ap, struct I3);
        struct B big    = va_arg(ap, struct B);
        r = r * 1000 + small.a * 100 + small.c * 10 + big.c;
    }
    va_end(ap);
    return r;
}
)";

static const std::string kCaller = kDecls + R"(
long twice(long x) { return 2 * x; }
int main(void)
{
    struct I2 i2 = { 1, 2 };
    struct I3 i3 = { 1, 2, 3 };
    struct B b = { 4, 5, 6 };
    struct FF ff = { 1.5f, 2 };
    struct DI di = { 2.5, 3 };
    struct CD cd = { 4, 0.25 };
    struct F3 f3 = { 1, 2, 3 };
    struct D1 d1 = { 7.5 };
    if (ext(200, -3, 60000, -300, 4000000000u, -5) != 4000059892) return 1;
    if (ruc(0x1ff) != 0xff) return 2;
    if (rs(0x18000) != -32768) return 3;
    if (ru(-1) != 4294967295u) return 4;
    if (spill(1, 1.5, 1, 1.5, 1, 1.5, 1, 1.5, 1, 1.5, 1, 1.5, 1, 1.5, 1, 1.5, 1, 1.5, 1, 1.5)
        != 137.5) return 5;
    if (fspill(1, 1, 1, 1, 1, 1, 1, 1, 1, 1.5f) != 60) return 6;
    if (s_i2(i2) != 12) return 7;
    if (s_i3(i3) != 123) return 8;
    if (s_b(b) != 456) return 9;
    if (s_ff(ff) != 17) return 10;
    if (s_di(di) != 28) return 11;
    if (s_cd(cd) != 40.25) return 12;
    if (s_f3(f3) != 123) return 13;
    if (s_d1(d1) != 7.5) return 14;
    if (split(1, 1, 1, 1, 1, 1, 1, i2) != 2107) return 15;
    if (fpx(1, 1, 1, 1, 1, 1, 1, 1, di) != 3258) return 16;
    if (fpy(1, 1, 1, 1, 1, 1, 1, ff) != 2157) return 17;
    struct I2 ri2 = r_i2(5);
    if (ri2.a != 5 || ri2.b != 6) return 18;
    struct I3 ri3 = r_i3(5);
    if (ri3.a != 5 || ri3.c != 7) return 19;
    struct B rb = r_b(5);
    if (rb.a != 5 || rb.c != 7) return 20;
    struct FF rff = r_ff(5);
    if (rff.a != 5 || rff.b != 6) return 21;
    struct DI rdi = r_di(1.25, 9);
    if (rdi.d != 1.25 || rdi.i != 9) return 22;
    struct CD rcd = r_cd(7, 1.75);
    if (rcd.c != 7 || rcd.d != 1.75) return 23;
    struct D1 rd1 = r_d1(3.5);
    if (rd1.d != 3.5) return 24;
    if (apply(twice, 20) != 41) return 25;
    if (vsum(10, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L) != 55) return 26;
    if (vdsum(3, 0.5, 1.25, (double)2.0f) != 3.75) return 27;
    if (vmix("idpi", 1, 2.5, "3", 4) != 1234) return 28;
    if (vstructs(2, i3, b, i3, b) != 136136) return 29;
    struct AL al = { 3, 4 };
    if (s_al(al) != 34) return 30;
    if (p_al(&al) != 3216) return 31;
    return 0;
}
)";

TEST_F(RiscvTest, InteropWeCallClang)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunWithClang(kCaller, kCallee));
    EXPECT_EQ(0, exit_status);
}

TEST_F(RiscvTest, InteropClangCallsUs)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunWithClang(kCallee, kCaller));
    EXPECT_EQ(0, exit_status);
}
