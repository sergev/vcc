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
EXPECT_CODE(ReturnSignedChar, R"(mov #-1, r12
ret
)", "signed char f(void) { return -1; }")
EXPECT_CODE(ReturnPlainChar, R"(mov #255, r12
ret
)", "char f(void) { return -1; }")
EXPECT_CODE(ReturnInt, R"(mov #4660, r12
ret
)", "int f(void) { return 0x1234; }")
EXPECT_CODE(ReturnLong, R"(mov #772, r12
mov #258, r13
ret
)",
            "long f(void) { return 0x01020304L; }")
EXPECT_CODE(ReturnLongLong,
            R"(mov #1800, r12
mov #1286, r13
mov #772, r14
mov #258, r15
ret
)",
            "long long f(void) { return 0x0102030405060708LL; }")
