//
// MMIX code generator: golden assembly.
//
#include "mmix_test.h"

TEST_F(MmixTest, ReturnConstant)
{
    EXPECT_EQ("\t.text\n"
              "\t.global\tmain\n"
              "\t.p2align 2\n"
              "main:\n"
              "\tsetl\t$0,#c8\n"
              "\tpop\t1,0\n",
              CompileToMmix("int main(void) { return 200; }"));
}

TEST_F(MmixTest, StaticFunctionIsLocal)
{
    std::string s = CompileToMmix("static int f(void) { return 1; }");
    EXPECT_EQ(std::string::npos, s.find(".global")) << s;
    EXPECT_NE(std::string::npos, s.find("f:\n")) << s;
}

// A function without a result pops none.
EXPECT_CODE(VoidReturn, "pop 0,0\n", "void f(void) { return; }")
EXPECT_CODE(VoidFallOff, "pop 0,0\n", "void f(void) { }")

// The result is the callee's $0, which pop 1,0 hands back; a wide constant is built a
// wyde at a time, as GCC builds it.
EXPECT_CODE(ReturnLong, "setl $0,#cdef\nincml $0,#89ab\nincmh $0,#4567\ninch $0,#123\npop 1,0\n",
            "long f(void) { return 0x0123456789abcdefL; }")
