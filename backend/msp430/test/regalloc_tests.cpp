//
// MSP430 register allocation: where values go, what the prologue saves, and runs with
// every register busy.
//
#include "msp430_test.h"

// A value live across a call takes a call-saved register, which the prologue pushes.
// The argument is not moved back from it: r12 still holds it.
EXPECT_CODE(LiveAcrossCallSaved, R"(push r10
mov r12, r10
call #g
add r10, r12
pop r10
ret
)",
            "int g(int); int f(int a) { return g(a) + a; }")

// Arguments that trade registers: the cycle broken by three xors, no temporary.
EXPECT_CODE(SwappedArgsByXor, R"(xor r13, r12
xor r12, r13
xor r13, r12
br #g
)",
            "int g(int, int); int f(int a, int b) { return g(b, a); }")

// An unused parameter is left where it came: its register serves the result.
EXPECT_CODE(DeadParamLeft, R"(mov r13, r12
ret
)",
            "int f(int a, int b) { return b; }")

// With a helper that takes r8-r11, a value live across it avoids r8-r10.
TEST_F(Msp430Test, R8HelperKeepsR8Free)
{
    std::string code = Code(CompileToMsp430(R"(
        long long x;
        int f(int k) { x = x * x; return k; }
    )"));
    EXPECT_NE(std::string::npos, code.find("push r7\n")) << code;
    EXPECT_NE(std::string::npos, code.find("call #__mspabi_mpyll\n")) << code;
    EXPECT_NE(std::string::npos, code.find("mov r7, r12\n")) << code;
}

// More values live across calls than registers: some stay in their slots.  The sum
// checks every one.
TEST_F(Msp430Test, RunManyLiveAcrossCalls)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430(R"(
        int id(int x) { return x; }
        long lid(long x) { return x; }
        int main(void)
        {
            int a = id(1), b = id(2), c = id(3), d = id(4), e = id(5), f = id(6);
            int g = id(7), h = id(8), i = id(9);
            long l = lid(100000L), m = lid(-7L);
            int s1 = a + b + c + d + e + f + g + h + i;
            long s2 = l + m;
            int s3 = id(a) * id(b) + c * d - e + f * g - h + i;
            if (s1 != 45 || s2 != 99993L || s3 != 52)
                return 1;
            return a + b + c + d + e + f + g + h + i + (int)(l / 1000) + (int)m == 138 ? 0 : 2;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// Arguments rotated through every register, chars and longs among them.
TEST_F(Msp430Test, RunArgumentsRotated)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430(R"(
        int g(int a, int b, int c, int d) { return a * 1000 + b * 100 + c * 10 + d; }
        long h(long a, long b) { return a - b; }
        int k(signed char a, unsigned char b, int c) { return a + b + c; }
        int f(int a, int b, int c, int d)
        {
            return g(b, c, d, a) + g(d, a, b, c);
        }
        int main(void)
        {
            if (f(1, 2, 3, 4) != 2341 + 4123)
                return 1;
            long x = 70000L, y = 5L;
            if (h(y, x) != -69995L || h(x, y) != 69995L)
                return 2;
            signed char sc = -3;
            unsigned char uc = 250;
            return k(sc, uc, 10) == 257 ? 0 : 3;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}
