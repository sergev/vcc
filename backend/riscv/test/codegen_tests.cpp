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

// A volatile local stays in its slot: the store is not followed into the reload, each
// read is a load of its own, and the parameter it is set from stays in its register.
TEST_F(RiscvTest, VolatileLocalInMemory)
{
    EXPECT_NE(std::string::npos,
              Code(CompileToRiscv("int f(int a) { volatile int x = a; return x; }"))
                  .find("addi sp, sp, -16\nsw a0, 12(sp)\nlw a0, 12(sp)\naddi sp, sp, 16\nret\n"));
}
TEST_F(RiscvTest, VolatileReadsEachLoad)
{
    std::string code = Code(CompileToRiscv("int f(int a) { volatile int x = a; return x + x; }"));
    size_t first     = code.find("lw ");
    ASSERT_NE(std::string::npos, first) << code;
    EXPECT_NE(std::string::npos, code.find("lw ", first + 1)) << code;
}

// A struct of two words lives in registers: no frame for this one, as for two longs.
// One whose address is taken stays in memory (below).
TEST_F(RiscvTest, WordPairInRegisters)
{
    std::string s = Code(CompileToRiscv(R"(
typedef struct { unsigned long hi, lo; } U;
U add(U a, U b)
{
    U r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo);
    return r;
}
)"));
    EXPECT_EQ(std::string::npos, s.find("sp")) << s;
    EXPECT_EQ(std::string::npos, s.find("ld ")) << s;
}

TEST_F(RiscvTest, WordPairAddressTaken)
{
    std::string s = Code(CompileToRiscv(R"(
typedef struct { unsigned long hi, lo; } U;
void g(unsigned long *);
unsigned long f(U a) { g(&a.lo); return a.hi; }
)"));
    EXPECT_NE(std::string::npos, s.find("sd a1, ")) << s;
}
