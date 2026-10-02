//
// AArch64 frame: slots below x29, copies through scratch registers, large frames.
//
#include <regex>

#include "aarch64_test.h"

// A local in a slot: stored, then loaded for the return.
TEST_F(Aarch64Test, LocalSlot)
{
    DisableOptimization();
    EXPECT_EQ(
        "stp x29, x30, [sp, #-16]!\n"
        "mov x29, sp\n"
        "sub sp, sp, #16\n"
        "mov w9, #5\n"
        "str w9, [x29, #-4]\n"
        "ldr w0, [x29, #-4]\n"
        "mov sp, x29\n"
        "ldp x29, x30, [sp], #16\n"
        "ret\n",
        Code(CompileToAarch64("int main(void) { int a = 5; return a; }")));
}

// Offsets beyond ldur's reach go through ip0: sub with lsl #12 and a remainder.
TEST_F(Aarch64Test, LargeFrameOffsets)
{
    DisableOptimization();
    std::string src = "long f(void) {\n";
    for (int i = 0; i < 600; i++)
        src += "    long v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    src += "    return v599;\n}\n";
    std::string code = Code(CompileToAarch64(src.c_str()));
    EXPECT_NE(std::string::npos, code.find(", lsl #12\nsub sp, sp, #")) << code;
    EXPECT_NE(std::string::npos, code.find("sub x16, x29, #1, lsl #12\n")) << code;
    EXPECT_TRUE(std::regex_search(code, std::regex("\nsub x16, x29, #[0-9]+\n"))) << code;
    EXPECT_NE(std::string::npos, code.find("str x9, [x16]\n")) << code;
}

TEST_F(Aarch64Test, RunLocals)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    EXPECT_EQ("",
              CompileAndRunAarch64("int main(void) {\n"
                                   "    int a = 40; int b = a; long c = 2; unsigned char d = 200;\n"
                                   "    b = b; c = c; d = d;\n"
                                   "    return b;\n"
                                   "}\n"));
    EXPECT_EQ(40, exit_status);
}

TEST_F(Aarch64Test, RunLargeFrame)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    DisableOptimization();
    std::string src = "int main(void) {\n"; // over 4 KiB of slots
    for (int i = 0; i < 1200; i++)
        src += "    int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    src += "    v0 = v1199;\n    return v0;\n}\n";
    EXPECT_EQ("", CompileAndRunAarch64(src));
    EXPECT_EQ(1199 & 255, exit_status);
}
