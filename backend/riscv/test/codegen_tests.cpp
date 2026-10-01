//
// RISC-V code generation from C source: golden assembly.
//
#include "riscv_test.h"

TEST_F(RiscvTest, EmptyFunction)
{
    EXPECT_EQ("    .text\n"
              "    .globl  f\n"
              "    .p2align 2\n"
              "    .type   f, @function\n"
              "f:\n"
              "    addi    sp, sp, -16\n"
              "    sd      ra, 8(sp)\n"
              "    sd      s0, 0(sp)\n"
              "    addi    s0, sp, 16\n"
              "    addi    sp, s0, -16\n"
              "    ld      ra, 8(sp)\n"
              "    ld      s0, 0(sp)\n"
              "    addi    sp, sp, 16\n"
              "    ret\n"
              "    .size   f, .-f\n",
              CompileToRiscv("void f(void) {}"));
}

// A static function is not .globl; an extern declaration emits nothing.
TEST_F(RiscvTest, StaticFunctionAndExtern)
{
    EXPECT_EQ("    .text\n"
              "    .p2align 2\n"
              "    .type   g, @function\n"
              "g:\n"
              "    addi    sp, sp, -16\n"
              "    sd      ra, 8(sp)\n"
              "    sd      s0, 0(sp)\n"
              "    addi    s0, sp, 16\n"
              ".L2:\n"
              "    j       .L2\n"
              "    .size   g, .-g\n",
              CompileToRiscv("extern int x; static void g(void) { for (;;) ; }"));
}

TEST_F(RiscvTest, ReturnConstant)
{
    EXPECT_EQ("    .text\n"
              "    .globl  main\n"
              "    .p2align 2\n"
              "    .type   main, @function\n"
              "main:\n"
              "    addi    sp, sp, -16\n"
              "    sd      ra, 8(sp)\n"
              "    sd      s0, 0(sp)\n"
              "    addi    s0, sp, 16\n"
              "    li      a0, -2\n"
              "    addi    sp, s0, -16\n"
              "    ld      ra, 8(sp)\n"
              "    ld      s0, 0(sp)\n"
              "    addi    sp, sp, 16\n"
              "    ret\n"
              "    .size   main, .-main\n",
              CompileToRiscv("int main(void) { return -2; }"));
}
