//
// MSP430 code generator: golden assembly.
//
#include "msp430_test.h"

TEST_F(Msp430Test, ReturnConstant)
{
    EXPECT_EQ(R"(    .text
    .globl  main
    .p2align 1
    .type   main, @function
main:
    mov     #2, r12
.Lv0:
    ret
    .size   main, .-main
)",
              CompileToMsp430("int main(void) { return 2; }"));
}

TEST_F(Msp430Test, StaticFunctionIsLocal)
{
    std::string s = CompileToMsp430("static int f(void) { return 1; }");
    EXPECT_EQ(std::string::npos, s.find(".globl")) << s;
    EXPECT_NE(std::string::npos, s.find("f:\n")) << s;
}

EXPECT_CODE(VoidReturn, "ret\n", "void f(void) { return; }")
EXPECT_CODE(VoidFallOff, "ret\n", "void f(void) { }")

// The result registers by size: r12, r13:r12, r15:r12; little-endian.  A char comes back
// extended to int, as clang's callers expect.
EXPECT_CODE(ReturnSignedChar, "mov #-1, r12\nret\n", "signed char f(void) { return -1; }")
EXPECT_CODE(ReturnPlainChar, "mov #255, r12\nret\n", "char f(void) { return -1; }")
EXPECT_CODE(ReturnInt, "mov #4660, r12\nret\n", "int f(void) { return 0x1234; }")
EXPECT_CODE(ReturnLong, "mov #772, r12\nmov #258, r13\nret\n",
            "long f(void) { return 0x01020304L; }")
EXPECT_CODE(ReturnLongLong,
            "mov #1800, r12\nmov #1286, r13\nmov #772, r14\nmov #258, r15\nret\n",
            "long long f(void) { return 0x0102030405060708LL; }")
