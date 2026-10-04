//
// The AVR peephole pass on hand-built IR, rule by rule, and compares fused with their
// branches in selection.
//
#include "avr_test.h"

extern "C" {
#include "internal.h"
}

namespace {

// A function under construction, with its result in r25:r24.
class Fn {
public:
    Fn()
    {
        gen_unit_begin();
        fn         = avr_new_func("f", true);
        fn->result = 3u << 24;
    }
    ~Fn() { avr_free_func(fn); }

    Fn &op(AVR_Op op, AVR_Operand a = {}, AVR_Operand b = {}, bool vol = false)
    {
        AVR_Instr *in = avr_append(fn, op);
        in->opnd[0]   = a;
        in->opnd[1]   = b;
        in->vol       = vol;
        return *this;
    }
    Fn &label(const char *l)
    {
        avr_new_block(fn, l);
        return *this;
    }

    // The instruction lines after the body pass (and the frame pass with `frame`).
    std::string Peephole(bool frame = false)
    {
        avr_peephole_pass(fn);
        if (frame)
            avr_peephole_frame(fn);
        FILE *f = tmpfile();
        avr_emit_func(f, fn);
        long len = ftell(f);
        rewind(f);
        std::string s(static_cast<size_t>(len), '\0');
        if (len > 0)
            EXPECT_EQ(1u, fread(&s[0], static_cast<size_t>(len), 1, f));
        fclose(f);
        std::string out;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t nl        = s.find('\n', pos);
            std::string line = s.substr(pos, nl - pos);
            pos              = nl == std::string::npos ? s.size() : nl + 1;
            if (line.size() > 1 && line[0] == '.' && line.back() == ':') {
                out += line + "\n"; // a label
                continue;
            }
            if (line.compare(0, 4, "    ") != 0 || line[4] == '.')
                continue;
            line      = line.substr(4);
            size_t sp = line.find(' ');
            if (sp != std::string::npos)
                line = line.substr(0, sp + 1) + line.substr(line.find_first_not_of(' ', sp));
            out += line + "\n";
        }
        return out;
    }

    AVR_Func *fn;
};

AVR_Operand r(int n)
{
    return avr_reg(n);
}

AVR_Operand imm(int n)
{
    return avr_imm(n);
}

AVR_Operand y(int q)
{
    return avr_disp(AVR_Y, q);
}

} // namespace

// A reload of a byte just stored is a move.
TEST(AvrPeephole, ReloadIsMove)
{
    Fn f;
    f.op(AVR_STD, y(1), r(22)).op(AVR_LDD, r(24), y(1)).op(AVR_MOV, r(25), r(1)).op(AVR_RET);
    EXPECT_EQ(R"(std Y+1, r22
mov r24, r22
mov r25, r1
ret
)", f.Peephole());
}

// A store of what the slot already holds goes.
TEST(AvrPeephole, StoreOfSameValueGoes)
{
    Fn f;
    f.op(AVR_LDD, r(24), y(1)).op(AVR_INC, r(25)).op(AVR_STD, y(1), r(24)).op(AVR_RET);
    EXPECT_EQ(R"(ldd r24, Y+1
inc r25
ret
)", f.Peephole());
}

// A volatile access stays as it is.
TEST(AvrPeephole, VolatileStays)
{
    Fn f;
    f.op(AVR_STD, y(1), r(22)).op(AVR_LDD, r(24), y(1), true).op(AVR_LDD, r(20), y(2), true);
    f.op(AVR_RET);
    EXPECT_EQ(R"(std Y+1, r22
ldd r24, Y+1
ldd r20, Y+2
ret
)", f.Peephole());
}

// A store through a pointer may write any slot: the next load stays.
TEST(AvrPeephole, PointerStoreForgetsSlots)
{
    Fn f;
    f.op(AVR_STD, y(1), r(22)).op(AVR_ST, avr_ptr(AVR_Z, AVR_PTR_PLAIN), r(23));
    f.op(AVR_LDD, r(24), y(1)).op(AVR_RET);
    EXPECT_EQ(R"(std Y+1, r22
st Z, r23
ldd r24, Y+1
ret
)", f.Peephole());
}

// A copy back of a copy goes, and then the first copy, now dead.
TEST(AvrPeephole, CopyBackGoes)
{
    Fn f;
    f.op(AVR_MOVW, r(20), r(30)).op(AVR_MOVW, r(30), r(20)).op(AVR_LDD, r(24), avr_disp(AVR_Z, 0));
    f.op(AVR_MOV, r(25), r(1)).op(AVR_RET);
    EXPECT_EQ(R"(ldd r24, Z+0
mov r25, r1
ret
)", f.Peephole());
}

// A constant a register already holds is not loaded again.
TEST(AvrPeephole, KnownConstant)
{
    Fn f;
    f.op(AVR_LDI, r(24), imm(5)).op(AVR_MOV, r(25), r(1)).op(AVR_LDI, r(24), imm(5));
    f.op(AVR_LDI, r(25), imm(0)).op(AVR_RET);
    EXPECT_EQ(R"(ldi r24, 5
mov r25, r1
ret
)", f.Peephole());
}

// The flags dead after it: subi/sbci of a small constant on r24 is adiw; with a branch
// reading them it stays.
TEST(AvrPeephole, AdiwWhenFlagsDead)
{
    Fn f;
    f.op(AVR_SUBI, r(24), imm(0xfe)).op(AVR_SBCI, r(25), imm(0xff)).op(AVR_RET);
    EXPECT_EQ(R"(adiw r24, 2
ret
)", f.Peephole());

    Fn g;
    g.op(AVR_SUBI, r(24), imm(3)).op(AVR_SBCI, r(25), imm(0)).op(AVR_RET);
    EXPECT_EQ(R"(sbiw r24, 3
ret
)", g.Peephole());

    Fn h;
    h.op(AVR_SUBI, r(24), imm(0xfe)).op(AVR_SBCI, r(25), imm(0xff));
    h.op(AVR_BRLO, avr_label(".L1")).op(AVR_INC, r(24)).label(".L1").op(AVR_RET);
    EXPECT_EQ(R"(subi r24, 254
sbci r25, 255
brlo .L1
inc r24
.L1:
ret
)", h.Peephole());
}

// ldi into a dead temporary, then cp: cpi.
TEST(AvrPeephole, CompareImmediate)
{
    Fn f;
    f.op(AVR_LDI, r(27), imm(7)).op(AVR_CP, r(24), r(27)).op(AVR_BREQ, avr_label(".L1"));
    f.op(AVR_INC, r(24)).label(".L1").op(AVR_RET);
    EXPECT_EQ(R"(cpi r24, 7
breq .L1
inc r24
.L1:
ret
)", f.Peephole());
}

// A branch over a jump is the inverse branch; a jump to the next instruction goes.
TEST(AvrPeephole, Jumps)
{
    Fn f;
    f.op(AVR_CP, r(24), r(22)).op(AVR_BREQ, avr_label(".L1")).op(AVR_RJMP, avr_label(".L2"));
    f.label(".L1").op(AVR_INC, r(24)).op(AVR_RJMP, avr_label(".L2"));
    f.label(".L2").op(AVR_RET);
    EXPECT_EQ(R"(cp r24, r22
brne .L2
.L1:
inc r24
.L2:
ret
)", f.Peephole());
}

// Code after an unconditional jump, up to a label something jumps to, goes.
TEST(AvrPeephole, UnreachableGoes)
{
    Fn f;
    f.op(AVR_RJMP, avr_label(".L2")).op(AVR_INC, r(24)).label(".L1").op(AVR_INC, r(24));
    f.label(".L2").op(AVR_RET);
    EXPECT_EQ(R"(.L1:
.L2:
ret
)", f.Peephole());
}

// After the frame: a call then ret is a tail jump; a jump to a lone ret is ret.
TEST(AvrPeephole, TailCall)
{
    Fn f;
    f.op(AVR_CP, r(24), r(22)).op(AVR_BREQ, avr_label(".L1")).op(AVR_CALL, avr_label("g"));
    f.op(AVR_RJMP, avr_label(".Lx")).label(".L1").op(AVR_INC, r(24)).label(".Lx").op(AVR_RET);
    EXPECT_EQ(R"(cp r24, r22
breq .L1
jmp g
.L1:
inc r24
.Lx:
ret
)", f.Peephole(true));
}

// A comparison read only by the branch after it: no 0 or 1 in between.
TEST_F(AvrTest, CompareFusedWithBranch)
{
    EXPECT_EQ(R"(cp r24, r22
cpc r25, r23
brge .L1
ldi r24, 1
ldi r25, 0
ret
ldi r24, 2
ldi r25, 0
ret
)",
              Code(CompileToAvr("int f(int a, int b) { if (a < b) return 1; return 2; }")));
}

TEST_F(AvrTest, RunCompareFusedWithBranch)
{
    SKIP_IF_NO_AVR_TOOLS();
    EXPECT_EQ("", CompileAndRunAvr(R"(
int lt(int a, int b) { if (a < b) return 1; return 2; }
int gtu(unsigned a, unsigned b) { return a > b ? 3 : 4; }
int lef(float a, float b) { if (a <= b) return 5; return 6; }
int nel(long a, long b) { while (a != b) a++; return (int)a; }
int main(void)
{
    if (lt(-1, 1) != 1 || lt(1, -1) != 2) return 1;
    if (gtu(-1, 1) != 3 || gtu(1, -1) != 4) return 2;
    if (lef(1.5f, 1.5f) != 5 || lef(2.0f, 1.0f) != 6) return 3;
    if (nel(70000, 70010) != 4474) return 4;
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// A volatile variable is read and written every time.
TEST_F(AvrTest, VolatileKept)
{
    std::string s = Code(CompileToAvr("int f(void) { volatile int v = 1; v = 2; return v + v; }"));
    EXPECT_NE(std::string::npos,
              s.find(R"(ldi r26, 1
std Y+1, r26
std Y+2, r1
ldi r26, 2
std Y+1, r26
std Y+2, r1
ldd r22, Y+1
ldd r23, Y+2
ldd r24, Y+1
ldd r25, Y+2
)"))
        << s;
}
