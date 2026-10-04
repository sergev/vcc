//
// avr-gcc ABI interop with clang over a table of signatures, both ways: the same source,
// one copy compiled by us (names prefixed our_) and one by clang -O1 (their_), each
// calling the other's.  Then the registers our code must preserve, and clang's code on
// our runtime.
//
#include <cstdint>
#include <cstring>

#include "avr_test.h"

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

const char sig_types[] = R"(
struct s1 { char a; };
struct s3 { char a[3]; };
struct s5 { char a; int b; int c; };
struct s8 { long a; long b; };
struct s9 { char a[9]; };
struct s10 { int a[5]; };
struct nest { struct { char x; int y; } p; char c; };
union un { long l; char c[3]; };
struct fl { float f; char c; };
)";

const char sig_decls[] = R"(
signed char PFX_narrow(signed char a, unsigned char b, short c, char d, _Bool e);
unsigned char PFX_unarrow(unsigned k);
long PFX_over(long long a, long long b, long c, int d);
long PFX_over2(long long a, long b, long c, signed char d, int e, long f);
long long PFX_ll(long long a, int b, long long c);
double PFX_fp(float a, double b, long double c, int d, float e);
float PFX_rf(double x);
struct s1 PFX_r1(struct s1 x, int k);
struct s3 PFX_r3(struct s3 x, int k);
struct s5 PFX_r5(struct s5 x, int k);
struct s8 PFX_r8(struct s8 x, int k);
struct s9 PFX_r9(struct s9 x, int k);
struct s10 PFX_r10(long pad, struct s10 x, int k);
int PFX_mixed(struct nest a, union un b, struct fl c, int k);
long PFX_split(long long a, long long b, struct s5 s, int k);
int PFX_inc(int x);
int PFX_apply(int (*f)(int), int x);
int (*PFX_getinc(void))(int);
long PFX_check(void);
)";

const char sig_defs[] = R"(
signed char PFX_narrow(signed char a, unsigned char b, short c, char d, _Bool e)
{
    return (signed char)(a + (b >> 4) + c + d + e);
}
unsigned char PFX_unarrow(unsigned k) { return (unsigned char)(k * 3); }
long PFX_over(long long a, long long b, long c, int d)
{
    return (long)a + (long)(b >> 8) * 10 + c * 100 + d * 1000L;
}
long PFX_over2(long long a, long b, long c, signed char d, int e, long f)
{
    return (long)a + b * 10 + c * 100 + d * 1000L + e * 10000L + f * 100000L;
}
long long PFX_ll(long long a, int b, long long c) { return a * b + c; }
double PFX_fp(float a, double b, long double c, int d, float e)
{
    return a + b * 10 + c * 100 + d * 1000 + e * 10000;
}
float PFX_rf(double x) { return x / 4; }
struct s1 PFX_r1(struct s1 x, int k) { x.a += k; return x; }
struct s3 PFX_r3(struct s3 x, int k) { x.a[0] += k; x.a[2] -= k; return x; }
struct s5 PFX_r5(struct s5 x, int k) { x.a += k; x.b *= k; x.c -= k; return x; }
struct s8 PFX_r8(struct s8 x, int k) { x.a += k; x.b -= k; return x; }
struct s9 PFX_r9(struct s9 x, int k) { for (int i = 0; i < 9; i++) x.a[i] += k; return x; }
struct s10 PFX_r10(long pad, struct s10 x, int k)
{
    for (int i = 0; i < 5; i++) x.a[i] += k + (int)pad;
    return x;
}
int PFX_mixed(struct nest a, union un b, struct fl c, int k)
{
    return a.p.x + a.p.y * 10 + a.c * 100 + (int)b.l * 1000 + (int)(c.f * 4) + c.c + k;
}
long PFX_split(long long a, long long b, struct s5 s, int k)
{
    return (long)a + (long)b + s.a + s.b + s.c + k;
}
int PFX_inc(int x) { return x + 1; }
int PFX_apply(int (*f)(int), int x) { return f(f(x)); }
int (*PFX_getinc(void))(int) { return PFX_inc; }
long PFX_check(void)
{
    long ok = 0;
    struct s1 a = { 10 };
    struct s3 b = { { 1, 2, 3 } };
    struct s5 c = { 1, 300, 5 };
    struct s8 d = { 100000, -5 };
    struct s9 e = { { 1, 2, 3, 4, 5, 6, 7, 8, 9 } };
    struct s10 f = { { 1, 2, 3, 4, 5 } };
    struct nest n = { { 3, 4 }, 5 };
    union un u;
    struct fl fl = { 2.5f, 7 };
    u.l = 6;
    ok |= (long)(OTHER_narrow(-3, 0x95, -300, 100, 1) == (signed char)(-3 + 9 - 300 + 100 + 1) &&
                 OTHER_unarrow(1000) == (unsigned char)3000) << 0;
    ok |= (long)(OTHER_over(1, 0x200, 3, 4) == 1 + 20 + 300 + 4000) << 1;
    ok |= (long)(OTHER_over2(1, 2, 3, -4, 5, 6) == 1 + 20 + 300 - 4000 + 50000 + 600000) << 2;
    ok |= (long)(OTHER_ll(-3000000000LL, 3, 7) == -8999999993LL) << 3;
    ok |= (long)(OTHER_fp(1, 2, 3, 4, 5) == 1 + 20 + 300 + 4000 + 50000 &&
                 OTHER_rf(10) == 2.5f) << 4;
    ok |= (long)(OTHER_r1(a, 2).a == 12 && OTHER_r3(b, 1).a[2] == 2) << 5;
    c = OTHER_r5(c, 3);
    ok |= (long)(c.a == 4 && c.b == 900 && c.c == 2) << 6;
    d = OTHER_r8(d, 7);
    ok |= (long)(d.a == 100007 && d.b == -12) << 7;
    e = OTHER_r9(e, 10);
    f = OTHER_r10(1000, f, 1);
    ok |= (long)(e.a[0] == 11 && e.a[8] == 19 && f.a[0] == 1002 && f.a[4] == 1006) << 8;
    ok |= (long)(OTHER_mixed(n, u, fl, 9) == 3 + 40 + 500 + 6000 + 10 + 7 + 9) << 9;
    ok |= (long)(OTHER_split(1000, 20000, c, 4000) == 1000 + 20000 + 4 + 900 + 2 + 4000) << 10;
    ok |= (long)(OTHER_apply(PFX_inc, 5) == 7 && OTHER_getinc()(8) == 9 &&
                 OTHER_apply(OTHER_getinc(), 1) == 3) << 11;
    return ok;
}
)";

} // namespace

// Narrow values, arguments past r8 and the ones after them, long long, every FP type,
// structures of every size and shape as arguments and results, function pointers.
TEST_F(AvrTest, RunSignatureTableWithClang)
{
    SKIP_IF_NO_AVR_TOOLS();
    std::string ours = BothSides(sig_types, sig_decls, sig_defs, "our", "their") + R"(
void putbyte(int c);
static void hex(long v)
{
    for (int i = 12; i >= 0; i -= 4)
        putbyte("0123456789abcdef"[(v >> i) & 15]);
}
int main(void)
{
    hex(our_check());
    putbyte(' ');
    hex(their_check());
    return 0;
}
)";
    std::string theirs = BothSides(sig_types, sig_decls, sig_defs, "their", "our");
    EXPECT_EQ("0fff 0fff", CompileAndRunWithClang(ours, theirs));
}

// r2-r17 and Y survive our calls and r1 is zero after them: a hand-written caller
// fills them, calls our code, which uses r10-r17, mul and the helpers, and checks.
TEST_F(AvrTest, RunPreservedRegisters)
{
    SKIP_IF_NO_AVR_TOOLS();
    std::string ours = CompileToAvr(R"(
int work(int a, int b)
{
    volatile long long x = a;
    long long y = x * x + (x << 40);
    long q = (long)a * b / 7;
    return (int)(y >> 3) + a * b + (int)q + (int)(y / 1000);
}
)");
    std::string check = R"(
    .text
    .globl  check
; Returns in r24 a bit per register that did not survive, 0 if all did.
check:
)";
    for (int r = 2; r <= 17; r++)
        check += "    push    r" + std::to_string(r) + "\n";
    check += "    push    r28\n    push    r29\n";
    for (int r = 2; r <= 17; r++)
        check += "    ldi     r26, " + std::to_string(0x40 + r) + "\n    mov     r" +
                 std::to_string(r) + ", r26\n";
    check += "    ldi     r28, 0x5a\n    ldi     r29, 0xa5\n";
    check += "    ldi     r24, 123\n    ldi     r25, 0\n    ldi     r22, 45\n    ldi     r23, 0\n";
    check += "    call    work\n    clr     r24\n";
    for (int r = 2; r <= 17; r++)
        check += "    ldi     r26, " + std::to_string(0x40 + r) + "\n    cpse    r" +
                 std::to_string(r) + ", r26\n    ori     r24, 1\n";
    check += "    cpi     r28, 0x5a\n    breq    1f\n    ori     r24, 2\n1:\n";
    check += "    cpi     r29, 0xa5\n    breq    2f\n    ori     r24, 4\n2:\n";
    check += "    tst     r1\n    breq    3f\n    ori     r24, 8\n3:\n";
    check += "    clr     r25\n    pop     r29\n    pop     r28\n";
    for (int r = 17; r >= 2; r--)
        check += "    pop     r" + std::to_string(r) + "\n";
    check += "    ret\n";
    std::string clang_src = R"(
void putbyte(int c);
int check(void);
int main(void)
{
    putbyte('0' + check());
    return 0;
}
)";
    EXPECT_EQ("0", Run(ours + check, "crt0.o", &clang_src, { "-O1" }, ".clang"));
}

// clang's code on our runtime: long long multiply and divide, float arithmetic and
// conversions, all through helpers compiled by genavr; the float results against the
// host's, bit for bit.
TEST_F(AvrTest, RunClangOnOurRuntime)
{
    SKIP_IF_NO_AVR_TOOLS();
    volatile float x = 1.75f, y = -0.3f;
    auto bits = [](float f) {
        uint32_t u;
        memcpy(&u, &f, 4);
        return std::to_string(u) + "UL";
    };
    EXPECT_EQ("ok", ClangRun(R"(
void putbyte(int c);
volatile long long a = -123456789012LL, b = 1000003;
volatile unsigned long long ua = 0xfedcba9876543210ULL, ub = 0x12345;
volatile float x = 1.75f, y = -0.3f;
volatile long l = -100000;
static unsigned long bits(float f) { return *(unsigned long *)&f; }
int main(void)
{
    if (a * b != -123457159382367036LL) return 1;
    if (a / b != -123456) return 2;
    if (a % b != -418644) return 3;
    if (ua / ub != 0xe0004fa01c4dULL || ua % ub != 0x10a4f) return 4;
    if (bits(x * y) != )" + bits(x * y) + R"( || bits(x / y) != )" + bits(x / y) +
                                 R"( || bits(x + y) != )" + bits(x + y) + R"() return 5;
    if ((long)(x * 1000) != 1750 || (float)l != -100000.0f) return 6;
    if ((long long)(x * 1e9f) != 1750000000LL) return 7;
    putbyte('o');
    putbyte('k');
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// Our headers against clang's own for the target: the same constants, types and
// layouts.  double is binary32 here, as on the clang side.
TEST_F(AvrTest, HeadersAgreeWithClang)
{
    SKIP_IF_NO_AVR_TOOLS();
    std::string values = R"(
#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
enum { NI = 26, NF = 8 };
void NAME(long long *i, double *f)
{
    long long iv[NI] = { sizeof(wchar_t), (wchar_t)-1 > 0, WCHAR_MIN, WCHAR_MAX,
                         WINT_MIN, WINT_MAX, sizeof(max_align_t), _Alignof(max_align_t),
                         SIZE_MAX, PTRDIFF_MIN, PTRDIFF_MAX, INTPTR_MIN, UINTPTR_MAX,
                         INT64_MIN, UINT32_MAX, CHAR_MIN, CHAR_MAX, LONG_MAX, INT_MIN,
                         UINT_MAX, sizeof(size_t) * 10 + sizeof(ptrdiff_t),
                         sizeof(int_fast16_t) * 10 + sizeof(int_least32_t),
                         sizeof(intmax_t), SIG_ATOMIC_MAX,
                         LDBL_MANT_DIG * 10000 + LDBL_MAX_EXP, DECIMAL_DIG + LDBL_DIG * 100 };
    double fv[NF] = { LDBL_EPSILON, LDBL_MIN, LDBL_MAX, LDBL_TRUE_MIN, DBL_EPSILON,
                      DBL_MAX, FLT_EPSILON, FLT_MIN_10_EXP + FLT_MAX_10_EXP };
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
void their_values(long long *i, double *f);
int main(void)
{
    long long oi[NI], ti[NI];
    double of[NF], tf[NF];
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

// The shared headers in a 16-bit int: RAND_MAX fits it, char32_t holds 32 bits.
TEST_F(AvrTest, SharedHeadersFitInt16)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("", CompileAndRunAvr(R"(
#include <stdlib.h>
#include <uchar.h>
#include <limits.h>
#include <inttypes.h>
#include <math.h>
int main(void)
{
    if (RAND_MAX != INT_MAX) return 1;
    if (sizeof(char32_t) != 4 || sizeof(char16_t) != 2) return 2;
    if ((char32_t)-1 < 0x7fffffff) return 3;
    if (sizeof(PRId32) != 3 || PRId32[0] != 'l' || PRIdPTR[0] != 'd') return 4;
    if (fabsf(-2.5f) != 2.5f || !(INFINITY > FLT_MAX)) return 5;
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}
