//
// AAPCS64 interop with clang over a table of signatures, both ways: the same source,
// one copy compiled by us (names prefixed our_) and one by clang -O1 (their_), each
// calling the other's.  Our side does no long double arithmetic yet (A21): clang's
// ld_val does it.
//
#include "aarch64_test.h"

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

// Mixed and interleaved int/FP arguments, narrow values both ways (whoever receives one
// extends it), results of every class, and more arguments than registers.
TEST_F(Aarch64Test, RunSignatureTableWithClang)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    const char *types = R"(
struct s3 { char a, b, c; };
struct s12 { int a, b, c; };
struct s40 { long a[5]; };
struct f2 { float x, y; };
struct d4 { double d[4]; };
struct q1 { long double q; };
long ld_val(long double x);
)";
    const char *decls = R"(
signed char PFX_narrow(signed char a, unsigned short b, short c, unsigned char d, _Bool e);
long PFX_mixed(int a, double b, long c, float d, char e, double f, short g, float h, long i,
               double j, int k, float l, unsigned m, double n, long o, float p, int q,
               double r);
struct s3 PFX_r3(int k);
struct s12 PFX_r12(int k);
struct s40 PFX_r40(int k);
struct f2 PFX_rf2(float k);
struct d4 PFX_rd4(double k);
struct q1 PFX_rq1(struct q1 k);
long double PFX_rld(int pick, long double a, long double b);
int PFX_check(void);
)";
    const char *defs  = R"(
signed char PFX_narrow(signed char a, unsigned short b, short c, unsigned char d, _Bool e)
{
    return (signed char)(a + (b >> 8) + c + d + e);
}
long PFX_mixed(int a, double b, long c, float d, char e, double f, short g, float h, long i,
               double j, int k, float l, unsigned m, double n, long o, float p, int q,
               double r)
{
    return a + (long)b + c + (long)d + e + (long)f + g + (long)h + i + (long)j + k + (long)l +
           m + (long)n + o + (long)p + q + (long)r;
}
struct s3 PFX_r3(int k) { struct s3 r = { (char)k, (char)(k + 1), (char)(k + 2) }; return r; }
struct s12 PFX_r12(int k) { struct s12 r = { k, -k, k * 2 }; return r; }
struct s40 PFX_r40(int k) { struct s40 r = { { k, 0, 0, 0, -k } }; return r; }
struct f2 PFX_rf2(float k) { struct f2 r = { k, k * 2 }; return r; }
struct d4 PFX_rd4(double k) { struct d4 r = { { k, k + 1, k + 2, k + 3 } }; return r; }
struct q1 PFX_rq1(struct q1 k) { return k; }
long double PFX_rld(int pick, long double a, long double b) { return pick ? b : a; }
int PFX_check(void)
{
    struct q1 q = { 2.5L };
    long sum = 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10 + 11 + 12 + 13 + 14 + 15 + 16 + 17 + 18;
    int ok = 0;
    ok |= (OTHER_narrow(-3, 0x1234, -300, 250, 1) == (signed char)(-3 + 0x12 - 300 + 250 + 1)) << 0;
    ok |= (OTHER_mixed(1, 2.5, 3, 4.5f, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18) ==
           sum) << 1;
    ok |= (OTHER_r3(65).c == 67 && OTHER_r12(4).b == -4 && OTHER_r40(9).a[4] == -9) << 2;
    ok |= (OTHER_rf2(1.5f).y == 3.0f && OTHER_rd4(1).d[3] == 4) << 3;
    ok |= (ld_val(OTHER_rq1(q).q) == 25 && ld_val(OTHER_rld(1, 1.0L, 7.0L)) == 70) << 4;
    return ok;
}
)";
    std::string ours   = BothSides(types, decls, defs, "our", "their") + R"(
int main(void) { return our_check() + 32 * (their_check() == 31); }
)";
    std::string theirs = BothSides(types, decls, defs, "their", "our") + R"(
long ld_val(long double x) { return (long)(x * 10); }
)";
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(63, exit_status);
}

// Variadic functions both ways, over every argument class, from registers and past
// them from the stack; and a va_list handed across in both directions.
TEST_F(Aarch64Test, RunVariadicInteropWithClang)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    const char *types = R"(
#include <stdarg.h>
struct s12 { int a, b, c; };
struct s24 { long a, b, c; };
struct f3 { float x, y, z; };
struct d4 { double d[4]; };
struct al { _Alignas(16) long a; long b; };
long ld_val(long double x);
)";
    const char *decls = R"(
long PFX_vsum(int n, va_list ap);
long PFX_add(int n, ...);
long PFX_add_own(int n, ...);
long PFX_check(void);
)";
    const char *defs  = R"(
long PFX_vsum(int n, va_list ap)
{
    long t = 0;
    for (int i = 0; i < n; i++) {
        switch (va_arg(ap, int)) {
        case 'i': t += va_arg(ap, int); break;
        case 'l': t += va_arg(ap, long); break;
        case 'd': t += (long)va_arg(ap, double); break;
        case 'q': t += ld_val(va_arg(ap, long double)); break;
        case 'p': t += *va_arg(ap, char *); break;
        case 's': { struct s12 v = va_arg(ap, struct s12); t += v.a + v.b + v.c; break; }
        case 'S': { struct s24 w = va_arg(ap, struct s24); t += w.a + w.b + w.c; break; }
        case 'f': { struct f3 x = va_arg(ap, struct f3); t += (long)(x.x + x.y + x.z); break; }
        case 'D': { struct d4 y = va_arg(ap, struct d4); t += (long)(y.d[0] + y.d[3]); break; }
        case 'a': { struct al z = va_arg(ap, struct al); t += z.a + z.b; break; }
        }
    }
    return t;
}
// The other side's va_arg reads our va_list.
long PFX_add(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long t = OTHER_vsum(n, ap);
    va_end(ap);
    return t;
}
// Ours reads the arguments the other side passed.
long PFX_add_own(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long t = PFX_vsum(n, ap);
    va_end(ap);
    return t;
}
long PFX_check(void)
{
    struct s12 s = { 1, 2, 3 };
    struct s24 S = { 10, 20, 30 };
    struct f3 f = { 0.5f, 1.5f, 2.0f };
    struct d4 D = { { 1, 2, 3, 4 } };
    struct al a = { 7, 8 };
    long want = 7 + 100 + 2 + 6 + 60 + 4 + 5 + 30 + 15 + 4 + 4 + 1000 + 'x';
    long r1 = OTHER_add(13, 'i', 7, 'l', 100L, 'd', 2.5, 's', s, 'S', S, 'f', f, 'D', D,
                        'q', 3.0L, 'a', a, 'f', f, 'd', 4.0, 'i', 1000, 'p', "x");
    long r2 = OTHER_add_own(13, 'i', 7, 'l', 100L, 'd', 2.5, 's', s, 'S', S, 'f', f, 'D', D,
                            'q', 3.0L, 'a', a, 'f', f, 'd', 4.0, 'i', 1000, 'p', "x");
    return (r1 == want) + 2 * (r2 == want);
}
)";
    std::string ours   = BothSides(types, decls, defs, "our", "their") + R"(
int main(void) { return (int)our_check() + 4 * (int)their_check(); }
)";
    std::string theirs = BothSides(types, decls, defs, "their", "our") + R"(
long ld_val(long double x) { return (long)(x * 10); }
)";
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(15, exit_status);
}
