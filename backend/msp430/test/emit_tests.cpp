//
// MSP430 IR emitter: operand syntax, instruction sizes and the function layout.
//
#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "test_tools.h"

extern "C" {
#include "msp_ir.h"
#include "xalloc.h"
}

// The libraries call fatal_error(); defined once for the msp430-tests binary.
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

static Msp_Instr Make(Msp_Op op, std::initializer_list<Msp_Operand> opnds, bool byte = false)
{
    Msp_Instr in{};
    in.op   = op;
    in.byte = byte;
    int i   = 0;
    for (const Msp_Operand &o : opnds)
        in.opnd[i++] = o;
    return in;
}

static void Free(Msp_Instr &in)
{
    for (auto &o : in.opnd)
        xfree(o.sym);
}

// One instruction line.
static std::string Line(Msp_Op op, std::initializer_list<Msp_Operand> opnds, bool byte = false)
{
    Msp_Instr in  = Make(op, opnds, byte);
    std::string s = Capture([&](FILE *f) { msp_emit_instr(f, &in); });
    Free(in);
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

TEST_F(EmitTest, Registers)
{
    EXPECT_EQ(Line(MSP_MOV, { msp_reg(12), msp_reg(13) }), "    mov     r12, r13\n");
    EXPECT_EQ(Line(MSP_PUSH, { msp_reg(10) }), "    push    r10\n");
    EXPECT_EQ(Line(MSP_SXT, { msp_reg(12) }), "    sxt     r12\n");
    EXPECT_EQ(Line(MSP_ADD, { msp_reg(MSP_VREG + 3), msp_reg(12) }), "    add     v3, r12\n");
}

TEST_F(EmitTest, Memory)
{
    EXPECT_EQ(Line(MSP_MOV, { msp_indexed(MSP_SP, nullptr, 4), msp_indexed(MSP_SP, nullptr, 0) }),
              "    mov     4(r1), 0(r1)\n");
    EXPECT_EQ(Line(MSP_MOV, { msp_indexed(12, "data", 2), msp_reg(13) }, true),
              "    mov.b   data+2(r12), r13\n");
    EXPECT_EQ(Line(MSP_ADD, { msp_abs("g", 0), msp_abs("h", -2) }), "    add     &g, &h-2\n");
    EXPECT_EQ(Line(MSP_MOV, { msp_reg(12), msp_abs(nullptr, 0x1fe) }), "    mov     r12, &510\n");
    EXPECT_EQ(Line(MSP_MOV, { msp_ind(14), msp_reg(12) }), "    mov     @r14, r12\n");
    EXPECT_EQ(Line(MSP_MOV, { msp_postinc(14), msp_reg(12) }, true), "    mov.b   @r14+, r12\n");
}

// An immediate prints sign-normalized to the operation's width, so all ones is -1.
TEST_F(EmitTest, Immediates)
{
    EXPECT_EQ(Line(MSP_MOV, { msp_imm(200), msp_reg(12) }), "    mov     #200, r12\n");
    EXPECT_EQ(Line(MSP_MOV, { msp_imm(0xffff), msp_reg(12) }), "    mov     #-1, r12\n");
    EXPECT_EQ(Line(MSP_MOV, { msp_imm(0x8000), msp_reg(12) }), "    mov     #-32768, r12\n");
    EXPECT_EQ(Line(MSP_AND, { msp_imm(0xff), msp_reg(12) }, true), "    and.b   #-1, r12\n");
    EXPECT_EQ(Line(MSP_MOV, { msp_imm_sym("g", 4), msp_reg(12) }), "    mov     #g+4, r12\n");
}

TEST_F(EmitTest, Control)
{
    EXPECT_EQ(Line(MSP_JNE, { msp_label(".L3") }), "    jne     .L3\n");
    EXPECT_EQ(Line(MSP_CALL, { msp_imm_sym("__mspabi_mpyi", 0) }),
              "    call    #__mspabi_mpyi\n");
    EXPECT_EQ(Line(MSP_CALL, { msp_reg(11) }), "    call    r11\n");
    EXPECT_EQ(Line(MSP_BR, { msp_imm_sym("f", 0) }), "    br      #f\n");
    EXPECT_EQ(Line(MSP_RLA, { msp_indexed(MSP_SP, nullptr, 2) }), "    rla     2(r1)\n");
    EXPECT_EQ(Line(MSP_RET, {}), "    ret\n");
}

TEST_F(EmitTest, Function)
{
    Msp_Func *fn  = msp_new_func("main", true);
    Msp_Instr *in = msp_append(fn, MSP_MOV);
    in->opnd[0]   = msp_imm(200);
    in->opnd[1]   = msp_reg(12);
    msp_new_block(fn, ".L1");
    msp_append(fn, MSP_RET);
    std::string s = Capture([&](FILE *f) { msp_emit_func(f, fn); });
    msp_free_func(fn);
    EXPECT_EQ(s, R"(    .text
    .globl  main
    .p2align 1
    .type   main, @function
main:
    mov     #200, r12
.L1:
    ret
    .size   main, .-main
)");
}

// Every operand form and emulated instruction that clang's assembler accepts.  It
// rejects some the ISA has: @rN+ as a source with a non-register destination, `push`
// of anything but a register or an immediate, `pop` to memory, and `br @rN`.
static std::vector<Msp_Instr> SizeCases()
{
    const int sp = MSP_SP;
    return {
        Make(MSP_MOV, { msp_reg(12), msp_reg(13) }),
        Make(MSP_MOV, { msp_indexed(sp, nullptr, 4), msp_reg(13) }),
        Make(MSP_MOV, { msp_indexed(12, nullptr, 0), msp_reg(13) }),
        Make(MSP_MOV, { msp_indexed(12, "g", 2), msp_reg(13) }),
        Make(MSP_MOV, { msp_abs("g", 0), msp_reg(13) }),
        Make(MSP_MOV, { msp_ind(12), msp_reg(13) }),
        Make(MSP_MOV, { msp_postinc(12), msp_reg(13) }),
        Make(MSP_MOV, { msp_reg(12), msp_indexed(sp, nullptr, 2) }),
        Make(MSP_MOV, { msp_reg(12), msp_abs("g", 0) }),
        Make(MSP_MOV, { msp_reg(12), msp_abs(nullptr, 0x1fe) }),
        Make(MSP_MOV, { msp_indexed(sp, nullptr, 4), msp_indexed(sp, nullptr, 6) }),
        Make(MSP_MOV, { msp_abs("g", 0), msp_abs("g", 2) }),
        Make(MSP_ADD, { msp_ind(12), msp_abs("g", 0) }),
        Make(MSP_MOV, { msp_imm(0), msp_reg(12) }),
        Make(MSP_MOV, { msp_imm(1), msp_reg(12) }),
        Make(MSP_MOV, { msp_imm(2), msp_reg(12) }),
        Make(MSP_MOV, { msp_imm(4), msp_reg(12) }),
        Make(MSP_MOV, { msp_imm(8), msp_reg(12) }),
        Make(MSP_MOV, { msp_imm(-1), msp_reg(12) }),
        Make(MSP_MOV, { msp_imm(0xffff), msp_reg(12) }),
        Make(MSP_MOV, { msp_imm(3), msp_reg(12) }),
        Make(MSP_MOV, { msp_imm(16), msp_reg(12) }),
        Make(MSP_MOV, { msp_imm(0xff), msp_reg(12) }, true),
        Make(MSP_MOV, { msp_imm(0xff), msp_reg(12) }),
        Make(MSP_MOV, { msp_imm(8), msp_reg(12) }, true),
        Make(MSP_MOV, { msp_imm(7), msp_indexed(sp, nullptr, 2) }),
        Make(MSP_MOV, { msp_imm(1), msp_abs("g", 0) }),
        Make(MSP_MOV, { msp_imm_sym("g", 0), msp_reg(12) }),
        Make(MSP_CMP, { msp_imm(-1), msp_indexed(sp, nullptr, 2) }, true),
        Make(MSP_RRA, { msp_reg(12) }),
        Make(MSP_RRC, { msp_indexed(sp, nullptr, 2) }, true),
        Make(MSP_SWPB, { msp_abs("g", 0) }),
        Make(MSP_PUSH, { msp_imm(8) }),
        Make(MSP_PUSH, { msp_imm(5) }),
        Make(MSP_CALL, { msp_imm_sym("g", 0) }),
        Make(MSP_CALL, { msp_reg(12) }),
        Make(MSP_CALL, { msp_indexed(sp, nullptr, 2) }),
        Make(MSP_CLR, { msp_reg(12) }),
        Make(MSP_CLR, { msp_indexed(sp, nullptr, 2) }),
        Make(MSP_INC, { msp_abs("g", 0) }),
        Make(MSP_INCD, { msp_reg(12) }),
        Make(MSP_DEC, { msp_indexed(sp, nullptr, 2) }),
        Make(MSP_DECD, { msp_reg(12) }),
        Make(MSP_TST, { msp_abs("g", 0) }, true),
        Make(MSP_INV, { msp_indexed(sp, nullptr, 4) }),
        Make(MSP_ADC, { msp_reg(13) }),
        Make(MSP_POP, { msp_reg(10) }),
        Make(MSP_RLA, { msp_reg(12) }),
        Make(MSP_RLA, { msp_indexed(sp, nullptr, 2) }),
        Make(MSP_RLC, { msp_abs("g", 0) }),
        Make(MSP_BR, { msp_imm_sym("g", 0) }),
        Make(MSP_BR, { msp_reg(12) }),
        Make(MSP_RET, {}),
        Make(MSP_NOP, {}),
        Make(MSP_CLRC, {}),
        Make(MSP_SETC, {}),
        Make(MSP_JNE, { msp_label("g") }),
        Make(MSP_JMP, { msp_label("g") }),
    };
}

// The model's sizes against clang's assembler: each case is assembled between two
// labels, and a data table of their differences is extracted from the object.
TEST_F(EmitTest, SizesAgreeWithAssembler)
{
    if (!MSP430_TOOLS_FOUND || !tool_available(MSP430_CLANG))
        GTEST_SKIP() << "MSP430 clang not found";
    std::vector<std::string> lines;
    std::vector<int> model;
    for (Msp_Instr &in : SizeCases()) {
        lines.push_back(Capture([&](FILE *f) { msp_emit_instr(f, &in); }));
        model.push_back(msp_instr_size(&in));
        Free(in);
    }
    std::string s_path           = TEST_DIR "/EmitTest.Sizes.s";
    std::string o_path           = TEST_DIR "/EmitTest.Sizes.o";
    std::string bin_path         = TEST_DIR "/EmitTest.Sizes.bin";
    std::string log_path         = TEST_DIR "/EmitTest.Sizes.log";
    {
        std::ofstream s(s_path);
        s << "    .text\n";
        for (size_t i = 0; i < lines.size(); i++)
            s << ".Ls" << i << ":\n" << lines[i];
        s << ".Ls" << lines.size() << ":\n";
        s << "g:  .short 0\n";
        s << "    .section .sizes, \"a\", @progbits\n";
        for (size_t i = 0; i < lines.size(); i++)
            s << "    .byte .Ls" << i + 1 << " - .Ls" << i << "\n";
    }
    std::string clang = MSP430_CLANG;
    std::string objcopy =
        clang.substr(0, clang.find_last_of('/') + 1) + "llvm-objcopy"; // beside clang
    ASSERT_EQ(0, RunTool({ clang, "--target=msp430", "-c", "-o", o_path, s_path }, log_path))
        << ReadFile(log_path);
    ASSERT_EQ(0, RunTool({ objcopy, "-O", "binary", "--only-section=.sizes", o_path, bin_path },
                         log_path))
        << ReadFile(log_path);
    std::string sizes = ReadFile(bin_path);
    ASSERT_EQ(sizes.size(), lines.size());
    for (size_t i = 0; i < lines.size(); i++)
        EXPECT_EQ(model[i], (unsigned char)sizes[i]) << lines[i];
}
