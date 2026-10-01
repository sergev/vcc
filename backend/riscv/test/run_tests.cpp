//
// RISC-V programs run on bare-metal qemu.
//
#include "riscv_test.h"

TEST_F(RiscvTest, RunReturn2)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("", CompileAndRunRiscv("int main(void) { return 2; }"));
    EXPECT_EQ(2, exit_status);
}

TEST_F(RiscvTest, RunBookStatus)
{
    SKIP_IF_NO_RISCV_TOOLS();
    EXPECT_EQ("-7\n", CompileAndRunBook("int main(void) { return -7; }"));
}

TEST_F(RiscvTest, RunLargeFrame)
{
    SKIP_IF_NO_RISCV_TOOLS();
    std::string src = "int main(void) {\n";
    for (int i = 0; i < 600; i++)
        src += "    int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    src += "    int t = v599;\n    v0 = t;\n    return v0;\n}\n";
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunRiscv(src));
    EXPECT_EQ(599 & 255, exit_status);
}

// 32-bit values stay sign-extended; unsigned division and widening are exact.
TEST_F(RiscvTest, RunIntegerWidths)
{
    SKIP_IF_NO_RISCV_TOOLS();
    DisableOptimization();
    EXPECT_EQ("", CompileAndRunRiscv(R"(
int main(void) {
    unsigned u = 4000000000u;
    unsigned long w = u;
    long l = -3;
    int i = -7;
    unsigned char c = 200;
    return (w != 4000000000ul) | (u / 3u != 1333333333u) << 1 |
           (l * 1000000000000l != -3000000000000l) << 2 | (i % 3 != -1) << 3 |
           ((unsigned)i >> 28 != 15u) << 4 | (c + 100 != 300) << 5 | !(u > 3000000000u) << 6;
})"));
    EXPECT_EQ(0, exit_status);
}
