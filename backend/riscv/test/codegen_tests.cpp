//
// RISC-V code generation from C source: golden assembly.
//
#include "riscv_test.h"

TEST_F(RiscvTest, EmptyFunction)
{
    EXPECT_EQ("\t.text\n"
              "\t.globl\tf\n"
              "\t.p2align\t2\n"
              "\t.type\tf, @function\n"
              "f:\n"
              "\tret\n"
              "\t.size\tf, .-f\n",
              CompileToRiscv("void f(void) {}"));
}

// A static function is not .globl; an extern declaration emits nothing.
TEST_F(RiscvTest, StaticFunctionAndExtern)
{
    EXPECT_EQ("\t.text\n"
              "\t.p2align\t2\n"
              "\t.type\tg, @function\n"
              "g:\n"
              ".L2:\n"
              "\tj\t.L2\n"
              "\tret\n"
              "\t.size\tg, .-g\n",
              CompileToRiscv("extern int x; static void g(void) { for (;;) ; }"));
}

TEST_F(RiscvTest, ReturnConstant)
{
    EXPECT_EQ("\t.text\n"
              "\t.globl\tmain\n"
              "\t.p2align\t2\n"
              "\t.type\tmain, @function\n"
              "main:\n"
              "\tli\ta0, -2\n"
              "\tret\n"
              "\t.size\tmain, .-main\n",
              CompileToRiscv("int main(void) { return -2; }"));
}
