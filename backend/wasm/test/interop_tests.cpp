//
// wasm32 C ABI interop with clang over a table of signatures, both ways: the same
// source, one copy compiled by us (names prefixed our_) and one by clang -O1 (their_),
// each calling the other's.  Then bit-fields across the boundary, and clang's
// main(argc, argv) on our crt0.
//
#include <gtest/gtest.h>

#include "wasm_test.h"
#include "../../common/test/bitfield_interop.h"

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

// One side of the table: the declarations of both copies, then the definitions under
// `pfx`, calling the ones under `other`.
std::string Side(const char *types, const char *decls, const char *defs, const char *pfx,
                 const char *other)
{
    return std::string(types) + Subst(decls, "our", "their") + Subst(decls, "their", "our") +
           Subst(defs, pfx, other);
}

const char sig_types[] = R"(
struct s1 { char a; };
struct s2 { short a; };
struct s3 { char a[3]; };
struct s4 { int a; };
struct sd { struct { double d; } in; };
struct s8 { int a; int b; };
struct s9 { char a[9]; };
struct s16 { long long a; long long b; };
struct s24 { long long a; double d; char c; };
union un { long long l; char c[3]; };
union u1 { float f; };
)";

const char sig_decls[] = R"(
signed char PFX_narrow(signed char a, unsigned char b, short c, char d, _Bool e,
                       unsigned short f);
unsigned char PFX_unarrow(unsigned k);
short PFX_snarrow(int k);
unsigned short PFX_usnarrow(int k);
char PFX_cnarrow(int k);
_Bool PFX_bnarrow(int k);
unsigned PFX_unsw(unsigned a, int b);
long long PFX_wide(long long a, unsigned long long b, int c);
double PFX_many(int a, long long b, const char *c, double d, float e, short f, unsigned g,
                long h, signed char i, double j, float k, unsigned char l, long long m, int n,
                unsigned short o, double p, float q, short r);
float PFX_rf(float a, double b);
struct s1 PFX_r1(struct s1 x, int k);
struct s2 PFX_r2(int k, struct s2 x);
struct s3 PFX_r3(struct s3 x, int k);
struct s4 PFX_r4(long pad, struct s4 x);
struct sd PFX_rd(struct sd x, double k);
struct s8 PFX_r8(struct s8 x, int k);
struct s9 PFX_r9(struct s9 x, int k);
struct s16 PFX_r16(double d, struct s16 x);
struct s24 PFX_r24(struct s24 x, int k);
union un PFX_ru(union un u, int k);
union u1 PFX_ru1(union u1 u);
long long PFX_mixed(int a, struct s3 b, double c, struct s9 d, struct s24 e, union un f,
                    char g, struct s1 h, struct sd i);
int PFX_inc(int x);
int PFX_apply(int (*f)(int), int x);
int (*PFX_getinc(void))(int);
struct s8 PFX_applys(struct s8 (*f)(struct s8, int), struct s8 x);
int PFX_leaf(int a);
int PFX_rec(int n, int a);
int PFX_check(void);
)";

const char sig_defs[] = R"(
signed char PFX_narrow(signed char a, unsigned char b, short c, char d, _Bool e,
                       unsigned short f)
{
    return (signed char)(a + (b >> 4) + c + d + e + (f >> 8));
}
unsigned char PFX_unarrow(unsigned k) { return (unsigned char)(k * 3); }
short PFX_snarrow(int k) { return (short)(k * 5); }
unsigned short PFX_usnarrow(int k) { return (unsigned short)(k * 5); }
char PFX_cnarrow(int k) { return (char)(k + 100); }
_Bool PFX_bnarrow(int k) { return k & 0x100; }
unsigned PFX_unsw(unsigned a, int b) { return a + b; }
long long PFX_wide(long long a, unsigned long long b, int c) { return a * c + (long long)(b >> 60); }
double PFX_many(int a, long long b, const char *c, double d, float e, short f, unsigned g,
                long h, signed char i, double j, float k, unsigned char l, long long m, int n,
                unsigned short o, double p, float q, short r)
{
    return a + b * 2 + c[1] + d * 4 + e * 5 + f * 6 + g * 7.0 + h * 8 + i * 9 + j * 10 +
           k * 11 + l * 12 + m * 13 + n * 14 + o * 15 + p * 16 + q * 17 + r * 18;
}
float PFX_rf(float a, double b) { return a * 2 + (float)b; }
struct s1 PFX_r1(struct s1 x, int k) { x.a += k; return x; }
struct s2 PFX_r2(int k, struct s2 x) { x.a *= k; return x; }
struct s3 PFX_r3(struct s3 x, int k) { x.a[0] += k; x.a[2] -= k; return x; }
struct s4 PFX_r4(long pad, struct s4 x) { x.a -= (int)pad; return x; }
struct sd PFX_rd(struct sd x, double k) { x.in.d *= k; return x; }
struct s8 PFX_r8(struct s8 x, int k) { x.a += k; x.b -= k; return x; }
struct s9 PFX_r9(struct s9 x, int k) { x.a[0] += k; x.a[8] += k; return x; }
struct s16 PFX_r16(double d, struct s16 x) { x.a += (long long)d; x.b = -x.b; return x; }
struct s24 PFX_r24(struct s24 x, int k) { x.a += k; x.d *= k; x.c += k; return x; }
union un PFX_ru(union un u, int k) { u.c[1] += k; return u; }
union u1 PFX_ru1(union u1 u) { u.f = -u.f; return u; }
long long PFX_mixed(int a, struct s3 b, double c, struct s9 d, struct s24 e, union un f,
                    char g, struct s1 h, struct sd i)
{
    long long s = a + b.a[1] + (long long)c + d.a[4] + e.a + (long long)e.d + e.c + f.c[0] +
                  g + h.a + (long long)i.in.d;
    d.a[4] = 0;
    e.a    = 0;
    return s;
}
int PFX_inc(int x) { return x + 1; }
int PFX_apply(int (*f)(int), int x) { return f(f(x)); }
int (*PFX_getinc(void))(int) { return PFX_inc; }
struct s8 PFX_applys(struct s8 (*f)(struct s8, int), struct s8 x) { return f(x, 2); }

/* Values live across calls into the other side. */
int PFX_leaf(int a) { return a * 3 + 1; }
/* Recursion alternating between the sides, deep enough to use the shadow stack. */
int PFX_rec(int n, int a)
{
    struct s9 frame = { { 1 } };
    if (n == 0)
        return a;
    int x = n * 3 + a, y = x * 7;
    int r = OTHER_rec(n - 1, a + frame.a[0]);
    return r + y / 7;
}

/* A bit per check that failed. */
int PFX_check(void)
{
    int fail = 0;
    fail |= !(OTHER_narrow(-3, 0x95, -300, 100, 1, 0x1234) ==
              (signed char)(-3 + 9 - 300 + 100 + 1 + 0x12)) << 0;
    int n1 = OTHER_unarrow(1000), n2 = OTHER_snarrow(10000), n3 = OTHER_usnarrow(-1);
    int n4 = OTHER_cnarrow(100), n5 = OTHER_bnarrow(0x300);
    fail |= !(n1 == (unsigned char)3000 && n2 == (short)50000 &&
              n3 == (unsigned short)-5 && n4 == (char)200 && n5 == 1) << 1;
    fail |= !(OTHER_unsw(0xfffffff0u, 5) == 0xfffffff5u &&
              OTHER_wide(-3, 0xf000000000000000ull, 1000000) == -3000000 + 15) << 2;
    double many = OTHER_many(-1, -2LL, "xyz", 0.5, 1.5f, -3, 4000000000u, -5L, -6, 0.25, -2.5f,
                             200, 7LL, -8, 60000, 0.125, 3.5f, -9);
    double want = -1 + -2LL * 2 + 'y' + 0.5 * 4 + 1.5f * 5 + (short)-3 * 6 + 4000000000u * 7.0 +
                  -5L * 8 + (signed char)-6 * 9 + 0.25 * 10 + -2.5f * 11 +
                  (unsigned char)200 * 12 + 7LL * 13 + -8 * 14 + (unsigned short)60000 * 15 +
                  0.125 * 16 + 3.5f * 17 + (short)-9 * 18;
    fail |= !(many == want) << 3;
    fail |= !(OTHER_rf(1.25f, 0.5) == 3.0f) << 4;

    struct s1 a = { 10 };
    struct s2 b = { 300 };
    struct s3 c = { { 1, 2, 3 } };
    struct s4 d = { 100 };
    struct sd e = { { 1.5 } };
    struct s8 f = { 7, -7 };
    struct s9 g = { { 1, 2, 3, 4, 5, 6, 7, 8, 9 } };
    struct s16 h = { 10, 20 };
    struct s24 i = { 1000, 2.5, 'a' };
    union un j;
    j.l = 0;
    j.c[0] = 5;
    j.c[1] = 6;
    union u1 k;
    k.f = 2.0f;
    fail |= !(OTHER_r1(a, 2).a == 12 && a.a == 10 && OTHER_r2(3, b).a == 900 && b.a == 300)
            << 5;
    struct s3 c2 = OTHER_r3(c, 1);
    struct s4 d2 = OTHER_r4(30, d);
    struct sd e2 = OTHER_rd(e, 4.0);
    fail |= !(c2.a[0] == 2 && c2.a[1] == 2 && c2.a[2] == 2 && c.a[0] == 1 && d2.a == 70 &&
              e2.in.d == 6.0 && e.in.d == 1.5) << 6;
    struct s8 f2 = OTHER_r8(f, 3);
    struct s9 g2 = OTHER_r9(g, 100);
    fail |= !(f2.a == 10 && f2.b == -10 && g2.a[0] == 101 && g2.a[8] == 109 && g2.a[4] == 5 &&
              g.a[0] == 1) << 7;
    struct s16 h2 = OTHER_r16(5.0, h);
    struct s24 i2 = OTHER_r24(i, 2);
    fail |= !(h2.a == 15 && h2.b == -20 && h.b == 20 && i2.a == 1002 && i2.d == 5.0 &&
              i2.c == 'c' && i.a == 1000 && i.d == 2.5 && i.c == 'a') << 8;
    union un j2 = OTHER_ru(j, 1);
    union u1 k2 = OTHER_ru1(k);
    fail |= !(j2.c[0] == 5 && j2.c[1] == 7 && j.c[1] == 6 && k2.f == -2.0f) << 9;
    fail |= !(OTHER_mixed(1, c, 20.0, g, i, j, 3, a, e) ==
              1 + 2 + 20 + 5 + 1000 + 2 + 'a' + 5 + 3 + 10 + 1 && g.a[4] == 5 && i.a == 1000)
            << 10;
    f = OTHER_r8(f, 1);
    fail |= !(f.a == 8 && f.b == -8) << 11;
    struct s8 f3 = OTHER_applys(OTHER_r8, f);
    fail |= !(OTHER_apply(PFX_inc, 5) == 7 && OTHER_getinc()(8) == 9 &&
              OTHER_apply(OTHER_getinc(), 1) == 3 && f3.a == 10 && f3.b == -10 &&
              OTHER_applys(PFX_r8, f).a == 10) << 12;
    int depth = 3000, want_r = 1 + depth;
    for (int n = depth, t = 0; n > 0; n--, t++)
        want_r += n * 3 + 1 + t;
    fail |= !(OTHER_rec(depth, 1) == want_r) << 13;
    return fail;
}
)";

// Main of our side: both checks, as hex masks of the failed ones.
const char sig_main[] = R"(
void putch(unsigned c);
static void hex(unsigned v)
{
    for (int i = 12; i >= 0; i -= 4)
        putch("0123456789abcdef"[(v >> i) & 15]);
}
int main(void)
{
    hex(our_check());
    putch(' ');
    hex(their_check());
    return 0;
}
)";

} // namespace

// Narrow values both ways, eighteen arguments of every scalar kind, structures and
// unions of one scalar (by value) and of more (by reference, as arguments and as results
// through the hidden parameter), a callee writing its copy, function pointers both
// ways, and recursion 3000 deep alternating between the sides.
TEST_F(WasmTest, RunSignatureTableWithClang)
{
    SKIP_IF_NO_WASM32_CLANG();
    std::string ours   = Side(sig_types, sig_decls, sig_defs, "our", "their") + sig_main;
    std::string theirs = Side(sig_types, sig_decls, sig_defs, "their", "our");
    EXPECT_EQ("0000 0000", CompileAndRunWithClang(ours, theirs));
    EXPECT_EQ(0, exit_status);
}

// Our headers' types, limits and float characteristics, against clang's own.
TEST_F(WasmTest, HeadersAgreeWithClang)
{
    SKIP_IF_NO_WASM32_CLANG();
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

// Bit-fields: our caller with clang's callee.
TEST_F(WasmTest, RunBitfieldsWeCallClang)
{
    SKIP_IF_NO_WASM32_CLANG();
    EXPECT_EQ("", CompileAndRunWithClang(kBitfieldCaller, kBitfieldCallee));
    EXPECT_EQ(0, exit_status);
}

// The same, clang calling ours.
TEST_F(WasmTest, RunBitfieldsClangCallsUs)
{
    SKIP_IF_NO_WASM32_CLANG();
    EXPECT_EQ("", CompileAndRunWithClang(kBitfieldCallee, kBitfieldCaller));
    EXPECT_EQ(0, exit_status);
}

// clang's main(argc, argv), __main_argc_argv when hosted, on our crt0 and libc:
// main(void)'s __main_void comes from the library, calling it with no arguments.
TEST_F(WasmTest, RunClangMainArgcArgv)
{
    SKIP_IF_NO_WASM32_CLANG();
    std::string theirs = R"(
        void putch(unsigned c);
        int main(int argc, char **argv)
        {
            putch('o'); putch('k'); putch('\n');
            return argc + (argv != 0) + 4;
        }
    )";
    EXPECT_EQ("ok\n", Run(CompileToWasm("void ours(void) {}"), "crt0.o", &theirs,
                          { "-O1", "-fhosted" }, ""));
    EXPECT_EQ(4, exit_status);
}
