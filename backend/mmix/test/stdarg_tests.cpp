//
// MMIX variadic functions: the caller passes variable arguments exactly as named ones,
// and the callee stores the argument registers after its named ones at the top of its
// frame, below the incoming stack arguments, so va_list walks one run of 8-byte slots.
//
#include "mmix_test.h"

// A variadic callee stores $1-$15 into the top of its frame, under the incoming stack
// arguments, and va_start points at the first of them; __va_start is no call, so the
// function stays a leaf.
TEST_F(MmixTest, VariadicSavesRegisters)
{
    std::string code = Code(CompileToMmix(R"(
        #include <stdarg.h>
        long v(int n, ...)
        {
            va_list ap;
            va_start(ap, n);
            long x = va_arg(ap, long);
            va_end(ap);
            return x;
        }
    )"));
    EXPECT_NE(std::string::npos, code.find("sto $1,$254,"));
    EXPECT_NE(std::string::npos, code.find("sto $15,$254,")) << code;
    EXPECT_EQ(std::string::npos, code.find("sto $16,")) << code;
    EXPECT_EQ(std::string::npos, code.find("rJ")) << code;
    EXPECT_EQ(std::string::npos, code.find("pushj")) << code;
}

// The save area is the top 120 bytes: $1 at frame - 120, $15 at frame - 8.
TEST_F(MmixTest, VariadicSaveAreaOffsets)
{
    std::string code = Code(CompileToMmix(R"(
        #include <stdarg.h>
        int v(int n, ...)
        {
            va_list ap;
            va_start(ap, n);
            return n;
        }
    )"));
    size_t sub = code.find("subu $254,$254,");
    ASSERT_NE(std::string::npos, sub) << code;
    int frame = std::stoi(code.substr(sub + 15));
    EXPECT_NE(std::string::npos, code.find("sto $1,$254," + std::to_string(frame - 120) + "\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find("sto $15,$254," + std::to_string(frame - 8) + "\n"))
        << code;
}

// With 16 named parameters there is nothing to save: the variable arguments begin with
// the first incoming stack slot.
TEST_F(MmixTest, VariadicSixteenNamed)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix(R"(
        #include <stdarg.h>
        long v(long a, long b, long c, long d, long e, long f, long g, long h, long i,
               long j, long k, long l, long m, long n, long o, long p, ...)
        {
            va_list ap;
            va_start(ap, p);
            long q = va_arg(ap, long);
            long r = va_arg(ap, long);
            va_end(ap);
            return a + p + q * 100 + r * 1000;
        }
        long w(long a, long b, long c, long d, long e, long f, long g, long h, long i,
               long j, long k, long l, long m, long n, long o, long p, long q, ...)
        {
            va_list ap;
            va_start(ap, q);
            long r = va_arg(ap, long);
            va_end(ap);
            return q + r * 1000;
        }
        int main(void)
        {
            if (v(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18) != 19717)
                return 1;
            if (w(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18) != 18017)
                return 2;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// The variadic functions both sides call: sums of every kind of argument.
static const char va_decls[] = R"(
#include <stdarg.h>
void putbyte(int c);
struct S { int a; long b; char c; };
struct T { char x, y, z; };
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
        case 'u': s += va_arg(ap, unsigned); break;
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
        case 't': {
            struct T t = va_arg(ap, struct T);
            s += t.x * 10000 + t.y * 100 + t.z;
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

// Ten pairs: the last ones go on the stack, past the save area.
static const char va_main[] = R"(
int main(void)
{
    int k = 1000;
    signed char c = -5;
    float f = 2.5f;
    struct S t = { 7, 70000L, 'x' };
    struct T u = { 1, 2, 3 };
    if (vsum(10, 'i', -300, 'u', 4000000000u, 'l', 100000L, 'q', 3LL << 40, 'c', c,
             'd', 1e6, 'p', &k, 'd', (double)f, 's', t, 't', u)
        != -300 + 4000000000L + 100000 + (3L << 20) - 5 + 1000000 + 1000 + 2
           + 7 + 70000L + 'x' + 10203)
        return 1;
    if (t.a != 7 || t.b != 70000L)
        return 4;
    if (vwrap(3, 1L, 20L, 300L) != 321) return 2;
    if (vwrap(20, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L, 11L, 12L, 13L, 14L, 15L, 16L,
              17L, 18L, 19L, 20L) != 210)
        return 5;
    if (vdouble(-1, 2, 0.5, 0.25) != -0.25) return 3;
    putbyte('o');
    putbyte('k');
    return 0;
}
)";

TEST_F(MmixTest, RunVariadic)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("ok", CompileAndRunMmix(std::string(va_decls) + va_defs + va_main));
    EXPECT_EQ(0, exit_status);
}

// Our variadic functions called from GCC's code.
TEST_F(MmixTest, RunVariadicCalledByGcc)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string gcc = std::string(va_decls) + va_main;
    EXPECT_EQ("ok", Run(CompileToMmix((std::string(va_decls) + va_defs).c_str()), "crt0.o",
                        &gcc, { "-O2" }, ".gcc"));
    EXPECT_EQ(0, exit_status);
}

// GCC's variadic functions called from ours.
TEST_F(MmixTest, RunVariadicCallGcc)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string gcc = std::string(va_decls) + va_defs;
    EXPECT_EQ("ok", Run(CompileToMmix((std::string(va_decls) + va_main).c_str()), "crt0.o",
                        &gcc, { "-O2" }, ".gcc"));
    EXPECT_EQ(0, exit_status);
}

// A va_list made by GCC's va_start, walked by our vlist, and the reverse.
TEST_F(MmixTest, RunVaListAcross)
{
    SKIP_IF_NO_MMIX_TOOLS();
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
    EXPECT_EQ("ok", Run(CompileToMmix(ours.c_str()), "crt0.o", &gcc, { "-O2" }, ".gcc"));
    EXPECT_EQ(0, exit_status);
}

// printf from libc.a, compiled by genmmix, against the host's formatting.
TEST_F(MmixTest, RunPrintf)
{
    SKIP_IF_NO_MMIX_TOOLS();
    char expected[512];
    snprintf(expected, sizeof expected,
             "[%d] [%u] [%ld] [%lu] [%lld] [%x] [%X] [%o] [%5d|%-5d|%05d] [%s|%8s|%-8s|%.2s] "
             "[%c%c] [%zu %td] [%hhd %hd] [%%] [%.3f] [%e] [%g] [%g] [%8.2f] [%Lf] [%lx]\n",
             -2147483647 - 1, 4294967295u, -9223372036854775807L - 1, 18446744073709551615UL,
             -1234567890123LL, 0xbeef, 0xbeef, 0777, 42, 42, -42, "mmix", "mmix", "mmix",
             "mmix", 'o', 'k', (size_t)65535, (ptrdiff_t)-3, (signed char)-1, (short)-2, 1.5,
             0.125, 1024.0, 0.0001, -3.25, 2.75L, 0x123456789abcdefUL);
    std::string out = CompileAndRunMmix(R"(
#include <stdio.h>
#include <stddef.h>
int main(void)
{
    printf("[%d] [%u] [%ld] [%lu] [%lld] [%x] [%X] [%o] [%5d|%-5d|%05d] [%s|%8s|%-8s|%.2s] "
           "[%c%c] [%zu %td] [%hhd %hd] [%%] [%.3f] [%e] [%g] [%g] [%8.2f] [%Lf] [%lx]\n",
           -2147483647 - 1, 4294967295u, -9223372036854775807L - 1, 18446744073709551615UL,
           -1234567890123LL, 0xbeef, 0xbeef, 0777, 42, 42, -42, "mmix", "mmix", "mmix",
           "mmix", 'o', 'k', (size_t)65535, (ptrdiff_t)-3, (signed char)-1, (short)-2, 1.5,
           0.125, 1024.0, 0.0001, -3.25, 2.75L, 0x123456789abcdefUL);
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
