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
