//
// MMIX IR emitter: the syntax of every instruction form, the operand checks, and the
// function layout.  Every rendering is also assembled by GNU as when it is installed.
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
#include "mmix_ir.h"
#include "xalloc.h"
}

// The libraries call fatal_error(); defined once for the mmix-tests binary.
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

static Mmix_Instr Make(Mmix_Op op, std::initializer_list<Mmix_Operand> opnds)
{
    Mmix_Instr in{};
    in.op = op;
    int i = 0;
    for (const Mmix_Operand &o : opnds)
        in.opnd[i++] = o;
    return in;
}

static void Free(Mmix_Instr &in)
{
    for (auto &o : in.opnd)
        xfree(o.sym);
}

// One instruction line.
static std::string Line(Mmix_Op op, std::initializer_list<Mmix_Operand> opnds)
{
    Mmix_Instr in = Make(op, opnds);
    std::string s = Capture([&](FILE *f) { mmix_emit_instr(f, &in); });
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

static Mmix_Operand R(int n)
{
    return mmix_reg(n);
}

static Mmix_Operand I(int64_t n)
{
    return mmix_imm(n);
}

TEST_F(EmitTest, Arithmetic)
{
    EXPECT_EQ(Line(MMIX_ADDU, { R(0), R(0), R(1) }), "    addu    $0, $0, $1\n");
    EXPECT_EQ(Line(MMIX_SUBU, { R(254), R(254), I(16) }), "    subu    $254, $254, 16\n");
    EXPECT_EQ(Line(MMIX_NEGU, { R(3), I(0), R(2) }), "    negu    $3, 0, $2\n");
    EXPECT_EQ(Line(MMIX_NEGU, { R(3), I(0), I(255) }), "    negu    $3, 0, 255\n");
    EXPECT_EQ(Line(MMIX_ADDU8, { R(1), R(2), R(3) }), "    8addu   $1, $2, $3\n");
    EXPECT_EQ(Line(MMIX_SLU, { R(1), R(1), I(32) }), "    slu     $1, $1, 32\n");
    EXPECT_EQ(Line(MMIX_ZSN, { R(1), R(2), I(1) }), "    zsn     $1, $2, 1\n");
    EXPECT_EQ(Line(MMIX_FIX, { R(1), I(1), R(2) }), "    fix     $1, 1, $2\n");
    EXPECT_EQ(Line(MMIX_FLOT, { R(1), R(2) }), "    flot    $1, $2\n");
    EXPECT_EQ(Line(MMIX_SET, { R(5), R(1) }), "    set     $5, $1\n");
    EXPECT_EQ(Line(MMIX_ADD, { R(MMIX_VREG + 3), R(0), R(1) }), "    add     v3, $0, $1\n");
}

// A wyde prints in hex, as the assembler reads #.
TEST_F(EmitTest, Wydes)
{
    EXPECT_EQ(Line(MMIX_SETL, { R(0), mmix_wyde(200) }), "    setl    $0, #c8\n");
    EXPECT_EQ(Line(MMIX_SETH, { R(4), mmix_wyde(0x4004) }), "    seth    $4, #4004\n");
    EXPECT_EQ(Line(MMIX_INCML, { R(2), mmix_wyde(0xffff) }), "    incml   $2, #ffff\n");
    EXPECT_EQ(Line(MMIX_ANDNL, { R(2), mmix_wyde(0x7ff) }), "    andnl   $2, #7ff\n");
}

TEST_F(EmitTest, Memory)
{
    EXPECT_EQ(Line(MMIX_LDO, { R(1), R(254), I(8) }), "    ldo     $1, $254, 8\n");
    EXPECT_EQ(Line(MMIX_LDO, { R(1), R(254), R(255) }), "    ldo     $1, $254, $255\n");
    EXPECT_EQ(Line(MMIX_STB, { R(1), R(2), I(0) }), "    stb     $1, $2, 0\n");
    EXPECT_EQ(Line(MMIX_LDT, { R(5), mmix_sym("g", 0) }), "    ldt     $5, g\n");
    EXPECT_EQ(Line(MMIX_STO, { R(0), mmix_sym("cnt$1", 8) }), "    sto     $0, cnt$1+8\n");
    EXPECT_EQ(Line(MMIX_LDSF, { R(0), mmix_sym("f", -4) }), "    ldsf    $0, f-4\n");
}

TEST_F(EmitTest, Control)
{
    EXPECT_EQ(Line(MMIX_BNZ, { R(1), mmix_label("L:3") }), "    bnz     $1, L:3\n");
    EXPECT_EQ(Line(MMIX_PBN, { R(1), mmix_label("L:12") }), "    pbn     $1, L:12\n");
    EXPECT_EQ(Line(MMIX_JMP, { mmix_label("L:1") }), "    jmp     L:1\n");
    EXPECT_EQ(Line(MMIX_JMP, { mmix_sym("f", 0) }), "    jmp     f\n");
    EXPECT_EQ(Line(MMIX_PUSHJ, { R(3), mmix_sym("ext", 0) }), "    pushj   $3, ext\n");
    EXPECT_EQ(Line(MMIX_PUSHGO, { R(3), R(4), I(0) }), "    pushgo  $3, $4, 0\n");
    EXPECT_EQ(Line(MMIX_POP, { I(1), I(0) }), "    pop     1, 0\n");
    EXPECT_EQ(Line(MMIX_LDA, { R(1), mmix_sym("arr", 16) }), "    lda     $1, arr+16\n");
    EXPECT_EQ(Line(MMIX_GETA, { R(2), mmix_label("LC:0") }), "    geta    $2, LC:0\n");
    EXPECT_EQ(Line(MMIX_GET, { R(2), mmix_special(MMIX_rJ) }), "    get     $2, rJ\n");
    EXPECT_EQ(Line(MMIX_PUT, { mmix_special(MMIX_rJ), R(2) }), "    put     rJ, $2\n");
    EXPECT_EQ(Line(MMIX_GET, { R(2), mmix_special(MMIX_rR) }), "    get     $2, rR\n");
    EXPECT_EQ(Line(MMIX_TRAP, { I(0), I(6), I(1) }), "    trap    0, 6, 1\n");
    EXPECT_EQ(Line(MMIX_SWYM, {}), "    swym\n");
}

// Operands that do not fit the form, an immediate out of its field above all, are a
// fatal error rather than a wrong instruction.
TEST_F(EmitTest, WrongOperandsAreFatal)
{
    EXPECT_DEATH(Line(MMIX_ADDU, { R(0), R(0), I(256) }), "wrong operands for addu");
    EXPECT_DEATH(Line(MMIX_ADDU, { R(0), R(0), I(-1) }), "wrong operands for addu");
    EXPECT_DEATH(Line(MMIX_ADDU, { R(0), I(1), R(0) }), "wrong operands for addu");
    EXPECT_DEATH(Line(MMIX_LDO, { R(0), R(254), I(256) }), "wrong operands for ldo");
    EXPECT_DEATH(Line(MMIX_SETL, { R(0), I(1) }), "wrong operands for setl");
    EXPECT_DEATH(Line(MMIX_SETL, { R(0), mmix_wyde(0x10000) }), "wrong operands for setl");
    EXPECT_DEATH(Line(MMIX_POP, { I(1), I(0x10000) }), "wrong operands for pop");
    EXPECT_DEATH(Line(MMIX_BZ, { R(0), mmix_sym("f", 0) }), "wrong operands for bz");
    EXPECT_DEATH(Line(MMIX_FADD, { R(0), R(1), I(2) }), "wrong operands for fadd");
    EXPECT_DEATH(Line(MMIX_NEGU, { R(0), R(1), R(2) }), "wrong operands for negu");
    EXPECT_DEATH(Line(MMIX_FIX, { R(0), I(5), R(2) }), "wrong operands for fix");
}

TEST_F(EmitTest, Function)
{
    Mmix_Func *fn  = mmix_new_func("main", true);
    Mmix_Instr *in = mmix_append(fn, MMIX_SETL);
    in->opnd[0]    = R(0);
    in->opnd[1]    = mmix_wyde(200);
    mmix_new_block(fn, "L:1");
    in          = mmix_append(fn, MMIX_POP);
    in->opnd[0] = I(1);
    in->opnd[1] = I(0);
    std::string s = Capture([&](FILE *f) { mmix_emit_func(f, fn); });
    mmix_free_func(fn);
    EXPECT_EQ(s, R"(    .text
    .global main
    .p2align 2
main:
    setl    $0, #c8
L:1:
    pop     1, 0
)");
}

// One instance of every opcode, in its form.
static std::vector<Mmix_Instr> AllOps()
{
    std::vector<Mmix_Instr> v;
    for (int i = 0; i < MMIX_NUM_OPS; i++) {
        Mmix_Op op = (Mmix_Op)i;
        switch (mmix_form[op]) {
        case MMIX_FORM_XYZ:
            v.push_back(Make(op, { R(1), R(2), R(3) }));
            v.push_back(Make(op, { R(31), R(30), I(255) }));
            break;
        case MMIX_FORM_FP:
            v.push_back(Make(op, { R(1), R(2), R(3) }));
            break;
        case MMIX_FORM_NEG:
            v.push_back(Make(op, { R(1), I(0), R(3) }));
            v.push_back(Make(op, { R(1), I(255), I(255) }));
            break;
        case MMIX_FORM_ROUND:
            v.push_back(Make(op, { R(1), I(1), R(3) }));
            v.push_back(Make(op, { R(1), I(4), R(3) }));
            break;
        case MMIX_FORM_MEM:
            v.push_back(Make(op, { R(1), R(254), I(248) }));
            v.push_back(Make(op, { R(1), R(254), R(255) }));
            v.push_back(Make(op, { R(1), mmix_sym("data", 16) }));
            break;
        case MMIX_FORM_XZ:
            v.push_back(Make(op, { R(1), R(2) }));
            v.push_back(Make(op, { R(1), I(255) }));
            break;
        case MMIX_FORM_XY:
            v.push_back(Make(op, { R(1), R(2) }));
            break;
        case MMIX_FORM_WYDE:
            v.push_back(Make(op, { R(1), mmix_wyde(0xffff) }));
            break;
        case MMIX_FORM_BRANCH:
            v.push_back(Make(op, { R(1), mmix_label("L:1") }));
            break;
        case MMIX_FORM_ADDR:
            v.push_back(Make(op, { R(1), op == MMIX_GETA ? mmix_label("L:1")
                                                          : mmix_sym("data", 8) }));
            break;
        case MMIX_FORM_JUMP:
            v.push_back(Make(op, { mmix_label("L:1") }));
            v.push_back(Make(op, { mmix_sym("ext", 0) }));
            break;
        case MMIX_FORM_PUSHJ:
            v.push_back(Make(op, { R(15), mmix_sym("ext", 0) }));
            break;
        case MMIX_FORM_POP:
            v.push_back(Make(op, { I(1), I(0) }));
            break;
        case MMIX_FORM_GET:
            v.push_back(Make(op, { R(2), mmix_special(MMIX_rJ) }));
            v.push_back(Make(op, { R(2), mmix_special(MMIX_rR) }));
            break;
        case MMIX_FORM_PUT:
            v.push_back(Make(op, { mmix_special(MMIX_rJ), R(2) }));
            break;
        case MMIX_FORM_TRAP:
            v.push_back(Make(op, { I(0), I(0), I(0) }));
            break;
        case MMIX_FORM_NONE:
            v.push_back(Make(op, {}));
            break;
        }
    }
    return v;
}

// Every opcode in every form, assembled by GNU as with the flags GCC passes it.
TEST_F(EmitTest, AssemblerAcceptsEveryForm)
{
    std::vector<std::string> lines;
    for (Mmix_Instr &in : AllOps()) {
        lines.push_back(Capture([&](FILE *f) { mmix_emit_instr(f, &in); }));
        Free(in);
    }
    if (!MMIX_TOOLS_FOUND || !tool_available(MMIX_AS))
        GTEST_SKIP() << "GNU MMIX assembler not found";
    std::string s_path   = TEST_DIR "/EmitTest.AllForms.s";
    std::string o_path   = TEST_DIR "/EmitTest.AllForms.o";
    std::string log_path = TEST_DIR "/EmitTest.AllForms.log";
    {
        std::ofstream s(s_path);
        s << R"(    .text
L:1:
)";
        for (const std::string &l : lines)
            s << l;
        s << "    .data\ndata:   .octa 0, 0, 0\n";
    }
    ASSERT_EQ(0, RunTool({ MMIX_AS, "-x", "-no-predefined-syms", "-o", o_path, s_path },
                         log_path))
        << ReadFile(log_path);
}
