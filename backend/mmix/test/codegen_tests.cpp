//
// MMIX code generator: golden assembly.
//
#include "mmix_test.h"

TEST_F(MmixTest, ReturnConstant)
{
    EXPECT_EQ(R"(    .text
    .global main
    .p2align 2
main:
    setl    $0, #c8
    pop     1, 0
)",
              CompileToMmix("int main(void) { return 200; }"));
}

TEST_F(MmixTest, StaticFunctionIsLocal)
{
    std::string s = CompileToMmix("static int f(void) { return 1; }");
    EXPECT_EQ(std::string::npos, s.find(".global")) << s;
    EXPECT_NE(std::string::npos, s.find("f:\n")) << s;
}

// A function without a result pops none.
EXPECT_CODE(VoidReturn, "pop 0, 0\n", "void f(void) { return; }")
EXPECT_CODE(VoidFallOff, "pop 0, 0\n", "void f(void) { }")

// The result is the callee's $0, which pop 1,0 hands back; a wide constant is built a
// wyde at a time, as GCC builds it.
EXPECT_CODE(ReturnLong, R"(setl $0, #cdef
incml $0, #89ab
incmh $0, #4567
inch $0, #123
pop 1, 0
)",
            "long f(void) { return 0x0123456789abcdefL; }")
