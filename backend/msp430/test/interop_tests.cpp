//
// MSP430 EABI interop over a table of signatures, both ways: the same source, one copy
// compiled by us (names prefixed our_) and one by GCC -O1 (their_), each calling the
// other's; the same with clang, structure arguments left out (clang copies them onto the
// stack, GCC and we pass their address).  Then the registers our code must preserve,
// GCC's and clang's code on our runtime, our helpers against libgcc's, and our code
// with newlib.
//
#include <cmath>
#include <cstdint>
#include <cstring>

#include "msp430_test.h"
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

// The declarations of both copies of `defs`, by its own declaration lines `decls`; AGG
// says whether the structure arguments are in.
std::string BothSides(bool agg, const char *types, const char *decls, const char *defs,
                      const char *pfx, const char *other)
{
    return std::string("#define AGG ") + (agg ? "1" : "0") + "\n" + types +
           Subst(decls, "our", "their") + Subst(decls, "their", "our") + Subst(defs, pfx, other);
}

const char sig_types[] = R"(
struct s1 { char a; };
struct s2 { int a; };
struct s3 { char a[3]; };
struct s4 { int a; int b; };
struct s6 { char a; int b; char c; };
struct s10 { char c; long l; int i; char d; };
union un { long l; char c[3]; };
struct nest { struct { char x; int y; } p; char c; };
)";

const char sig_decls[] = R"(
signed char PFX_narrow(signed char a, unsigned char b, short c, char d, _Bool e);
unsigned char PFX_unarrow(unsigned k);
long PFX_split(int a, int b, int c, long d);
long PFX_pair(int a, long b, long c);
long PFX_llback(int a, long long b, int c, int d);
int PFX_five(int a, int b, int c, int d, int e);
long long PFX_ll(long long a, int b, long long c);
double PFX_fp(float a, double b, long double c, int d, float e);
float PFX_rf(double x);
long long PFX_rll(long x);
struct s4 PFX_mk4(int a, int b);
union un PFX_mku(long l);
#if AGG
struct s1 PFX_r1(struct s1 x, int k);
struct s2 PFX_r2(int k, struct s2 x);
struct s3 PFX_r3(struct s3 x, int k);
struct s4 PFX_r4(struct s4 x, int k);
struct s6 PFX_r6(long pad, struct s6 x, int k);
struct s10 PFX_r10(int a, int b, int c, int d, struct s10 x);
int PFX_mixed(struct nest a, union un b, int k);
long PFX_aggsplit(struct s4 s, int a, int b, long c);
#endif
int PFX_inc(int x);
int PFX_apply(int (*f)(int), int x);
int (*PFX_getinc(void))(int);
int PFX_check(void);
)";

const char sig_defs[] = R"(
signed char PFX_narrow(signed char a, unsigned char b, short c, char d, _Bool e)
{
    return (signed char)(a + (b >> 4) + c + d + e);
}
unsigned char PFX_unarrow(unsigned k) { return (unsigned char)(k * 3); }
long PFX_split(int a, int b, int c, long d) { return a + b * 10 + c * 100 + d; }
long PFX_pair(int a, long b, long c) { return a + b * 10 + c * 100; }
long PFX_llback(int a, long long b, int c, int d)
{
    return a + (long)(b >> 20) * 10 + c * 100L + d * 1000L;
}
int PFX_five(int a, int b, int c, int d, int e) { return a - b + c * 2 - d + e * 3; }
long long PFX_ll(long long a, int b, long long c) { return a * b + c; }
double PFX_fp(float a, double b, long double c, int d, float e)
{
    return a + b * 10 + c * 100 + d * 1000 + e * 10000;
}
float PFX_rf(double x) { return x / 4; }
long long PFX_rll(long x) { return (long long)x * x; }
struct s4 PFX_mk4(int a, int b) { struct s4 s = { a, b }; return s; }
union un PFX_mku(long l) { union un u; u.l = l; return u; }
#if AGG
struct s1 PFX_r1(struct s1 x, int k) { x.a += k; return x; }
struct s2 PFX_r2(int k, struct s2 x) { x.a *= k; return x; }
struct s3 PFX_r3(struct s3 x, int k) { x.a[0] += k; x.a[2] -= k; return x; }
struct s4 PFX_r4(struct s4 x, int k) { x.a += k; x.b -= k; return x; }
struct s6 PFX_r6(long pad, struct s6 x, int k)
{
    x.a += k;
    x.b += (int)pad;
    x.c -= k;
    return x;
}
struct s10 PFX_r10(int a, int b, int c, int d, struct s10 x)
{
    x.c += a;
    x.l += b;
    x.i += c;
    x.d += d;
    return x;
}
int PFX_mixed(struct nest a, union un b, int k)
{
    a.c = 0;
    b.l = 0;
    return k;
}
long PFX_aggsplit(struct s4 s, int a, int b, long c) { return s.a + s.b + a + b + c; }
#endif
int PFX_inc(int x) { return x + 1; }
int PFX_apply(int (*f)(int), int x) { return f(f(x)); }
int (*PFX_getinc(void))(int) { return PFX_inc; }

/* A bit per check that failed. */
int PFX_check(void)
{
    int fail = 0;
    fail |= !(OTHER_narrow(-3, 0x95, -300, 100, 1) == (signed char)(-3 + 9 - 300 + 100 + 1) &&
              OTHER_unarrow(1000) == (unsigned char)3000) << 0;
    fail |= !(OTHER_split(1, 2, 3, 0x12345678L) == 0x12345678L + 321) << 1;
    fail |= !(OTHER_pair(1, 70000L, -2L) == 1 + 700000L - 200) << 2;
    fail |= !(OTHER_llback(1, 5LL << 20, 3, 4) == 1 + 50 + 300 + 4000) << 3;
    fail |= !(OTHER_five(1, 2, 3, 4, 5) == 1 - 2 + 6 - 4 + 15) << 4;
    fail |= !(OTHER_ll(-3000000000LL, 3, 7) == -8999999993LL &&
              OTHER_rll(-100000L) == 10000000000LL) << 5;
    fail |= !(OTHER_fp(1, 2, 3, 4, 5) == 1 + 20 + 300 + 4000 + 50000 &&
              OTHER_rf(10) == 2.5f) << 6;
    struct s4 m = OTHER_mk4(-7, 9);
    union un u = OTHER_mku(0x10203L);
    fail |= !(m.a == -7 && m.b == 9 && u.c[0] == 3 && u.c[2] == 1) << 7;
    fail |= !(OTHER_apply(PFX_inc, 5) == 7 && OTHER_getinc()(8) == 9 &&
              OTHER_apply(OTHER_getinc(), 1) == 3) << 8;
#if AGG
    struct s1 a = { 10 };
    struct s2 b = { 300 };
    struct s3 c = { { 1, 2, 3 } };
    struct s4 d = { 100, -5 };
    struct s6 e = { 1, 2, 3 };
    struct s10 f = { 'a', 100000L, 7, 'z' };
    struct nest n = { { 3, 4 }, 5 };
    union un v;
    v.l = 6;
    fail |= !(OTHER_r1(a, 2).a == 12 && a.a == 10 && OTHER_r2(3, b).a == 900 && b.a == 300) << 9;
    struct s3 c2 = OTHER_r3(c, 1);
    fail |= !(c2.a[0] == 2 && c2.a[2] == 2 && c.a[0] == 1) << 10;
    struct s4 d2 = OTHER_r4(d, 7);
    fail |= !(d2.a == 107 && d2.b == -12 && d.a == 100) << 11;
    struct s6 e2 = OTHER_r6(1000L, e, 1);
    fail |= !(e2.a == 2 && e2.b == 1002 && e2.c == 2 && e.b == 2) << 12;
    struct s10 f2 = OTHER_r10(1, 2, 3, -1, f);
    fail |= !(f2.c == 'b' && f2.l == 100002L && f2.i == 10 && f2.d == 'y' && f.l == 100000L)
            << 13;
    fail |= !(OTHER_mixed(n, v, 9) == 9 && n.c == 5 && v.l == 6) << 14;
    fail |= !(OTHER_aggsplit(d, 1, 2, 0x12345678L) == 0x12345678L + 98) << 15;
#endif
    return fail;
}
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

// Narrow values, the split long, a long in r13:r14, a long long on the stack with later
// ints in registers, five ints, long long and every FP type, structures of every size
// and shape as arguments and results, function pointers.
TEST_F(Msp430Test, RunSignatureTableWithGcc)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    std::string ours =
        BothSides(true, sig_types, sig_decls, sig_defs, "our", "their") + sig_main;
    std::string theirs = BothSides(true, sig_types, sig_decls, sig_defs, "their", "our");
    EXPECT_EQ("0000 0000", GccRun(theirs, CompileToMsp430(ours.c_str())));
}

// The same with clang, but for the structure arguments.
TEST_F(Msp430Test, RunSignatureTableWithClang)
{
    SKIP_IF_NO_MSP430_CLANG();
    std::string ours =
        BothSides(false, sig_types, sig_decls, sig_defs, "our", "their") + sig_main;
    std::string theirs = BothSides(false, sig_types, sig_decls, sig_defs, "their", "our");
    EXPECT_EQ("0000 0000", ClangRun(theirs, CompileToMsp430(ours.c_str())));
}

// R4-R10 survive our calls: a hand-written caller fills them, calls our code, which
// multiplies, divides and does floating point through the helpers, and checks them.
TEST_F(Msp430Test, RunPreservedRegisters)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    std::string ours = CompileToMsp430(R"(
int work(int a, int b)
{
    volatile long long x = a;
    long long y = x * x + (x << 40);
    long q = (long)a * b / 7;
    double d = (double)a / b;
    return (int)(y >> 3) + a * b + (int)q + (int)(y / 1000) + (int)(d * 100);
}
)");
    std::string check = R"(
    .text
    .globl  check
; Returns in r12 a bit per register that did not survive, 0 if all did.
check:
)";
    for (int r = 4; r <= 10; r++)
        check += "    push    r" + std::to_string(r) + "\n";
    for (int r = 4; r <= 10; r++)
        check += "    mov     #" + std::to_string(0x1111 * (r - 3)) + ", r" + std::to_string(r) +
                 "\n";
    check += "    mov     #123, r12\n    mov     #45, r13\n    call    #work\n    clr     r12\n";
    for (int r = 4; r <= 10; r++)
        check += "    cmp     #" + std::to_string(0x1111 * (r - 3)) + ", r" + std::to_string(r) +
                 "\n    jeq     1f\n    bis     #" + std::to_string(1 << (r - 4)) +
                 ", r12\n1:\n";
    for (int r = 10; r >= 4; r--)
        check += "    pop     r" + std::to_string(r) + "\n";
    check += "    ret\n";
    std::string main_src = R"(
void putbyte(int c);
int check(void);
int main(void)
{
    int bad = check();
    putbyte('0' + (bad >> 4));
    putbyte('0' + (bad & 15));
    return 0;
}
)";
    EXPECT_EQ("00", GccRun(main_src, ours + check));
}

namespace {

// A program for GCC or clang that exercises every runtime helper their code calls: the
// 64-bit integer operations, shifts by a variable count, float and double arithmetic,
// conversions and comparisons, NaN included; it prints each result in hex.  `nan_order`
// adds the ordered comparisons with a NaN, which clang answers wrong for > and >=.
std::string RuntimeProgram(bool nan_order)
{
    std::string src = R"(
void putbyte(int c);
static void hex(unsigned long long v, int digits)
{
    while (digits-- > 0)
        putbyte("0123456789abcdef"[(v >> (4 * digits)) & 15]);
    putbyte(' ');
}
static unsigned long fbits(float f) { union { float f; unsigned long u; } x = { f }; return x.u; }
static unsigned long long dbits(double d)
{
    union { double d; unsigned long long u; } x = { d };
    return x.u;
}
volatile long long a = -123456789012LL, b = 1000003;
volatile unsigned long long ua = 0xfedcba9876543210ULL, ub = 0x12345;
volatile long la = -100000, lb = 7;
volatile int ia = -1234, ib = 5, n = 3, n2 = 13;
volatile unsigned ua16 = 0xbeef;
volatile float fx = 1.75f, fy = -0.3f;
volatile double dx = 1.75, dy = -0.3, nan0 = 0.0;
int main(void)
{
    hex(a * b, 16); hex(a / b, 16); hex(a % b, 16); hex(ua / ub, 16); hex(ua % ub, 16);
    hex(la * lb, 8); hex(la / lb, 8); hex(la % lb, 8);
    hex((unsigned long)la / (unsigned long)lb, 8);
    hex(ia * ib, 4); hex(ia / ib, 4); hex(ia % ib, 4); hex(ua16 / 7u, 4);
    hex(ia << n, 4); hex(ia >> n, 4); hex(ua16 >> n2, 4);
    hex(la << n2, 8); hex(la >> n, 8); hex((unsigned long)la >> n2, 8);
    hex(a << n2, 16); hex(a >> n, 16); hex(ua >> n2, 16);
    hex(fbits(fx + fy), 8); hex(fbits(fx - fy), 8); hex(fbits(fx * fy), 8);
    hex(fbits(fx / fy), 8);
    hex(dbits(dx + dy), 16); hex(dbits(dx - dy), 16); hex(dbits(dx * dy), 16);
    hex(dbits(dx / dy), 16);
    hex(dbits(fx), 16); hex(fbits((float)dy), 8);
    hex((long)(fx * 1000), 8); hex((long)(dy * 1e6), 8);
    hex((unsigned long)(dx * 1e9), 8); hex((long long)(dx * 1e15), 16);
    hex((long long)(fx * 1e9f), 16);
    hex(fbits((float)la), 8); hex(dbits((double)la), 16); hex(dbits((double)a), 16);
    hex(fbits((float)a), 8); hex(dbits((double)ia), 16); hex(fbits((float)ua16), 8);
    double qn = nan0 / nan0;
    float fn = (float)qn;
    hex((qn == qn) | (qn != qn) << 1 | (dx == dy) << 2 | (dx != dy) << 3 | (fn == fn) << 4 |
        (fn != fn) << 5, 2);
)";
    if (nan_order)
        src += R"(
    hex((qn < dx) | (qn <= dx) << 1 | (qn > dx) << 2 | (qn >= dx) << 3 | (fn < fx) << 4 |
        (fn <= fx) << 5 | (fn > fx) << 6 | (fn >= fx) << 7, 2);
)";
    src += R"(
    hex((dx < dy) | (dx <= dy) << 1 | (dx > dy) << 2 | (dx >= dy) << 3 | (fx < fy) << 4 |
        (fx <= fy) << 5 | (fx > fy) << 6 | (fx >= fy) << 7, 2);
    return 0;
}
)";
    return src;
}

std::string Hex(uint64_t v, int digits)
{
    std::string s;
    while (digits-- > 0)
        s += "0123456789abcdef"[(v >> (4 * digits)) & 15];
    return s + " ";
}

uint32_t FBits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

uint64_t DBits(double d)
{
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

// The program's output, computed here on the host with the target's types.
std::string RuntimeExpected(bool nan_order)
{
    volatile int64_t a = -123456789012LL, b = 1000003;
    volatile uint64_t ua = 0xfedcba9876543210ULL, ub = 0x12345;
    volatile int32_t la = -100000, lb = 7;
    volatile int16_t ia = -1234, ib = 5;
    int n = 3, n2 = 13;
    volatile uint16_t ua16 = 0xbeef;
    volatile float fx = 1.75f, fy = -0.3f;
    volatile double dx = 1.75, dy = -0.3, nan0 = 0.0;
    std::string s;
    s += Hex(a * b, 16) + Hex(a / b, 16) + Hex(a % b, 16) + Hex(ua / ub, 16) + Hex(ua % ub, 16);
    s += Hex((uint32_t)(la * lb), 8) + Hex((uint32_t)(la / lb), 8) + Hex((uint32_t)(la % lb), 8);
    s += Hex((uint32_t)la / (uint32_t)lb, 8);
    s += Hex((uint16_t)(ia * ib), 4) + Hex((uint16_t)(ia / ib), 4) + Hex((uint16_t)(ia % ib), 4) +
         Hex((uint16_t)(ua16 / 7u), 4);
    // cppcheck-suppress shiftNegativeLHS ; the arithmetic shift is the expectation
    s += Hex((uint16_t)(int16_t)(ia * (1 << n)), 4) + Hex((uint16_t)(ia >> n), 4) +
         Hex((uint16_t)(ua16 >> n2), 4);
    // cppcheck-suppress shiftNegativeLHS ; the arithmetic shift is the expectation
    s += Hex((uint32_t)((uint32_t)la << n2), 8) + Hex((uint32_t)(la >> n), 8) +
         Hex((uint32_t)la >> n2, 8);
    // cppcheck-suppress shiftNegativeLHS ; the arithmetic shift is the expectation
    s += Hex((uint64_t)a << n2, 16) + Hex((uint64_t)(a >> n), 16) + Hex(ua >> n2, 16);
    s += Hex(FBits(fx + fy), 8) + Hex(FBits(fx - fy), 8) + Hex(FBits(fx * fy), 8) +
         Hex(FBits(fx / fy), 8);
    s += Hex(DBits(dx + dy), 16) + Hex(DBits(dx - dy), 16) + Hex(DBits(dx * dy), 16) +
         Hex(DBits(dx / dy), 16);
    s += Hex(DBits(fx), 16) + Hex(FBits((float)dy), 8);
    s += Hex((uint32_t)(int32_t)(fx * 1000), 8) + Hex((uint32_t)(int32_t)(dy * 1e6), 8);
    s += Hex((uint32_t)(dx * 1e9), 8) + Hex((uint64_t)(int64_t)(dx * 1e15), 16);
    s += Hex((uint64_t)(int64_t)(fx * 1e9f), 16);
    s += Hex(FBits((float)la), 8) + Hex(DBits((double)la), 16) + Hex(DBits((double)a), 16);
    s += Hex(FBits((float)a), 8) + Hex(DBits((double)ia), 16) + Hex(FBits((float)ua16), 8);
    // cppcheck-suppress duplicateExpression ; 0.0 / 0.0 makes the quiet NaN
    double qn = nan0 / nan0;
    float fn  = (float)qn;
    s += Hex((qn == qn) | (qn != qn) << 1 | (dx == dy) << 2 | (dx != dy) << 3 | (fn == fn) << 4 |
                 (fn != fn) << 5,
             2);
    if (nan_order)
        s += Hex((qn < dx) | (qn <= dx) << 1 | (qn > dx) << 2 | (qn >= dx) << 3 |
                     (fn < fx) << 4 | (fn <= fx) << 5 | (fn > fx) << 6 | (fn >= fx) << 7,
                 2);
    s += Hex((dx < dy) | (dx <= dy) << 1 | (dx > dy) << 2 | (dx >= dy) << 3 | (fx < fy) << 4 |
                 (fx <= fy) << 5 | (fx > fy) << 6 | (fx >= fy) << 7,
             2);
    return s;
}

} // namespace

// GCC's code on our runtime: every helper it calls, results against the host's bit for
// bit, NaN comparisons included.  Linked once with libgcc.a after our libc.a, once with
// our runtime alone, which then serves all of GCC's calls.
TEST_F(Msp430Test, RunGccOnOurRuntime)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    std::string expected = RuntimeExpected(true);
    EXPECT_EQ(expected, GccRun(RuntimeProgram(true)));
    EXPECT_EQ(0, exit_status);
    QemuConfig alone = msp430_gcc_config();
    alone.extra_libs.clear();
    std::string src = RuntimeProgram(true);
    EXPECT_EQ(expected, Run(alone, "", "crt0.o", &src, { "-O1" }, ".alone"));
    EXPECT_EQ(0, exit_status);
}

// clang's code on our runtime: the R8-R11 shims and __mspabi_cmpd/cmpf too.  Its
// ordered comparisons with a NaN are left out: through cmpd's one result, > and >= come
// out true.
TEST_F(Msp430Test, RunClangOnOurRuntime)
{
    SKIP_IF_NO_MSP430_CLANG();
    EXPECT_EQ(RuntimeExpected(false), ClangRun(RuntimeProgram(false)));
    EXPECT_EQ(0, exit_status);
}

namespace {

// Calls of the helpers with a 64-bit first operand in R8-R11, from C: w<name>(a, b) takes
// a in R12-R15 and b (64 bits, or a shift count) on the stack, as the ordinary ABI
// passes them, and calls <name> with a in R8-R11 and b in R12-R15 (or R12).
std::string Wrappers(const std::vector<std::string> &names)
{
    std::string s = R"(    .text
    .macro  wrap name, wide
    .globl  w\name
w\name:
    push    r10
    push    r9
    push    r8
    mov     r12, r8
    mov     r13, r9
    mov     r14, r10
    mov     r15, r11
    mov     8(r1), r12
    .if     \wide
    mov     10(r1), r13
    mov     12(r1), r14
    mov     14(r1), r15
    .endif
    call    #\name
    pop     r8
    pop     r9
    pop     r10
    ret
    .endm
)";
    for (const std::string &n : names) {
        bool shift = n.find("ll", n.size() - 2) != std::string::npos && n.find("_s") != n.npos;
        for (const char *pfx : { "", "gcc" })
            s += "    wrap    " + std::string(pfx) + n + ", " + (shift ? "0" : "1") + "\n";
    }
    return s;
}

} // namespace

// Our runtime against libgcc's (and libmul_none's), helper by helper over the MSP430
// EABI names GCC's code calls: libgcc's copy is linked beside ours with every symbol
// prefixed "gcc".  The cases C leaves undefined are skipped: a zero divisor, the most
// negative dividend over -1, a conversion out of range.  Results are compared bit for
// bit, any NaN with any NaN; a comparison by its sign.
TEST_F(Msp430Test, RunHelpersAgreeWithLibgcc)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    std::string libgcc = MSP430_LIBGCC;
    std::string libmul = libgcc.substr(0, libgcc.find_last_of('/') + 1) + "libmul_none.a";
    std::string pgcc   = TEST_DIR "/HelpersAgree.libgcc.a";
    std::string pmul   = TEST_DIR "/HelpersAgree.libmul.a";
    std::string log    = TEST_DIR "/HelpersAgree.log";
    ASSERT_EQ(0, RunTool({ MSP430_OBJCOPY, "--prefix-symbols=gcc", libgcc, pgcc }, log))
        << ReadFile(log);
    ASSERT_EQ(0, RunTool({ MSP430_OBJCOPY, "--prefix-symbols=gcc", libmul, pmul }, log))
        << ReadFile(log);
    QemuConfig cfg = msp430_gcc_config();
    cfg.extra_libs = { MSP430_LIBGCC, pgcc, pmul };

    // Four programs, each small enough for the ROM with both runtimes: the integers,
    // binary32, binary64, and the conversions.
    const std::vector<std::string> wrapped[] = {
        { "__mspabi_mpyll", "__mspabi_divlli", "__mspabi_divull", "__mspabi_remlli",
          "__mspabi_remull", "__mspabi_sllll", "__mspabi_srlll", "__mspabi_srall" },
        {},
        { "__mspabi_addd", "__mspabi_subd", "__mspabi_mpyd", "__mspabi_divd", "__mspabi_cmpd" },
        {},
    };
    std::string src = R"(
void putbyte(int c);
static void hex(unsigned long long v, int digits)
{
    while (digits-- > 0)
        putbyte("0123456789abcdef"[(v >> (4 * digits)) & 15]);
    putbyte(' ');
}
static void puts_(const char *s) { while (*s) putbyte(*s++); }
void *memcpy(void *d, const void *s, unsigned n);
void *gccmemcpy(void *d, const void *s, unsigned n) { return memcpy(d, s, n); }
static int bad;
static void mismatch(const char *name, int i, int j, unsigned long long ours,
                     unsigned long long theirs, int digits)
{
    if (++bad > 40)
        return;
    puts_(name);
    putbyte(' ');
    hex(i, 2);
    hex(j, 2);
    hex(ours, digits);
    hex(theirs, digits);
    putbyte('\n');
}
typedef union { double d; unsigned long long u; } D;
typedef union { float f; unsigned long u; } F;
static int dnan(unsigned long long u) { return (u >> 52 & 0x7ff) == 0x7ff && (u << 12); }
static int fnan(unsigned long u) { return (u >> 23 & 0xff) == 0xff && (u << 9); }
static int dsame(unsigned long long a, unsigned long long b) { return a == b || (dnan(a) && dnan(b)); }
static int fsame(unsigned long a, unsigned long b) { return a == b || (fnan(a) && fnan(b)); }
static int sgn(long v) { return v < 0 ? -1 : v > 0; }

#define DECL2(T, R, n) R n(T, T); R gcc##n(T, T);
#define DECL1(T, R, n) R n(T); R gcc##n(T);
#define WDECL(T, R, n) R w##n(T, T); R wgcc##n(T, T);
DECL2(int, int, __mspabi_divi) DECL2(int, int, __mspabi_remi)
DECL2(unsigned, unsigned, __mspabi_divu) DECL2(unsigned, unsigned, __mspabi_remu)
DECL2(int, int, __mspabi_mpyi)
DECL2(long, long, __mspabi_divli) DECL2(long, long, __mspabi_remli)
DECL2(unsigned long, unsigned long, __mspabi_divul)
DECL2(unsigned long, unsigned long, __mspabi_remul)
DECL2(long, long, __mspabi_mpyl)
int __mspabi_slli(int, int); int gcc__mspabi_slli(int, int);
int __mspabi_srai(int, int); int gcc__mspabi_srai(int, int);
unsigned __mspabi_srli(unsigned, int); unsigned gcc__mspabi_srli(unsigned, int);
long __mspabi_slll(long, int); long gcc__mspabi_slll(long, int);
long __mspabi_sral(long, int); long gcc__mspabi_sral(long, int);
unsigned long __mspabi_srll(unsigned long, int); unsigned long gcc__mspabi_srll(unsigned long, int);
WDECL(long long, long long, __mspabi_mpyll) WDECL(long long, long long, __mspabi_divlli)
WDECL(long long, long long, __mspabi_remlli)
WDECL(unsigned long long, unsigned long long, __mspabi_divull)
WDECL(unsigned long long, unsigned long long, __mspabi_remull)
long long w__mspabi_sllll(long long, int); long long wgcc__mspabi_sllll(long long, int);
long long w__mspabi_srall(long long, int); long long wgcc__mspabi_srall(long long, int);
unsigned long long w__mspabi_srlll(unsigned long long, int);
unsigned long long wgcc__mspabi_srlll(unsigned long long, int);
DECL2(float, float, __mspabi_addf) DECL2(float, float, __mspabi_subf)
DECL2(float, float, __mspabi_mpyf) DECL2(float, float, __mspabi_divf)
DECL2(float, int, __mspabi_cmpf)
WDECL(double, double, __mspabi_addd) WDECL(double, double, __mspabi_subd)
WDECL(double, double, __mspabi_mpyd) WDECL(double, double, __mspabi_divd)
WDECL(double, int, __mspabi_cmpd)
DECL1(float, double, __mspabi_cvtfd) DECL1(double, float, __mspabi_cvtdf)
DECL1(double, long, __mspabi_fixdli) DECL1(double, unsigned long, __mspabi_fixdul)
DECL1(double, long long, __mspabi_fixdlli) DECL1(double, unsigned long long, __mspabi_fixdull)
DECL1(float, long, __mspabi_fixfli) DECL1(float, long long, __mspabi_fixflli)
unsigned long __mspabi_fixful(float); unsigned long gcc__fixunssfsi(float);
unsigned long long __mspabi_fixfull(float); unsigned long long gcc__fixunssfdi(float);
DECL1(long, double, __mspabi_fltlid) DECL1(unsigned long, double, __mspabi_fltuld)
DECL1(long long, double, __mspabi_fltllid) DECL1(unsigned long long, double, __mspabi_fltulld)
DECL1(long, float, __mspabi_fltlif) DECL1(unsigned long, float, __mspabi_fltulf)
DECL1(long long, float, __mspabi_fltllif) DECL1(unsigned long long, float, __mspabi_fltullf)
DECL1(int, double, __mspabi_fltid) DECL1(unsigned, double, __mspabi_fltud)
DECL1(int, float, __mspabi_fltif) DECL1(unsigned, float, __mspabi_fltuf)

static const int i16[] = { 0, 1, -1, 2, 7, -7, 3, 32767, -32767 - 1, 1000, -1000, 12345, 255 };
static const long i32[] = { 0, 1, -1, 7, -7, 2147483647L, -2147483647L - 1, 100000L,
                            -100000L, 123456789L, 65536L, -65537L, 0x7fffL };
static const long long i64[] = { 0, 1, -1, 7, -7, 9223372036854775807LL,
                                 -9223372036854775807LL - 1, 4294967296LL, -4294967297LL,
                                 123456789012345LL, 0x12345678LL, 1000003LL };
static const unsigned long long dv[] = {
    0, 0x8000000000000000ULL, 0x3ff0000000000000ULL, 0xbff0000000000000ULL,
    0x3fb999999999999aULL, 0x3ff8000000000000ULL, 0xc006000000000000ULL,
    0x7e37e43c8800759cULL, 0x8000000000000001ULL, 0x0010000000000000ULL,
    0x000fffffffffffffULL, 0x7fefffffffffffffULL, 0x7ff0000000000000ULL,
    0xfff0000000000000ULL, 0x7ff8000000000000ULL, 0x41e65a0bc0000000ULL,
    0xc1e65a0bc0000000ULL, 0x43e0000000000000ULL, 0x3fe0000000000000ULL,
    0x4004000000000000ULL, 0x400c000000000000ULL, 0x4330000000000001ULL,
    0x3ff0000000000001ULL, 0x433fffffffffffffULL };
static const unsigned long fv[] = { 0, 0x80000000UL, 0x3f800000UL, 0xbf800000UL, 0x3dcccccdUL,
                                    0x3fc00000UL, 0xc0300000UL, 0x7f7fffffUL, 0x00000001UL,
                                    0x00800000UL, 0x007fffffUL, 0x7f800000UL, 0xff800000UL,
                                    0x7fc00000UL, 0x4f000000UL, 0xcf000000UL, 0x5f000000UL,
                                    0x3f000000UL, 0x40200000UL, 0x4b000001UL, 0x3f800001UL };
#define N(a) (int)(sizeof(a) / sizeof(a[0]))

int main(void)
{
#if PART == 0
    for (int i = 0; i < N(i16); i++)
        for (int j = 0; j < N(i16); j++) {
            int x = i16[i], y = i16[j];
            unsigned ux = x, uy = y;
            if (__mspabi_mpyi(x, y) != gcc__mspabi_mpyi(x, y))
                mismatch("mpyi", i, j, __mspabi_mpyi(x, y), gcc__mspabi_mpyi(x, y), 4);
            if (y != 0 && !(x == -32767 - 1 && y == -1)) {
                if (__mspabi_divi(x, y) != gcc__mspabi_divi(x, y))
                    mismatch("divi", i, j, __mspabi_divi(x, y), gcc__mspabi_divi(x, y), 4);
                if (__mspabi_remi(x, y) != gcc__mspabi_remi(x, y))
                    mismatch("remi", i, j, __mspabi_remi(x, y), gcc__mspabi_remi(x, y), 4);
            }
            if (uy != 0) {
                if (__mspabi_divu(ux, uy) != gcc__mspabi_divu(ux, uy))
                    mismatch("divu", i, j, __mspabi_divu(ux, uy), gcc__mspabi_divu(ux, uy), 4);
                if (__mspabi_remu(ux, uy) != gcc__mspabi_remu(ux, uy))
                    mismatch("remu", i, j, __mspabi_remu(ux, uy), gcc__mspabi_remu(ux, uy), 4);
            }
        }
    for (int i = 0; i < N(i16); i++)
        for (int n = 0; n < 16; n++) {
            int x = i16[i];
            if (__mspabi_slli(x, n) != gcc__mspabi_slli(x, n))
                mismatch("slli", i, n, __mspabi_slli(x, n), gcc__mspabi_slli(x, n), 4);
            if (__mspabi_srai(x, n) != gcc__mspabi_srai(x, n))
                mismatch("srai", i, n, __mspabi_srai(x, n), gcc__mspabi_srai(x, n), 4);
            if (__mspabi_srli(x, n) != gcc__mspabi_srli(x, n))
                mismatch("srli", i, n, __mspabi_srli(x, n), gcc__mspabi_srli(x, n), 4);
        }
    for (int i = 0; i < N(i32); i++)
        for (int j = 0; j < N(i32); j++) {
            long x = i32[i], y = i32[j];
            unsigned long ux = x, uy = y;
            if (__mspabi_mpyl(x, y) != gcc__mspabi_mpyl(x, y))
                mismatch("mpyl", i, j, __mspabi_mpyl(x, y), gcc__mspabi_mpyl(x, y), 8);
            if (y != 0 && !(x == -2147483647L - 1 && y == -1)) {
                if (__mspabi_divli(x, y) != gcc__mspabi_divli(x, y))
                    mismatch("divli", i, j, __mspabi_divli(x, y), gcc__mspabi_divli(x, y), 8);
                if (__mspabi_remli(x, y) != gcc__mspabi_remli(x, y))
                    mismatch("remli", i, j, __mspabi_remli(x, y), gcc__mspabi_remli(x, y), 8);
            }
            if (uy != 0) {
                if (__mspabi_divul(ux, uy) != gcc__mspabi_divul(ux, uy))
                    mismatch("divul", i, j, __mspabi_divul(ux, uy), gcc__mspabi_divul(ux, uy), 8);
                if (__mspabi_remul(ux, uy) != gcc__mspabi_remul(ux, uy))
                    mismatch("remul", i, j, __mspabi_remul(ux, uy), gcc__mspabi_remul(ux, uy), 8);
            }
        }
    for (int i = 0; i < N(i32); i++)
        for (int n = 0; n < 32; n++) {
            long x = i32[i];
            if (__mspabi_slll(x, n) != gcc__mspabi_slll(x, n))
                mismatch("slll", i, n, __mspabi_slll(x, n), gcc__mspabi_slll(x, n), 8);
            if (__mspabi_sral(x, n) != gcc__mspabi_sral(x, n))
                mismatch("sral", i, n, __mspabi_sral(x, n), gcc__mspabi_sral(x, n), 8);
            if (__mspabi_srll(x, n) != gcc__mspabi_srll(x, n))
                mismatch("srll", i, n, __mspabi_srll(x, n), gcc__mspabi_srll(x, n), 8);
        }
    for (int i = 0; i < N(i64); i++) {
        for (int j = 0; j < N(i64); j++) {
            long long x = i64[i], y = i64[j];
            unsigned long long ux = x, uy = y;
            if (w__mspabi_mpyll(x, y) != wgcc__mspabi_mpyll(x, y))
                mismatch("mpyll", i, j, w__mspabi_mpyll(x, y), wgcc__mspabi_mpyll(x, y), 16);
            if (y != 0 && !(x == -9223372036854775807LL - 1 && y == -1)) {
                if (w__mspabi_divlli(x, y) != wgcc__mspabi_divlli(x, y))
                    mismatch("divlli", i, j, w__mspabi_divlli(x, y), wgcc__mspabi_divlli(x, y), 16);
                if (w__mspabi_remlli(x, y) != wgcc__mspabi_remlli(x, y))
                    mismatch("remlli", i, j, w__mspabi_remlli(x, y), wgcc__mspabi_remlli(x, y), 16);
            }
            if (uy != 0) {
                if (w__mspabi_divull(ux, uy) != wgcc__mspabi_divull(ux, uy))
                    mismatch("divull", i, j, w__mspabi_divull(ux, uy), wgcc__mspabi_divull(ux, uy), 16);
                if (w__mspabi_remull(ux, uy) != wgcc__mspabi_remull(ux, uy))
                    mismatch("remull", i, j, w__mspabi_remull(ux, uy), wgcc__mspabi_remull(ux, uy), 16);
            }
        }
        for (int n = 0; n < 64; n += 3) {
            long long x = i64[i];
            if (w__mspabi_sllll(x, n) != wgcc__mspabi_sllll(x, n))
                mismatch("sllll", i, n, w__mspabi_sllll(x, n), wgcc__mspabi_sllll(x, n), 16);
            if (w__mspabi_srall(x, n) != wgcc__mspabi_srall(x, n))
                mismatch("srall", i, n, w__mspabi_srall(x, n), wgcc__mspabi_srall(x, n), 16);
            if (w__mspabi_srlll(x, n) != wgcc__mspabi_srlll(x, n))
                mismatch("srlll", i, n, w__mspabi_srlll(x, n), wgcc__mspabi_srlll(x, n), 16);
        }
    }
#elif PART == 1
    for (int i = 0; i < N(fv); i++) {
        F a = { .u = fv[i] };
        for (int j = 0; j < N(fv); j++) {
            F b = { .u = fv[j] }, o, t;
#define F2(op)                                                       \
    o.f = __mspabi_##op(a.f, b.f);                                   \
    t.f = gcc__mspabi_##op(a.f, b.f);                                \
    if (!fsame(o.u, t.u))                                            \
        mismatch(#op, i, j, o.u, t.u, 8);
            F2(addf) F2(subf) F2(mpyf) F2(divf)
            if (!fnan(a.u) && !fnan(b.u) &&
                sgn(__mspabi_cmpf(a.f, b.f)) != sgn(gcc__mspabi_cmpf(a.f, b.f)))
                mismatch("cmpf", i, j, __mspabi_cmpf(a.f, b.f), gcc__mspabi_cmpf(a.f, b.f), 4);
        }
        float x = a.f;
        if (!fnan(a.u) && x > -2147483648.0f && x < 2147483648.0f &&
            __mspabi_fixfli(x) != gcc__mspabi_fixfli(x))
            mismatch("fixfli", i, 0, __mspabi_fixfli(x), gcc__mspabi_fixfli(x), 8);
        if (!fnan(a.u) && x > -1.0f && x < 4294967296.0f &&
            __mspabi_fixful(x) != gcc__fixunssfsi(x))
            mismatch("fixful", i, 0, __mspabi_fixful(x), gcc__fixunssfsi(x), 8);
        if (!fnan(a.u) && x > -9.2e18f && x < 9.2e18f &&
            __mspabi_fixflli(x) != gcc__mspabi_fixflli(x))
            mismatch("fixflli", i, 0, __mspabi_fixflli(x), gcc__mspabi_fixflli(x), 16);
        if (!fnan(a.u) && x > -1.0f && x < 1.8e19f &&
            __mspabi_fixfull(x) != gcc__fixunssfdi(x))
            mismatch("fixfull", i, 0, __mspabi_fixfull(x), gcc__fixunssfdi(x), 16);
    }
#elif PART == 2
    for (int i = 0; i < N(dv); i++) {
        D a = { .u = dv[i] };
        for (int j = 0; j < N(dv); j++) {
            D b = { .u = dv[j] }, o, t;
#define D2(op)                                                       \
    o.d = w__mspabi_##op(a.d, b.d);                                  \
    t.d = wgcc__mspabi_##op(a.d, b.d);                               \
    if (!dsame(o.u, t.u))                                            \
        mismatch(#op, i, j, o.u, t.u, 16);
            D2(addd) D2(subd) D2(mpyd) D2(divd)
            if (!dnan(a.u) && !dnan(b.u) &&
                sgn(w__mspabi_cmpd(a.d, b.d)) != sgn(wgcc__mspabi_cmpd(a.d, b.d)))
                mismatch("cmpd", i, j, w__mspabi_cmpd(a.d, b.d), wgcc__mspabi_cmpd(a.d, b.d), 4);
        }
    }
#else
    for (int i = 0; i < N(fv); i++) {
        F a = { .u = fv[i] };
        D o, t;
        o.d = __mspabi_cvtfd(a.f);
        t.d = gcc__mspabi_cvtfd(a.f);
        if (!dsame(o.u, t.u))
            mismatch("cvtfd", i, 0, o.u, t.u, 16);
    }
    for (int i = 0; i < N(dv); i++) {
        D a = { .u = dv[i] };
        F o, t;
        o.f = __mspabi_cvtdf(a.d);
        t.f = gcc__mspabi_cvtdf(a.d);
        if (!fsame(o.u, t.u))
            mismatch("cvtdf", i, 0, o.u, t.u, 8);
        double x = a.d;
        if (!dnan(a.u) && x > -2147483649.0 && x < 2147483648.0 &&
            __mspabi_fixdli(x) != gcc__mspabi_fixdli(x))
            mismatch("fixdli", i, 0, __mspabi_fixdli(x), gcc__mspabi_fixdli(x), 8);
        if (!dnan(a.u) && x > -1.0 && x < 4294967296.0 &&
            __mspabi_fixdul(x) != gcc__mspabi_fixdul(x))
            mismatch("fixdul", i, 0, __mspabi_fixdul(x), gcc__mspabi_fixdul(x), 8);
        if (!dnan(a.u) && x > -9.2e18 && x < 9.2e18 &&
            __mspabi_fixdlli(x) != gcc__mspabi_fixdlli(x))
            mismatch("fixdlli", i, 0, __mspabi_fixdlli(x), gcc__mspabi_fixdlli(x), 16);
        if (!dnan(a.u) && x > -1.0 && x < 1.8e19 &&
            __mspabi_fixdull(x) != gcc__mspabi_fixdull(x))
            mismatch("fixdull", i, 0, __mspabi_fixdull(x), gcc__mspabi_fixdull(x), 16);
    }
    for (int i = 0; i < N(i64); i++) {
        long long v = i64[i];
        D o, t;
        F p, q;
        o.d = __mspabi_fltllid(v); t.d = gcc__mspabi_fltllid(v);
        if (o.u != t.u) mismatch("fltllid", i, 0, o.u, t.u, 16);
        o.d = __mspabi_fltulld(v); t.d = gcc__mspabi_fltulld(v);
        if (o.u != t.u) mismatch("fltulld", i, 0, o.u, t.u, 16);
        p.f = __mspabi_fltllif(v); q.f = gcc__mspabi_fltllif(v);
        if (p.u != q.u) mismatch("fltllif", i, 0, p.u, q.u, 8);
        p.f = __mspabi_fltullf(v); q.f = gcc__mspabi_fltullf(v);
        if (p.u != q.u) mismatch("fltullf", i, 0, p.u, q.u, 8);
        long l = (long)v;
        o.d = __mspabi_fltlid(l); t.d = gcc__mspabi_fltlid(l);
        if (o.u != t.u) mismatch("fltlid", i, 0, o.u, t.u, 16);
        o.d = __mspabi_fltuld(l); t.d = gcc__mspabi_fltuld(l);
        if (o.u != t.u) mismatch("fltuld", i, 0, o.u, t.u, 16);
        p.f = __mspabi_fltlif(l); q.f = gcc__mspabi_fltlif(l);
        if (p.u != q.u) mismatch("fltlif", i, 0, p.u, q.u, 8);
        p.f = __mspabi_fltulf(l); q.f = gcc__mspabi_fltulf(l);
        if (p.u != q.u) mismatch("fltulf", i, 0, p.u, q.u, 8);
        int s = (int)v;
        o.d = __mspabi_fltid(s); t.d = gcc__mspabi_fltid(s);
        if (o.u != t.u) mismatch("fltid", i, 0, o.u, t.u, 16);
        o.d = __mspabi_fltud(s); t.d = gcc__mspabi_fltud(s);
        if (o.u != t.u) mismatch("fltud", i, 0, o.u, t.u, 16);
        p.f = __mspabi_fltif(s); q.f = gcc__mspabi_fltif(s);
        if (p.u != q.u) mismatch("fltif", i, 0, p.u, q.u, 8);
        p.f = __mspabi_fltuf(s); q.f = gcc__mspabi_fltuf(s);
        if (p.u != q.u) mismatch("fltuf", i, 0, p.u, q.u, 8);
    }
#endif
    hex(bad, 4);
    return 0;
}
)";
    for (int part = 0; part < 4; part++) {
        std::string tag = ".part" + std::to_string(part);
        EXPECT_EQ("0000 ", Run(cfg, Wrappers(wrapped[part]), "crt0.o", &src,
                               { "-O1", "-w", "-DPART=" + std::to_string(part) }, tag.c_str()))
            << "part " << part;
        EXPECT_EQ(0, exit_status) << "part " << part;
    }
}

// Our code under newlib: newlib's startup calls our main, which calls newlib's variadic
// printf, its strtod, and its qsort with a comparison of ours; GCC's code in the same
// program takes a structure from us.  Linked by msp430-elf-gcc -msim; mspsim serves
// newlib's I/O, and our main's result is the exit status.
TEST_F(Msp430Test, RunOurCodeWithNewlib)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    std::string ours = CompileToMsp430(R"(
#include <stdio.h>
#include <stdlib.h>
struct pt { int x; long y; };
long gcc_norm(struct pt p, int scale);
static int calls;
static int by_key(const void *a, const void *b)
{
    calls++;
    long x = *(const long *)a, y = *(const long *)b;
    return x < y ? -1 : x > y;
}
int main(void)
{
    long v[6] = { 70000L, -3, 12, -100000L, 0, 5 };
    qsort(v, 6, sizeof v[0], by_key);
    for (int i = 0; i < 6; i++)
        printf("%ld%c", v[i], i < 5 ? ',' : '\n');
    char *end;
    double d = strtod("  -12.375e1xyz", &end);
    printf("%d %s %d\n", (int)(d * 8), end, calls > 5);
    struct pt p = { 3, 400000L };
    printf("%ld %d %s\n", gcc_norm(p, 10), p.x, "done");
    return 42;
}
)");
    std::string s_path = TEST_DIR "/Msp430Test.RunOurCodeWithNewlib.ours.s";
    std::string o_path = TEST_DIR "/Msp430Test.RunOurCodeWithNewlib.ours.o";
    std::string log    = TEST_DIR "/Msp430Test.RunOurCodeWithNewlib.ours.log";
    {
        std::ofstream f(s_path);
        f << ours;
    }
    ASSERT_EQ(0, RunTool({ MSP430_GCC, "-mcpu=msp430", "-c", "-o", o_path, s_path }, log))
        << ReadFile(log);
    std::string gcc = R"(
struct pt { int x; long y; };
long gcc_norm(struct pt p, int scale)
{
    p.x *= scale;
    return p.x + p.y;
}
)";
    EXPECT_EQ("-100000,-3,0,5,12,70000\n-990 xyz 1\n400030 3 done\n",
              NewlibRun(gcc, { "-O1" }, { o_path }));
    EXPECT_EQ(42, exit_status);
}

namespace {

// The values our headers give, as NAME(i, f) fills them.  The first NW are the types in
// which GCC and clang differ: wchar_t, wint_t, sig_atomic_t and the fast 8-bit types.
const char header_values[] = R"(
#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
enum { NW = 9, NI = 30, NF = 9 };
void NAME(long long *i, double *f)
{
    long long iv[NI] = { sizeof(wchar_t), (wchar_t)-1 > 0, WCHAR_MIN, WCHAR_MAX,
                         WINT_MIN, WINT_MAX, SIG_ATOMIC_MAX, SIG_ATOMIC_MIN,
                         sizeof(int_fast8_t) * 10 + sizeof(uint_fast8_t),
                         sizeof(max_align_t), _Alignof(max_align_t),
                         SIZE_MAX, PTRDIFF_MIN, PTRDIFF_MAX, INTPTR_MIN, UINTPTR_MAX,
                         INT64_MIN, UINT32_MAX, CHAR_MIN, CHAR_MAX, LONG_MAX, INT_MIN,
                         UINT_MAX, sizeof(size_t) * 10 + sizeof(ptrdiff_t),
                         sizeof(int_fast16_t) * 10 + sizeof(int_least32_t),
                         sizeof(int_fast32_t), sizeof(intmax_t),
                         LDBL_MANT_DIG * 10000 + LDBL_MAX_EXP, DECIMAL_DIG + LDBL_DIG * 100,
                         sizeof(long double) * 10 + sizeof(double) };
    double fv[NF] = { LDBL_EPSILON, LDBL_MIN, LDBL_MAX, LDBL_TRUE_MIN, DBL_EPSILON,
                      DBL_MAX, FLT_EPSILON, FLT_TRUE_MIN, FLT_MIN_10_EXP + FLT_MAX_10_EXP };
    for (int k = 0; k < NI; k++)
        i[k] = iv[k];
    for (int k = 0; k < NF; k++)
        f[k] = fv[k];
}
)";

// Ours, and a main that compares them with their_values(), from index `from`.
std::string OurHeaderValues(const char *from)
{
    std::string ours = header_values;
    ours.replace(ours.find("NAME"), 4, "our_values");
    return ours + R"(
void their_values(long long *i, double *f);
int main(void)
{
    long long oi[NI], ti[NI];
    double of[NF], tf[NF];
    our_values(oi, of);
    their_values(ti, tf);
    for (int k = )" + from + R"(; k < NI; k++)
        if (oi[k] != ti[k])
            return 1 + k;
    for (int k = 0; k < NF; k++)
        if (of[k] != tf[k])
            return 100 + k;
    return 0;
})";
}

std::string TheirHeaderValues()
{
    std::string theirs = header_values;
    theirs.replace(theirs.find("NAME"), 4, "their_values");
    return theirs;
}

} // namespace

// Our headers against GCC's own for the target: the same constants, types and layouts.
TEST_F(Msp430Test, HeadersAgreeWithGcc)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    EXPECT_EQ("", GccRun(TheirHeaderValues(), CompileToMsp430(OurHeaderValues("0").c_str())));
    EXPECT_EQ(0, exit_status);
}

// And against clang's, but for the types where clang differs from GCC: its wchar_t and
// wint_t are int, its sig_atomic_t long and its fast 8-bit types char.
TEST_F(Msp430Test, HeadersAgreeWithClang)
{
    SKIP_IF_NO_MSP430_CLANG();
    std::string ours = CompileToMsp430(OurHeaderValues("NW").c_str());
    EXPECT_EQ("", ClangRun(TheirHeaderValues(), ours));
    EXPECT_EQ(0, exit_status);
}

// The shared headers in a 16-bit int: RAND_MAX fits it, char32_t holds 32 bits.
TEST_F(Msp430Test, SharedHeadersFitInt16)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430(R"(
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
    if (fabs(-2.5) != 2.5 || !(INFINITY > DBL_MAX)) return 5;
    if (sqrt(2.0) != 1.4142135623730951 || sqrtf(2.0f) != 1.41421354f) return 6;
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// Bit-field structures as arguments, results and shared data, ours calling Gcc's.
TEST_F(Msp430Test, RunBitfieldsWeCallGcc)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    EXPECT_EQ("", GccRun(kBitfieldCallee, CompileToMsp430(kBitfieldCaller.c_str())));
    EXPECT_EQ(0, exit_status);
}

// The same, Gcc calling ours.
TEST_F(Msp430Test, RunBitfieldsGccCallsUs)
{
    SKIP_IF_NO_MSP430_TOOLS();
    SKIP_IF_NO_MSP430_GCC();
    EXPECT_EQ("", GccRun(kBitfieldCaller, CompileToMsp430(kBitfieldCallee.c_str())));
    EXPECT_EQ(0, exit_status);
}
