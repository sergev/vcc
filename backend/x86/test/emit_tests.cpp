//
// x86-64 IR emitter: register names, operand syntax and instruction suffixes.
//
#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" {
#include "x86.h"
#include "xalloc.h"
}

// The libraries call fatal_error(); defined once for the x86-tests binary.
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
static std::string Line(X86_Op op, X86_Width width, std::initializer_list<X86_Operand> opnds)
{
    X86_Instr in{};
    in.op    = op;
    in.width = width;
    int i    = 0;
    for (const X86_Operand &o : opnds)
        in.opnd[i++] = o;
    std::string s = Capture([&](FILE *f) { x86_emit_instr(f, &in); });
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

// Every width of every general register.  %spl, %bpl, %sil and %dil need a REX
// prefix, which is why %ah..%bh are never named.
TEST_F(EmitTest, RegisterNames)
{
    EXPECT_STREQ("al", x86_reg_name(X86_RAX, X86_B));
    EXPECT_STREQ("ax", x86_reg_name(X86_RAX, X86_W));
    EXPECT_STREQ("eax", x86_reg_name(X86_RAX, X86_L));
    EXPECT_STREQ("rax", x86_reg_name(X86_RAX, X86_Q));
    EXPECT_STREQ("cl", x86_reg_name(X86_RCX, X86_B));
    EXPECT_STREQ("dx", x86_reg_name(X86_RDX, X86_W));
    EXPECT_STREQ("ebx", x86_reg_name(X86_RBX, X86_L));
    EXPECT_STREQ("spl", x86_reg_name(X86_RSP, X86_B));
    EXPECT_STREQ("rsp", x86_reg_name(X86_RSP, X86_Q));
    EXPECT_STREQ("bpl", x86_reg_name(X86_RBP, X86_B));
    EXPECT_STREQ("bp", x86_reg_name(X86_RBP, X86_W));
    EXPECT_STREQ("sil", x86_reg_name(X86_RSI, X86_B));
    EXPECT_STREQ("esi", x86_reg_name(X86_RSI, X86_L));
    EXPECT_STREQ("dil", x86_reg_name(X86_RDI, X86_B));
    EXPECT_STREQ("rdi", x86_reg_name(X86_RDI, X86_Q));
    EXPECT_STREQ("r8b", x86_reg_name(X86_R8, X86_B));
    EXPECT_STREQ("r8w", x86_reg_name(X86_R8, X86_W));
    EXPECT_STREQ("r8d", x86_reg_name(X86_R8, X86_L));
    EXPECT_STREQ("r8", x86_reg_name(X86_R8, X86_Q));
    EXPECT_STREQ("r15b", x86_reg_name(X86_R15, X86_B));
    EXPECT_STREQ("r15", x86_reg_name(X86_R15, X86_Q));
    EXPECT_STREQ("xmm0", x86_reg_name(X86_XMM0, X86_Q));
    EXPECT_STREQ("xmm15", x86_reg_name(X86_XMM0 + 15, X86_L)); // width does not matter
    EXPECT_STREQ("st", x86_reg_name(X86_ST0, X86_Q));
    EXPECT_STREQ("st(1)", x86_reg_name(X86_ST0 + 1, X86_Q));
    EXPECT_STREQ("st(7)", x86_reg_name(X86_ST0 + 7, X86_Q));
    EXPECT_EQ(nullptr, x86_reg_name(X86_VREG, X86_Q)); // virtual
    EXPECT_EQ(nullptr, x86_reg_name(-1, X86_Q));
}

TEST_F(EmitTest, Imm32)
{
    EXPECT_TRUE(x86_imm32(0));
    EXPECT_TRUE(x86_imm32(INT32_MAX));
    EXPECT_TRUE(x86_imm32(INT32_MIN));
    EXPECT_FALSE(x86_imm32(int64_t(INT32_MAX) + 1));
    EXPECT_FALSE(x86_imm32(int64_t(INT32_MIN) - 1));
    EXPECT_FALSE(x86_imm32(int64_t(UINT32_MAX)));
}

// The suffix follows the instruction's width; an unsuffixed mnemonic stands alone.
TEST_F(EmitTest, Suffixes)
{
    EXPECT_EQ("    movb    $1, %al\n", Line(X86_MOV, X86_B, { x86_imm(1), x86_reg(X86_RAX, X86_B) }));
    EXPECT_EQ("    movw    $1, %ax\n", Line(X86_MOV, X86_W, { x86_imm(1), x86_reg(X86_RAX, X86_W) }));
    EXPECT_EQ("    movl    $-1, %eax\n",
              Line(X86_MOV, X86_L, { x86_imm(-1), x86_reg(X86_RAX, X86_L) }));
    EXPECT_EQ("    movq    %rsp, %rbp\n",
              Line(X86_MOV, X86_Q, { x86_reg(X86_RSP, X86_Q), x86_reg(X86_RBP, X86_Q) }));
    EXPECT_EQ("    movabsq $81985529216486895, %r11\n",
              Line(X86_MOVABS, X86_Q, { x86_imm(0x123456789abcdefLL), x86_reg(X86_R11, X86_Q) }));
    EXPECT_EQ("    ret\n", Line(X86_RET, X86_Q, {}));
}

TEST_F(EmitTest, MemoryOperands)
{
    X86_Operand eax = x86_reg(X86_RAX, X86_L);
    EXPECT_EQ("    movl    -8(%rbp), %eax\n", Line(X86_MOV, X86_L, { x86_mem(X86_RBP, -8), eax }));
    EXPECT_EQ("    movl    (%rdi), %eax\n", Line(X86_MOV, X86_L, { x86_mem(X86_RDI, 0), eax }));
    EXPECT_EQ("    movl    %eax, 16(%rsp)\n", Line(X86_MOV, X86_L, { eax, x86_mem(X86_RSP, 16) }));
    EXPECT_EQ("    movl    12(%rdi,%rsi,4), %eax\n",
              Line(X86_MOV, X86_L, { x86_mem_index(X86_RDI, X86_RSI, 4, 12), eax }));
    EXPECT_EQ("    movl    (%r8,%r9,8), %eax\n",
              Line(X86_MOV, X86_L, { x86_mem_index(X86_R8, X86_R9, 8, 0), eax }));
    EXPECT_EQ("    movl    8(,%rcx,8), %eax\n",
              Line(X86_MOV, X86_L, { x86_mem_index(-1, X86_RCX, 8, 8), eax }));
    EXPECT_EQ("    movl    4096, %eax\n", Line(X86_MOV, X86_L, { x86_mem(-1, 4096), eax }));
}

TEST_F(EmitTest, SymbolOperands)
{
    X86_Operand rax = x86_reg(X86_RAX, X86_Q);
    EXPECT_EQ("    movq    counter(%rip), %rax\n", Line(X86_MOV, X86_Q, { x86_rip("counter", 0), rax }));
    EXPECT_EQ("    movq    table+8(%rip), %rax\n", Line(X86_MOV, X86_Q, { x86_rip("table", 8), rax }));
    EXPECT_EQ("    movq    table-8(%rip), %rax\n", Line(X86_MOV, X86_Q, { x86_rip("table", -8), rax }));
    EXPECT_EQ("    movq    .L3, %rax\n", Line(X86_MOV, X86_Q, { x86_label(".L3"), rax }));
}

TEST_F(EmitTest, VirtualRegisters)
{
    EXPECT_EQ("    movl    %v0l, %v12l\n",
              Line(X86_MOV, X86_L, { x86_reg(X86_VREG, X86_L), x86_reg(X86_VREG + 12, X86_L) }));
}

TEST_F(EmitTest, Function)
{
    X86_Func *fn  = x86_new_func("main", true);
    X86_Instr *in = x86_append(fn, X86_MOV, X86_L);
    in->opnd[0]   = x86_imm(2);
    in->opnd[1]   = x86_reg(X86_RAX, X86_L);
    x86_new_block(fn, ".L1");
    x86_append(fn, X86_RET, X86_Q);
    std::string s = Capture([&](FILE *f) { x86_emit_func(f, fn); });
    x86_free_func(fn);
    EXPECT_EQ(R"(    .text
    .globl  main
    .p2align 4
    .type   main, @function
main:
    movl    $2, %eax
.L1:
    ret
    .size   main, .-main
)",
              s);
}
