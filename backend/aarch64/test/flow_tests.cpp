//
// AArch64 control flow: labels, jumps and conditional jumps.
//
#include "aarch64_test.h"

TEST_F(Aarch64Test, ConditionalJump)
{
    DisableOptimization();
    std::string code =
        Code(CompileToAarch64("int f(void) { int a = 3; if (a) return 1; return 2; }"));
    EXPECT_NE(std::string::npos, code.find("ldr w9, [x29, #-4]\ncbz w9, .L")) << code;
}

TEST_F(Aarch64Test, LoopJumpsBack)
{
    DisableOptimization();
    std::string s = CompileToAarch64(
        "long f(void) { long n = 5; long s = 0; while (n) { s = s + n; n = n - 1; } return s; }");
    EXPECT_NE(std::string::npos, s.find("cbz     x9, .L")) << s;
    EXPECT_NE(std::string::npos, s.find("    b       .L")) << s;
    EXPECT_NE(std::string::npos, s.find("\n.L")) << s; // a label line
}

TEST_F(Aarch64Test, RunLoopAndBranches)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunAarch64("int main(void) {\n"
                                       "    int sum = 0;\n"
                                       "    for (int i = 0; i < 10; i = i + 1) {\n"
                                       "        if (i % 2) continue;\n"
                                       "        sum = sum + i;\n"
                                       "        if (sum > 15) break;\n"
                                       "    }\n"
                                       "    long n = 0;\n"
                                       "    do n = n + 1; while (n < 5);\n"
                                       "    return sum * 10 + (int)n + (sum > 3 && n == 5) * 100;\n"
                                       "}\n"));
    EXPECT_EQ((20 * 10 + 5 + 100) & 255, exit_status);
}
