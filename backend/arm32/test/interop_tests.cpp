//
// AAPCS-VFP interop with clang over a table of signatures, both ways: the same source,
// one copy compiled by us (names prefixed our_) and one by clang -O1 (their_), each
// calling the other's.
//
#include "arm32_test.h"

namespace {

// `text` with every PFX replaced by `pfx` and every OTHER by `other`.
std::string Subst(std::string text, const std::string &pfx, const std::string &other)
{
    for (const auto &[from, to] : { std::pair{ std::string("PFX"), pfx },
                                    std::pair{ std::string("OTHER"), other } })
        for (size_t at; (at = text.find(from)) != std::string::npos;)
            text.replace(at, from.size(), to);
    return text;
}

// The declarations of both copies of `defs`, by its own declaration lines `decls`.
std::string BothSides(const char *types, const char *decls, const char *defs, const char *pfx,
                      const char *other)
{
    return std::string(types) + Subst(decls, "our", "their") + Subst(decls, "their", "our") +
           Subst(defs, pfx, other);
}

} // namespace

// Back-filling, long long in an even pair and past r3, narrow values extended by the
// sender, more arguments than registers, and results of every class.
TEST_F(Arm32Test, RunSignatureTableWithClang)
{
    SKIP_IF_NO_ARM32_TOOLS();
    SKIP_IF_NO_ARM32_CLANG();
    const char *types = R"(
struct s3 { char a, b, c; };
struct s12 { int a, b, c; };
struct s40 { int a[10]; };
struct f1 { float x; };
struct f2 { float x, y; };
struct d4 { double d[4]; };
)";
    const char *decls = R"(
signed char PFX_narrow(signed char a, unsigned short b, short c, unsigned char d, _Bool e);
unsigned short PFX_unarrow(unsigned k);
double PFX_backfill(float a, double b, float c, double d, float e, struct f2 f, float g);
long long PFX_pairs(int a, long long b, int c, long long d, int e);
long long PFX_mixed(int a, double b, long long c, float d, char e, double f, short g, float h,
                    long long i, double j, int k, float l, unsigned m, double n, int o,
                    float p, int q, double r, float s, double t);
int PFX_split(int a, int b, struct s12 c, int d);
int PFX_big(struct s40 a, struct s3 b, struct d4 c, float d, struct s40 e);
struct s3 PFX_r3(int k);
struct f1 PFX_rf1(float k);
struct f2 PFX_rf2(float k);
struct d4 PFX_rd4(double k);
struct s12 PFX_r12(int k);
struct s40 PFX_r40(int k, struct s40 v);
long long PFX_rll(long long a, int b);
int PFX_check(void);
)";
    const char *defs  = R"(
signed char PFX_narrow(signed char a, unsigned short b, short c, unsigned char d, _Bool e)
{
    return (signed char)(a + (b >> 8) + c + d + e);
}
unsigned short PFX_unarrow(unsigned k) { return (unsigned short)(k * 3); }
double PFX_backfill(float a, double b, float c, double d, float e, struct f2 f, float g)
{
    return a + b * 10 + c * 100 + d * 1000 + e * 10000 + f.x * 1e5 + f.y * 1e6 + g * 1e7;
}
long long PFX_pairs(int a, long long b, int c, long long d, int e)
{
    return a + b * 10 + c * 100 + d * 1000 + e * 10000;
}
long long PFX_mixed(int a, double b, long long c, float d, char e, double f, short g, float h,
                    long long i, double j, int k, float l, unsigned m, double n, int o,
                    float p, int q, double r, float s, double t)
{
    return a + (long long)b + c + (long long)d + e + (long long)f + g + (long long)h + i +
           (long long)j + k + (long long)l + m + (long long)n + o + (long long)p + q +
           (long long)r + (long long)s + (long long)t;
}
int PFX_split(int a, int b, struct s12 c, int d) { return a + b * 10 + c.a * 100 + c.c * 1000 + d * 10000; }
int PFX_big(struct s40 a, struct s3 b, struct d4 c, float d, struct s40 e)
{
    return a.a[0] + a.a[9] + b.c + (int)c.d[3] + (int)d + e.a[5];
}
struct s3 PFX_r3(int k) { struct s3 r = { (char)k, (char)(k + 1), (char)(k + 2) }; return r; }
struct f1 PFX_rf1(float k) { struct f1 r = { k * 3 }; return r; }
struct f2 PFX_rf2(float k) { struct f2 r = { k, k * 2 }; return r; }
struct d4 PFX_rd4(double k) { struct d4 r = { { k, k + 1, k + 2, k + 3 } }; return r; }
struct s12 PFX_r12(int k) { struct s12 r = { k, -k, k * 2 }; return r; }
struct s40 PFX_r40(int k, struct s40 v) { v.a[9] = k; return v; }
long long PFX_rll(long long a, int b) { return a * b; }
int PFX_check(void)
{
    struct s3 b = { 1, 2, 3 };
    struct s12 s = { 4, 5, 6 };
    struct s40 a = { { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 } };
    struct f2 f = { 0.25f, 0.5f };
    struct d4 c = { { 1, 2, 3, 4 } };
    int ok = 0;
    ok |= (OTHER_narrow(-3, 0x1234, -300, 250, 1) == (signed char)(-3 + 0x12 - 300 + 250 + 1) &&
           OTHER_unarrow(30000) == (unsigned short)90000) << 0;
    ok |= (OTHER_backfill(1, 2, 3, 4, 5, f, 6) == 1 + 20 + 300 + 4000 + 50000 + 25000 + 500000 + 6e7)
          << 1;
    ok |= (OTHER_pairs(1, 2, 3, 4000000000LL, 5) == 1 + 20 + 300 + 4000000000000LL + 50000) << 2;
    ok |= (OTHER_mixed(1, 2.5, 3, 4.5f, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                       20) == 210) << 3;
    ok |= (OTHER_split(1, 2, s, 3) == 1 + 20 + 400 + 6000 + 30000) << 4;
    ok |= (OTHER_big(a, b, c, 7.5f, a) == 1 + 10 + 3 + 4 + 7 + 6) << 5;
    ok |= (OTHER_r3(65).c == 67 && OTHER_rf1(2).x == 6 && OTHER_rf2(1.5f).y == 3.0f &&
           OTHER_rd4(1).d[3] == 4) << 6;
    ok |= (OTHER_r12(4).b == -4 && OTHER_r40(99, a).a[9] == 99 && OTHER_r40(0, a).a[8] == 9 &&
           OTHER_rll(-3000000000LL, 3) == -9000000000LL) << 7;
    return ok;
}
)";
    std::string ours   = BothSides(types, decls, defs, "our", "their") + R"(
int main(void) { return our_check() == 255 && their_check() == 255; }
)";
    std::string theirs = BothSides(types, decls, defs, "their", "our");
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(1, exit_status);
}

// Variadic functions both ways, over every argument class, from registers and past
// them from the stack; and a va_list handed across in both directions.
TEST_F(Arm32Test, RunVariadicTableWithClang)
{
    SKIP_IF_NO_ARM32_TOOLS();
    SKIP_IF_NO_ARM32_CLANG();
    const char *types = R"(
#include <stdarg.h>
struct s12 { int a, b, c; };
struct s3 { char a, b, c; };
struct f3 { float x, y, z; };
struct d2 { double d[2]; };
struct al { long long a; int b; };
)";
    const char *decls = R"(
long long PFX_vsum(int n, va_list ap);
long long PFX_add(int n, ...);
long long PFX_add_own(int n, ...);
double PFX_vdouble(int n, ...);
int PFX_check(void);
)";
    const char *defs  = R"(
long long PFX_vsum(int n, va_list ap)
{
    long long t = 0;
    for (int i = 0; i < n; i++) {
        switch (va_arg(ap, int)) {
        case 'i': t += va_arg(ap, int); break;
        case 'l': t += va_arg(ap, long long); break;
        case 'd': t += (long long)va_arg(ap, double); break;
        case 'q': t += (long long)(va_arg(ap, long double) * 10); break;
        case 'p': t += *va_arg(ap, char *); break;
        case 's': { struct s12 v = va_arg(ap, struct s12); t += v.a + v.b + v.c; break; }
        case 'c': { struct s3 v = va_arg(ap, struct s3); t += v.a + v.c; break; }
        case 'f': { struct f3 x = va_arg(ap, struct f3); t += (long long)(x.x + x.y + x.z); break; }
        case 'D': { struct d2 y = va_arg(ap, struct d2); t += (long long)(y.d[0] + y.d[1]); break; }
        case 'a': { struct al z = va_arg(ap, struct al); t += z.a + z.b; break; }
        }
    }
    return t;
}
// The other side's va_arg reads our va_list.
long long PFX_add(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long long t = OTHER_vsum(n, ap);
    va_end(ap);
    return t;
}
// Ours reads the arguments the other side passed.
long long PFX_add_own(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long long t = PFX_vsum(n, ap);
    va_end(ap);
    return t;
}
// A double result of a variadic function comes back in r0:r1.
double PFX_vdouble(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    double d = va_arg(ap, double);
    va_end(ap);
    return d * n;
}
int PFX_check(void)
{
    struct s12 s = { 1, 2, 3 };
    struct s3 c = { 4, 5, 6 };
    struct f3 f = { 0.5f, 1.5f, 2.0f };
    struct d2 D = { { 1, 4 } };
    struct al a = { 7000000000LL, 8 };
    long long want = 7 + 100000000000LL + 2 + 6 + 10 + 4 + 5 + 30 + 7000000008LL + 4 + 4 +
                     1000 + 'x';
    long long r1 = OTHER_add(13, 'i', 7, 'l', 100000000000LL, 'd', 2.5, 's', s, 'c', c, 'f', f,
                             'D', D, 'q', 3.0L, 'a', a, 'f', f, 'd', 4.0, 'i', 1000, 'p', "x");
    long long r2 = OTHER_add_own(13, 'i', 7, 'l', 100000000000LL, 'd', 2.5, 's', s, 'c', c,
                                 'f', f, 'D', D, 'q', 3.0L, 'a', a, 'f', f, 'd', 4.0, 'i', 1000,
                                 'p', "x");
    return (r1 == want) + 2 * (r2 == want) + 4 * (OTHER_vdouble(3, 1.5) == 4.5);
}
)";
    std::string ours   = BothSides(types, decls, defs, "our", "their") + R"(
int main(void) { return our_check() + 8 * their_check(); }
)";
    std::string theirs = BothSides(types, decls, defs, "their", "our");
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(63, exit_status);
}

// Clang-compiled code calls the RTABI helpers, which our libc.a provides: 64-bit
// division and remainder, conversions, and the memory functions for a struct copy.
TEST_F(Arm32Test, RunClangRuntimeHelpers)
{
    SKIP_IF_NO_ARM32_TOOLS();
    SKIP_IF_NO_ARM32_CLANG();
    std::string ours   = R"(
long long their_div(long long a, long long b);
unsigned long long their_udiv(unsigned long long a, unsigned long long b);
long long their_mod(long long a, long long b);
double their_l2d(long long a);
float their_ul2f(unsigned long long a);
long long their_d2l(double a);
unsigned long long their_f2ul(float a);
long long their_shift(long long a, int n);
struct big { int a[40]; };
int their_copy(struct big *dst, struct big *src);
int main(void) {
    struct big x, y;
    for (int i = 0; i < 40; i++)
        x.a[i] = i;
    return (their_div(-100000000000LL, 7) == -14285714285LL)
         + 2 * (their_udiv(18000000000000000000ULL, 3) == 6000000000000000000ULL)
         + 4 * (their_mod(-100000000000LL, 7) == -5)
         + 8 * (their_l2d(-5000000000LL) == -5e9 && their_ul2f(1ULL << 40) == 1099511627776.0f)
         + 16 * (their_d2l(-1e12) == -1000000000000LL && their_f2ul(1e10f) == 10000000000ULL)
         + 32 * (their_shift(1, 40) == 1LL << 40 && their_copy(&y, &x) == 39 && y.a[20] == 20);
})";
    std::string theirs = R"(
long long their_div(long long a, long long b) { return a / b; }
unsigned long long their_udiv(unsigned long long a, unsigned long long b) { return a / b; }
long long their_mod(long long a, long long b) { return a % b; }
double their_l2d(long long a) { return a; }
float their_ul2f(unsigned long long a) { return a; }
long long their_d2l(double a) { return a; }
unsigned long long their_f2ul(float a) { return a; }
long long their_shift(long long a, int n) { return a << n; }
struct big { int a[40]; };
int their_copy(struct big *dst, struct big *src) { *dst = *src; return dst->a[39]; }
)";
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(63, exit_status);
}

// Our headers against clang's own: the same constants, types and layouts.
TEST_F(Arm32Test, HeadersAgreeWithClang)
{
    SKIP_IF_NO_ARM32_TOOLS();
    SKIP_IF_NO_ARM32_CLANG();
    std::string values = R"(
#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
enum { NI = 20, NF = 9 };
void NAME(long long *i, long double *f)
{
    long long iv[NI] = { sizeof(wchar_t), (wchar_t)-1 > 0, WCHAR_MIN, WCHAR_MAX,
                         WINT_MIN, WINT_MAX, sizeof(max_align_t), _Alignof(max_align_t),
                         SIZE_MAX, PTRDIFF_MIN, PTRDIFF_MAX, INTPTR_MIN, UINTPTR_MAX,
                         INT64_MIN, UINT32_MAX, CHAR_MIN, CHAR_MAX, LONG_MAX,
                         LDBL_MANT_DIG * 10000 + LDBL_MAX_EXP, DECIMAL_DIG + LDBL_DIG * 100 };
    long double fv[NF] = { LDBL_EPSILON, LDBL_MIN, LDBL_MAX, LDBL_TRUE_MIN, DBL_EPSILON,
                           DBL_MAX, FLT_EPSILON, FLT_MAX, LDBL_MIN_10_EXP + LDBL_MAX_10_EXP };
    for (int k = 0; k < NI; k++)
        i[k] = iv[k];
    for (int k = 0; k < NF; k++)
        f[k] = fv[k];
}
)";
    std::string ours   = values;
    std::string theirs = values;
    ours.replace(ours.find("NAME"), 4, "our_values");
    theirs.replace(theirs.find("NAME"), 4, "their_values");
    ours += R"(
void their_values(long long *i, long double *f);
int main(void)
{
    long long oi[NI], ti[NI];
    long double of[NF], tf[NF];
    our_values(oi, of);
    their_values(ti, tf);
    for (int k = 0; k < NI; k++)
        if (oi[k] != ti[k])
            return 1 + k;
    for (int k = 0; k < NF; k++)
        if (of[k] != tf[k])
            return 100 + k;
    return 0;
})";
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(0, exit_status);
}
