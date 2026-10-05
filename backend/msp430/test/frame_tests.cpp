//
// MSP430 frames: slots off SP, the outgoing area below them, parameters stored from
// their registers, stack parameters left where they came in.
//
#include "msp430_test.h"

// No slots: no frame at all.
EXPECT_CODE(FramelessConstant, R"(mov #7, r12
ret
)", "int f(void) { return 7; }")

// Register parameters go to their slots; the frame is reserved and released around the
// body.
EXPECT_CODE(ParamsStored,
            R"(sub #6, r1
mov r12, 0(r1)
mov r13, 2(r1)
mov 0(r1), r12
add 2(r1), r12
mov r12, 4(r1)
mov 4(r1), r12
add #6, r1
ret
)",
            "int f(int a, int b) { return a + b; }")

// The fifth int comes on the stack, above the frame and the return address, and is
// read where it lies.
TEST_F(Msp430Test, StackParamAboveFrame)
{
    std::string code = Code(CompileToMsp430(
        "int f(int a, int b, int c, int d, int e) { return e; }"));
    EXPECT_NE(std::string::npos, code.find("sub #8, r1\n")) << code;
    EXPECT_NE(std::string::npos, code.find("mov 10(r1), r12\n")) << code;
}

// A long with only r15 left: its low word in r15, its high word on the stack, copied
// into the slot.
TEST_F(Msp430Test, SplitLongParam)
{
    std::string code =
        Code(CompileToMsp430("long f(int a, int b, int c, long d) { return d; }"));
    EXPECT_NE(std::string::npos, code.find(R"(mov r15, 6(r1)
mov 12(r1), 8(r1)
)")) << code;
}

// A char parameter is stored as a byte.
EXPECT_CODE(CharParam,
            R"(sub #4, r1
mov.b r12, 0(r1)
mov.b 0(r1), r12
sxt r12
mov r12, 2(r1)
mov 2(r1), r12
add #4, r1
ret
)",
            "int f(signed char c) { return c; }")

// Slots are aligned to their types; a char array may be odd-sized.
TEST_F(Msp430Test, SlotsAligned)
{
    std::string code = Code(CompileToMsp430(R"(
        int g(char *p, int *q);
        int f(void) { char s[3]; int i; return g(s, &i); }
    )"));
    EXPECT_NE(std::string::npos, code.find("sub #")) << code;
    // &i is even: no odd offset added to SP.
    for (const char *odd : { "add #1, r12", "add #3, r12", "add #5, r12", "add #7, r12" })
        EXPECT_EQ(std::string::npos, code.find(odd)) << odd << "\n" << code;
}

// Run: the parameters of every kind arrive where the caller put them.
TEST_F(Msp430Test, RunStackAndSplitParams)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430(R"(
        int five(int a, int b, int c, int d, int e) { return a + 2 * b + 3 * c + 4 * d + 5 * e; }
        long split(int a, int b, int c, long d) { return d - a; }
        long after(int a, long long b, int c, int d, long e) { return c + d + e; }
        int main(void)
        {
            if (five(1, 2, 3, 4, 5) != 55)
                return 1;
            if (split(1, 2, 3, 100000L) != 99999L)
                return 2;
            if (after(1, 2, 3, 4, 70000L) != 70007L)
                return 3;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}

// Run: a frame over 1 KB, its locals at large offsets.
TEST_F(Msp430Test, RunLargeFrame)
{
    SKIP_IF_NO_MSP430_TOOLS();
    EXPECT_EQ("", CompileAndRunMsp430(R"(
        int f(int n)
        {
            int a[600];
            for (int i = 0; i < 600; i++)
                a[i] = i * n;
            return a[599] - a[1];
        }
        int main(void) { return f(1) == 598 ? 0 : 1; }
    )"));
    EXPECT_EQ(0, exit_status);
}
