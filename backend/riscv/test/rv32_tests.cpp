// RV32 (ILP32D) code generation: run tests on qemu-system-riscv32, and a few checks of
// the assembly that differs from rv64.  Built into riscv32-tests only.
#include <cstdarg>
#include <cstdlib>

#include "riscv_test.h"

static_assert(RISCV_TEST_XLEN == 32, "rv32_tests.cpp belongs in riscv32-tests");

// The libraries call fatal_error(); defined once for the riscv32-tests binary.
extern "C" void fatal_error(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

using Rv32Test = RiscvTest;

// Decimal output without printf.
static const std::string kPutd = R"(
#include <stdio.h>
static void putd(long n)
{
    char buf[12];
    int i = 0;
    unsigned long u = n < 0 ? 0 - (unsigned long)n : (unsigned long)n;
    if (n < 0)
        putchar('-');
    do {
        buf[i++] = '0' + u % 10;
        u /= 10;
    } while (u);
    while (i > 0)
        putchar(buf[--i]);
    putchar(' ');
}
)";

TEST_F(Rv32Test, Return42)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv("int main(void) { return 42; }"));
    EXPECT_EQ(42, exit_status);
}

TEST_F(Rv32Test, NoDoublewordInstructions)
{
    std::string code = Code(CompileToRiscv(R"(
long g;
long *p = &g;
long f(long *a, long n, unsigned long u) { return a[n] + (long)u + *p + (int)(short)n; }
)"));
    for (const char *op : { "ld ", "sd ", "addw ", "addiw ", "subw ", "sext.w ", "sllw ",
                            "slliw " })
        EXPECT_EQ(std::string::npos, code.find(std::string("\n") + op)) << op << "in:\n" << code;
}

TEST_F(Rv32Test, PointerInitializer)
{
    EXPECT_NE(std::string::npos, CompileToRiscv("int x; int *p = &x;").find(".word   x"));
}

TEST_F(Rv32Test, DoubleLiteral)
{
    std::string s = CompileToRiscv("double f(void) { return 2.5; }\n"
                                   "double g(void) { return 2.5 + 0.0 * f(); }");
    EXPECT_NE(std::string::npos, Code(s).find("la t6, .LC0\nfld fa0, 0(t6)\n")) << s;
    EXPECT_NE(std::string::npos, s.find(".LC0:\n    .word   0x00000000\n    .word   0x40040000\n"))
        << s;
    EXPECT_EQ(std::string::npos, s.find(".LC2")) << "one literal per value and function:\n" << s;
}


TEST_F(Rv32Test, IntegerWidths)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = kPutd + R"(
#include <limits.h>
int main(void)
{
    putd(sizeof(long));
    putd(sizeof(void *));
    putd(sizeof(int (*)(void)));
    putd(LONG_MAX);
    putd(LONG_MIN);
    unsigned long u = ULONG_MAX;
    putd(u + 1 == 0);
    putd((long)(u >> 1));
    signed char c = (signed char)200;
    short h = (short)70000;
    unsigned char uc = (unsigned char)-1;
    unsigned short us = (unsigned short)-1;
    putd(c);
    putd(h);
    putd(uc);
    putd(us);
    long l = -7;
    putd(l / 2);
    putd(l % 2);
    putd((unsigned long)l / 2 > 0x7fffffff);
    putd(l >> 1);
    putd((long)((unsigned long)l >> 28));
    putd(1L << 31 < 0);
    int x = 0x12345678;
    putd(x * 16);
    return 0;
}
)";
    EXPECT_EQ("4 4 4 2147483647 -2147483648 1 2147483647 -56 4464 255 65535 -3 -1 0 -4 15 1 "
              "591751040 ",
              CompileAndRunRiscv(src));
    EXPECT_EQ(0, exit_status);
}

TEST_F(Rv32Test, Calls)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = kPutd + R"(
long sum(signed char a, int b, long c, unsigned d, short e, long f, int g, long h, int i,
         long j, char *k)
{
    return a + b + c + (long)d + e + f + g + h + i + j + *k;
}
long fib(long n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
static long apply(long (*f)(long), long x) { return f(x); }
int main(void)
{
    char k = 100;
    putd(sum(-1, 2, 3, 4, -5, 6, 7, 8, 9, 10, &k));
    putd(fib(20));
    putd(apply(fib, 10));
    return 0;
}
)";
    EXPECT_EQ("143 6765 55 ", CompileAndRunRiscv(src));
}

TEST_F(Rv32Test, PointersAndArrays)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = kPutd + R"(
#include <stddef.h>
long sum(long *a, int n) { long s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }
long table[3] = { 10, 20, 30 };
long *ptrs[2] = { &table[2], &table[0] };
char *name = "rv32";
int main(void)
{
    long a[5] = { 1, 2, 3, 4, 5 };
    long *q = a + 4;
    putd(sum(a, 5));
    putd(q - a);
    putd(*ptrs[0] + *ptrs[1]);
    putd(name[2]);
    char buf[3][5];
    buf[2][4] = 7;
    char (*row)[5] = &buf[2];
    putd((*row)[4]);
    putd(sizeof(size_t));
    putd(sizeof(ptrdiff_t));
    long **pp = &q;
    **pp = -9;
    putd(a[4]);
    unsigned u = 2;
    putd(table[u]);
    return 0;
}
)";
    EXPECT_EQ("15 4 40 51 7 4 4 -9 30 ", CompileAndRunRiscv(src));
}

TEST_F(Rv32Test, Structs)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = kPutd + R"(
struct pair { long a; int b; };         // 8 bytes: two registers
struct three { long x, y, z; };         // 12 bytes: by reference
struct sh { short a, b, c; };           // 6 bytes, align 2
struct fl { float f; long n; };         // an FP and an integer register
struct pair mk(long a, int b) { struct pair p = { a, b }; return p; }
long take(struct pair p, struct three t, struct sh s, struct fl f)
{
    return p.a * 1000 + p.b * 100 + t.x + t.y + t.z + s.a + s.c + (long)f.f + f.n;
}
struct three bump(struct three t) { t.x++; t.z--; return t; }
int main(void)
{
    struct pair p = mk(3, 4);
    struct three t = { 1, 2, 3 };
    struct sh s = { 10, 20, 30 };
    struct fl f = { 2.5f, 7 };
    putd(take(p, t, s, f));
    struct three u = bump(t);
    putd(u.x * 100 + u.y * 10 + u.z);
    putd(sizeof(struct pair));
    putd(sizeof(struct three));
    return 0;
}
)";
    EXPECT_EQ("3455 222 8 12 ", CompileAndRunRiscv(src));
}

TEST_F(Rv32Test, FloatingPoint)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = kPutd + R"(
double half(double x) { return x * 0.5; }
float twice(float x) { return x * 2.0f; }
double mix(int a, double b, float c, long d, double e) { return a + b + c + d + e; }
int main(void)
{
    double d = 3.75;
    putd((long)half(10.0));
    putd((long)twice(1.5f));
    putd((long)(mix(1, 2.5, 0.5f, 4, 100.0) * 2));
    putd((long)(d * 1000));
    putd((long)-d);
    unsigned long u = 4000000000UL;
    double ud = u;
    putd(ud > 3.9e9);
    putd((long)((unsigned long)ud / 1000));
    putd(d == 3.75);
    putd(d != 3.75);
    putd(d < 0.0);
    putd(!d);
    double z = 0.0;
    if (z)
        putd(99);
    putd((long)(1e9 / 3));
    putd((int)(float)d);
    return 0;
}
)";
    EXPECT_EQ("5 3 216 3750 -3 1 4000000 1 0 0 0 333333333 3 ", CompileAndRunRiscv(src));
}

TEST_F(Rv32Test, Stdarg)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = kPutd + R"(
#include <stdarg.h>
long isum(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long s = 0;
    for (int i = 0; i < n; i++)
        s += va_arg(ap, long);
    va_end(ap);
    return s;
}
long mix(char c, ...)
{
    va_list ap;
    va_start(ap, c);
    int a = va_arg(ap, int);
    char *p = va_arg(ap, char *);
    unsigned long u = va_arg(ap, unsigned long);
    va_end(ap);
    return c + a + p[1] + (long)u;
}
long late(long a, long b, long c, long d, long e, long f, long g, long h, long k, ...)
{
    va_list ap;
    va_start(ap, k);
    long x = va_arg(ap, long);
    long y = va_arg(ap, long);
    va_end(ap);
    return a + b + c + d + e + f + g + h + k + x * 100 + y * 1000;
}
int main(void)
{
    putd(isum(4, 1L, 2L, 3L, 4L));
    putd(isum(10, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L));
    putd(mix(1, 2, "AB", 1000UL));
    putd(late(1, 2, 3, 4, 5, 6, 7, 8, 9, 10L, 20L));
    return 0;
}
)";
    EXPECT_EQ("10 55 1069 21045 ", CompileAndRunRiscv(src));
}

TEST_F(Rv32Test, Libc)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = kPutd + R"(
#include <math.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    char *p = malloc(16);
    strcpy(p, "hello");
    strcat(p, ", rv32");
    puts(p);
    putd(strlen(p));
    p = realloc(p, 100);
    memset(p + 20, 'x', 3);
    p[23] = 0;
    puts(p + 20);
    putd(memcmp("abc", "abd", 3) < 0);
    putd(atoi("-1234"));
    int e;
    double f = frexp(48.0, &e);
    putd((long)(f * 1000));
    putd(e);
    putd((long)ldexp(3.0, 10));
    putd(ldexp(1.0, -1074) > 0);
    double ip;
    double fr = modf(-1234567890.75, &ip);
    putd((long)ip);
    putd((long)(fr * 100));
    fr = modf(4503599627370495.5, &ip);
    putd((long)(fr * 10));
    putd((long)(fabs(-2.5) * 2));
    return 0;
}
)";
    EXPECT_EQ("hello, rv32\n11 xxx\n1 -1234 750 6 3072 1 -1234567890 -75 5 5 ",
              CompileAndRunRiscv(src));
}

// alloca on rv32: the memory above two 4-byte stack arguments, rounded to 16.
TEST_F(Rv32Test, AllocaAboveOutgoing)
{
    std::string code = Code(CompileToRiscv(R"(
void *__builtin_alloca(unsigned int);
long g(long, long, long, long, long, long, long, long, long, long);
long f(long n, long k)
{
    long *p = __builtin_alloca(n * sizeof(long));
    p[0] = k;
    return g(1, 2, 3, 4, 5, 6, 7, 8, p[0], k) + p[0];
}
)"));
    EXPECT_NE(std::string::npos, code.find("sub sp, sp, t0\naddi s1, sp, 16\n")) << code;
    EXPECT_NE(std::string::npos, code.find("sw a1, 0(sp)\nsw a1, 4(sp)\n")) << code;
    EXPECT_NE(std::string::npos, code.find("lw s1, -24(s0)\naddi sp, s0, -16\n")) << code;
}

TEST_F(Rv32Test, LargeFrameAndSavedRegisters)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = kPutd + R"(
long id(long x) { return x; }
long big(int k)
{
    long a[1000];
    for (int i = 0; i < 1000; i++)
        a[i] = i;
    long s1 = id(1), s2 = id(2), s3 = id(3), s4 = id(4);
    long t = id(a[k]);
    return s1 + s2 + s3 + s4 + t + a[999];
}
int main(void)
{
    putd(big(500));
    return 0;
}
)";
    EXPECT_EQ("1509 ", CompileAndRunRiscv(src));
}

TEST_F(Rv32Test, Switch)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = kPutd + R"(
long f(unsigned long u)
{
    switch (u) {
    case 0: return 10;
    case 0xffffffffUL: return 20;
    case 7: return 30;
    default: return 40;
    }
}
int main(void)
{
    putd(f(0));
    putd(f(-1));
    putd(f(7));
    putd(f(8));
    return 0;
}
)";
    EXPECT_EQ("10 20 30 40 ", CompileAndRunRiscv(src));
}

TEST_F(Rv32Test, BookStatus)
{
    SKIP_IF_NO_RISCV_TOOLS();
    SKIP_IF_NO_RISCV_CLANG();
    std::string src = "int main(void) { return -17; }";
    EXPECT_EQ("-17\n", CompileAndRunBook(src));
    EXPECT_EQ(ClangRunBook(src), "-17\n");
}

TEST_F(Rv32Test, PrintfLong)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("-2147483648 4294967295 ffffffff 4 12345678901 1.5 2.25\n", CompileAndRunRiscv(R"(
#include <limits.h>
#include <stdio.h>
int main(void)
{
    printf("%ld %lu %lx %zu %lld %g %Lg\n", LONG_MIN, ULONG_MAX, ULONG_MAX, sizeof(long),
           12345678901LL, 1.5, 2.25L);
    return 0;
}
)"));
}

// A long long gets a register pair: these need no stack at all.
TEST_F(Rv32Test, LongLongInRegisters)
{
    std::string s = Code(CompileToRiscv(R"(
long long add(long long a, long long b) { return a + b; }
long long mix(long long a, int i) { long long x = a * i; return x < 0 ? -x : x << 3; }
)"));
    EXPECT_EQ(std::string::npos, s.find("sw ")) << s;
    EXPECT_EQ(std::string::npos, s.find("lw ")) << s;
    EXPECT_EQ(std::string::npos, s.find("sp")) << s;
}

// The peephole takes the copies around a pair: an add reads its operands where they
// are and computes the halves where they go.  Both words of a parameter stay in the
// registers it arrives in, and a result is computed in a0/a1.
TEST_F(Rv32Test, LongLongPairMoves)
{
    std::string s = Code(CompileToRiscv("long long add(long long a, long long b) { return a + b; }"));
    EXPECT_NE(std::string::npos,
              s.find("add a0, a0, a2\nsltu t0, a0, a2\nadd t1, a1, a3\nadd a1, t1, t0\nret\n"))
        << s;
}

TEST_F(Rv32Test, LongLongParamsInPlace)
{
    std::string s = Code(CompileToRiscv("int lt(long long a, long long b) { return a < b; }"));
    EXPECT_EQ(std::string::npos, s.find("mv ")) << s;
}

// A copy of a long long is coalesced as a pair: an accumulator in a loop is added to
// in place.
TEST_F(Rv32Test, LongLongCopyCoalesced)
{
    std::string s = Code(CompileToRiscv(R"(
long long sum(long long *p, int n)
{
    long long s = 0;
    for (int i = 0; i < n; i++)
        s += p[i];
    return s;
}
)"));
    size_t loop = s.find("beqz"), end = s.find("bnez", loop);
    ASSERT_NE(std::string::npos, end) << s;
    EXPECT_EQ(std::string::npos, s.substr(loop, end - loop).find("mv ")) << s;
}
