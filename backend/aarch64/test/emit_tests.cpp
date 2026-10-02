//
// AArch64 IR emitter: register names and operand syntax.
//
#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" {
#include "a64.h"
#include "xalloc.h"
}

// The libraries call fatal_error(); defined once for the aarch64-tests binary.
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
static std::string Line(A64_Op op, std::initializer_list<A64_Operand> opnds)
{
    A64_Instr in{};
    in.op = op;
    int i = 0;
    for (const A64_Operand &o : opnds)
        in.opnd[i++] = o;
    std::string s = Capture([&](FILE *f) { a64_emit_instr(f, &in); });
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
    EXPECT_STREQ("w0", a64_reg_name(A64_X0, A64_W));
    EXPECT_STREQ("x30", a64_reg_name(A64_LR, A64_X));
    EXPECT_STREQ("sp", a64_reg_name(A64_SP, A64_X));
    EXPECT_STREQ("wsp", a64_reg_name(A64_SP, A64_W));
    EXPECT_STREQ("xzr", a64_reg_name(A64_ZR, A64_X));
    EXPECT_STREQ("wzr", a64_reg_name(A64_ZR, A64_W));
    EXPECT_STREQ("s0", a64_reg_name(A64_V0, A64_S));
    EXPECT_STREQ("d31", a64_reg_name(A64_V0 + 31, A64_D));
    EXPECT_STREQ("q7", a64_reg_name(A64_V0 + 7, A64_Q));
    EXPECT_EQ(nullptr, a64_reg_name(A64_X0, A64_D)); // the wrong register file
    EXPECT_EQ(nullptr, a64_reg_name(A64_V0, A64_X));
    EXPECT_EQ(nullptr, a64_reg_name(A64_VREG, A64_X)); // virtual
}

TEST_F(EmitTest, RegistersAndImmediates)
{
    EXPECT_EQ("    mov     w0, #2\n", Line(A64_MOV, { a64_reg(A64_X0, A64_W), a64_imm(2) }));
    EXPECT_EQ("    mov     x1, #-5\n", Line(A64_MOV, { a64_reg(1, A64_X), a64_imm(-5) }));
    EXPECT_EQ("    movk    x0, #4660, lsl #48\n",
              Line(A64_MOVK, { a64_reg(A64_X0, A64_X), a64_imm(4660), a64_lsl(48) }));
    EXPECT_EQ("    mov     %x3, %w0\n",
              Line(A64_MOV, { a64_reg(A64_VREG + 3, A64_X), a64_reg(A64_VREG, A64_W) }));
    EXPECT_EQ("    ret\n", Line(A64_RET, {}));
}

TEST_F(EmitTest, MemoryOperands)
{
    EXPECT_EQ("    mov     x0, [x1]\n", Line(A64_MOV, { a64_reg(0, A64_X), a64_mem(1, 0) }));
    EXPECT_EQ("    mov     x0, [sp, #16]\n",
              Line(A64_MOV, { a64_reg(0, A64_X), a64_mem(A64_SP, 16) }));
    EXPECT_EQ("    mov     x0, [x29, #-8]\n",
              Line(A64_MOV, { a64_reg(0, A64_X), a64_mem(A64_FP, -8) }));
    EXPECT_EQ("    mov     x29, [sp, #-32]!\n",
              Line(A64_MOV, { a64_reg(A64_FP, A64_X), a64_mem_pre(A64_SP, -32) }));
    EXPECT_EQ("    mov     x29, [sp], #32\n",
              Line(A64_MOV, { a64_reg(A64_FP, A64_X), a64_mem_post(A64_SP, 32) }));
}

TEST_F(EmitTest, SymbolsShiftsExtendsConditions)
{
    EXPECT_EQ("    mov     x0, counter\n",
              Line(A64_MOV, { a64_reg(0, A64_X), a64_sym("counter", 0) }));
    EXPECT_EQ("    mov     x0, x0, :lo12:counter+8\n",
              Line(A64_MOV, { a64_reg(0, A64_X), a64_reg(0, A64_X), a64_lo12("counter", 8) }));
    EXPECT_EQ("    mov     x0, x1, lsl #3\n",
              Line(A64_MOV, { a64_reg(0, A64_X), a64_shift(1, A64_X, A64_SHIFT_LSL, 3) }));
    EXPECT_EQ("    mov     w0, w1, asr #31\n",
              Line(A64_MOV, { a64_reg(0, A64_W), a64_shift(1, A64_W, A64_SHIFT_ASR, 31) }));
    EXPECT_EQ("    mov     x0, w1, sxtw\n",
              Line(A64_MOV, { a64_reg(0, A64_X), a64_ext(1, A64_W, A64_EXT_SXTW, 0) }));
    EXPECT_EQ("    mov     x0, w1, uxtw #2\n",
              Line(A64_MOV, { a64_reg(0, A64_X), a64_ext(1, A64_W, A64_EXT_UXTW, 2) }));
    EXPECT_EQ("    mov     w0, lt\n", Line(A64_MOV, { a64_reg(0, A64_W), a64_cond(A64_LT) }));
}

TEST_F(EmitTest, Function)
{
    A64_Func *fn  = a64_new_func("f", true);
    A64_Instr *in = a64_append(fn, A64_MOV);
    in->opnd[0]   = a64_reg(A64_X0, A64_W);
    in->opnd[1]   = a64_imm(0);
    a64_new_block(fn, ".L1");
    a64_append(fn, A64_RET);
    std::string s = Capture([&](FILE *f) { a64_emit_func(f, fn); });
    a64_free_func(fn);
    EXPECT_EQ(
        "    .text\n"
        "    .globl  f\n"
        "    .p2align 2\n"
        "    .type   f, @function\n"
        "f:\n"
        "    mov     w0, #0\n"
        ".L1:\n"
        "    ret\n"
        "    .size   f, .-f\n",
        s);
}
