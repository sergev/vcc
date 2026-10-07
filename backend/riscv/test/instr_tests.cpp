//
// Instruction selection: the operation chosen for each TAC operator and type.
//
#include <initializer_list>
#include <utility>

#include "riscv_test.h"


// Selection over operands in memory: register allocation off, slots from s0.
class InstrTest : public RiscvTest {
protected:
    InstrTest()
    {
        riscv_regalloc      = false;
        riscv_frame_pointer = true;
    }

    // The code of functions `T fN(void) { T a = 7; T b = 2; return a OP b; }`, one per
    // {type, op} pair, compiled unoptimized in one unit.
    std::string Ops(std::initializer_list<std::pair<const char *, const char *>> ops)
    {
        DisableOptimization();
        std::string src;
        int n = 0;
        for (const auto &o : ops) {
            std::string t = o.first;
            src += t + " f" + std::to_string(n++) + "(void) { " + t + " a = 7; " + t +
                   " b = 2; return a " + o.second + " b; }\n";
        }
        return Code(CompileToRiscv(src.c_str()));
    }
    static bool Has(const std::string &code, const std::string &seq)
    {
        return code.find(seq) != std::string::npos;
    }
};

TEST_F(InstrTest, IntUsesWordOps)
{
    std::string s = Ops({ { "int", "+" }, { "int", "/" }, { "int", ">>" }, { "unsigned", "%" },
                          { "unsigned", ">>" } });
    EXPECT_TRUE(Has(s, "addw t0, t0, t1\n"));
    EXPECT_TRUE(Has(s, "divw t0, t0, t1\n"));
    EXPECT_TRUE(Has(s, "sraw t0, t0, t1\n"));
    EXPECT_TRUE(Has(s, "remuw t0, t0, t1\n"));
    EXPECT_TRUE(Has(s, "srlw t0, t0, t1\n"));
}

TEST_F(InstrTest, LongUsesDoublewordOps)
{
    std::string s = Ops({ { "long", "*" }, { "unsigned long", "/" }, { "long", "<<" } });
    EXPECT_TRUE(Has(s, "ld t1, "));
    EXPECT_TRUE(Has(s, "mul t0, t0, t1\n"));
    EXPECT_TRUE(Has(s, "divu t0, t0, t1\n"));
    EXPECT_TRUE(Has(s, "sll t0, t0, t1\n"));
}

TEST_F(InstrTest, Comparisons)
{
    std::string s = Ops({ { "int", "<" }, { "int", "<=" }, { "unsigned", ">" }, { "long", "==" } });
    EXPECT_TRUE(Has(s, "slt t0, t0, t1\n"));
    EXPECT_TRUE(Has(s, R"(slt t0, t1, t0
xori t0, t0, 1
)"));
    EXPECT_TRUE(Has(s, "sltu t0, t1, t0\n"));
    EXPECT_TRUE(Has(s, R"(xor t0, t0, t1
seqz t0, t0
)"));
}

// unsigned → unsigned long zero-extends the sign-extended 32-bit register.
TEST_F(InstrTest, ZeroExtend)
{
    DisableOptimization();
    std::string s = Code(CompileToRiscv(
        "unsigned long f(void) { unsigned u = 4000000000u; return u; }"));
    EXPECT_TRUE(Has(s, R"(lw t0, -20(s0)
slli t0, t0, 32
srli t0, t0, 32
)")) << s;
}

// A loop condition loads into t0 and branches on zero; the back edge is a j.
TEST_F(InstrTest, Branches)
{
    DisableOptimization();
    std::string s = Code(CompileToRiscv("int main(void) { int a = 3; while (a) a = a - 1; return a; }"));
    EXPECT_TRUE(Has(s, R"(lw t0, -20(s0)
beqz t0, .LL0
)")) << s;
    EXPECT_TRUE(Has(s, "j .LL1\n")) << s;
}

// The ninth argument goes to the outgoing area at 0(sp) first, the others straight
// into a-registers; the result comes back in a0.
TEST_F(InstrTest, Call)
{
    std::string s = Code(CompileToRiscv(R"(
int f(int a, int b, int c, int d, int e, int f, int g, int h, int i);
int main(void) { return f(1, 2, 3, 4, 5, 6, 7, 8, 9); }
)"));
    EXPECT_TRUE(Has(s, R"(addi s0, sp, 16
addi sp, sp, -16
li t0, 9
sd t0, 0(sp)
li a0, 1
li a1, 2
)")) << s;
    EXPECT_TRUE(Has(s, R"(li a7, 8
call f
sw a0, -20(s0)
)")) << s;
}

// Doubles go through ft registers; a comparison lands in t0; double → int truncates.
TEST_F(InstrTest, FloatingPoint)
{
    std::string s = Code(CompileToRiscv(R"(
double add(double a, double b) { return a + b; }
int less(float a, float b) { return a < b; }
long trunc(double d) { return (long)d; }
double widen(unsigned u) { return u; }
)"));
    EXPECT_TRUE(Has(s, R"(fsd fa0, -24(s0)
fsd fa1, -32(s0)
fld ft0, -24(s0)
fld ft1, -32(s0)
fadd.d ft0, ft0, ft1
)")) << s;
    EXPECT_TRUE(Has(s, "flt.s t0, ft0, ft1\n")) << s;
    EXPECT_TRUE(Has(s, "fcvt.l.d t1, ft0, rtz\n")) << s;
    EXPECT_TRUE(Has(s, "fcvt.d.wu ft1, t0\n")) << s;
}

// Loads and stores through a pointer take the pointee's width; indexing scales by a
// shift.
TEST_F(InstrTest, PointerAccess)
{
    std::string s = Code(CompileToRiscv(R"(
void put(short *p, long i) { p[i] = 7; }
unsigned char get(unsigned char *p) { return *p; }
)"));
    EXPECT_TRUE(Has(s, "slli t1, t1, 1\nadd t0, t0, t1\n")) << s;
    EXPECT_TRUE(Has(s, "lh t0, -34(s0)\nsh t0, 0(t4)\n")) << s;
    EXPECT_TRUE(Has(s, "lbu t0, 0(t3)\n")) << s;
}

// A 16-byte struct argument goes in two registers; a 24-byte one by reference to a
// copy; a 16-byte result comes back in a0/a1.
TEST_F(InstrTest, StructArguments)
{
    std::string s = Code(CompileToRiscv(R"(
struct pair { long a, b; };
struct big { long x, y, z; };
struct pair f(struct pair p, struct big q);
long g(void) {
    struct pair p = { 1, 2 };
    struct big q = { 3, 4, 5 };
    struct pair r = f(p, q);
    return r.b;
}
)"));
    EXPECT_TRUE(Has(s, R"(ld a0, -32(s0)
ld a1, -24(s0)
)")) << s;
    EXPECT_TRUE(Has(s, R"(addi a2, s0, )")) << s;
    EXPECT_TRUE(Has(s, R"(call f
sd a0, )")) << s;
}

// A struct of a float and an int goes in fa0 and a0, one of two doubles in two FP
// registers, and comes back in fa0/fa1.
TEST_F(InstrTest, FloatStructArguments)
{
    std::string s = Code(CompileToRiscv(R"(
struct fi { float f; int i; };
struct dd { double a, b; };
struct dd f(struct fi p, struct dd q);
double g(void) {
    struct fi p = { 1.5f, 2 };
    struct dd q = { 3, 4 };
    struct dd r = f(p, q);
    return r.b;
}
)"));
    EXPECT_TRUE(Has(s, R"(flw fa0, -24(s0)
lw a0, -20(s0)
fld fa1, -40(s0)
fld fa2, -32(s0)
call f
fsd fa0, -72(s0)
fsd fa1, -64(s0)
)")) << s;
}

// Each bit-field is a field of its own, as with clang: a float and one bit-field go in
// fa0 and a0, a float and two bit-fields sharing a byte are three fields, so that struct
// goes in an integer register, and the next float in fa1.
TEST_F(InstrTest, FloatBitfieldStructArgument)
{
    std::string s = Code(CompileToRiscv(R"(
struct fx { float f; int x : 5; };
struct fb { float f; unsigned char p : 3, q : 4; };
int f(struct fx a, struct fb b, float g, int k);
int h(void) {
    struct fx a = { 1.5f, 2 };
    struct fb b = { 2.5f, 3, 4 };
    return f(a, b, 3.5f, 5);
}
)"));
    EXPECT_TRUE(Has(s, R"(flw fa0, -24(s0)
lbu a0, -20(s0)
ld a1, -52(s0)
li t6, 1080033280
fmv.w.x fa1, t6
li a2, 5
call f
)")) << s;
}

// An unnamed bit-field is a field too, so a float and one go in fa0 and a0.  A `:0`
// ahead of the second field makes the struct go in integer registers, as with clang.
TEST_F(InstrTest, UnnamedBitfieldStructArgument)
{
    std::string s = Code(CompileToRiscv(R"(
struct fa { float f; int : 3; };
struct fz { float f; int : 0; int x : 5; };
struct ff { float f; float g; int : 0; };
int f(struct fa a, struct fz z, struct ff w, int k);
int h(void) {
    struct fa a = { 1.5f };
    struct fz z = { 2.5f, 3 };
    struct ff w = { 3.5f, 4.5f };
    return f(a, z, w, 5);
}
)"));
    EXPECT_TRUE(Has(s, R"(flw fa0, -24(s0)
lbu a0, -20(s0)
ld a1, -32(s0)
flw fa1, -60(s0)
flw fa2, -56(s0)
li a2, 5
call f
)")) << s;
}
