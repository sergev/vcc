//
// ARM32 long long: inline word-pair sequences joined by the carry flag, and the RTABI
// helpers for the rest.
//
#include "arm32_test.h"

#define EXPECT_SELECTS(name, expected, src)                        \
    TEST_F(Arm32Test, name)                                        \
    {                                                              \
        DisableOptimization();                                     \
        std::string code = Code(CompileToArm32(src));              \
        EXPECT_NE(std::string::npos, code.find(expected)) << code; \
    }

// Each word is stored as it is computed; the carry survives the loads and the store.
EXPECT_SELECTS(LongLongAdd, R"(ldr r12, [r11, #-8]
ldr lr, [r11, #-24]
adds r12, r12, lr
str r12, [r11, #-40]
ldr r12, [r11, #-4]
ldr lr, [r11, #-20]
adc r12, r12, lr
str r12, [r11, #-36]
)",
               "long long f(void) { long long a = 1; long long b = 2; return a + b; }")
EXPECT_SELECTS(LongLongSubtract, "subs r12, r12, lr\nstr r12, [r11, #-40]\n",
               "long long f(void) { long long a = 1; long long b = 2; return a - b; }")
EXPECT_SELECTS(LongLongNegate, "rsbs r12, r12, #0\nrsc lr, lr, #0\n",
               "long long f(void) { long long a = 1; return -a; }")
// A compare subtracts with the borrow: cmp of the low words, sbcs of the high ones.
EXPECT_SELECTS(LongLongLess, R"(cmp r12, lr
ldr r12, [r11, #-4]
ldr lr, [r11, #-20]
sbcs r12, r12, lr
mov r12, #0
movlt r12, #1
)",
               "int f(void) { long long a = 1; long long b = 2; return a < b; }")
// Greater compares the other way round.
EXPECT_SELECTS(UnsignedLongLongGreater, R"(ldr r12, [r11, #-24]
ldr lr, [r11, #-8]
cmp r12, lr
)",
               "int f(void) { unsigned long long a = 1; unsigned long long b = 2; return a > b; }")
EXPECT_SELECTS(LongLongEqual, R"(cmp r12, lr
ldr r12, [r11, #-8]
ldr lr, [r11, #-24]
cmpeq r12, lr
mov r12, #0
moveq r12, #1
)",
               "int f(void) { long long a = 1; long long b = 2; return a == b; }")
EXPECT_SELECTS(LongLongShiftLeftConstant, R"(lsl lr, lr, #4
orr lr, lr, r12, lsr #28
lsl r12, r12, #4
)",
               "long long f(void) { long long a = 1; return a << 4; }")
EXPECT_SELECTS(LongLongShiftRightConstant40, "asr r12, lr, #8\nasr lr, lr, #31\n",
               "long long f(void) { long long a = 1; return a >> 40; }")
EXPECT_SELECTS(LongLongShiftVariable, "bl __aeabi_lasr\n",
               "long long f(void) { long long a = 1; int n = 3; return a >> n; }")
EXPECT_SELECTS(LongLongMultiply, R"(ldr r0, [r11, #-8]
ldr r1, [r11, #-4]
ldr r2, [r11, #-24]
ldr r3, [r11, #-20]
bl __aeabi_lmul
str r0,)",
               "long long f(void) { long long a = 3; long long b = 4; return a * b; }")
// The quotient comes back in r0:r1, the remainder in r2:r3.
EXPECT_SELECTS(LongLongDivide, "bl __aeabi_ldivmod\nstr r0,",
               "long long f(void) { long long a = 7; long long b = 2; return a / b; }")
EXPECT_SELECTS(UnsignedLongLongRemainder, "bl __aeabi_uldivmod\nstr r2,",
               "unsigned long long f(void) { unsigned long long a = 7; unsigned long long b = 2; "
               "return a % b; }")
// The conversions with FP take and return a double in a core register pair.
EXPECT_SELECTS(LongLongToDouble, "bl __aeabi_l2d\nstr r0, [r11, #-",
               "double f(void) { long long a = 7; return a; }")
EXPECT_SELECTS(FloatToUnsignedLongLong, "ldr r0, [r11, #-4]\nbl __aeabi_f2ulz\n",
               "unsigned long long f(void) { float a = 7; return a; }")
EXPECT_SELECTS(SignExtendToLongLong, "ldr r12, [r11, #-4]\nmov lr, r12, asr #31\n",
               "long long f(void) { int a = -3; return a; }")

TEST_F(Arm32Test, RunLongLongArithmetic)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    long long a = 0xffffffffLL; long long b = 1; unsigned long long m = 0x8000000000000000ULL;
    long long c = a + b; long long d = c - b; long long n = -c;
    long long p = 0x123456789LL; long long q = 0x100000001LL;
    return (c == 0x100000000LL) + 2 * (d == a) + 4 * (n == -4294967296LL)
         + 8 * ((p & q) == 0x100000001LL) + 16 * ((p ^ q) == 0x23456788LL)
         + 32 * (~b == -2) + 64 * (p * q == 0x2345678A23456789LL) + 128 * !(m == 0);
})"));
    EXPECT_EQ(255, exit_status);
}

TEST_F(Arm32Test, RunLongLongShiftsAndCompares)
{
    SKIP_IF_NO_ARM32_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunArm32(R"(
int main(void) {
    long long p = 0x123456789LL; unsigned long long m = 0x8000000000000000ULL;
    long long s = (long long)m; long long a = 0xffffffffLL; int n = 40; long long k = 63;
    return (p << 4 == 0x1234567890LL) + 2 * (m >> 63 == 1) + 4 * (s >> 40 == -8388608)
         + 8 * (a << 32 == (long long)0xffffffff00000000ULL) + 16 * (s >> n == -8388608)
         + 32 * (m >> k == 1) + 64 * (s < 1 && m > 1 && s <= s && !(s > 1))
         + 128 * (p >= p && p != a && -1LL < 0 && 0xffffffffULL < 0x100000000ULL);
})"));
    EXPECT_EQ(255, exit_status);
}
