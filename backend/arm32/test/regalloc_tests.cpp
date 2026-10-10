//
// ARM32 register allocation: values in argument registers unless live across a call,
// callee-saved registers pushed with the frame record, parameters and arguments moved
// as if at once (core pairs, and s registers inside d ones), and integers kept in the
// canonical form of their type.
//
#include "arm32_test.h"

// A loop's values stay in argument registers: the pointer, stepped through the array,
// where it arrives.
TEST_F(Arm32Test, LoopInRegisters)
{
    arm32_peephole   = false;
    std::string code = Code(CompileToArm32(R"(
int sum(int *p, int n)
{
    int s = 0;
    for (int i = 0; i < n; i++)
        s += p[i];
    return s;
}
)"));
    EXPECT_NE(std::string::npos, code.find(R"(ldr r1, [r0]
add r2, r2, r1
add r0, r0, #4
)")) << code;
    EXPECT_EQ(std::string::npos, code.find("[r11, #-")) << code;
}

// A value live across a call takes a callee-saved register, pushed with lr and popped
// with pc.
TEST_F(Arm32Test, CalleeSavedAcrossCall)
{
    arm32_peephole   = false;
    std::string code = Code(CompileToArm32(R"(
int g(int);
int keep(int a, int b) { int x = g(a); return x + b; }
)"));
    EXPECT_EQ(R"(push {r4, lr}
mov r4, r1
bl g
add r0, r0, r4
pop {r4, pc}
)",
              code);
}

// With a frame record asked for, the callee-saved registers are pushed with it, and r11
// points at the saved r11.
TEST_F(Arm32Test, CalleeSavedWithFrameRecord)
{
    arm32_peephole      = false;
    arm32_frame_pointer = true;
    std::string code    = Code(CompileToArm32(R"(
int g(int);
int keep(int a, int b) { int x = g(a); return x + b; }
)"));
    EXPECT_EQ(R"(push {r4, r11, lr}
add r11, sp, #4
sub sp, sp, #4
mov r4, r1
bl g
add r0, r0, r4
sub sp, r11, #4
pop {r4, r11, pc}
)",
              code);
}

// Floating-point values in d registers, the result computed in place.
TEST_F(Arm32Test, DoublesInRegisters)
{
    arm32_peephole   = false;
    std::string code = Code(CompileToArm32(R"(
double dot(double a, double b, double c) { return a * b + c; }
)"));
    EXPECT_NE(std::string::npos, code.find("vmul.f64 d0, d0, d1\nvadd.f64 d0, d0, d2\n")) << code;
}

// Arguments trading registers: a cycle, broken through r12.
TEST_F(Arm32Test, ArgumentsMovedAtOnce)
{
    arm32_peephole   = false;
    std::string code = Code(CompileToArm32(R"(
int g2(int, int);
int swap(int a, int b) { return g2(b, a); }
)"));
    EXPECT_NE(std::string::npos, code.find("mov r12, r0\nmov r0, r1\nmov r1, r12\nbl g2\n"))
        << code;
}

// Long longs trading pairs: two cycles, a word each.
TEST_F(Arm32Test, PairsMovedAtOnce)
{
    arm32_peephole   = false;
    std::string code = Code(CompileToArm32(R"(
long long l2(long long, long long);
long long lswap(long long a, long long b) { return l2(b, a); }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(mov r12, r1
mov r1, r3
mov r3, r12
mov r12, r0
mov r0, r2
mov r2, r12
bl l2
)")) << code;
}

// Floats back-filled around a double: a move into s1 writes half of d0, so the double
// leaves d0 first; the cycle through s2 is broken through s28.
TEST_F(Arm32Test, SinglesInsideDoublesMovedAtOnce)
{
    arm32_peephole   = false;
    std::string code = Code(CompileToArm32(R"(
double fd(float, double, float);
double back(double d, float f, float e) { return fd(e, d, f); }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(vmov.f32 s4, s3
vmov.f32 s28, s2
vmov.f64 d1, d0
vmov.f32 s0, s4
vmov.f32 s1, s28
bl fd
)")) << code;
}

// The callee's address, in an argument register, goes to r10 out of the way.
TEST_F(Arm32Test, IndirectCalleeInArgumentRegister)
{
    arm32_peephole   = false;
    std::string code = Code(CompileToArm32(R"(
int indirect(int (*p)(int), int x) { return p(x); }
)"));
    EXPECT_NE(std::string::npos, code.find("mov r10, r0\nmov r0, r1\nblx r10\n")) << code;
}

// A narrow result in a register is kept extended.
TEST_F(Arm32Test, NarrowValuesCanonical)
{
    arm32_peephole   = false;
    std::string code = Code(CompileToArm32(R"(
unsigned char next(unsigned char c) { return c + 1; }
)"));
    EXPECT_EQ("add r0, r0, #1\nuxtb r0, r0\nbx lr\n", code);
}

// Many values live across calls: more than the callee-saved registers, so some stay in
// their slots, and the results are still right.
TEST_F(Arm32Test, RunManyCalleeSaved)
{
    SKIP_IF_NO_ARM32_TOOLS();
    CompileAndRunArm32(R"(
int id(int x) { return x; }
double did(double x) { return x; }
float fid(float x) { return x; }
long long lid(long long x) { return x; }
int main(void)
{
    int a = id(1), b = id(2), c = id(3), d = id(4), e = id(5), f = id(6), g = id(7);
    int h = id(8), i = id(9), j = id(10), k = id(11), l = id(12);
    double x = did(0.5), y = did(1.5), z = did(2.5), w = did(3.5), u = did(4.5), v = did(5.5);
    double q = did(6.5);
    float m = fid(0.25f), n = fid(0.75f);
    long long p = lid(1LL << 40), r = lid(3);
    int s = id(a + b + c + d + e + f + g + h + i + j + k + l);
    double t = did(x + y + z + w + u + v + q + m + n);
    return s + (int)t + a * l + (int)(x * w * 4) + (int)(p >> 40) + (int)r;
}
)");
    EXPECT_EQ(78 + 25 + 12 + 7 + 1 + 3, exit_status);
}

// A narrow value through registers stays in range: wrapped on assignment, extended by
// its own type as a parameter and as a call result.
TEST_F(Arm32Test, RunNarrowInRegisters)
{
    SKIP_IF_NO_ARM32_TOOLS();
    CompileAndRunArm32(R"(
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
    unsigned char e = b + 1;
    signed char f = a - 100;
    return (r == -56 + 255 + 4464 + 65534) + 2 * (e == 0) + 4 * (f == 100);
}
)");
    EXPECT_EQ(7, exit_status);
}

// Arguments and parameters in every order: rotations of core values, pairs, floats and
// doubles, so the moves form cycles in each file; a long long result whose registers
// are its operands'.
TEST_F(Arm32Test, RunMovesAtOnce)
{
    SKIP_IF_NO_ARM32_TOOLS();
    CompileAndRunArm32(R"(
int i3(int a, int b, int c) { return a * 100 + b * 10 + c; }
long long l3(long long a, long long b, int c) { return a * 100 + b * 10 + c; }
double f4(float a, double b, float c, double d) { return a * 1000 + b * 100 + c * 10 + d; }
int ri(int a, int b, int c) { return i3(c, a, b) + i3(b, c, a); }
long long rl(int c, long long a, long long b) { return l3(b, a, c); }
double rf(double b, float a, double d, float c) { return f4(c, d, a, b); }
typedef unsigned long long u64;
u64 mix(u64 a, u64 b) { u64 c = a + b; u64 d = c - a; return d * c + (c ^ d); }
int main(void)
{
    return (ri(1, 2, 3) == 312 + 231) + 2 * (rl(7, 1LL << 33, 5) == 500 + (10LL << 33) + 7) +
           4 * (rf(4.0, 1.0f, 2.0, 3.0f) == 3000 + 200 + 10 + 4) +
           8 * (mix(5, 1ULL << 32) == (5ULL << 32) + 5);
}
)");
    EXPECT_EQ(15, exit_status);
}

// Values in argument registers survive the runtime calls of long long arithmetic and
// conversions, and floats and doubles from registers go to a variadic callee in core
// registers.
TEST_F(Arm32Test, RunRuntimeCallsAndVariadics)
{
    SKIP_IF_NO_ARM32_TOOLS();
    EXPECT_EQ("7 2.5 0.75 -3 12345678901\n", CompileAndRunArm32(R"(
#include <stdio.h>
long long f(long long a, long long b, int k, double d, float e)
{
    long long q = a / b, m = a % b;
    double x = (double)q + d;
    long long y = (long long)(x * e);
    printf("%d %g %g %lld %lld\n", k, d, (double)e, m - b, a + y - y);
    return q * b + m - a + k + y - y;
}
int main(void) { return f(12345678901LL, 4, 7, 2.5, 0.75f) == 7 ? 0 : 1; }
)"));
    EXPECT_EQ(0, exit_status);
}
