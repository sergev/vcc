//
// MSP430 frames: slots off SP, the outgoing area below them, parameters stored from
// their registers, stack parameters left where they came in.
//
#include "msp430_test.h"

// No slots: no frame at all.
EXPECT_CODE(FramelessConstant, R"(mov #7, r12
ret
)", "int f(void) { return 7; }")

// A leaf with no slots and no call-saved register: no frame, the compare fused with its
// branch, which goes straight to the bare ret.
EXPECT_CODE(FramelessEarlyReturn, R"(cmp r12, r13
jl .Lv0
mov r13, r12
ret
)", "int max(int a, int b) { if (a > b) return a; return b; }")

// With a frame, an early return still goes to the one epilogue.
TEST_F(Msp430Test, EarlyReturnToEpilogue)
{
    std::string code = Code(CompileToMsp430(
        "int g(int); int f(int a) { if (a) return g(a) + a; return 0; }"));
    EXPECT_NE(std::string::npos, code.find("jmp")) << code;
    EXPECT_NE(std::string::npos, code.find("pop r10\nret\n")) << code;
}

// Allocated: the parameters stay in the registers they came in, and the frame vanishes.
EXPECT_CODE(ParamsInRegisters, R"(add r13, r12
ret
)", "int f(int a, int b) { return a + b; }")

// Without allocation, register parameters go to their slots; the frame is reserved and
// released around the body, and the operation works on the slots.
TEST_F(Msp430Test, ParamsStored)
{
    NaiveSelection();
    EXPECT_EQ(R"(sub #6, r1
mov r12, 0(r1)
mov r13, 2(r1)
mov @r1, 4(r1)
add 2(r1), 4(r1)
mov 4(r1), r12
add #6, r1
ret
)",
              Code(CompileToMsp430("int f(int a, int b) { return a + b; }")));
}

// The fifth int comes on the stack, above the return address (and the frame, when
// there is one), and is read where it lies.
EXPECT_CODE(StackParamAboveFrame, R"(mov 2(r1), r12
ret
)", "int f(int a, int b, int c, int d, int e) { return e; }")

TEST_F(Msp430Test, StackParamAboveSlots)
{
    NaiveSelection();
    std::string code = Code(CompileToMsp430(
        "int f(int a, int b, int c, int d, int e) { return e; }"));
    EXPECT_NE(std::string::npos, code.find("sub #8, r1\n")) << code;
    EXPECT_NE(std::string::npos, code.find("mov 10(r1), r12\n")) << code;
}

// --frame-pointer: the slots and the incoming arguments from r4, set after the frame is
// reserved, r4 saved; the epilogue puts SP back from it.
TEST_F(Msp430Test, FramePointerOption)
{
    NaiveSelection();
    msp430_frame_pointer = true;
    EXPECT_EQ(R"(push r4
sub #8, r1
mov r1, r4
mov r12, 0(r4)
mov r13, 2(r4)
mov r14, 4(r4)
mov r15, 6(r4)
mov 12(r4), r12
mov r4, r1
add #8, r1
pop r4
ret
)",
              Code(CompileToMsp430("int f(int a, int b, int c, int d, int e) { return e; }")));
    msp430_frame_pointer = false;
}

// A long with only r15 left: its low word in r15, its high word on the stack, taken
// into registers, or copied into the slot.
EXPECT_CODE(SplitLongParam, R"(mov r15, r12
mov 2(r1), r13
ret
)", "long f(int a, int b, int c, long d) { return d; }")

TEST_F(Msp430Test, SplitLongParamStored)
{
    NaiveSelection();
    std::string code =
        Code(CompileToMsp430("long f(int a, int b, int c, long d) { return d; }"));
    EXPECT_NE(std::string::npos, code.find(R"(mov r15, 6(r1)
mov 12(r1), 8(r1)
)")) << code;
}

// A char parameter is extended where it is, or stored as a byte.
EXPECT_CODE(CharParam, R"(sxt r12
ret
)", "int f(signed char c) { return c; }")

TEST_F(Msp430Test, CharParamStored)
{
    NaiveSelection();
    EXPECT_EQ(R"(sub #4, r1
mov.b r12, 0(r1)
mov.b @r1, r15
sxt r15
mov r15, 2(r1)
mov 2(r1), r12
add #4, r1
ret
)",
              Code(CompileToMsp430("int f(signed char c) { return c; }")));
}

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
