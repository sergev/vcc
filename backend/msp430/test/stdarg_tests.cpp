//
// MSP430 variadic functions: every argument of a variadic call on the stack, a structure
// as its address, va_list a pointer walk over them; across to GCC both ways, a va_list
// too; and printf.
//
#include <cstdio>

#include "msp430_test.h"

// A variadic call stores all its arguments in the outgoing area, named ones included.
TEST_F(Msp430Test, VariadicCallAllOnStack)
{
    std::string s = Code(CompileToMsp430("int v(int n, ...);\nint f(void) { return v(1, 2L); }"));
    EXPECT_NE(std::string::npos, s.find(R"(mov #1, 0(r1)
mov #2, 2(r1)
clr 4(r1)
call #v
)")) << s;
}

// A structure in the variadic part goes as its address.
TEST_F(Msp430Test, VariadicCallStructAddress)
{
    std::string s = Code(CompileToMsp430(R"(
        struct S { int a, b; };
        int v(int n, ...);
        int f(struct S *p) { return v(1, *p); }
    )"));
    EXPECT_NE(std::string::npos, s.find(R"(mov #1, 0(r1)
mov r1, r15
add #4, r15
mov r15, 2(r1)
call #v
)")) << s;
}

// A variadic function stores nothing: its parameters live where they came in.
TEST_F(Msp430Test, VariadicFunctionStoresNothing)
{
    std::string s = Code(CompileToMsp430("int v(int n, ...) { return n; }"));
    EXPECT_EQ(std::string::npos, s.find("r12, ")) << s;
}

// The variadic functions both sides call: sums of every kind of argument.
static const char va_decls[] = R"(
#include <stdarg.h>
void putbyte(int c);
struct S { int a; long b; char c; };
long vsum(int n, ...);
long vlist(int n, va_list ap);
long vwrap(int n, ...);
double vdouble(signed char tag, int n, ...);
)";

static const char va_defs[] = R"(
long vsum(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long s = 0;
    for (int i = 0; i < n; i++) {
        switch (va_arg(ap, int)) {
        case 'i': s += va_arg(ap, int); break;
        case 'l': s += va_arg(ap, long); break;
        case 'q': s += (long)(va_arg(ap, long long) >> 20); break;
        case 'c': s += va_arg(ap, int); break;
        case 'd': s += (long)va_arg(ap, double); break;
        case 'p': s += *va_arg(ap, int *); break;
        case 's': {
            struct S t = va_arg(ap, struct S);
            s += t.a + t.b + t.c;
            break;
        }
        }
    }
    va_end(ap);
    return s;
}
long vlist(int n, va_list ap)
{
    long s = 0;
    while (n-- > 0)
        s += va_arg(ap, long);
    return s;
}
long vwrap(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long s = vlist(n, ap);
    va_end(ap);
    return s;
}
double vdouble(signed char tag, int n, ...)
{
    va_list ap;
    va_start(ap, n);
    double s = tag;
    while (n-- > 0)
        s += va_arg(ap, double);
    va_end(ap);
    return s;
}
)";

static const char va_main[] = R"(
int main(void)
{
    int k = 1000;
    signed char c = -5;
    float f = 2.5f;
    struct S t = { 7, 70000L, 'x' };
    if (vsum(8, 'i', 300, 'l', 100000L, 'q', 3LL << 40, 'c', c, 'd', 1e6, 'p', &k,
             'd', (double)f, 's', t) != 300 + 100000 + (3L << 20) - 5 + 1000000 + 1000 + 2
                                          + 7 + 70000L + 'x')
        return 1;
    if (t.a != 7 || t.b != 70000L)
        return 4;
    if (vwrap(3, 1L, 20L, 300L) != 321) return 2;
    if (vdouble(-1, 2, 0.5, 0.25) != -0.25) return 3;
    putbyte('o');
    putbyte('k');
    return 0;
}
)";

TEST_F(Msp430Test, RunVariadic)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("ok", CompileAndRunMsp430(std::string(va_decls) + va_defs + va_main));
    EXPECT_EQ(0, exit_status);
}

// Our variadic functions called from GCC's code, a va_list handed across.
TEST_F(Msp430Test, RunVariadicCalledByGcc)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("ok", GccRun(std::string(va_decls) + va_main,
                           CompileToMsp430((std::string(va_decls) + va_defs).c_str())));
    EXPECT_EQ(0, exit_status);
}

// GCC's variadic functions called from ours.
TEST_F(Msp430Test, RunVariadicCallGcc)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("ok", GccRun(std::string(va_decls) + va_defs,
                           CompileToMsp430((std::string(va_decls) + va_main).c_str())));
    EXPECT_EQ(0, exit_status);
}

// A va_list made by GCC's va_start, walked by our vlist, and the reverse.
TEST_F(Msp430Test, RunVaListAcross)
{
    SKIP_IF_NO_MSP430_TOOLS();
    std::string decls = std::string(va_decls) + R"(long cwrap(int n, ...);
long clist(int n, va_list ap);
)";
    std::string ours = decls + R"(
long vlist(int n, va_list ap)
{
    long s = 0;
    while (n-- > 0)
        s += va_arg(ap, long);
    return s;
}
long vwrap(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long s = clist(n, ap);
    va_end(ap);
    return s;
}
int main(void)
{
    if (cwrap(2, 7L, 70000L) != 70007) return 1;
    if (vwrap(2, 9L, 90000L) != 90009 * 2) return 2;
    putbyte('o');
    putbyte('k');
    return 0;
}
)";
    std::string gcc = decls + R"(
long cwrap(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long s = vlist(n, ap);
    va_end(ap);
    return s;
}
long clist(int n, va_list ap)
{
    long s = 0;
    while (n-- > 0)
        s += 2 * va_arg(ap, long);
    return s;
}
)";
    EXPECT_EQ("ok", GccRun(gcc, CompileToMsp430(ours.c_str())));
    EXPECT_EQ(0, exit_status);
}

// printf from libc.a, compiled by genmsp430, against the host's formatting.
TEST_F(Msp430Test, RunPrintf)
{
    SKIP_IF_NO_MSP430_TOOLS();
    char expected[512];
    snprintf(expected, sizeof expected,
             "[%d] [%u] [%ld] [%lu] [%lld] [%x] [%X] [%o] [%5d|%-5d|%05d] [%s|%8s|%-8s|%.2s] "
             "[%c%c] [%zu %td] [%hhd %hd] [%%] [%.3f] [%e] [%g] [%g] [%8.2f]\n",
             -32768, 65535u, -2147483647L - 1, 4294967295UL, -1234567890123LL, 0xbeef, 0xbeef,
             0777, 42, 42, -42, "msp", "msp", "msp", "msp", 'o', 'k', (size_t)65535,
             (ptrdiff_t)-3, (signed char)-1, (short)-2, 1.5, 0.125, 1024.0, 0.0001, -3.25);
    std::string out = CompileAndRunMsp430(R"(
#include <stdio.h>
#include <stddef.h>
int main(void)
{
    printf("[%d] [%u] [%ld] [%lu] [%lld] [%x] [%X] [%o] [%5d|%-5d|%05d] [%s|%8s|%-8s|%.2s] "
           "[%c%c] [%zu %td] [%hhd %hd] [%%] [%.3f] [%e] [%g] [%g] [%8.2f]\n",
           -32767 - 1, 65535u, -2147483647L - 1, 4294967295UL, -1234567890123LL, 0xbeef,
           0xbeef, 0777, 42, 42, -42, "msp", "msp", "msp", "msp", 'o', 'k', (size_t)65535,
           (ptrdiff_t)-3, (signed char)-1, (short)-2, 1.5, 0.125, 1024.0, 0.0001, -3.25);
    char buf[16];
    int n = snprintf(buf, sizeof buf, "%s-%d", "abcdefghij", 12345);
    printf("%s %d\n", buf, n);
    sprintf(buf, "%04x", 0xab);
    puts(buf);
    return 0;
}
)");
    EXPECT_EQ(std::string(expected) + R"(abcdefghij-1234 16
00ab
)", out);
    EXPECT_EQ(0, exit_status);
}
