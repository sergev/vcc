//
// ARM32 IR emitter: register names, the immediate encoder and operand syntax.
//
#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" {
#include "a32.h"
#include "xalloc.h"
}

// The libraries call fatal_error(); defined once for the arm32-tests binary.
extern "C" void fatal_error(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

template <typename F>
static std::string Capture(F write)
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

// One instruction line.  The operand syntax does not depend on the opcode, so the
// operand tests below all use `mov`.
static std::string Line(A32_Op op, std::initializer_list<A32_Operand> opnds,
                        A32_Cond cond = A32_AL, bool set_flags = false)
{
    A32_Instr in{};
    in.op        = op;
    in.cond      = cond;
    in.set_flags = set_flags;
    int i        = 0;
    for (const A32_Operand &o : opnds)
        in.opnd[i++] = o;
    std::string s = Capture([&](FILE *f) { a32_emit_instr(f, &in); });
    for (int k = 0; k < i; k++)
        xfree(in.opnd[k].sym);
    return s;
}

class EmitTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        EXPECT_EQ(xtotal_allocated_size(), 0);
        xfree_all();
    }
};

TEST_F(EmitTest, RegisterNames)
{
    EXPECT_STREQ("r0", a32_reg_name(A32_R0, A32_CORE));
    EXPECT_STREQ("r11", a32_reg_name(A32_FP, A32_CORE));
    EXPECT_STREQ("r12", a32_reg_name(A32_IP, A32_CORE));
    EXPECT_STREQ("sp", a32_reg_name(A32_SP, A32_CORE));
    EXPECT_STREQ("lr", a32_reg_name(A32_LR, A32_CORE));
    EXPECT_STREQ("pc", a32_reg_name(A32_PC, A32_CORE));
    EXPECT_STREQ("s0", a32_reg_name(A32_S0, A32_S));
    EXPECT_STREQ("s31", a32_reg_name(A32_S0 + 31, A32_S));
    EXPECT_STREQ("d0", a32_reg_name(A32_S0, A32_D));
    EXPECT_STREQ("d15", a32_reg_name(A32_S0 + 30, A32_D));
    EXPECT_EQ(nullptr, a32_reg_name(A32_S0 + 1, A32_D)); // an odd single
    EXPECT_EQ(nullptr, a32_reg_name(A32_R0, A32_D));     // the wrong register file
    EXPECT_EQ(nullptr, a32_reg_name(A32_S0, A32_CORE));
    EXPECT_EQ(nullptr, a32_reg_name(A32_VREG, A32_CORE)); // virtual
}

TEST_F(EmitTest, ModifiedImmediates)
{
    for (uint32_t v : { 0u, 1u, 0xffu, 0x100u, 0x3fcu, 0xff000000u, 0xf000000fu, 0x00ab0000u,
                        0xc0000034u })
        EXPECT_TRUE(a32_operand2_imm(v)) << std::hex << v;
    // Not 8 significant bits, or an odd rotation.
    for (uint32_t v : { 0x101u, 0xc06u, 0x1feu, 0xffffu, 0x12345678u, 0xffffffffu, 0x00ff00ffu })
        EXPECT_FALSE(a32_operand2_imm(v)) << std::hex << v;
}

TEST_F(EmitTest, ConditionAndFlags)
{
    EXPECT_EQ("    mov     r0, #1\n", Line(A32_MOV, { a32_reg(0), a32_imm(1) }));
    EXPECT_EQ("    moveq   r0, #1\n", Line(A32_MOV, { a32_reg(0), a32_imm(1) }, A32_EQ));
    EXPECT_EQ("    movs    r0, r1\n", Line(A32_MOV, { a32_reg(0), a32_reg(1) }, A32_AL, true));
    EXPECT_EQ("    mvnsle  r0, #0\n", Line(A32_MVN, { a32_reg(0), a32_imm(0) }, A32_LE, true));
    EXPECT_EQ("    bxne    lr\n", Line(A32_BX, { a32_reg(A32_LR) }, A32_NE));
}

TEST_F(EmitTest, Operands)
{
    EXPECT_EQ("    movw    r1, #:lower16:counter\n", Line(A32_MOVW, { a32_reg(1), a32_lower16("counter", 0) }));
    EXPECT_EQ("    movt    r1, #:upper16:table+8\n", Line(A32_MOVT, { a32_reg(1), a32_upper16("table", 8) }));
    EXPECT_EQ("    mov     r0, r1, lsl #2\n", Line(A32_MOV, { a32_reg(0), a32_shift(1, A32_SHIFT_LSL, 2) }));
    EXPECT_EQ("    mov     r0, r1, asr r2\n", Line(A32_MOV, { a32_reg(0), a32_shift_reg(1, A32_SHIFT_ASR, 2) }));
    EXPECT_EQ("    mov     s2, d3\n", Line(A32_MOV, { a32_sreg(A32_S0 + 2), a32_dreg(A32_S0 + 6) }));
    EXPECT_EQ("    mov     %r0, %d1\n", Line(A32_MOV, { a32_reg(A32_VREG), a32_dreg(A32_VREG + 1) }));
}

TEST_F(EmitTest, MemoryOperands)
{
    EXPECT_EQ("    mov     [r11]\n", Line(A32_MOV, { a32_mem(A32_FP, 0) }));
    EXPECT_EQ("    mov     [r11, #-8]\n", Line(A32_MOV, { a32_mem(A32_FP, -8) }));
    EXPECT_EQ("    mov     [sp, #-8]!\n", Line(A32_MOV, { a32_mem_pre(A32_SP, -8) }));
    EXPECT_EQ("    mov     [sp], #8\n", Line(A32_MOV, { a32_mem_post(A32_SP, 8) }));
    EXPECT_EQ("    mov     [r0, r1]\n", Line(A32_MOV, { a32_mem_index(0, 1, false, 0) }));
    EXPECT_EQ("    mov     [r0, -r1, lsl #2]\n", Line(A32_MOV, { a32_mem_index(0, 1, true, 2) }));
}

TEST_F(EmitTest, RegisterList)
{
    unsigned mask = (1u << 4) | (1u << 5) | (1u << A32_FP) | (1u << A32_LR);
    EXPECT_EQ("    mov     {r4, r5, r11, lr}\n", Line(A32_MOV, { a32_reglist(mask) }));
}

TEST_F(EmitTest, Function)
{
    A32_Func *fn = a32_new_func("main", true);
    A32_Instr *in = a32_append(fn, A32_MOV);
    in->opnd[0]   = a32_reg(A32_R0);
    in->opnd[1]   = a32_imm(2);
    a32_new_block(fn, ".L1");
    in          = a32_append(fn, A32_BX);
    in->opnd[0] = a32_reg(A32_LR);
    EXPECT_EQ(R"(    .text
    .globl  main
    .p2align 2
    .type   main, %function
main:
    mov     r0, #2
.L1:
    bx      lr
    .size   main, .-main
)",
              Capture([&](FILE *f) { a32_emit_func(f, fn); }));
    a32_free_func(fn);
}

TEST_F(EmitTest, HeaderDeclaresTheAbi)
{
    std::string h = Capture([](FILE *f) { a32_emit_header(f); });
    EXPECT_EQ(0u, h.find("    .syntax unified\n"));
    EXPECT_NE(std::string::npos, h.find("    .fpu    vfpv3-d16\n"));
    EXPECT_NE(std::string::npos, h.find("    .eabi_attribute Tag_ABI_VFP_args, 1\n"));
    EXPECT_NE(std::string::npos, h.find("    .eabi_attribute Tag_ABI_enum_size, 2\n"));
    EXPECT_EQ(h.size() - 9, h.rfind("    .arm\n")); // last
}
