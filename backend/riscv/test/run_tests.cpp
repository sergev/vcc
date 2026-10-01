//
// RISC-V programs run on bare-metal qemu.
//
#include "riscv_test.h"

TEST_F(RiscvTest, RunReturn2)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv("int main(void) { return 2; }"));
    EXPECT_EQ(2, exit_status);
}

TEST_F(RiscvTest, RunBookStatus)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("-7\n", CompileAndRunBook("int main(void) { return -7; }"));
}

TEST_F(RiscvTest, RunLargeFrame)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = "int main(void) {\n";
    for (int i = 0; i < 600; i++)
        src += "    int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    src += "    int t = v599;\n    v0 = t;\n    return v0;\n}\n";
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunRiscv(src));
    EXPECT_EQ(599 & 255, exit_status);
}

// 32-bit values stay sign-extended; unsigned division and widening are exact.
TEST_F(RiscvTest, RunIntegerWidths)
{
    SKIP_IF_NO_RISCV_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
int main(void) {
    unsigned u = 4000000000u;
    unsigned long w = u;
    long l = -3;
    int i = -7;
    unsigned char c = 200;
    return (w != 4000000000ul) | (u / 3u != 1333333333u) << 1 |
           (l * 1000000000000l != -3000000000000l) << 2 | (i % 3 != -1) << 3 |
           ((unsigned)i >> 28 != 15u) << 4 | (c + 100 != 300) << 5 | !(u > 3000000000u) << 6;
})"));
    EXPECT_EQ(0, exit_status);
}

// Ten arguments: a0-a7, then two on the stack; narrow ones arrive extended.
TEST_F(RiscvTest, RunCalls)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
long sum(signed char a, int b, long c, unsigned d, short e, long f, int g, long h, int i, long j)
{
    return a + b + c + d + e + f + g + h + i * 1000 + j * 100000;
}
int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
int twice(int (*f)(int), int x) { return f(f(x)); }
int inc(int x) { return x + 1; }
int main(void) {
    if (sum(-1, 2, 3, 4u, -5, 6, 7, 8, 9, 10) != 1009024)
        return 1;
    if (fact(10) != 3628800)
        return 2;
    if (twice(inc, 40) != 42)
        return 3;
    return 0;
})"));
    EXPECT_EQ(0, exit_status);
}

// A constant truncated to a 16-bit short is not folded as a char.
TEST_F(RiscvTest, RunShortTruncation)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
int main(void) {
    short s = 70000;
    short t = -5;
    unsigned short u = -1;
    return (s != 4464) | (t != -5) << 1 | (u != 65535) << 2;
})"));
    EXPECT_EQ(0, exit_status);
}

// A case constant converts to the controlling type: with an int controlling
// expression, `case 8589934592l` is `case 0`.
TEST_F(RiscvTest, RunSwitchCaseConversion)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
int f(int i) {
    switch (i) {
    case 8589934592l:
        return 1;
    case 4294967295u:
        return 2;
    default:
        return 3;
    }
}
int g(unsigned long u) {
    switch (u) {
    case 4294967295u:
        return 1;
    default:
        return 2;
    }
}
int main(void) {
    return (f(0) != 1) | (f(-1) != 2) << 1 | (g(4294967295ul) != 1) << 2 | (g(-1) != 2) << 3;
})"));
    EXPECT_EQ(0, exit_status);
}

// E1 op= E2 is E1 = E1 op E2 in the common type (C11 6.5.16.2p3).
TEST_F(RiscvTest, RunCompoundAssignCommonType)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
int main(void) {
    int x = 1, b = 2147483647, i = -50;
    x += -0.5;
    b /= -34359738367l;
    i %= 4294967200u;
    return (x != 0) | (b != 0) << 1 | (i != 46) << 2;
})"));
    EXPECT_EQ(0, exit_status);
}

// Globals of each width, and a static local.
TEST_F(RiscvTest, RunGlobals)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
signed char sc = -3;
unsigned short us = 65535;
long big = -8589934592l;
int counter(void) { static int n; return ++n; }
int main(void) {
    counter();
    counter();
    return (sc != -3) | (us != 65535) << 1 | (big / 2 != -4294967296l) << 2 |
           (counter() != 3) << 3;
})"));
    EXPECT_EQ(0, exit_status);
}

// Float and double arithmetic, conversions both ways, FP arguments past fa7 and
// mixed with integers, NaN comparisons.
TEST_F(RiscvTest, RunFloatingPoint)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
double sum(double a, int i, double b, double c, double d, double e, double f, double g,
           double h, double k, float x)
{
    return a + i + b + c + d + e + f + g + h + k + x;
}
int main(void) {
    double zero = 0.0;
    double nan = zero / zero;
    float f = 1.5f;
    unsigned long big = 18446744073709551615ul;
    double d = big;
    return (sum(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0.5f) != 55.5) |
           ((int)-2.7 != -2) << 1 | ((unsigned)3e9 != 3000000000u) << 2 |
           (d != 18446744073709551616.0) << 3 | (f * 2 != 3.0f) << 4 |
           (nan == nan) << 5 | !(nan != nan) << 6 | (nan < 1.0) << 7 |
           ((unsigned long)1e19 != 10000000000000000000ul) << 8;
})"));
    EXPECT_EQ(0, exit_status);
}

// Arrays of each width, pointer arithmetic and differences, chars, strings.
TEST_F(RiscvTest, RunPointersAndArrays)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("hello\nhi\n", CompileAndRunRiscv(R"(
#include <stdio.h>
#include <string.h>
char *msg = "hi";
long sum(long *a, int n) { long s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }
int main(void) {
    long a[5] = { 1, 2, 3, 4, 5 };
    short s[3] = { -1, 2, -3 };
    unsigned char bytes[4] = "abc";
    int m[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    int *p = &m[1][0];
    long *q = a + 4;
    double d[2] = { 0.5, 1.5 };
    puts("hello");
    puts(msg);
    return (sum(a, 5) != 15) | (s[0] + s[2] != -4) << 1 | (bytes[2] != 'c') << 2 |
           (bytes[3] != 0) << 3 | (p[2] != 6) << 4 | (q - a != 4) << 5 |
           (*--q != 4) << 6 | (d[0] + d[1] != 2.0) << 7 | (strlen(msg) != 2) << 8 |
           (strcmp(msg, "hi") != 0) << 9 | (strcmp("a", "b") >= 0) << 10;
})"));
    EXPECT_EQ(0, exit_status);
}

// Structs by value: up to 8 and 16 bytes in registers, larger by reference; returned
// in a0/a1 or through the hidden pointer; copied whole and member by member.
TEST_F(RiscvTest, RunStructs)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
struct small { char c; int i; };
struct pair { long a; double d; };
struct odd { char c[13]; };
struct big { long x, y, z; };
union u { long l; char c; };
struct small mk_small(int i) { struct small s = { 'x', i }; return s; }
struct pair swap(struct pair p) { struct pair r = { (long)p.d, (double)p.a }; return r; }
struct odd shift(struct odd o) { for (int i = 0; i < 12; i++) o.c[i] = o.c[i + 1]; return o; }
struct big twice(struct big b) { b.x *= 2; b.y *= 2; b.z *= 2; return b; }
long many(long a, long b, long c, long d, long e, long f, long g, struct pair p, struct big q)
{
    return a + b + c + d + e + f + g + p.a + q.z;
}
int main(void) {
    struct small s = mk_small(7);
    struct pair p = { 3, 4.5 };
    struct pair q = swap(p);
    struct odd o = { "abcdefghijkl" };
    struct odd o2 = shift(o);
    struct big b = { 1, 2, 3 };
    struct big b2 = twice(b);
    struct big *bp = &b2;
    union u un;
    un.l = 0x4142;
    return (s.c != 'x' || s.i != 7) | (q.a != 4 || q.d != 3.0) << 1 |
           (o2.c[0] != 'b' || o2.c[10] != 'l' || o2.c[11] != 0) << 2 |
           (b2.z != 6 || b.z != 3) << 3 | (bp->y != 4) << 4 | (un.c != 0x42) << 5 |
           (many(1, 2, 3, 4, 5, 6, 7, p, b) != 34) << 6;
})"));
    EXPECT_EQ(0, exit_status);
}

// Structs flattening to one or two scalars, some in FP registers: arguments, results,
// FP registers running out, and structs that do not qualify.
TEST_F(RiscvTest, RunFloatStructs)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
struct f1 { float x; };
struct fi { float f; int i; };
struct cd { char c; double d; };
struct a2 { float v[2]; };
struct f3 { float a, b, c; };
union u { float f; int i; };
double f1(struct f1 s) { return s.x; }
double fi(struct fi s) { return s.f + s.i * 100; }
double cd(struct cd s) { return s.c + s.d; }
double a2(struct a2 s) { return s.v[0] - s.v[1]; }
double f3(struct f3 s) { return s.a + s.b + s.c; }
double u(union u s) { return s.f; }
double many(double a, double b, double c, double d, double e, double f, double g, struct fi s)
{
    return a + b + c + d + e + f + g + s.f * 1000 + s.i * 100;
}
struct cd rcd(char c) { struct cd r = { c, 2.25 }; return r; }
struct a2 ra2(void) { struct a2 r = { { 1, 2 } }; return r; }
int main(void) {
    struct f1 s1 = { 1.5f };
    struct fi s2 = { 0.25f, 3 };
    struct cd s3 = { 5, 0.5 };
    struct a2 s4 = { { 4, 1 } };
    struct f3 s5 = { 1, 2, 4 };
    union u s6;
    s6.f = 9;
    struct cd r1 = rcd(3);
    struct a2 r2 = ra2();
    return (f1(s1) != 1.5) | (fi(s2) != 300.25) << 1 | (cd(s3) != 5.5) << 2 |
           (a2(s4) != 3) << 3 | (f3(s5) != 7) << 4 | (u(s6) != 9) << 5 |
           (many(1, 1, 1, 1, 1, 1, 1, s2) != 557) << 6 | (r1.c + r1.d != 5.25) << 7 |
           (r2.v[1] != 2) << 8;
})"));
    EXPECT_EQ(0, exit_status);
}

// <stdarg.h>: integer, FP and pointer arguments, named parameters filling the
// registers, and variadic ones spilling to the stack.
TEST_F(RiscvTest, RunStdarg)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
#include <stdarg.h>
long isum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    long s = 0;
    for (int i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}
double dsum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    double s = 0;
    for (int i = 0; i < n; i++)
        s += va_arg(ap, double);
    va_end(ap);
    return s;
}
long mix(char c, ...) {
    va_list ap;
    va_start(ap, c);
    long a = va_arg(ap, long);
    double d = va_arg(ap, double);
    char *p = va_arg(ap, char *);
    int i = va_arg(ap, int);
    va_end(ap);
    return c + a + (long)d + p[1] + i;
}
long late(long a, long b, long c, long d, long e, long f, long g, long h, long k, ...) {
    va_list ap;
    va_start(ap, k);
    long x = va_arg(ap, long);
    long y = va_arg(ap, long);
    va_end(ap);
    return a + b + c + d + e + f + g + h + k + x * 100 + y * 1000;
}
double fnamed(double q, int n, ...) {
    va_list ap;
    va_start(ap, n);
    double s = q;
    for (int i = 0; i < n; i++)
        s += va_arg(ap, double);
    va_end(ap);
    return s;
}
int main(void) {
    float f = 1.25f;
    return (isum(4, 1, 2, -3, 40) != 40) | (dsum(3, 1.5, 2.25, 4.0) != 7.75) << 1 |
           (mix(1, 20L, 3.5, "xyz", 5) != 1 + 20 + 3 + 'y' + 5) << 2 |
           (late(1, 1, 1, 1, 1, 1, 1, 1, 1, 2L, 3L) != 3209) << 3 |
           (fnamed(0.5, 2, 1.0, 2.0) != 3.5) << 4 |
           (dsum(9, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, f) != 9.25) << 5;
})"));
    EXPECT_EQ(0, exit_status);
}

// The C library: ldexp/frexp at the edges of binary64, and some of libc/common.
TEST_F(RiscvTest, RunLibc)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("hello\n", CompileAndRunRiscv(R"PROG(
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    int e;
    double tiny = ldexp(1.0, -1074);         // the smallest subnormal
    double m    = frexp(tiny, &e);
    char buf[16] = "abcdef";
    memmove(buf + 2, buf, 4);                // overlapping: "ababcd"
    char words[] = "one, two";
    char *w1 = strtok(words, ", ");
    char *w2 = strtok(0, ", ");
    puts("hello");
    return (ldexp(3.0, 4) != 48.0) | (ldexp(1.0, 1024) != ldexp(2.0, 1023)) << 1 |
           (tiny == 0 || tiny * 0.5 != 0) << 2 | (m != 0.5 || e != -1073) << 3 |
           (frexp(-12.0, &e) != -0.75 || e != 4) << 4 | (strcmp(buf, "ababcd") != 0) << 5 |
           (strcmp(w1, "one") || strcmp(w2, "two")) << 6 |
           (atoi("  -42x") != -42) << 7 | (strcmp(strstr("haystack", "st"), "stack") != 0) << 8 |
           (fma(2.0, 3.0, 1.0) != 7.0 || fmax(1.0, 2.0) != 2.0) << 9;
})PROG"));
    EXPECT_EQ(0, exit_status);
}
