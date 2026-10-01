//
// RISC-V code generation from C source: golden assembly.
//
#include "riscv_test.h"

TEST_F(RiscvTest, EmptyFunction)
{
    EXPECT_EQ(R"(    .text
    .globl  f
    .p2align 2
    .type   f, @function
f:
    addi    sp, sp, -16
    sd      ra, 8(sp)
    sd      s0, 0(sp)
    addi    s0, sp, 16
    addi    sp, s0, -16
    ld      ra, 8(sp)
    ld      s0, 0(sp)
    addi    sp, sp, 16
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
    addi    sp, sp, -16
    sd      ra, 8(sp)
    sd      s0, 0(sp)
    addi    s0, sp, 16
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
    addi    sp, sp, -16
    sd      ra, 8(sp)
    sd      s0, 0(sp)
    addi    s0, sp, 16
    li      a0, -2
    addi    sp, s0, -16
    ld      ra, 8(sp)
    ld      s0, 0(sp)
    addi    sp, sp, 16
    ret
    .size   main, .-main
)",
              CompileToRiscv("int main(void) { return -2; }"));
}
