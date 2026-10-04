//
// AArch64 peephole pass (peephole.c) and compare-and-branch fusion (instr.c): each
// rewrite on a small function, and a run test of the corners.
//
#include "aarch64_test.h"

// Each test compiles one translation unit: the fixture's symbol table lives per test.
#define EXPECT_PEEPHOLE(name, expected, src)                     \
    TEST_F(Aarch64Test, name)                                    \
    {                                                            \
        EXPECT_EQ(expected, Code(CompileToAarch64(src))) << src; \
    }

// Immediate operands, the parameter's own `mov w0, w0` gone with them.
EXPECT_PEEPHOLE(PeepholeAddImmediate, "add w0, w0, #100\nret\n",
                "int f(int a) { return a + 100; }")
EXPECT_PEEPHOLE(PeepholeSubShiftedImmediate, "sub x0, x0, #5, lsl #12\nret\n",
                "long f(long a) { return a - 0x5000; }")
EXPECT_PEEPHOLE(PeepholeCompareNegative, "cmn w0, #5\ncset w0, eq\nret\n",
                "int f(int a) { return a == -5; }")
EXPECT_PEEPHOLE(PeepholeBitmaskImmediate, "and w0, w0, #65280\nret\n",
                "unsigned f(unsigned a) { return a & 0xff00; }")
EXPECT_PEEPHOLE(PeepholeNoBitmaskImmediate, "mov x10, #12345\nand x0, x0, x10\nret\n",
                "long f(long a) { return a & 12345; }")
EXPECT_PEEPHOLE(PeepholeShiftImmediate, "lsl w0, w0, #3\nret\n", "int f(int a) { return a << 3; }")
EXPECT_PEEPHOLE(PeepholeStoreZero, "str wzr, [x0]\nret\n", "void f(int *p) { *p = 0; }")

// mul + add/sub.
EXPECT_PEEPHOLE(PeepholeMadd, "madd x0, x0, x1, x2\nret\n",
                "long f(long a, long b, long c) { return a * b + c; }")
EXPECT_PEEPHOLE(PeepholeMsub, "msub x0, x0, x1, x2\nret\n",
                "long f(long a, long b, long c) { return c - a * b; }")

// Addresses: a constant index is an offset, a variable one is scaled in the load.
EXPECT_PEEPHOLE(PeepholeConstantIndex, "ldr x0, [x0, #16]\nret\n",
                "long f(long *p) { return p[2]; }")

// A compare and branch, the index sign-extended and scaled in the load, a zero test as
// cbz, the increment an immediate.
EXPECT_PEEPHOLE(PeepholeLoop, R"(mov w1, w1
mov w4, #0
mov w3, #0
cmp w3, w1
b.ge .LL0
ldr w2, [x0, w3, sxtw #2]
cbz w2, .L8
add w4, w4, #1
add w3, w3, #1
b .L2
mov w0, w4
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
EXPECT_PEEPHOLE(PeepholeFloatBranch, "fcmp d0, d1\nb.pl .L1\nret\nfmov d0, d1\nret\n",
                "double f(double x, double y) { if (x < y) return x; return y; }")

// Stores of adjacent slots pair, either order; the stored registers are not reloaded.
TEST_F(Aarch64Test, PeepholePairsAndReloads)
{
    aarch64_frame_pointer = true;
    std::string code      = Code(CompileToAarch64(R"(
long double f(long double a, long double b);
long double g(long double a, long double b) { return f(a, b); }
)"));
    EXPECT_NE(std::string::npos, code.find("stp q1, q0, [x29, #-32]\nbl f\n")) << code;
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
                  .find("sub sp, sp, #16\nstr w0, [sp, #12]\nldr w0, [sp, #12]\nadd sp, sp, #16\nret\n"));
}
TEST_F(Aarch64Test, PeepholeKeepsVolatileUnpaired)
{
    std::string code = Code(CompileToAarch64(
        "long g(long a) { volatile long x = a, y = a; return x + y; }"));
    EXPECT_EQ(std::string::npos, code.find("ldp ")) << code;
    EXPECT_EQ(std::string::npos, code.find("stp ")) << code;
}
