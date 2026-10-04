//
// x86-64 code generator: golden assembly.
//
#include "x86_test.h"

TEST_F(X86Test, ReturnConstant)
{
    EXPECT_EQ(R"(    .text
    .globl  main
    .p2align 4
    .type   main, @function
main:
    movl    $2, %eax
    ret
    .size   main, .-main
)",
              CompileToX86("int main(void) { return 2; }"));
}

TEST_F(X86Test, StaticFunctionIsLocal)
{
    std::string s = CompileToX86("static int f(void) { return 1; }");
    EXPECT_EQ(std::string::npos, s.find(".globl")) << s;
    EXPECT_NE(std::string::npos, s.find("f:\n")) << s;
}

EXPECT_CODE(VoidReturn, "ret\n", "void f(void) { return; }")
EXPECT_CODE(VoidFallOff, "ret\n", "void f(void) { }")

// Zero is xor; a 32-bit constant one movl.
EXPECT_CODE(ConstZero, "xorl %eax, %eax\nret\n", "int f(void) { return 0; }")
EXPECT_CODE(ConstMinusOne, "movl $-1, %eax\nret\n", "int f(void) { return -1; }")
EXPECT_CODE(ConstUnsigned, "movl $-1, %eax\nret\n", "unsigned f(void) { return 0xffffffffu; }")
// A 64-bit one: movl when it fits 32 bits zero-extended (the upper half is cleared),
// movq when it fits them sign-extended, otherwise movabsq.
EXPECT_CODE(ConstLongZeroExtended, "movl $-1, %eax\nret\n",
            "unsigned long f(void) { return 0xffffffffUL; }")
EXPECT_CODE(ConstLongSignExtended, "movq $-5, %rax\nret\n", "long f(void) { return -5; }")
EXPECT_CODE(ConstLongWide, "movabsq $4886718345, %rax\nret\n",
            "long f(void) { return 0x123456789L; }")
EXPECT_CODE(ConstLongMin, "movabsq $-9223372036854775808, %rax\nret\n",
            "long f(void) { return -9223372036854775807L - 1; }")
