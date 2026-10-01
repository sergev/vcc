//
// RISC-V IR emitter and codegen skeleton.
//
#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" {
#include "codegen.h"
#include "rv.h"
#include "xalloc.h"
}

// The libraries call fatal_error(); defined once for the riscv-tests binary.
extern "C" void fatal_error(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

template <typename F> static std::string Capture(F write)
{
    FILE *f = tmpfile();
    EXPECT_NE(nullptr, f);
    write(f);
    long len = ftell(f);
    rewind(f);
    std::string s(static_cast<size_t>(len), '\0');
    if (len > 0)
        EXPECT_EQ(1u, fread(&s[0], static_cast<size_t>(len), 1, f));
    fclose(f);
    return s;
}

class EmitTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        xreport_lost_memory();
        EXPECT_EQ(xtotal_allocated_size(), 0);
        xfree_all();
    }
};

TEST_F(EmitTest, Operands)
{
    Rv_Func *fn = rv_new_func("f", false);
    Rv_Instr *in = rv_append(fn, RV_LI);
    in->opnd[0]  = rv_reg(RV_A0);
    in->opnd[1]  = rv_imm(-5);
    in           = rv_append(fn, RV_MV);
    in->opnd[0]  = rv_reg(RV_VREG + 3);
    in->opnd[1]  = rv_reg(RV_FA0 + 1);
    rv_new_block(fn, ".L7");
    rv_append(fn, RV_J)->opnd[0] = rv_sym("g", 8);
    in          = rv_append(fn, RV_FLD);
    in->opnd[0] = rv_reg(RV_F0);
    in->opnd[1] = rv_mem(RV_S0, -24);
    rv_append(fn, RV_RET);
    std::string s = Capture([&](FILE *f) { rv_emit_func(f, fn); });
    rv_free_func(fn);
    EXPECT_EQ(R"(    .text
    .p2align 2
    .type   f, @function
f:
    li      a0, -5
    mv      v3, fa1
.L7:
    j       g+8
    fld     ft0, -24(s0)
    ret
    .size   f, .-f
)",
              s);
}

// A function with labels and jumps only; TAC built by hand.
TEST_F(EmitTest, LabelsAndJumps)
{
    Tac_TopLevel *tl      = tac_new_toplevel(TAC_TOPLEVEL_FUNCTION);
    tl->u.function.name   = xstrdup("loop");
    tl->u.function.global = true;
    Tac_Instruction *l    = tac_new_instruction(TAC_INSTRUCTION_LABEL);
    l->u.label.name       = xstrdup("%L1");
    Tac_Instruction *j    = tac_new_instruction(TAC_INSTRUCTION_JUMP);
    j->u.jump.target      = xstrdup("%L1");
    l->next               = j;
    tl->u.function.body   = l;
    std::string s = Capture([&](FILE *f) { riscv_codegen(tl, tl, f); });
    tac_free_toplevel(tl);
    EXPECT_EQ(R"(    .text
    .globl  loop
    .p2align 2
    .type   loop, @function
loop:
    addi    sp, sp, -16
    sd      ra, 8(sp)
    sd      s0, 0(sp)
    addi    s0, sp, 16
.LL1:
    j       .LL1
    .size   loop, .-loop
)",
              s);
}
