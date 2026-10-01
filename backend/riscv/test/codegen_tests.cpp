//
// RISC-V code generation from C source: golden assembly.
//
#include "riscv_test.h"

// A leaf function that needs no stack has no frame.
TEST_F(RiscvTest, EmptyFunction)
{
    EXPECT_EQ(R"(    .text
    .globl  f
    .p2align 2
    .type   f, @function
f:
    ret
    .size   f, .-f
)",
              CompileToRiscv("void f(void) {}"));
}

// A static function is not .globl; an extern declaration emits nothing.
TEST_F(RiscvTest, StaticFunctionAndExtern)
{
    EXPECT_EQ(R"(    .text
    .p2align 2
    .type   g, @function
g:
.L2:
    j       .L2
    .size   g, .-g
)",
              CompileToRiscv("extern int x; static void g(void) { for (;;) ; }"));
}

TEST_F(RiscvTest, ReturnConstant)
{
    EXPECT_EQ(R"(    .text
    .globl  main
    .p2align 2
    .type   main, @function
main:
    li      a0, -2
    ret
    .size   main, .-main
)",
              CompileToRiscv("int main(void) { return -2; }"));
}
