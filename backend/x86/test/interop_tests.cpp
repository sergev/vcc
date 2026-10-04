//
// System V AMD64 interop with clang over a table of signatures, both ways: the same
// source, one copy compiled by us (names prefixed our_) and one by clang -O1 (their_),
// each calling the other's.
//
#include "x86_test.h"

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

// Scalars: narrow values both ways (whoever receives one extends it), more than six
// integer and more than eight FP arguments interleaved, and long double arguments
// (on the stack) and results (in st(0)).
TEST_F(X86Test, RunScalarTableWithClang)
{
    SKIP_IF_NO_X86_TOOLS();
    const char *types = "";
    const char *decls = R"(
signed char PFX_narrow(signed char a, unsigned short b, short c, unsigned char d, _Bool e);
unsigned short PFX_unarrow(int k);
long PFX_mixed(int a, double b, long c, float d, char e, double f, short g, float h, long i,
               double j, int k, float l, unsigned m, double n, long o, float p, int q,
               double r);
long double PFX_rld(int pick, long double a, double x, long double b, int y);
int PFX_check(void);
)";
    const char *defs  = R"(
signed char PFX_narrow(signed char a, unsigned short b, short c, unsigned char d, _Bool e)
{
    return (signed char)(a + (b >> 8) + c + d + e);
}
unsigned short PFX_unarrow(int k) { return (unsigned short)(k * 1000); }
long PFX_mixed(int a, double b, long c, float d, char e, double f, short g, float h, long i,
               double j, int k, float l, unsigned m, double n, long o, float p, int q,
               double r)
{
    return a + (long)b + c + (long)d + e + (long)f + g + (long)h + i + (long)j + k + (long)l +
           m + (long)n + o + (long)p + q + (long)r;
}
long double PFX_rld(int pick, long double a, double x, long double b, int y)
{
    return (pick ? b : a) * y + x;
}
int PFX_check(void)
{
    long sum = 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10 + 11 + 12 + 13 + 14 + 15 + 16 + 17 + 18;
    int ok = 0;
    ok |= (OTHER_narrow(-3, 0x1234, -300, 250, 1) == (signed char)(-3 + 0x12 - 300 + 250 + 1)) << 0;
    ok |= (OTHER_unarrow(70) == 70000 - 65536) << 1;
    ok |= (OTHER_mixed(1, 2.5, 3, 4.5f, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18) ==
           sum) << 2;
    ok |= (OTHER_rld(1, 1.0L, 0.5, 7.25L, 2) == 15.0L) << 3;
    OTHER_rld(0, 1.0L, 0.5, 7.25L, 2); // the unused st(0) is popped
    ok |= (OTHER_rld(0, 1.0L / 3, 0, 0, 3) == 1.0L) << 4;
    return ok;
}
)";
    std::string ours   = BothSides(types, decls, defs, "our", "their") + R"(
int main(void) { return our_check() + 32 * (their_check() == 31); }
)";
    std::string theirs = BothSides(types, decls, defs, "their", "our");
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(63, exit_status);
}

// Structs of every class as arguments and results: mixed eightbytes in either order,
// two floats sharing an xmm register, an odd size, 16 bytes in two registers and 17 in
// memory, a long double alone (X87) and with an int (MEMORY); the all-or-nothing rule
// for both register files; results in rax:rdx, xmm0:xmm1, mixed, st(0) and memory.
TEST_F(X86Test, RunStructTableWithClang)
{
    SKIP_IF_NO_X86_TOOLS();
    const char *types = R"(
struct ld { long l; double d; };
struct dl { double d; long l; };
struct ffi { float a, b; int c; };
struct c3 { char c[3]; };
struct s16 { long a, b; };
struct s17 { char c[17]; };
struct dd { double a, b; };
struct q1 { long double q; };
struct qi { long double q; int i; };
)";
    const char *decls = R"(
long PFX_args(struct ld a, struct dl b, struct ffi c, struct c3 d, struct s17 e, struct q1 f,
              struct qi g, int h);
long PFX_aon(long a, long b, long c, long d, long e, struct s16 x, long f);
double PFX_aon_fp(double a, double b, double c, double d, double e, double f, double g,
                  struct dd x, double h);
struct ld PFX_rld(long k);
struct dl PFX_rdl(long k);
struct ffi PFX_rffi(int k);
struct c3 PFX_rc3(int k);
struct s16 PFX_rs16(long k);
struct s17 PFX_rs17(int k);
struct dd PFX_rdd(double k);
struct q1 PFX_rq1(long double k);
struct qi PFX_rqi(int k);
int PFX_check(void);
)";
    const char *defs  = R"(
long PFX_args(struct ld a, struct dl b, struct ffi c, struct c3 d, struct s17 e, struct q1 f,
              struct qi g, int h)
{
    return a.l + (long)a.d * 10 + (long)b.d * 100 + b.l * 1000 + (long)(c.a + c.b) + c.c +
           d.c[0] + d.c[2] + e.c[0] + e.c[16] + (long)f.q + (long)g.q + g.i + h;
}
long PFX_aon(long a, long b, long c, long d, long e, struct s16 x, long f)
{
    return a + b + c + d + e + x.a * 100 + x.b * 1000 + f * 10000;
}
double PFX_aon_fp(double a, double b, double c, double d, double e, double f, double g,
                  struct dd x, double h)
{
    return a + b + c + d + e + f + g + x.a * 100 + x.b * 1000 + h * 10000;
}
struct ld PFX_rld(long k) { struct ld r = { k, k + 0.5 }; return r; }
struct dl PFX_rdl(long k) { struct dl r = { k + 0.25, -k }; return r; }
struct ffi PFX_rffi(int k) { struct ffi r = { k, k * 2, -k }; return r; }
struct c3 PFX_rc3(int k) { struct c3 r = { { (char)k, (char)(k + 1), (char)(k + 2) } }; return r; }
struct s16 PFX_rs16(long k) { struct s16 r = { k, k << 40 }; return r; }
struct s17 PFX_rs17(int k) { struct s17 r = { { (char)k } }; r.c[16] = (char)-k; return r; }
struct dd PFX_rdd(double k) { struct dd r = { k, -k }; return r; }
struct q1 PFX_rq1(long double k) { struct q1 r = { k * 3 }; return r; }
struct qi PFX_rqi(int k) { struct qi r = { k / 4.0L, k }; return r; }
int PFX_check(void)
{
    struct ld a = { 1, 2.5 };
    struct dl b = { 3.5, 4 };
    struct ffi c = { 1.5f, 2.5f, 5 };
    struct c3 d = { { 6, 7, 8 } };
    struct s17 e = { { 9 } };
    struct q1 f = { 10.75L };
    struct qi g = { 11.5L, 12 };
    struct s16 x = { 6, 7 };
    struct dd y = { 0.5, 0.25 };
    e.c[16] = 13;
    int ok = 0;
    ok |= (OTHER_args(a, b, c, d, e, f, g, 14) ==
           1 + 20 + 300 + 4000 + 4 + 5 + 6 + 8 + 9 + 13 + 10 + 11 + 12 + 14) << 0;
    ok |= (OTHER_aon(1, 2, 3, 4, 5, x, 8) == 15 + 600 + 7000 + 80000) << 1;
    ok |= (OTHER_aon_fp(1, 2, 3, 4, 5, 6, 7, y, 8) == 28 + 50 + 250 + 80000) << 2;
    struct ld r1 = OTHER_rld(5);
    struct dl r2 = OTHER_rdl(6);
    struct ffi r3 = OTHER_rffi(7);
    ok |= (r1.l == 5 && r1.d == 5.5 && r2.d == 6.25 && r2.l == -6 && r3.a == 7 && r3.b == 14 &&
           r3.c == -7) << 3;
    struct c3 r4 = OTHER_rc3(65);
    struct s16 r5 = OTHER_rs16(3);
    struct s17 r6 = OTHER_rs17(20);
    struct dd r7 = OTHER_rdd(1.5);
    ok |= (r4.c[0] == 65 && r4.c[2] == 67 && r5.a == 3 && r5.b == 3L << 40 && r6.c[0] == 20 &&
           r6.c[16] == -20 && r7.a == 1.5 && r7.b == -1.5) << 4;
    struct q1 r8 = OTHER_rq1(0.5L);
    struct qi r9 = OTHER_rqi(9);
    OTHER_rq1(1); // the unused st(0) is popped
    ok |= (r8.q == 1.5L && r9.q == 2.25L && r9.i == 9) << 5;
    return ok;
}
)";
    std::string ours   = BothSides(types, decls, defs, "our", "their") + R"(
int main(void) { return our_check() + 64 * (their_check() == 63); }
)";
    std::string theirs = BothSides(types, decls, defs, "their", "our");
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(127, exit_status);
}

// Variadic functions both ways, over every argument class, from registers and past
// them from the overflow area; and a va_list handed across in both directions.
TEST_F(X86Test, RunVariadicInteropWithClang)
{
    SKIP_IF_NO_X86_TOOLS();
    const char *types = R"(
#include <stdarg.h>
struct s12 { int a, b, c; };
struct s24 { long a, b, c; };
struct f3 { float x, y, z; };
struct dl { double d; long l; };
struct ld { long l; double d; };
struct q1 { long double q; };
struct al { _Alignas(16) long a; long b; };
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
        case 'q': t += (long)(va_arg(ap, long double) * 10); break;
        case 'p': t += *va_arg(ap, char *); break;
        case 's': { struct s12 v = va_arg(ap, struct s12); t += v.a + v.b + v.c; break; }
        case 'S': { struct s24 w = va_arg(ap, struct s24); t += w.a + w.b + w.c; break; }
        case 'f': { struct f3 x = va_arg(ap, struct f3); t += (long)(x.x + x.y + x.z); break; }
        case 'm': { struct dl y = va_arg(ap, struct dl); t += (long)y.d + y.l; break; }
        case 'M': { struct ld z = va_arg(ap, struct ld); t += z.l + (long)z.d; break; }
        case 'Q': { struct q1 u = va_arg(ap, struct q1); t += (long)(u.q * 100); break; }
        case 'a': { struct al w = va_arg(ap, struct al); t += w.a + w.b; break; }
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
    struct dl m = { 2.5, 40 };
    struct ld M = { 50, 3.5 };
    struct q1 Q = { 1.25L };
    struct al a = { 7, 8 };
    long want = 7 + 100 + 2 + 6 + 60 + 4 + 42 + 53 + 30 + 125 + 15 + 4 + 42 + 4 + 53 + 15 +
                1000 + 'x' + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9;
    long r1 = OTHER_add(27, 'i', 7, 'l', 100L, 'd', 2.5, 's', s, 'S', S, 'f', f, 'm', m,
                        'M', M, 'q', 3.0L, 'Q', Q, 'a', a, 'f', f, 'm', m, 'd', 4.0, 'M', M,
                        'a', a, 'i', 1000, 'p', "x", 'd', 1.0, 'd', 2.0, 'd', 3.0, 'd', 4.0,
                        'd', 5.0, 'd', 6.0, 'd', 7.0, 'd', 8.0, 'd', 9.0);
    long r2 = OTHER_add_own(27, 'i', 7, 'l', 100L, 'd', 2.5, 's', s, 'S', S, 'f', f, 'm', m,
                            'M', M, 'q', 3.0L, 'Q', Q, 'a', a, 'f', f, 'm', m, 'd', 4.0, 'M', M,
                            'a', a, 'i', 1000, 'p', "x", 'd', 1.0, 'd', 2.0, 'd', 3.0, 'd', 4.0,
                            'd', 5.0, 'd', 6.0, 'd', 7.0, 'd', 8.0, 'd', 9.0);
    return (r1 == want) + 2 * (r2 == want);
}
)";
    std::string ours   = BothSides(types, decls, defs, "our", "their") + R"(
int main(void) { return (int)our_check() + 4 * (int)their_check(); }
)";
    std::string theirs = BothSides(types, decls, defs, "their", "our");
    EXPECT_EQ("", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(15, exit_status);
}
