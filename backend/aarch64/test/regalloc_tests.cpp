//
// AArch64 register allocation: values in argument registers unless live across a call,
// callee-saved registers saved and restored, parameters and arguments moved as if at
// once, and integers kept in the canonical form of their type.
//
#include "aarch64_test.h"

// A loop's values stay in argument registers: the pointer and count where they arrive.
TEST_F(Aarch64Test, LoopInRegisters)
{
    aarch64_peephole = false;
    std::string code = Code(CompileToAarch64(R"(
long sum(long *p, int n)
{
    long s = 0;
    for (int i = 0; i < n; i++)
        s += p[i];
    return s;
}
)"));
    EXPECT_NE(std::string::npos, code.find(R"(sxtw x2, w3
add x2, x0, x2, lsl #3
ldr x2, [x2]
add x4, x4, x2
)")) << code;
    EXPECT_EQ(std::string::npos, code.find("[x29, #-")) << code;
}

// A value live across a call takes a callee-saved register, saved in the prologue and
// restored in the epilogue.
TEST_F(Aarch64Test, CalleeSavedAcrossCall)
{
    aarch64_peephole = false;
    std::string code = Code(CompileToAarch64(R"(
int g(int);
int keep(int a, int b) { int x = g(a); return x + b; }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(str x19, [x29, #-16]
mov w0, w0
mov w19, w1
bl g
add w0, w0, w19
ldr x19, [x29, #-16]
)")) << code;
}

// Floating-point values in v registers, the result computed in place.
TEST_F(Aarch64Test, DoublesInRegisters)
{
    aarch64_peephole = false;
    std::string code = Code(CompileToAarch64(R"(
double dot(double a, double b, double c) { return a * b + c; }
)"));
    EXPECT_NE(std::string::npos, code.find("fmul d0, d0, d1\nfadd d0, d0, d2\n")) << code;
}

// Arguments trading registers: a cycle, broken through x9.
TEST_F(Aarch64Test, ArgumentsMovedAtOnce)
{
    aarch64_peephole = false;
    std::string code = Code(CompileToAarch64(R"(
int g2(int, int);
int swap(int a, int b) { return g2(b, a); }
)"));
    EXPECT_NE(std::string::npos, code.find("mov x9, x1\nmov w1, w0\nmov w0, w9\nbl g2\n")) << code;
}

// A narrow parameter is extended on arrival, and a narrow result in a register is kept
// extended.
TEST_F(Aarch64Test, NarrowValuesCanonical)
{
    aarch64_peephole = false;
    std::string code = Code(CompileToAarch64(R"(
signed char narrow(signed char c, int k) { signed char d = c + k; return d; }
)"));
    EXPECT_NE(std::string::npos, code.find("sxtb w0, w0\nmov w1, w1\n")) << code;
    EXPECT_NE(std::string::npos, code.find("add w0, w0, w1\nsxtb w0, w0\n")) << code;
}

// Many values live across calls: every callee-saved register, saved in pairs, and the
// results still right.
TEST_F(Aarch64Test, RunManyCalleeSaved)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    CompileAndRunAarch64(R"(
int id(int x) { return x; }
double did(double x) { return x; }
int main(void)
{
    int a = id(1), b = id(2), c = id(3), d = id(4), e = id(5), f = id(6), g = id(7);
    int h = id(8), i = id(9), j = id(10), k = id(11), l = id(12);
    double x = did(0.5), y = did(1.5), z = did(2.5), w = did(3.5);
    int s = id(a + b + c + d + e + f + g + h + i + j + k + l);
    double t = did(x + y + z + w);
    return s + (int)t + a * l + (int)(x * w * 4);
}
)");
    EXPECT_EQ(78 + 8 + 12 + 7, exit_status);
}

// A narrow value through registers stays in range: wrapped on assignment, extended by
// its own type as a parameter and as a call result.
TEST_F(Aarch64Test, RunNarrowInRegisters)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    CompileAndRunAarch64(R"(
signed char sc(int x) { return x; }
unsigned char uc(int x) { return x; }
int pass(signed char a, unsigned char b, short c, unsigned short d) { return a + b + c + d; }
int main(void)
{
    signed char a = sc(200);
    unsigned char b = uc(-1);
    short c = (short)70000;
    unsigned short d = (unsigned short)-2;
    int r = pass(a, b, c, d);
    long wide = a;
    return (a == -56) + 2 * (b == 255) + 4 * (r == -56 + 255 + 4464 + 65534) + 8 * (wide == -56);
}
)");
    EXPECT_EQ(15, exit_status);
}
