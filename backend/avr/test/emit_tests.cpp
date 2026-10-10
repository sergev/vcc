//
// AVR IR emitter: operand syntax, instruction sizes and the function layout.
//
#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" {
#include "avr_ir.h"
#include "xalloc.h"
}

// The libraries call fatal_error(); defined once for the avr-tests binary.
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

// One instruction line.
static std::string Line(AVR_Op op, std::initializer_list<AVR_Operand> opnds)
{
    AVR_Instr in{};
    in.op = op;
    int i = 0;
    for (const AVR_Operand &o : opnds)
        in.opnd[i++] = o;
    std::string s = Capture([&](FILE *f) { avr_emit_instr(f, &in); });
    for (int k = 0; k < i; k++)
        xfree(in.opnd[k].sym);
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
    EXPECT_EQ(Line(AVR_MOV, { avr_reg(24), avr_reg(0) }), "    mov     r24, r0\n");
    EXPECT_EQ(Line(AVR_MOVW, { avr_reg(30), avr_reg(28) }), "    movw    r30, r28\n");
    EXPECT_EQ(Line(AVR_CLR, { avr_reg(1) }), "    clr     r1\n");
    EXPECT_EQ(Line(AVR_PUSH, { avr_reg(17) }), "    push    r17\n");
}

TEST_F(EmitTest, Immediates)
{
    EXPECT_EQ(Line(AVR_LDI, { avr_reg(24), avr_imm(200) }), "    ldi     r24, 200\n");
    EXPECT_EQ(Line(AVR_ADIW, { avr_reg(28), avr_imm(63) }), "    adiw    r28, 63\n");
    EXPECT_EQ(Line(AVR_IN, { avr_reg(28), avr_imm(61) }), "    in      r28, 61\n");
    EXPECT_EQ(Line(AVR_SBRS, { avr_reg(25), avr_imm(7) }), "    sbrs    r25, 7\n");
}

// The halves of an address: lo8/hi8/hh8 of a data address, pm_lo8/pm_hi8 of a code
// (word) address; plain for lds/sts.
TEST_F(EmitTest, Symbols)
{
    EXPECT_EQ(Line(AVR_LDI, { avr_reg(24), avr_sym(AVR_MOD_LO8, "g", 0) }),
              "    ldi     r24, lo8(g)\n");
    EXPECT_EQ(Line(AVR_LDI, { avr_reg(25), avr_sym(AVR_MOD_HI8, "g", 3) }),
              "    ldi     r25, hi8(g+3)\n");
    EXPECT_EQ(Line(AVR_LDI, { avr_reg(16), avr_sym(AVR_MOD_HH8, "g", 0) }),
              "    ldi     r16, hh8(g)\n");
    EXPECT_EQ(Line(AVR_LDI, { avr_reg(24), avr_sym(AVR_MOD_PM_LO8, "f", 0) }),
              "    ldi     r24, pm_lo8(f)\n");
    EXPECT_EQ(Line(AVR_LDI, { avr_reg(25), avr_sym(AVR_MOD_PM_HI8, "f", 0) }),
              "    ldi     r25, pm_hi8(f)\n");
    EXPECT_EQ(Line(AVR_LDS, { avr_reg(18), avr_sym(AVR_MOD_NONE, "g", 1) }),
              "    lds     r18, g+1\n");
    EXPECT_EQ(Line(AVR_STS, { avr_sym(AVR_MOD_NONE, "g", -2), avr_reg(18) }),
              "    sts     g-2, r18\n");
}

TEST_F(EmitTest, Pointers)
{
    EXPECT_EQ(Line(AVR_LD, { avr_reg(24), avr_ptr(AVR_X, AVR_PTR_PLAIN) }), "    ld      r24, X\n");
    EXPECT_EQ(Line(AVR_LD, { avr_reg(24), avr_ptr(AVR_Z, AVR_PTR_POST_INC) }),
              "    ld      r24, Z+\n");
    EXPECT_EQ(Line(AVR_ST, { avr_ptr(AVR_Y, AVR_PTR_PRE_DEC), avr_reg(0) }),
              "    st      -Y, r0\n");
    EXPECT_EQ(Line(AVR_LDD, { avr_reg(24), avr_disp(AVR_Y, 1) }), "    ldd     r24, Y+1\n");
    EXPECT_EQ(Line(AVR_STD, { avr_disp(AVR_Z, 63), avr_reg(25) }), "    std     Z+63, r25\n");
}

TEST_F(EmitTest, Branches)
{
    EXPECT_EQ(Line(AVR_BRNE, { avr_label(".L3") }), "    brne    .L3\n");
    EXPECT_EQ(Line(AVR_CALL, { avr_label("__divmodhi4") }), "    call    __divmodhi4\n");
    EXPECT_EQ(Line(AVR_RET, {}), "    ret\n");
}

// The sizes the branch relaxation pass computes offsets from.
TEST_F(EmitTest, Sizes)
{
    EXPECT_EQ(avr_size[AVR_LDS], 4);
    EXPECT_EQ(avr_size[AVR_STS], 4);
    EXPECT_EQ(avr_size[AVR_JMP], 4);
    EXPECT_EQ(avr_size[AVR_CALL], 4);
    EXPECT_EQ(avr_size[AVR_RJMP], 2);
    EXPECT_EQ(avr_size[AVR_BREQ], 2);
    EXPECT_EQ(avr_size[AVR_LDD], 2);
}

TEST_F(EmitTest, Function)
{
    AVR_Func *fn  = avr_new_func("main", true);
    AVR_Instr *in = avr_append(fn, AVR_LDI);
    in->opnd[0]   = avr_reg(24);
    in->opnd[1]   = avr_imm(200);
    avr_new_block(fn, ".L1");
    avr_append(fn, AVR_RET);
    std::string s = Capture([&](FILE *f) { avr_emit_func(f, fn); });
    avr_free_func(fn);
    EXPECT_EQ(s, R"(    .text
    .globl  main
    .p2align 1
    .type   main, @function
main:
    ldi     r24, 200
.L1:
    ret
    .size   main, .-main
)");
}

TEST_F(EmitTest, Header)
{
    std::string s = Capture([&](FILE *f) { avr_emit_header(f); });
    EXPECT_NE(s.find("__zero_reg__ = 1\n"), std::string::npos) << s;
    EXPECT_NE(s.find("__SP_L__ = 61\n"), std::string::npos) << s;
}
