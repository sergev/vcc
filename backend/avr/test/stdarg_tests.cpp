//
// AVR variadic functions: every argument of a variadic call on the stack, va_list a
// pointer walk over them; across to clang both ways, a va_list too; and printf.
//
#include <cstdio>

#include "avr_test.h"

// A variadic call pushes all its arguments, named ones included.
TEST_F(AvrTest, VariadicCallAllOnStack)
{
    std::string s = Body(CompileToAvr("int v(int n, ...);\nint f(void) { return v(1, 2L); }"));
    EXPECT_NE(std::string::npos,
              s.find("ldi r22, 2\nldi r23, 0\nldi r24, 0\nldi r25, 0\npush r25\npush r24\n"
                     "push r23\npush r22\nldi r24, 1\nldi r25, 0\npush r25\npush r24\ncall v\n"))
        << s;
}

// A variadic function stores nothing: its parameters live where they came in.
TEST_F(AvrTest, VariadicFunctionStoresNothing)
{
    std::string s = Body(CompileToAvr("#include <stdarg.h>\n"
                                      "int v(int n, ...) { return n; }"));
    EXPECT_EQ("ldd r24, Y+5\nldd r25, Y+6\n", s);
}

// The variadic functions both sides call: sums of every kind of argument.
static const char va_decls[] = R"(
#include <stdarg.h>
void putbyte(int c);
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
    if (vsum(7, 'i', 300, 'l', 100000L, 'q', 3LL << 40, 'c', c, 'd', 1e6, 'p', &k,
             'd', (double)f) != 300 + 100000 + (3L << 20) - 5 + 1000000 + 1000 + 2)
        return 1;
    if (vwrap(3, 1L, 20L, 300L) != 321) return 2;
    if (vdouble(-1, 2, 0.5, 0.25) != -0.25) return 3;
    putbyte('o');
    putbyte('k');
    return 0;
}
)";

TEST_F(AvrTest, RunVariadic)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("ok", CompileAndRunAvr(std::string(va_decls) + va_defs + va_main));
    EXPECT_EQ(0, exit_status);
}

// Our variadic functions called from clang's code, a va_list handed across.
TEST_F(AvrTest, RunVariadicCalledByClang)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("ok", CompileAndRunWithClang(std::string(va_decls) + va_defs,
                                           std::string(va_decls) + va_main));
    EXPECT_EQ(0, exit_status);
}

// clang's variadic functions called from ours.
TEST_F(AvrTest, RunVariadicCallClang)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("ok", CompileAndRunWithClang(std::string(va_decls) + va_main,
                                           std::string(va_decls) + va_defs));
    EXPECT_EQ(0, exit_status);
}

// A va_list made by clang's va_start, walked by our vlist, and the reverse.
TEST_F(AvrTest, RunVaListAcross)
{
    SKIP_IF_NO_AVR_TOOLS();
    std::string decls = std::string(va_decls) + "long cwrap(int n, ...);\nlong clist(int n, va_list ap);\n";
    EXPECT_EQ("ok", CompileAndRunWithClang(decls + R"(
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
)",
                                           decls + R"(
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
)"));
    EXPECT_EQ(0, exit_status);
}

// printf from libc.a, compiled by genavr, against the host's formatting.
TEST_F(AvrTest, RunPrintf)
{
    SKIP_IF_NO_AVR_TOOLS();
    char expected[512];
    snprintf(expected, sizeof expected,
             "[%d] [%u] [%ld] [%lu] [%lld] [%x] [%X] [%o] [%5d|%-5d|%05d] [%s|%8s|%-8s|%.2s] "
             "[%c%c] [%zu %td] [%hhd %hd] [%%] [%.3f] [%e] [%g] [%g] [%8.2f]\n",
             -32768, 65535, -2147483647L - 1, 4294967295UL, -1234567890123LL, 0xbeef, 0xbeef,
             0777, 42, 42, -42, "avr", "avr", "avr", "avr", 'o', 'k', (size_t)65535,
             (ptrdiff_t)-3, (signed char)-1, (short)-2, 1.5, 0.125, 1024.0, 0.0001, -3.25);
    std::string out = CompileAndRunAvr(R"(
#include <stdio.h>
#include <stddef.h>
int main(void)
{
    printf("[%d] [%u] [%ld] [%lu] [%lld] [%x] [%X] [%o] [%5d|%-5d|%05d] [%s|%8s|%-8s|%.2s] "
           "[%c%c] [%zu %td] [%hhd %hd] [%%] [%.3f] [%e] [%g] [%g] [%8.2f]\n",
           -32767 - 1, 65535u, -2147483647L - 1, 4294967295UL, -1234567890123LL, 0xbeef,
           0xbeef, 0777, 42, 42, -42, "avr", "avr", "avr", "avr", 'o', 'k', (size_t)65535,
           (ptrdiff_t)-3, (signed char)-1, (short)-2, 1.5, 0.125, 1024.0, 0.0001, -3.25);
    char buf[16];
    int n = snprintf(buf, sizeof buf, "%s-%d", "abcdefghij", 12345);
    printf("%s %d\n", buf, n);
    sprintf(buf, "%04x", 0xab);
    puts(buf);
    return 0;
}
)");
    EXPECT_EQ(std::string(expected) + "abcdefghij-1234 16\n00ab\n", out);
    EXPECT_EQ(0, exit_status);
}
