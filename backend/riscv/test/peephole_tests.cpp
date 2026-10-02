//
// Peephole pass over the RISC-V IR (peephole.c), on hand-built code.
//
#include <gtest/gtest.h>

#include <cstdio>
#include <initializer_list>
#include <string>

extern "C" {
#include "rv.h"
#include "xalloc.h"
}

class PeepholeTest : public ::testing::Test {
protected:
    Rv_Func *fn = rv_new_func("f", false);

    void TearDown() override
    {
        rv_free_func(fn);
        xreport_lost_memory();
        EXPECT_EQ(xtotal_allocated_size(), 0);
        xfree_all();
    }

    void I(Rv_Op op, std::initializer_list<Rv_Operand> opnds)
    {
        Rv_Instr *in = rv_append(fn, op);
        int i        = 0;
        for (const Rv_Operand &o : opnds)
            in->opnd[i++] = o;
    }

    void Label(const char *l) { rv_new_block(fn, l); }

    // The code after the pass: one line per instruction or label, unindented.
    std::string Run()
    {
        rv_peephole(fn);
        FILE *f = tmpfile();
        rv_emit_func(f, fn);
        long len = ftell(f);
        rewind(f);
        std::string s(static_cast<size_t>(len), '\0');
        if (len > 0)
            EXPECT_EQ(1u, fread(&s[0], static_cast<size_t>(len), 1, f));
        fclose(f);
        std::string out;
        for (size_t pos = 0; pos < s.size();) {
            size_t nl        = s.find('\n', pos);
            std::string line = s.substr(pos, nl - pos);
            pos              = nl + 1;
            if (line.compare(0, 5, "    .") == 0 || line == "f:")
                continue;
            line      = line.substr(line.find_first_not_of(' '));
            size_t sp = line.find(' ');
            if (sp != std::string::npos)
                line = line.substr(0, sp + 1) + line.substr(line.find_first_not_of(' ', sp));
            out += line + "\n";
        }
        return out;
    }
};

static Rv_Operand R(int r)
{
    return rv_reg(r);
}

static const int S1 = RV_S1, S2 = RV_S2, T0 = RV_T0, T1 = RV_T1, A0 = RV_A0;

// An operation on a just-loaded constant takes the immediate form; a subtraction adds
// the negation; a commutative operation swaps its operands first.
TEST_F(PeepholeTest, ImmediateForms)
{
    I(RV_LI, { R(T1), rv_imm(5) });
    I(RV_ADDW, { R(S1), R(S1), R(T1) });
    I(RV_LI, { R(T1), rv_imm(48) });
    I(RV_SUB, { R(S1), R(S2), R(T1) });
    I(RV_LI, { R(T0), rv_imm(7) });
    I(RV_AND, { R(S1), R(T0), R(S2) });
    I(RV_LI, { R(T1), rv_imm(40) });
    I(RV_SLLW, { R(S1), R(S1), R(T1) });
    I(RV_LI, { R(T1), rv_imm(3) });
    I(RV_SLTU, { R(S1), R(S2), R(T1) });
    EXPECT_EQ(R"(addiw s1, s1, 5
addi s1, s2, -48
andi s1, s2, 7
slliw s1, s1, 8
sltiu s1, s2, 3
)",
              Run());
}

// No immediate form: a constant out of range, a constant as the subtrahend, or one
// read again later.
TEST_F(PeepholeTest, ImmediateKept)
{
    I(RV_LI, { R(T1), rv_imm(5000) });
    I(RV_ADD, { R(S1), R(S1), R(T1) });
    I(RV_LI, { R(T1), rv_imm(5) });
    I(RV_SLT, { R(S1), R(T1), R(S2) });
    I(RV_LI, { R(T1), rv_imm(2) });
    I(RV_ADD, { R(S1), R(S1), R(T1) });
    I(RV_ADD, { R(S2), R(S2), R(T1) });
    EXPECT_EQ(R"(li t1, 5000
add s1, s1, t1
li t1, 5
slt s1, t1, s2
li t1, 2
add s1, s1, t1
add s2, s2, t1
)",
              Run());
}

// Zero becomes the zero register; an operation with zero, a move to itself.
TEST_F(PeepholeTest, Zero)
{
    I(RV_LI, { R(T0), rv_imm(0) });
    I(RV_SD, { R(T0), rv_mem(S1, 8) });
    I(RV_LI, { R(T1), rv_imm(0) });
    I(RV_XOR, { R(S1), R(S1), R(T1) });
    I(RV_LI, { R(RV_T6), rv_imm(0) });
    I(RV_FMVDX, { R(RV_F0 + 1), R(RV_T6) });
    EXPECT_EQ(R"(sd zero, 8(s1)
fmv.d.x ft1, zero
)",
              Run());
}

// A scratch copy is read from its source; a value in a callee-saved register is not
// scratch.
TEST_F(PeepholeTest, ScratchMove)
{
    I(RV_MV, { R(T0), R(S1) });
    I(RV_SD, { R(T0), rv_mem(RV_SP, 0) });
    I(RV_MV, { R(RV_T3), R(S2) });
    I(RV_LD, { R(T0), rv_mem(RV_T3, 0) });
    I(RV_MV, { R(A0), R(T0) });
    I(RV_MV, { R(S2), R(S1) });
    I(RV_ADD, { R(A0), R(S2), R(S2) });
    EXPECT_EQ(R"(sd s1, 0(sp)
ld a0, 0(s2)
mv s2, s1
add a0, s2, s2
)",
              Run());
}

// The copies around an rv32 register-pair add: the operands are read where they are,
// and the halves are computed where they go.
TEST_F(PeepholeTest, PairMoves)
{
    I(RV_MV, { R(T0), R(S1) });
    I(RV_MV, { R(T1), R(S2) });
    I(RV_MV, { R(RV_T2), R((RV_S2 + 1)) });
    I(RV_MV, { R(RV_T3), R((RV_S2 + 2)) });
    I(RV_ADD, { R(RV_T2), R(T0), R(RV_T2) });
    I(RV_SLTU, { R(T0), R(RV_T2), R(T0) });
    I(RV_ADD, { R(T1), R(T1), R(RV_T3) });
    I(RV_ADD, { R(T1), R(T1), R(T0) });
    I(RV_MV, { R((RV_S2 + 3)), R(RV_T2) });
    I(RV_MV, { R((RV_S2 + 4)), R(T1) });
    EXPECT_EQ(R"(add s5, s1, s3
sltu t0, s5, s1
add t1, s2, s4
add s6, t1, t0
)",
              Run());
}

// A move straight back goes; so does a move into an argument register the return does
// not read, and one computed into a0 only to be moved, as a0 is written again.  a1 is
// returned, so its move stays.
TEST_F(PeepholeTest, ArgumentMoves)
{
    I(RV_MV, { R((RV_A0 + 2)), R(S1) });
    I(RV_MV, { R(S1), R((RV_A0 + 2)) });
    I(RV_ADD, { R(A0), R(S1), R(S2) });
    I(RV_MV, { R((RV_A0 + 1)), R(A0) });
    I(RV_MV, { R(A0), R(S2) });
    I(RV_RET, {});
    EXPECT_EQ(R"(add a1, s1, s2
mv a0, s2
ret
)",
              Run());
}

// A register other than scratch and arguments may be read after a branch.
TEST_F(PeepholeTest, MoveBeforeBranch)
{
    I(RV_MV, { R(S1), R(S2) });
    I(RV_BNEZ, { R(A0), rv_sym(".L1", 0) });
    I(RV_MV, { R(S1), R(A0) });
    Label(".L1");
    I(RV_RET, {});
    EXPECT_EQ(R"(mv s1, s2
bnez a0, .L1
mv s1, a0
.L1:
ret
)",
              Run());
}

// A doubleword reload of what was just stored goes; a byte load needs no mask.
TEST_F(PeepholeTest, ReloadAndMask)
{
    I(RV_SD, { R(S1), rv_mem(RV_S0, -40) });
    I(RV_LD, { R(S1), rv_mem(RV_S0, -40) });
    I(RV_SW, { R(S1), rv_mem(RV_S0, -44) });
    I(RV_LW, { R(S1), rv_mem(RV_S0, -44) });
    I(RV_LBU, { R(S2), rv_mem(S1, 0) });
    I(RV_ANDI, { R(S2), R(S2), rv_imm(255) });
    EXPECT_EQ(R"(sd s1, -40(s0)
sw s1, -44(s0)
lw s1, -44(s0)
lbu s2, 0(s1)
)",
              Run());
}

// A frame slot's reload goes past other code, unless the register, the slot, or
// memory through another base may have changed.
TEST_F(PeepholeTest, ReloadLater)
{
    I(RV_SD, { R(A0), rv_mem(RV_SP, 16) });
    I(RV_SD, { R(A0 + 1), rv_mem(RV_SP, 24) });
    I(RV_LD, { R(A0), rv_mem(RV_SP, 16) });
    I(RV_LD, { R(A0 + 1), rv_mem(RV_SP, 24) });
    I(RV_SD, { R(S1), rv_mem(RV_SP, 0) });
    I(RV_SD, { R(S2), rv_mem(RV_SP, 4) });
    I(RV_LD, { R(S1), rv_mem(RV_SP, 0) });
    I(RV_SD, { R(S2), rv_mem(RV_SP, 8) });
    I(RV_SD, { R(A0), rv_mem(S1, 0) });
    I(RV_LD, { R(S2), rv_mem(RV_SP, 8) });
    EXPECT_EQ(R"(sd a0, 16(sp)
sd a1, 24(sp)
sd s1, 0(sp)
sd s2, 4(sp)
ld s1, 0(sp)
sd s2, 8(sp)
sd a0, 0(s1)
ld s2, 8(sp)
)",
              Run());
}

// A branch over a jump turns around; a jump to the next label, through empty blocks,
// goes; so does code after a jump.
TEST_F(PeepholeTest, Branches)
{
    I(RV_BEQZ, { R(S1), rv_sym(".L1", 0) });
    I(RV_J, { rv_sym(".L2", 0) });
    Label(".L1");
    I(RV_ADDI, { R(S1), R(S1), rv_imm(1) });
    I(RV_J, { rv_sym(".L3", 0) });
    I(RV_ADDI, { R(S1), R(S1), rv_imm(2) });
    Label(".L4");
    Label(".L3");
    I(RV_RET, {});
    Label(".L2");
    I(RV_MV, { R(A0), R(S1) });
    I(RV_RET, {});
    EXPECT_EQ(R"(bnez s1, .L2
.L1:
addi s1, s1, 1
.L4:
.L3:
ret
.L2:
mv a0, s1
ret
)",
              Run());
}
