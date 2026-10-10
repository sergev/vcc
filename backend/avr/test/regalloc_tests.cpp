//
// AVR register allocation: values in register pairs, the parallel moves at calls and on
// entry, the call-saved registers an argument takes, and the fallback to memory when a
// slot would lie past Y+63.
//
#include "avr_test.h"

// A loop over registers alone: no slot is read or written.
TEST_F(AvrTest, LoopInRegisters)
{
    std::string s = Body(CompileToAvr(R"(
int sum(int *a, int n)
{
    int s = 0;
    for (int i = 0; i < n; i++)
        s += a[i];
    return s;
}
)"));
    EXPECT_EQ(std::string::npos, s.find("Y+")) << s;
    EXPECT_NE(std::string::npos, s.find(R"(ldd r22, Z+0
ldd r23, Z+1
add r20, r22
adc r21, r23
adiw r24, 2
)")) << s;
}

// Arguments passed on swapped are a cycle of moves, broken through Z; the call, then
// nothing to restore, is a tail jump.
TEST_F(AvrTest, SwappedArgumentsThroughZ)
{
    EXPECT_EQ(R"(movw r30, r22
movw r22, r24
movw r24, r30
jmp h
)",
              Code(CompileToAvr("int h(int a, int b);\n"
                                "int f(int a, int b) { return h(b, a); }")));
}

TEST_F(AvrTest, RunSwappedArguments)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("", CompileAndRunAvr(R"(
int h(int a, int b) { return a * 10 + b; }
long hl(long a, char b, long c) { return a * 100 + b * 10 + c; }
int f(int a, int b) { return h(b, a); }
long fl(long a, char b, long c) { return hl(c, b, a); }
int main(void)
{
    if (f(1, 2) != 21) return 1;
    if (fl(1, 2, 3) != 321) return 2;
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// Twelve values live across calls whose long long arguments take r10-r17: the values
// there are saved around each call.
TEST_F(AvrTest, RunValuesAcrossWideArguments)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("", CompileAndRunAvr(R"(
long long g(long long a, long long b) { return a - b; }
int main(void)
{
    volatile int seed = 1;
    int a = seed, b = a + 1, c = b + 1, d = c + 1, e = d + 1, f = e + 1;
    int h = f + 1, i = h + 1, j = i + 1, k = j + 1, l = k + 1, m = l + 1;
    long long r = g(a + 100LL, b);
    r += g(c, d) + g(e, f);
    if (r != 99 - 2) return 1;
    if (a + b + c + d + e + f + h + i + j + k + l + m != 78) return 2;
    r = g(h, i) * g(j, k) + g(l, m);
    if (r != 0) return 3;
    if (a * b * c != 6 || k * l * m != 1320) return 4;
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// A long long operation takes r10-r17 for its second operand: the values there survive.
TEST_F(AvrTest, RunValuesAcrossWideOperation)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("", CompileAndRunAvr(R"(
volatile long long x = 1000000007, y = 3;
int main(void)
{
    volatile int seed = 1;
    int a = seed, b = a + 1, c = b + 1, d = c + 1, e = d + 1, f = e + 1;
    int h = f + 1, i = h + 1, j = i + 1, k = j + 1, l = k + 1, m = l + 1;
    long long p = x * y, q = x / y, r = x + y;
    if (p != 3000000021LL || q != 333333335LL || r != 1000000010LL) return 1;
    if (a + b + c + d + e + f + h + i + j + k + l + m != 78) return 2;
    if (p < q || a * m != 12 || f * h != 42) return 3;
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// Scalar slots past Y+63 (volatile ones stay in memory): the function falls back to the
// naive selection, all in memory.
TEST_F(AvrTest, RunFarSlotsFallBack)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("", CompileAndRunAvr(R"(
long f(long n)
{
    volatile long a0 = n, a1 = n + 1, a2 = n + 2, a3 = n + 3, a4 = n + 4, a5 = n + 5;
    volatile long a6 = n + 6, a7 = n + 7, a8 = n + 8, a9 = n + 9, b0 = n + 10;
    volatile long b1 = n + 11, b2 = n + 12, b3 = n + 13, b4 = n + 14, b5 = n + 15;
    volatile long b6 = n + 16, b7 = n + 17;
    long s = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9;
    return s + b0 + b1 + b2 + b3 + b4 + b5 + b6 + b7;
}
int main(void)
{
    return f(1000) == 18 * 1000 + 153 ? 0 : 1;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// Parameters arrive in the registers they are allocated, or are moved there at once.
TEST_F(AvrTest, ParametersStayWhereTheyArrive)
{
    std::string s = Body(CompileToAvr("int f(int a, int b) { return a - b; }"));
    EXPECT_EQ(R"(sub r24, r22
sbc r25, r23
)",
              s);
}
