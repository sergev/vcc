//
// AVR code generator: golden assembly of returns.
//
#include "avr_test.h"

TEST_F(AvrTest, ReturnConstant)
{
    NaiveSelection();
    EXPECT_EQ(R"(__tmp_reg__ = 0
__zero_reg__ = 1
__SREG__ = 63
__SP_H__ = 62
__SP_L__ = 61
    .text
    .globl  main
    .p2align 1
    .type   main, @function
main:
    ldi     r24, 2
    ldi     r25, 0
.Lv0:
    ret
    .size   main, .-main
)",
              CompileToAvr("int main(void) { return 2; }"));
}

TEST_F(AvrTest, StaticFunctionIsLocal)
{
    NaiveSelection();
    std::string s = CompileToAvr("static int f(void) { return 1; }");
    EXPECT_EQ(std::string::npos, s.find(".globl")) << s;
    EXPECT_NE(std::string::npos, s.find("f:\n")) << s;
}

EXPECT_CODE(VoidReturn, "", "void f(void) { return; }")
EXPECT_CODE(VoidFallOff, "", "void f(void) { }")

// The result registers by size: r25:r24 (a char extended), r25:r22, r25:r18;
// little-endian.
EXPECT_CODE(ReturnChar, R"(ldi r24, 255
ldi r25, 255
)", "signed char f(void) { return -1; }")
EXPECT_CODE(ReturnUnsignedChar, R"(ldi r24, 255
ldi r25, 0
)",
            "unsigned char f(void) { return 255; }")
EXPECT_CODE(ReturnInt, R"(ldi r24, 52
ldi r25, 18
)", "int f(void) { return 0x1234; }")
EXPECT_CODE(ReturnLong, R"(ldi r22, 4
ldi r23, 3
ldi r24, 2
ldi r25, 1
)",
            "long f(void) { return 0x01020304L; }")
EXPECT_CODE(ReturnLongLong,
            R"(ldi r18, 8
ldi r19, 7
ldi r20, 6
ldi r21, 5
ldi r22, 4
ldi r23, 3
ldi r24, 2
ldi r25, 1
)",
            "long long f(void) { return 0x0102030405060708LL; }")

