//
// ARM32 integer operations and width conversions.  The optimizer is off, so each
// operation reaches the code generator; a run test checks several results at once,
// one bit of the exit status each.
//
#include "arm32_test.h"

#define EXPECT_SELECTS(name, expected, src)                        \
    TEST_F(Arm32Test, name)                                        \
    {                                                              \
        DisableOptimization();                                     \
        NaiveSelection();                                          \
        std::string code = Code(CompileToArm32(src));              \
        EXPECT_NE(std::string::npos, code.find(expected)) << code; \
    }

EXPECT_SELECTS(AddInt, R"(ldr r12, [r11, #-4]
ldr lr, [r11, #-8]
add r12, r12, lr
str r12,)",
               "int f(void) { int a = 1; int b = 2; return a + b; }")
// A constant operand is a modified immediate, or its negation with add and sub
// swapped, or its complement with and turned into bic; else it is loaded.
EXPECT_SELECTS(AddImmediate, "add r12, r12, #1020\n",
               "int f(void) { int a = 1; return a + 1020; }")
EXPECT_SELECTS(AddNegatedImmediate, "sub r12, r12, #256\n",
               "unsigned f(void) { unsigned a = 1; return a + 0xffffff00u; }")
EXPECT_SELECTS(SubtractNegatedImmediate, "add r12, r12, #1\n",
               "unsigned f(void) { unsigned a = 1; return a - 0xffffffffu; }")
EXPECT_SELECTS(AndComplement, "bic r12, r12, #255\n",
               "unsigned f(void) { unsigned a = 1; return a & 0xffffff00u; }")
EXPECT_SELECTS(OrLoadedConstant, R"(movw lr, #4660
orr r12, r12, lr
)",
               "int f(void) { int a = 1; return a | 0x1234; }")
EXPECT_SELECTS(CompareNegatedImmediate, R"(cmn r12, #256
mov r12, #0
movhi r12, #1
)",
               "int f(void) { unsigned a = 1; return a > 0xffffff00u; }")
EXPECT_SELECTS(MultiplyInt, "mul r12, r12, lr\n",
               "int f(void) { int a = 3; int b = 4; return a * b; }")
EXPECT_SELECTS(Remainder, R"(sdiv r10, r12, lr
mls r12, r10, lr, r12
)",
               "int f(void) { int a = 7; int b = 2; return a % b; }")
EXPECT_SELECTS(UnsignedDivide, "udiv r12, r12, lr\n",
               "unsigned f(void) { unsigned a = 7; unsigned b = 2; return a / b; }")
EXPECT_SELECTS(SignedCompare, R"(cmp r12, lr
mov r12, #0
movlt r12, #1
)",
               "int f(void) { int a = 7; int b = 2; return a < b; }")
EXPECT_SELECTS(UnsignedCompare, R"(cmp r12, lr
mov r12, #0
movhs r12, #1
)",
               "int f(void) { unsigned long a = 7; unsigned long b = 2; return a >= b; }")
EXPECT_SELECTS(ArithmeticShift, "asr r12, r12, lr\n",
               "int f(void) { int a = -8; int b = 1; return a >> b; }")
EXPECT_SELECTS(LogicalShift, "lsr r12, r12, lr\n",
               "unsigned f(void) { unsigned a = 8; int b = 1; return a >> b; }")
EXPECT_SELECTS(ShiftByConstant, "lsl r12, r12, #3\n", "int f(void) { int a = 8; return a << 3; }")
EXPECT_SELECTS(Negate, "rsb r12, r12, #0\n", "int f(void) { int a = 3; return -a; }")
EXPECT_SELECTS(Not, R"(cmp r12, #0
mov r12, #0
moveq r12, #1
)",
               "int f(void) { int a = 3; return !a; }")
EXPECT_SELECTS(Complement, "mvn r12, r12\n", "long f(void) { long a = 3; return ~a; }")
EXPECT_SELECTS(SignExtendChar, R"(ldrsb r12, [r11, #-1]
str r12,)",
               "int f(void) { signed char a = -3; return a; }")
EXPECT_SELECTS(TruncateIntToChar, R"(ldr r12, [r11, #-4]
strb r12,)",
               "char f(void) { int a = 300; char c = a; return c; }")
// A narrow result is extended by the callee.
EXPECT_SELECTS(NarrowResult, "ldrsh r0, [r11, #-2]\n",
               "short f(void) { short a = -3; return a; }")

TEST_F(Arm32Test, RunDivisionAndRemainder)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    int a = -7; int b = 2; unsigned u = -7; unsigned v = 2;
    int m = -2147483647 - 1;
    return (a / b == -3) + 2 * (a % b == -1) + 4 * (u / v == 2147483644u)
         + 8 * (u % v == 1) + 16 * (a / -b == 3) + 32 * (m / 2 == -1073741824)
         + 64 * (-a % b == 1) + 128 * (u % 3u == 0);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Arm32Test, RunShifts)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    int a = -16; int two = 2; unsigned u = 0x80000000u; int n = 31;
    long l = 1; long thirty = 30; int m = -1;
    return (a >> two == -4) + 2 * (u >> n == 1) + 4 * (l << thirty == 1073741824l)
         + 8 * (u >> 31 == 1) + 16 * (m >> 31 == -1) + 32 * (a << two == -64);
})"));
    EXPECT_EQ(63, exit_status);
}

TEST_F(Arm32Test, RunComparisons)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    int a = -1; int b = 1; unsigned u = -1; unsigned v = 1;
    long la = -1; unsigned long ua = -1; unsigned long ub = 1;
    return (a < b) + 2 * (u > v) + 4 * (la <= a) + 8 * (ua >= ub)
         + 16 * (a != b) + 32 * !(u < v) + 64 * (la == -1) + 128 * (b > a);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Arm32Test, RunImmediates)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    int a = 1000; unsigned u = 0xffffu; unsigned m = 0xffffff80u;
    return (u + 0xffffff00u == 0xfeffu) + 2 * (u - 0xffffffffu == 0x10000u)
         + 4 * ((u & 0xffffff00u) == 0xff00u) + 8 * (m > 0xffffff00u)
         + 16 * (a - 0x12345 == -73565) + 32 * ((u | 0x10000) == 0x1ffff)
         + 64 * ((u ^ 0xff000000u) == 0xff00ffffu) + 128 * !(u > 0xffffff00u);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Arm32Test, RunConversions)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    signed char c = 200; unsigned char u = 200; short s = 70000;
    long l = -1; int i = -5; unsigned short us = 65535; unsigned w = -1;
    return (c == -56) + 2 * (u == 200) + 4 * (s == 4464)
         + 8 * ((unsigned)l == 4294967295u) + 16 * ((long)i == -5l)
         + 32 * ((signed char)u == -56) + 64 * (us + 1 == 65536)
         + 128 * ((short)w == -1);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Arm32Test, RunUnary)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    int a = 5; long l = 0; unsigned u = 0;
    return (-a == -5) + 2 * (~a == -6) + 4 * (!a == 0)
         + 8 * (!l == 1) + 16 * (-u == 0) + 32 * (~u == 4294967295u);
})"));
    EXPECT_EQ(63, exit_status);
}
