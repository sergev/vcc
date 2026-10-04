//
// AVR code generator: golden assembly.
//
#include "avr_test.h"

TEST_F(AvrTest, ReturnConstant)
{
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
    ret
    .size   main, .-main
)",
              CompileToAvr("int main(void) { return 2; }"));
}

TEST_F(AvrTest, StaticFunctionIsLocal)
{
    std::string s = CompileToAvr("static int f(void) { return 1; }");
    EXPECT_EQ(std::string::npos, s.find(".globl")) << s;
    EXPECT_NE(std::string::npos, s.find("f:\n")) << s;
}

EXPECT_CODE(VoidReturn, "ret\n", "void f(void) { return; }")
EXPECT_CODE(VoidFallOff, "ret\n", "void f(void) { }")

// The result registers by size: r24, r25:r24, r25:r22, r25:r18; little-endian.
EXPECT_CODE(ReturnChar, "ldi r24, 255\nret\n", "signed char f(void) { return -1; }")
EXPECT_CODE(ReturnInt, "ldi r24, 52\nldi r25, 18\nret\n", "int f(void) { return 0x1234; }")
EXPECT_CODE(ReturnLong, "ldi r22, 4\nldi r23, 3\nldi r24, 2\nldi r25, 1\nret\n",
            "long f(void) { return 0x01020304L; }")
EXPECT_CODE(ReturnLongLong,
            "ldi r18, 8\nldi r19, 7\nldi r20, 6\nldi r21, 5\n"
            "ldi r22, 4\nldi r23, 3\nldi r24, 2\nldi r25, 1\nret\n",
            "long long f(void) { return 0x0102030405060708LL; }")
