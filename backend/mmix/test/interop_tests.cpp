//
// MMIXware ABI interop with GCC over a table of signatures, both ways: the same source,
// one copy compiled by us (names prefixed our_) and one by GCC -O2 (their_), each calling
// the other's.  Then the register stack across both (a hand-written harness checks rJ,
// $254, rD and its own locals around a call), GCC's code on our runtime and libgcc, and
// our code under newlib.
//
#include "mmix_test.h"

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
struct s5 { char a[5]; };
struct s8 { int a; int b; };
struct s9 { char a[9]; };
struct s16 { long a; long b; };
struct s24 { long a; double d; char c; };
union un { long l; char c[3]; };
long regcheck(long (*f)(long), long a);
)";

const char sig_decls[] = R"(
signed char PFX_narrow(signed char a, unsigned char b, short c, char d, _Bool e,
                       unsigned short f);
unsigned char PFX_unarrow(unsigned k);
short PFX_snarrow(long k);
unsigned short PFX_usnarrow(long k);
char PFX_cnarrow(int k);
_Bool PFX_bnarrow(long k);
unsigned PFX_unsw(unsigned a, int b);
int PFX_sw(int a, unsigned b);
double PFX_many(int a, long b, const char *c, double d, float e, short f, unsigned g, long h,
                signed char i, double j, float k, unsigned char l, long m, int n,
                unsigned short o, double p, float q, short r);
float PFX_rf(float a, double b);
double PFX_fd(float a);
float PFX_fneg(float a);
struct s1 PFX_r1(struct s1 x, int k);
struct s2 PFX_r2(int k, struct s2 x);
struct s3 PFX_r3(struct s3 x, int k);
struct s4 PFX_r4(long pad, struct s4 x);
struct s5 PFX_r5(struct s5 x, char k);
struct s8 PFX_r8(struct s8 x, int k);
struct s9 PFX_r9(struct s9 x, int k);
struct s16 PFX_r16(double d, struct s16 x);
struct s24 PFX_r24(struct s24 x, int k);
union un PFX_ru(union un u, int k);
long PFX_mixed(int a, struct s3 b, double c, struct s9 d, struct s24 e, union un f, char g,
               struct s1 h);
long PFX_stackagg(long a1, long a2, long a3, long a4, long a5, long a6, long a7, long a8,
                  long a9, long a10, long a11, long a12, long a13, long a14, long a15,
                  struct s3 x, struct s16 y, struct s8 z);
int PFX_inc(int x);
int PFX_apply(int (*f)(int), int x);
int (*PFX_getinc(void))(int);
long PFX_leaf(long a);
long PFX_survive(long a);
long PFX_rec(long n, long a);
int PFX_check(void);
)";

const char sig_defs[] = R"(
signed char PFX_narrow(signed char a, unsigned char b, short c, char d, _Bool e,
                       unsigned short f)
{
    return (signed char)(a + (b >> 4) + c + d + e + (f >> 8));
}
unsigned char PFX_unarrow(unsigned k) { return (unsigned char)(k * 3); }
short PFX_snarrow(long k) { return (short)(k * 5); }
unsigned short PFX_usnarrow(long k) { return (unsigned short)(k * 5); }
char PFX_cnarrow(int k) { return (char)(k + 100); }
_Bool PFX_bnarrow(long k) { return k & 0x100; }
unsigned PFX_unsw(unsigned a, int b) { return a + b; }
int PFX_sw(int a, unsigned b) { return a * 2 - (int)(b >> 28); }
double PFX_many(int a, long b, const char *c, double d, float e, short f, unsigned g, long h,
                signed char i, double j, float k, unsigned char l, long m, int n,
                unsigned short o, double p, float q, short r)
{
    return a + b * 2 + c[1] + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 + j * 10 + k * 11 +
           l * 12 + m * 13 + n * 14 + o * 15 + p * 16 + q * 17 + r * 18;
}
float PFX_rf(float a, double b) { return a * 2 + (float)b; }
double PFX_fd(float a) { return a; }
float PFX_fneg(float a) { return -a; }
struct s1 PFX_r1(struct s1 x, int k) { x.a += k; return x; }
struct s2 PFX_r2(int k, struct s2 x) { x.a *= k; return x; }
struct s3 PFX_r3(struct s3 x, int k) { x.a[0] += k; x.a[2] -= k; return x; }
struct s4 PFX_r4(long pad, struct s4 x) { x.a -= (int)pad; return x; }
struct s5 PFX_r5(struct s5 x, char k)
{
    for (int i = 0; i < 5; i++)
        x.a[i] += k;
    return x;
}
struct s8 PFX_r8(struct s8 x, int k) { x.a += k; x.b -= k; return x; }
struct s9 PFX_r9(struct s9 x, int k) { x.a[0] += k; x.a[8] += k; return x; }
struct s16 PFX_r16(double d, struct s16 x) { x.a += (long)d; x.b = -x.b; return x; }
struct s24 PFX_r24(struct s24 x, int k) { x.a += k; x.d *= k; x.c += k; return x; }
union un PFX_ru(union un u, int k) { u.c[1] += k; return u; }
long PFX_mixed(int a, struct s3 b, double c, struct s9 d, struct s24 e, union un f, char g,
               struct s1 h)
{
    long s = a + b.a[1] + (long)c + d.a[4] + e.a + (long)e.d + e.c + f.c[0] + g + h.a;
    d.a[4] = 0;
    e.a    = 0;
    return s;
}
long PFX_stackagg(long a1, long a2, long a3, long a4, long a5, long a6, long a7, long a8,
                  long a9, long a10, long a11, long a12, long a13, long a14, long a15,
                  struct s3 x, struct s16 y, struct s8 z)
{
    return a1 + a15 * 2 + x.a[2] * 100 + y.b * 1000 + z.b * 100000;
}
int PFX_inc(int x) { return x + 1; }
int PFX_apply(int (*f)(int), int x) { return f(f(x)); }
int (*PFX_getinc(void))(int) { return PFX_inc; }

/* Values live across calls into the other side. */
long PFX_leaf(long a) { return a * 3 + 1; }
long PFX_survive(long a)
{
    long b = a * 3, c = a + 7, d = a ^ 0x55, e = a * a, f = a - 9, g = a << 3, h = ~a;
    long r = OTHER_leaf(a) + OTHER_leaf(b);
    return r + b + c + d + e + f + g + h;
}
/* Recursion alternating between the sides, deep enough to spill the register ring. */
long PFX_rec(long n, long a)
{
    if (n == 0)
        return a;
    long x = n * 3 + a, y = x * 7;
    long r = OTHER_rec(n - 1, a + 1);
    return r + y / 7;
}

/* A bit per check that failed. */
int PFX_check(void)
{
    int fail = 0;
    fail |= !(OTHER_narrow(-3, 0x95, -300, 100, 1, 0x1234) ==
              (signed char)(-3 + 9 - 300 + 100 + 1 + 0x12)) << 0;
    long n1 = OTHER_unarrow(1000), n2 = OTHER_snarrow(10000), n3 = OTHER_usnarrow(-1);
    long n4 = OTHER_cnarrow(100), n5 = OTHER_bnarrow(0x300);
    fail |= !(n1 == (unsigned char)3000 && n2 == (short)50000 &&
              n3 == (unsigned short)-5 && n4 == (char)200 && n5 == 1) << 1;
    long u1 = OTHER_unsw(0xfffffff0u, 5), u2 = OTHER_sw(-1000000000, 0xf0000000u);
    fail |= !(u1 == 0xfffffff5L && u2 == -2000000015L) << 2;
    double many = OTHER_many(-1, -2L, "xyz", 0.5, 1.5f, -3, 4000000000u, -5L, -6, 0.25, -2.5f,
                             200, 7L, -8, 60000, 0.125, 3.5f, -9);
    double want = -1 + -2L * 2 + 'y' + 0.5 * 4 + 1.5f * 5 + (short)-3 * 6 + 4000000000u * 7 +
                  -5L * 8 + (signed char)-6 * 9 + 0.25 * 10 + -2.5f * 11 +
                  (unsigned char)200 * 12 + 7L * 13 + -8 * 14 + (unsigned short)60000 * 15 +
                  0.125 * 16 + 3.5f * 17 + (short)-9 * 18;
    fail |= !(many == want) << 3;
    fail |= !(OTHER_rf(1.25f, 0.5) == 3.0f && OTHER_fd(0.1f) == (double)0.1f &&
              OTHER_fneg(3.5f) == -3.5f) << 4;

    struct s1 a = { 10 };
    struct s2 b = { 300 };
    struct s3 c = { { 1, 2, 3 } };
    struct s4 d = { 100 };
    struct s5 e = { { 1, 2, 3, 4, 5 } };
    struct s8 f = { 7, -7 };
    struct s9 g = { { 1, 2, 3, 4, 5, 6, 7, 8, 9 } };
    struct s16 h = { 10, 20 };
    struct s24 i = { 1000, 2.5, 'a' };
    union un j;
    j.l = 0;
    j.c[0] = 5;
    j.c[1] = 6;
    fail |= !(OTHER_r1(a, 2).a == 12 && a.a == 10 && OTHER_r2(3, b).a == 900 && b.a == 300)
            << 5;
    struct s3 c2 = OTHER_r3(c, 1);
    struct s4 d2 = OTHER_r4(30, d);
    struct s5 e2 = OTHER_r5(e, 10);
    fail |= !(c2.a[0] == 2 && c2.a[1] == 2 && c2.a[2] == 2 && c.a[0] == 1 && d2.a == 70 &&
              e2.a[0] == 11 && e2.a[4] == 15 && e.a[4] == 5) << 6;
    struct s8 f2 = OTHER_r8(f, 3);
    struct s9 g2 = OTHER_r9(g, 100);
    fail |= !(f2.a == 10 && f2.b == -10 && g2.a[0] == 101 && g2.a[8] == 109 && g2.a[4] == 5 &&
              g.a[0] == 1) << 7;
    struct s16 h2 = OTHER_r16(5.0, h);
    struct s24 i2 = OTHER_r24(i, 2);
    fail |= !(h2.a == 15 && h2.b == -20 && h.b == 20 && i2.a == 1002 && i2.d == 5.0 &&
              i2.c == 'c' && i.a == 1000 && i.d == 2.5 && i.c == 'a') << 8;
    union un j2 = OTHER_ru(j, 1);
    fail |= !(j2.c[0] == 5 && j2.c[1] == 7 && j.c[1] == 6) << 9;
    fail |= !(OTHER_mixed(1, c, 20.0, g, i, j, 3, a) ==
              1 + 2 + 20 + 5 + 1000 + 2 + 'a' + 5 + 3 + 10 && g.a[4] == 5 && i.a == 1000)
            << 10;
    fail |= !(OTHER_stackagg(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, c, h, f) ==
              1 + 30 + 300 + 20000 - 700000) << 11;
    fail |= !(OTHER_apply(PFX_inc, 5) == 7 && OTHER_getinc()(8) == 9 &&
              OTHER_apply(OTHER_getinc(), 1) == 3) << 12;

    long s = 7;
    long want_s = (7 * 3 + 1) + (21 * 3 + 1) + 21 + 14 + (7 ^ 0x55) + 49 - 2 + 56 + ~7L;
    fail |= !(OTHER_survive(s) == want_s && regcheck(OTHER_survive, s) == want_s) << 13;
    long depth = 3000, want_r = depth + 1;
    for (long k = depth, t = 1; k > 0; k--, t++)
        want_r += k * 3 + t;
    fail |= !(OTHER_rec(depth, 1) == want_r) << 14;
    return fail;
}
)";

// Calls f(a) with values in its own locals below the hole, and returns f's result when
// rJ (as pushgo set it), $254, rD and those locals came back unchanged; else minus a mask
// of what changed.
const char regcheck_asm[] = R"(
	.text
	.global	regcheck
	.p2align 2
regcheck:
	get	$2,rJ
	set	$3,$254
	get	$4,rD
	setl	$5,#1234
	set	$9,$1
	pushgo	$8,$0,0
L:rc:	get	$6,rJ
	geta	$7,L:rc
	cmpu	$10,$6,$7
	zsnz	$11,$10,1
	cmpu	$10,$254,$3
	zsnz	$10,$10,2
	or	$11,$11,$10
	get	$12,rD
	cmpu	$10,$12,$4
	zsnz	$10,$10,4
	or	$11,$11,$10
	setl	$12,#1234
	cmpu	$10,$5,$12
	zsnz	$10,$10,8
	or	$11,$11,$10
	negu	$12,0,$11
	csnz	$8,$11,$12
	put	rJ,$2
	set	$0,$8
	pop	1,0
)";

// Main of our side: both checks, as hex masks of the failed ones.
const char sig_main[] = R"(
void putbyte(int c);
static void hex(unsigned v)
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

} // namespace

// Narrow values both ways, eighteen arguments of every scalar kind with the last two on
// the stack, float as binary32 bits, structures of 1-24 bytes and a union as arguments
// (mixed with scalars, and on the stack) and as results through $251, a callee writing
// its by-reference copy, function pointers, values live across calls, recursion 3000
// deep alternating between the sides, and the register-stack harness.
TEST_F(MmixTest, RunSignatureTableWithGcc)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string ours   = Side(sig_types, sig_decls, sig_defs, "our", "their") + sig_main;
    std::string theirs = Side(sig_types, sig_decls, sig_defs, "their", "our");
    EXPECT_EQ("0000 0000", Run(CompileToMmix(ours.c_str()) + regcheck_asm, "crt0.o", &theirs,
                               { "-O2" }, ".gcc"));
    EXPECT_EQ(0, exit_status);
}

// The harness itself: a callee that changes $254 or rJ is caught.
TEST_F(MmixTest, RunRegcheckCatchesClobber)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string bad = R"(
	.text
	.global	clobber
	.p2align 2
clobber:
	subu	$254,$254,8
	pop	1,0
)";
    std::string ours = CompileToMmix(R"(
        long regcheck(long (*f)(long), long a);
        long clobber(long a);
        int main(void) { return regcheck(clobber, 5) == -2 ? 0 : 1; }
    )");
    EXPECT_EQ("", Run(ours + regcheck_asm + bad, "crt0.o"));
    EXPECT_EQ(0, exit_status);
}

// GCC's code on our runtime: our crt0.o and libc.a (printf, the string functions,
// malloc), then libgcc.a for the 128-bit helpers it calls.
TEST_F(MmixTest, RunGccOnOurRuntime)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string gcc = R"(
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned long mulhi(unsigned long a, unsigned long b)
{
    return (unsigned long)(((unsigned __int128)a * b) >> 64);
}
int main(void)
{
    char *p = malloc(32);
    strcpy(p, "gcc");
    strcat(p, " on ours");
    printf("%s %zu %d %ld %g %.3f\n", p, strlen(p), -42, -1234567890123L, 0.5, 3.14159);
    printf("%lx %d\n", mulhi(0x123456789abcdef0UL, 0xfedcba9876543210UL),
           memcmp("abc", "abd", 3) < 0);
    return 7;
}
)";
    char want[128];
    snprintf(want, sizeof want, "%s %d %d %ld %g %.3f\n%lx %d\n", "gcc on ours", 11, -42,
             -1234567890123L, 0.5, 3.14159,
             (unsigned long)(((unsigned __int128)0x123456789abcdef0UL * 0xfedcba9876543210UL) >> 64),
             1);
    EXPECT_EQ(want, GccOnOurRuntime(gcc));
    EXPECT_EQ(7, exit_status);
}

// Our code under newlib, linked by GCC with its startup: newlib's printf, strtod and
// qsort calling our comparison function.
TEST_F(MmixTest, RunOursUnderNewlib)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string ours = CompileToMmix(R"(
#include <stdio.h>
#include <stdlib.h>
static int cmp(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;
    return x < y ? -1 : x > y;
}
int main(void)
{
    long v[6] = { 42, -7, 1000000000000L, 0, -7, 3 };
    qsort(v, 6, sizeof v[0], cmp);
    for (int i = 0; i < 6; i++)
        printf("%ld ", v[i]);
    char *end;
    double d = strtod("  -2.5e3xyz", &end);
    printf("%g %s %d %5.2f|%-4s|%c\n", d, end, (int)sizeof(long), 3.14159, "ab", 'z');
    return 3;
}
)");
    EXPECT_EQ("-7 -7 0 3 42 1000000000000 -2500 xyz 8  3.14|ab  |z\n", NewlibRunOurs(ours));
    EXPECT_EQ(3, exit_status);
}
