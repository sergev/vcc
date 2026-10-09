//
// AArch64 peephole pass (peephole.c) and compare-and-branch fusion (instr.c): each
// rewrite on a small function, and a run test of the corners.
//
#include "../../common/test/bitfield_run.h"
#include "aarch64_test.h"

// Each test compiles one translation unit: the fixture's symbol table lives per test.
#define EXPECT_PEEPHOLE(name, expected, src)                     \
    TEST_F(Aarch64Test, name)                                    \
    {                                                            \
        EXPECT_EQ(expected, Code(CompileToAarch64(src))) << src; \
    }

// Immediate operands, the parameter's own `mov w0, w0` gone with them.
EXPECT_PEEPHOLE(PeepholeAddImmediate, R"(add w0, w0, #100
ret
)",
                "int f(int a) { return a + 100; }")
EXPECT_PEEPHOLE(PeepholeSubShiftedImmediate, R"(sub x0, x0, #5, lsl #12
ret
)",
                "long f(long a) { return a - 0x5000; }")
EXPECT_PEEPHOLE(PeepholeCompareNegative, R"(cmn w0, #5
cset w0, eq
ret
)",
                "int f(int a) { return a == -5; }")
EXPECT_PEEPHOLE(PeepholeBitmaskImmediate, R"(and w0, w0, #65280
ret
)",
                "unsigned f(unsigned a) { return a & 0xff00; }")
EXPECT_PEEPHOLE(PeepholeNoBitmaskImmediate, R"(mov x10, #12345
and x0, x0, x10
ret
)",
                "long f(long a) { return a & 12345; }")
EXPECT_PEEPHOLE(PeepholeShiftImmediate, R"(lsl w0, w0, #3
ret
)", "int f(int a) { return a << 3; }")
EXPECT_PEEPHOLE(PeepholeStoreZero, R"(str wzr, [x0]
ret
)", "void f(int *p) { *p = 0; }")

// mul + add/sub.
EXPECT_PEEPHOLE(PeepholeMadd, R"(madd x0, x0, x1, x2
ret
)",
                "long f(long a, long b, long c) { return a * b + c; }")
EXPECT_PEEPHOLE(PeepholeMsub, R"(msub x0, x0, x1, x2
ret
)",
                "long f(long a, long b, long c) { return c - a * b; }")

// Addresses: a constant index is an offset, a variable one is scaled in the load.
EXPECT_PEEPHOLE(PeepholeConstantIndex, R"(ldr x0, [x0, #16]
ret
)",
                "long f(long *p) { return p[2]; }")
// A constant offset added extended or shifted (a member, a pointer step) is an
// immediate, a negative one subtracted.
EXPECT_PEEPHOLE(PeepholeMemberOffset, R"(add x1, x0, #4
ldrh w0, [x1]
add w0, w0, #1
strh w0, [x1]
ret
)",
                "struct T { int a; unsigned short h; }; void f(struct T *p) { p->h++; }")
EXPECT_PEEPHOLE(PeepholeNegativeOffset, R"(sub x0, x0, #12
ret
)",
                "int *f(int *p) { return p - 3; }")

// A compare and branch, the index sign-extended and scaled in the load, a zero test as
// cbz, the increment an immediate.
EXPECT_PEEPHOLE(PeepholeLoop, R"(mov w2, #0
add x3, x0, w1, sxtw #2
cmp w1, #0
b.le .LL0
ldr w1, [x0]
cbz w1, .L8
add w2, w2, #1
add x0, x0, #4
cmp x0, x3
b.lo .L3
mov w0, w2
ret
)",
                R"(int f(int *a, int n)
{
    int s = 0;
    for (int i = 0; i < n; i++)
        if (a[i] != 0)
            s++;
    return s;
}
)")

// A floating-point compare and branch: the inverse condition is true for a NaN.
EXPECT_PEEPHOLE(PeepholeFloatBranch, R"(fcmp d0, d1
b.pl .L1
ret
fmov d0, d1
ret
)",
                "double f(double x, double y) { if (x < y) return x; return y; }")

// Stores of adjacent slots pair, either order; the stored registers are not reloaded.
TEST_F(Aarch64Test, PeepholePairsAndReloads)
{
    aarch64_frame_pointer = true;
    std::string code      = Code(CompileToAarch64(R"(
long double f(long double a, long double b);
long double g(long double a, long double b) { return f(a, b); }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(stp q1, q0, [x29, #-32]
bl f
)")) << code;
}

// The corners of each rewrite, run: immediates of every kind and sign, at both widths;
// indexed loads of every size with negative and unsigned indices; madd and msub.
TEST_F(Aarch64Test, RunPeepholeCorners)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    CompileAndRunAarch64(R"(
int ok = 1, bit = 0;
void check(long got, long want)
{
    if (got != want)
        ok = 0;
    bit++;
}
long id(long x) { return x; }
int main(void)
{
    int i = (int)id(7);
    long l = id(-3);
    unsigned u = (unsigned)id(0x12345678);
    unsigned long ul = (unsigned long)id(-1);
    check(i + 4095, 4102);
    check(i - 4096, -4089);
    check(i + 0x7000, 7 + 0x7000);
    check(i - -5, 12);
    check(l + 0x123000, 0x123000 - 3);
    check(l - 0xfff000, -3 - 0xfff000);
    check(i < -5, 0);
    check(l > -4, 1);
    check(l == -3, 1);
    check(u & 0xff00ff00, 0x12005600);
    check(u | 0x80000000u, 0x92345678);
    check(u ^ 0xffff, 0x1234a987);
    check(ul & 0x5555555555555555, 0x5555555555555555);
    check(u & 12345, 0x12345678 & 12345);
    check(i << 4, 112);
    check(l >> 1, -2);
    check(u >> 28, 1);
    check(i * l + 100, 79);
    check(100 - i * l, 121);
    long a[5] = { 10, 20, 30, 40, 50 };
    short s[5] = { -1, -2, -3, -4, -5 };
    unsigned char c[5] = { 200, 201, 202, 203, 204 };
    long *pa = a + 4;
    int k = (int)id(-2);
    unsigned uk = (unsigned)id(1);
    check(pa[k], 30);
    check(a[uk], 20);
    check(s[uk + 2], -4);
    check(c[k + 4], 202);
    check(pa[-4], 10);
    s[3] = 0;
    check(s[3] + s[4], -5);
    return ok ? bit : 100 + bit;
}
)");
    EXPECT_EQ(25, exit_status);
}

// A volatile local stays in its slot: the store is not followed into the reload, and
// two accesses are two, not a pair.
TEST_F(Aarch64Test, PeepholeKeepsVolatileReload)
{
    EXPECT_NE(std::string::npos,
              Code(CompileToAarch64("int f(int a) { volatile int x = a; return x; }"))
                  .find(R"(sub sp, sp, #16
str w0, [sp, #12]
ldr w0, [sp, #12]
add sp, sp, #16
ret
)"));
}
TEST_F(Aarch64Test, PeepholeKeepsVolatileUnpaired)
{
    std::string code = Code(CompileToAarch64(
        "long g(long a) { volatile long x = a, y = a; return x + y; }"));
    EXPECT_EQ(std::string::npos, code.find("ldp ")) << code;
    EXPECT_EQ(std::string::npos, code.find("stp ")) << code;
}

// Shifts and masks: ubfx, sbfx, bfi and ubfiz, at either width; and what they are not:
// a mask with a hole, a shifted value read again.
EXPECT_PEEPHOLE(PeepholeUbfx, R"(ubfx w0, w0, #13, #11
ret
)",
                "unsigned f(unsigned x) { return (x >> 13) & 0x7ff; }")
EXPECT_PEEPHOLE(PeepholeUbfxX, R"(ubfx x0, x0, #40, #16
ret
)",
                "unsigned long f(unsigned long x) { return (x >> 40) & 0xffff; }")
EXPECT_PEEPHOLE(PeepholeSbfx, R"(sbfx w0, w0, #13, #12
ret
)",
                "int f(int x) { return (x << 7) >> 20; }")
EXPECT_PEEPHOLE(
    PeepholeBfi, R"(bfi w0, w1, #8, #12
ret
)",
    "unsigned f(unsigned x, unsigned v) { return (x & 0xfff000ff) | (v & 0xfff) << 8; }")
EXPECT_PEEPHOLE(PeepholeBfiX, R"(bfi x0, x1, #20, #30
ret
)",
                "unsigned long f(unsigned long x, unsigned long v) "
                "{ return (x & ~(0x3ffffffful << 20)) | (v & 0x3ffffffful) << 20; }")
EXPECT_PEEPHOLE(PeepholeUbfiz, R"(ubfiz w0, w0, #8, #12
ret
)",
                "unsigned f(unsigned v) { return (v & 0xfff) << 8; }")
EXPECT_PEEPHOLE(PeepholeMaskWithHole, R"(lsr w0, w0, #4
mov w10, #1285
and w0, w0, w10
ret
)",
                "unsigned f(unsigned x) { return (x >> 4) & 0x505; }")
EXPECT_PEEPHOLE(PeepholeShiftReadAgain, R"(lsr w1, w0, #4
and w0, w1, #4095
add w0, w0, w1
ret
)",
                "unsigned f(unsigned x) { unsigned t = x >> 4; return (t & 0xfff) + t; }")

// Bit-fields: a read is ubfx or sbfx, a store bfi, a store of zero an and with the
// mask as an immediate (movz + movk once); a narrow unit loaded and stored without
// extensions, and a field updated in place stored from the unit's register.
TEST_F(Aarch64Test, PeepholeBitfields)
{
    std::string code = Code(CompileToAarch64(R"(
struct S { unsigned a : 3; int b : 5; unsigned c : 12; unsigned d : 12; };
unsigned get_c(struct S *p) { return p->c; }
int get_b(struct S *p) { return p->b; }
void set_c(struct S *p, unsigned v) { p->c = v; }
void set_b(struct S *p, int v) { p->b = v; }
void clear_c(struct S *p) { p->c = 0; }
void bump_d(struct S *p) { p->d++; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(ldr w0, [x0]
ubfx w0, w0, #8, #12
ret
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(ldrb w0, [x0]
sbfx w0, w0, #3, #5
ret
)")) << code;
    EXPECT_NE(std::string::npos,
              code.find(R"(ldr w0, [x2]
bfi w0, w1, #8, #12
str w0, [x2]
ret
)"))
        << code;
    EXPECT_NE(std::string::npos,
              code.find(R"(ldrb w0, [x3]
bfi w0, w1, #3, #5
strb w0, [x3]
ret
)"))
        << code;
    EXPECT_NE(std::string::npos, code.find("and w0, w0, #-1048321\n")) << code;
    EXPECT_NE(std::string::npos,
              code.find(R"(ubfx w0, w2, #4, #12
add w0, w0, #1
bfi w2, w0, #4, #12
strh w2, [x3]
ret
)"))
        << code;
    EXPECT_EQ(std::string::npos, code.find("orr")) << code;
    EXPECT_EQ(std::string::npos, code.find("movk")) << code;
    EXPECT_EQ(std::string::npos, code.find("uxt")) << code;
}

// Bit-fields and shift-and-mask expressions computed as the host computes them.
TEST_F(Aarch64Test, RunPeepholeBitfields)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ(BitfieldRunExpected(), CompileAndRunAarch64(kBitfieldRunProgram));
}

// Pointer steps: a load or store at the old pointer post-indexed, at the new one
// pre-indexed.
EXPECT_PEEPHOLE(PeepholePostIndex, R"(ldrb w3, [x1], #1
strb w3, [x0], #1
cbz w3, .LL0
ldrb w3, [x1], #1
strb w3, [x0], #1
cbnz w3, .L9
ret
)",
                "void f(char *d, const char *s) { while ((*d++ = *s++) != 0) ; }")
EXPECT_PEEPHOLE(PeepholePreIndex, R"(ldrb w1, [x0, #1]!
cbz w1, .LL0
ldrb w1, [x0, #1]!
cbnz w1, .L4
ldrb w0, [x0]
ret
)",
                "int f(const char *p) { while (*++p) ; return *p; }")

// A byte loaded and extended the other way is loaded that way; an extension before a
// narrow store goes.
EXPECT_PEEPHOLE(PeepholeLoadExtend, R"(ldrb w0, [x0]
ret
)",
                "int f(signed char *p) { return (unsigned char)*p; }")
EXPECT_PEEPHOLE(PeepholeLoadSignExtendLong, R"(ldrsb x0, [x0]
ret
)",
                "long f(signed char *p) { return *p; }")
EXPECT_PEEPHOLE(PeepholeStoreNarrowed, R"(add w2, w1, #1
strb w2, [x0, w1, sxtw]
ret
)",
                "void f(signed char *p, int x) { signed char c = (signed char)(x + 1); p[x] = c; }")

// The flags: a 0/1 added is cinc, a 0/1 tested again is the first test, a single bit
// tested is tbz, the old value of `n--` compared ahead of the decrement.
EXPECT_PEEPHOLE(PeepholeCinc, R"(cmp w0, w1
cinc w0, w2, ne
ret
)",
                "int f(int a, int b, int c) { return c + (a != b); }")
EXPECT_PEEPHOLE(PeepholeCsetTestedAgain, R"(cmp w0, w1
cset w0, lt
ret
)",
                "int f(int a, int b) { return !(a < b) == 0; }")
EXPECT_PEEPHOLE(PeepholeTbz, R"(tbz w0, #3, .L2
mov w0, w1
ret
mov w0, #0
ret
)",
                "int f(unsigned x, int y) { if (x & 8) return y; return 0; }")
EXPECT_PEEPHOLE(PeepholeTbzHighBit, R"(tbz x0, #40, .L2
mov x0, #1
ret
mov x0, #2
ret
)",
                "long f(long x) { if (x & (1L << 40)) return 1; return 2; }")
EXPECT_PEEPHOLE(PeepholeCompareBeforeDecrement, R"(mov w2, #0
cmp w0, #0
sub w0, w0, #1
b.le .LL0
add w2, w2, w0
cmp w0, #0
sub w0, w0, #1
b.gt .L5
mov w0, w2
ret
)",
                "int f(int n) { int s = 0; while (n-- > 0) s += n; return s; }")

// A diamond setting 0 or 1, or two constants one apart, is cset or cinc, whether its
// arms meet or each returns.
EXPECT_PEEPHOLE(PeepholeDiamondCset, R"(cmp w0, #0
cset w0, eq
ret
)",
                "int f(int x) { return x == 0 ? 1 : 0; }")
EXPECT_PEEPHOLE(PeepholeDiamondCinc, R"(cmp w0, w1
mov w0, #4
cinc w0, w0, lt
ret
)",
                "int f(int a, int b) { if (a < b) return 5; return 4; }")

// A test of a 0/1 just set, or of a constant, branches where it would go: && and || as
// branches.
EXPECT_PEEPHOLE(PeepholeThreadAnd, R"(ldrb w3, [x0]
cbz w3, .LL0
ldrb w2, [x1]
cmp w3, w2
b.ne .LL0
add x1, x1, #1
ldrb w3, [x0, #1]!
cbz w3, .LL0
ldrb w2, [x1]
cmp w3, w2
b.eq .L13
ldrb w2, [x0]
ldrb w0, [x1]
sub w0, w2, w0
ret
)",
                R"(int f(const unsigned char *a, const unsigned char *b)
{
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}
)")
EXPECT_PEEPHOLE(PeepholeThreadOr, R"(cmp w0, #0
b.gt .L1
cmp w1, #0
b.le .L5
mov w0, #7
ret
mov w0, #3
ret
)",
                "int f(int a, int b) { if (a > 0 || b > 0) return 7; return 3; }")

// The size rewrites, run: steps of every access size and sign, the loaded register
// also the old pointer, loads extended either way.
TEST_F(Aarch64Test, RunPeepholeSize)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    CompileAndRunAarch64(R"(
int ok = 1, bit = 0;
void check(long got, long want)
{
    if (got != want)
        ok = 0;
    bit++;
}
void copy(char *d, const char *s)
{
    while ((*d++ = *s++) != 0)
        ;
}
long sum_shorts(const short *p, int n)
{
    long s = 0;
    while (n-- > 0)
        s += *p++;
    return s;
}
long sum_longs(const long *p, const long *end)
{
    long s = 0;
    for (; p < end; p++)
        s += *p;
    return s;
}
int last(const signed char *p)
{
    while (*++p)
        ;
    return (unsigned char)p[-1];
}
void fill(unsigned *p, int n, unsigned v)
{
    while (n-- > 0)
        *p++ = v--;
}
int bits(unsigned long x)
{
    int n = 0;
    for (int k = 0; k < 64; k++)
        if (x & (1UL << k))
            n++;
    return n;
}
int count_ne(const int *a, const int *b, int n)
{
    int c = 0;
    while (n-- > 0)
        c += *a++ != *b++;
    return c;
}
int both(int a, int b) { return a > 0 && b > 0; }
int either(long a, long b)
{
    if (a < 0 || b == 3)
        return 10;
    return 20;
}
int pick(unsigned x) { return x > 9 ? 6 : 5; }
int main(void)
{
    check(both(1, 2) * 4 + both(1, 0) * 2 + both(0, 1), 4);
    check(either(-1, 0) + either(0, 3) + either(0, 0), 40);
    check(pick(10) * 10 + pick(9), 65);
    check(bits(0x8000000100000005UL), 4);
    int x[4] = { 1, 2, 3, 4 }, y[4] = { 1, 0, 3, 0 };
    check(count_ne(x, y, 4), 2);
    check(!(x[0] < y[1]) == 0, 0);
    char buf[8];
    copy(buf, "abcdef");
    check(buf[0] + buf[5], 'a' + 'f');
    check(buf[6], 0);
    short s[4] = { -1, -200, 300, -4 };
    check(sum_shorts(s, 4), 95);
    long l[3] = { 1L << 40, -5, 7 };
    check(sum_longs(l, l + 3), (1L << 40) + 2);
    signed char c[4] = { 1, -2, -3, 0 };
    check(last(c), 253);
    unsigned u[3];
    fill(u, 3, 10);
    check(u[0] * 100 + u[1] * 10 + u[2], 1098);
    return ok ? bit : 100 + bit;
}
)");
    EXPECT_EQ(12, exit_status);
}
