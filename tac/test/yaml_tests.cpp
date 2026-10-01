#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "tac.h"
#include "xalloc.h"

extern "C" int xalloc_debug;

class TacYamlTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Enable to debug allocations:
        // xalloc_debug = 1;
    }

    void TearDown() override
    {
        xreport_lost_memory();
        EXPECT_EQ(xtotal_allocated_size(), 0);
        xfree_all();
    }

    // Capture tac_export_yaml() output for a single toplevel into a std::string.
    static std::string capture(const Tac_TopLevel *tl)
    {
        FILE *f = tmpfile();
        EXPECT_NE(f, nullptr);
        tac_export_yaml(f, tl);
        long len = ftell(f);
        rewind(f);
        std::string result(static_cast<size_t>(len), '\0');
        EXPECT_TRUE(fread(&result[0], 1, static_cast<size_t>(len), f));
        fclose(f);
        return result;
    }

    // Build a constant Val (does not go into any instruction; caller owns it).
    static Tac_Val *make_const_int(int v)
    {
        Tac_Const *c    = tac_new_const(TAC_CONST_INT);
        c->u.int_val    = v;
        Tac_Val *val    = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant = c;
        return val;
    }

    static Tac_Val *make_var(const char *name)
    {
        Tac_Val *val    = tac_new_val(TAC_VAL_VAR);
        val->u.var_name = xstrdup(name);
        return val;
    }

    // Build a minimal function toplevel with no params and no body.
    static Tac_TopLevel *make_empty_function(const char *name, bool global)
    {
        Tac_TopLevel *tl      = tac_new_toplevel(TAC_TOPLEVEL_FUNCTION);
        tl->u.function.name   = xstrdup(name);
        tl->u.function.global = global;
        return tl;
    }
};

// ---------------------------------------------------------------------------
// Toplevel: function
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, FunctionEmpty)
{
    Tac_TopLevel *tl = make_empty_function("foo", true);
    std::string out  = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("- toplevel:"), std::string::npos);
    EXPECT_NE(out.find("kind: function"), std::string::npos);
    EXPECT_NE(out.find("name: foo"), std::string::npos);
    EXPECT_NE(out.find("global: true"), std::string::npos);
    // No params or body sections when absent
    EXPECT_EQ(out.find("params:"), std::string::npos);
    EXPECT_EQ(out.find("body:"), std::string::npos);
}

TEST_F(TacYamlTest, GlobalFalse)
{
    Tac_TopLevel *tl = make_empty_function("bar", false);
    std::string out  = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("global: false"), std::string::npos);
}

TEST_F(TacYamlTest, FunctionWithParams)
{
    Tac_TopLevel *tl      = make_empty_function("add", true);
    Tac_Param *p1         = tac_new_param();
    p1->name              = xstrdup("x");
    Tac_Param *p2         = tac_new_param();
    p2->name              = xstrdup("y");
    p1->next              = p2;
    tl->u.function.params = p1;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("params:"), std::string::npos);
    EXPECT_NE(out.find("- param: x"), std::string::npos);
    EXPECT_NE(out.find("- param: y"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Return instruction with various constant kinds
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, ReturnInt)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    instr->u.return_.src   = make_const_int(42);
    tl->u.function.body    = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("- instruction:"), std::string::npos);
    EXPECT_NE(out.find("kind: return"), std::string::npos);
    EXPECT_NE(out.find("kind: constant"), std::string::npos);
    EXPECT_NE(out.find("kind: int"), std::string::npos);
    EXPECT_NE(out.find("value: 42"), std::string::npos);
}

TEST_F(TacYamlTest, ReturnLong)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Const *c           = tac_new_const(TAC_CONST_LONG);
    c->u.long_val          = 100000L;
    Tac_Val *val           = tac_new_val(TAC_VAL_CONSTANT);
    val->u.constant        = c;
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    instr->u.return_.src   = val;
    tl->u.function.body    = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: long"), std::string::npos);
    EXPECT_NE(out.find("value: 100000"), std::string::npos);
}

TEST_F(TacYamlTest, ReturnLongLong)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Const *c           = tac_new_const(TAC_CONST_LONG_LONG);
    c->u.long_long_val     = -9000000000LL;
    Tac_Val *val           = tac_new_val(TAC_VAL_CONSTANT);
    val->u.constant        = c;
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    instr->u.return_.src   = val;
    tl->u.function.body    = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: long_long"), std::string::npos);
    EXPECT_NE(out.find("-9000000000"), std::string::npos);
}

TEST_F(TacYamlTest, ReturnUnsignedInt)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Const *c           = tac_new_const(TAC_CONST_UINT);
    c->u.uint_val          = 7u;
    Tac_Val *val           = tac_new_val(TAC_VAL_CONSTANT);
    val->u.constant        = c;
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    instr->u.return_.src   = val;
    tl->u.function.body    = instr;
    std::string out        = capture(tl);
    tac_free_toplevel(tl);
    EXPECT_NE(out.find("kind: uint"), std::string::npos);
}

TEST_F(TacYamlTest, ReturnUnsignedLong)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Const *c           = tac_new_const(TAC_CONST_ULONG);
    c->u.ulong_val         = 123456ul;
    Tac_Val *val           = tac_new_val(TAC_VAL_CONSTANT);
    val->u.constant        = c;
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    instr->u.return_.src   = val;
    tl->u.function.body    = instr;
    std::string out        = capture(tl);
    tac_free_toplevel(tl);
    EXPECT_NE(out.find("kind: ulong"), std::string::npos);
}

TEST_F(TacYamlTest, ReturnUnsignedLongLong)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Const *c           = tac_new_const(TAC_CONST_ULONG_LONG);
    c->u.ulong_long_val    = 999999999999ull;
    Tac_Val *val           = tac_new_val(TAC_VAL_CONSTANT);
    val->u.constant        = c;
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    instr->u.return_.src   = val;
    tl->u.function.body    = instr;
    std::string out        = capture(tl);
    tac_free_toplevel(tl);
    EXPECT_NE(out.find("kind: ulong_long"), std::string::npos);
}

TEST_F(TacYamlTest, ReturnFloat)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Const *c           = tac_new_const(TAC_CONST_FLOAT);
    c->u.float_val         = 1.5f;
    Tac_Val *val           = tac_new_val(TAC_VAL_CONSTANT);
    val->u.constant        = c;
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    instr->u.return_.src   = val;
    tl->u.function.body    = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: float"), std::string::npos);
    EXPECT_NE(out.find("value: "), std::string::npos);
}

TEST_F(TacYamlTest, ReturnDouble)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Const *c           = tac_new_const(TAC_CONST_DOUBLE);
    c->u.double_val        = 3.14;
    Tac_Val *val           = tac_new_val(TAC_VAL_CONSTANT);
    val->u.constant        = c;
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    instr->u.return_.src   = val;
    tl->u.function.body    = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: double"), std::string::npos);
    EXPECT_NE(out.find("value: "), std::string::npos);
}

TEST_F(TacYamlTest, ReturnChar)
{
    // char
    {
        Tac_TopLevel *tl       = make_empty_function("f", true);
        Tac_Const *c           = tac_new_const(TAC_CONST_SCHAR);
        c->u.char_val          = 65; // 'A'
        Tac_Val *val           = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant        = c;
        Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_RETURN);
        instr->u.return_.src   = val;
        tl->u.function.body    = instr;
        std::string out        = capture(tl);
        tac_free_toplevel(tl);
        EXPECT_NE(out.find("kind: char"), std::string::npos);
        EXPECT_NE(out.find("value: 65"), std::string::npos);
    }
    // uchar
    {
        Tac_TopLevel *tl       = make_empty_function("f", true);
        Tac_Const *c           = tac_new_const(TAC_CONST_UCHAR);
        c->u.uchar_val         = 200;
        Tac_Val *val           = tac_new_val(TAC_VAL_CONSTANT);
        val->u.constant        = c;
        Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_RETURN);
        instr->u.return_.src   = val;
        tl->u.function.body    = instr;
        std::string out        = capture(tl);
        tac_free_toplevel(tl);
        EXPECT_NE(out.find("kind: uchar"), std::string::npos);
        EXPECT_NE(out.find("value: 200"), std::string::npos);
    }
}

TEST_F(TacYamlTest, ReturnVar)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    instr->u.return_.src   = make_var("tmp.0");
    tl->u.function.body    = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: var"), std::string::npos);
    EXPECT_NE(out.find("name: tmp.0"), std::string::npos);
}

TEST_F(TacYamlTest, ReturnNoSrc)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    // src is NULL (void return)
    tl->u.function.body = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: return"), std::string::npos);
    // No "src:" field when src is NULL
    EXPECT_EQ(out.find("src:"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Unary instruction
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, UnaryInstructions)
{
    const Tac_UnaryOperator ops[] = { TAC_UNARY_COMPLEMENT, TAC_UNARY_NEGATE, TAC_UNARY_NOT };
    const char *const names[]     = { "complement", "negate", "not" };

    for (int i = 0; i < 3; i++) {
        Tac_TopLevel *tl       = make_empty_function("f", true);
        Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_UNARY);
        instr->u.unary.op      = ops[i];
        instr->u.unary.src     = make_var("src");
        instr->u.unary.dst     = make_var("dst");
        tl->u.function.body    = instr;

        std::string out = capture(tl);
        tac_free_toplevel(tl);

        EXPECT_NE(out.find("kind: unary"), std::string::npos) << "op=" << names[i];
        std::string opstr = std::string("op: ") + names[i];
        EXPECT_NE(out.find(opstr), std::string::npos) << "op=" << names[i];
        EXPECT_NE(out.find("src:"), std::string::npos);
        EXPECT_NE(out.find("dst:"), std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Binary instruction
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, BinaryInstructions)
{
    const struct {
        Tac_BinaryOperator op;
        const char *name;
    } cases[] = {
        { TAC_BINARY_ADD, "add" },
        { TAC_BINARY_SUBTRACT, "subtract" },
        { TAC_BINARY_MULTIPLY, "multiply" },
        { TAC_BINARY_DIVIDE, "divide" },
        { TAC_BINARY_REMAINDER, "remainder" },
        { TAC_BINARY_EQUAL, "equal" },
        { TAC_BINARY_NOT_EQUAL, "not_equal" },
        { TAC_BINARY_LESS_THAN, "less_than" },
        { TAC_BINARY_LESS_OR_EQUAL, "less_or_equal" },
        { TAC_BINARY_GREATER_THAN, "greater_than" },
        { TAC_BINARY_GREATER_OR_EQUAL, "greater_or_equal" },
        { TAC_BINARY_BITWISE_AND, "bitwise_and" },
        { TAC_BINARY_BITWISE_OR, "bitwise_or" },
        { TAC_BINARY_BITWISE_XOR, "bitwise_xor" },
        { TAC_BINARY_LEFT_SHIFT, "left_shift" },
        { TAC_BINARY_RIGHT_SHIFT, "right_shift" },
        { TAC_BINARY_DIVIDE_UNSIGNED, "divide_unsigned" },
        { TAC_BINARY_REMAINDER_UNSIGNED, "remainder_unsigned" },
        { TAC_BINARY_LESS_THAN_UNSIGNED, "less_than_unsigned" },
        { TAC_BINARY_LESS_OR_EQUAL_UNSIGNED, "less_or_equal_unsigned" },
        { TAC_BINARY_GREATER_THAN_UNSIGNED, "greater_than_unsigned" },
        { TAC_BINARY_GREATER_OR_EQUAL_UNSIGNED, "greater_or_equal_unsigned" },
        { TAC_BINARY_RIGHT_SHIFT_LOGICAL, "right_shift_logical" },
    };

    for (const auto &tc : cases) {
        Tac_TopLevel *tl       = make_empty_function("f", true);
        Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_BINARY);
        instr->u.binary.op     = tc.op;
        instr->u.binary.src1   = make_var("a");
        instr->u.binary.src2   = make_var("b");
        instr->u.binary.dst    = make_var("c");
        tl->u.function.body    = instr;

        std::string out = capture(tl);
        tac_free_toplevel(tl);

        EXPECT_NE(out.find("kind: binary"), std::string::npos) << "op=" << tc.name;
        std::string opstr = std::string("op: ") + tc.name;
        EXPECT_NE(out.find(opstr), std::string::npos) << "op=" << tc.name;
        EXPECT_NE(out.find("src1:"), std::string::npos);
        EXPECT_NE(out.find("src2:"), std::string::npos);
        EXPECT_NE(out.find("dst:"), std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Copy and GetAddress
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, CopyInstruction)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_COPY);
    instr->u.copy.src      = make_var("x");
    instr->u.copy.dst      = make_var("y");
    tl->u.function.body    = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: copy"), std::string::npos);
    EXPECT_NE(out.find("src:"), std::string::npos);
    EXPECT_NE(out.find("dst:"), std::string::npos);
}

TEST_F(TacYamlTest, GetAddressInstruction)
{
    Tac_TopLevel *tl         = make_empty_function("f", true);
    Tac_Instruction *instr   = tac_new_instruction(TAC_INSTRUCTION_GET_ADDRESS);
    instr->u.get_address.src = make_var("x");
    instr->u.get_address.dst = make_var("p");
    tl->u.function.body      = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: get_address"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Load and Store
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, LoadInstruction)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_LOAD);
    instr->u.load.src_ptr  = make_var("ptr");
    instr->u.load.dst      = make_var("val");
    tl->u.function.body    = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: load"), std::string::npos);
    EXPECT_NE(out.find("src_ptr:"), std::string::npos);
    EXPECT_NE(out.find("dst:"), std::string::npos);
}

TEST_F(TacYamlTest, StoreInstruction)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_STORE);
    instr->u.store.src     = make_var("val");
    instr->u.store.dst_ptr = make_var("ptr");
    tl->u.function.body    = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: store"), std::string::npos);
    EXPECT_NE(out.find("src:"), std::string::npos);
    EXPECT_NE(out.find("dst_ptr:"), std::string::npos);
}

// ---------------------------------------------------------------------------
// AddPtr
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, AddPtrInstruction)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_ADD_PTR);
    instr->u.add_ptr.ptr   = make_var("p");
    instr->u.add_ptr.index = make_var("i");
    instr->u.add_ptr.scale = 4;
    instr->u.add_ptr.dst   = make_var("r");
    tl->u.function.body    = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: add_ptr"), std::string::npos);
    EXPECT_NE(out.find("ptr:"), std::string::npos);
    EXPECT_NE(out.find("index:"), std::string::npos);
    EXPECT_NE(out.find("scale: 4"), std::string::npos);
    EXPECT_NE(out.find("dst:"), std::string::npos);
}

// ---------------------------------------------------------------------------
// CopyToOffset / CopyFromOffset
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, CopyToOffsetInstruction)
{
    Tac_TopLevel *tl               = make_empty_function("f", true);
    Tac_Instruction *instr         = tac_new_instruction(TAC_INSTRUCTION_COPY_TO_OFFSET);
    instr->u.copy_to_offset.src    = make_var("s");
    instr->u.copy_to_offset.dst    = xstrdup("agg");
    instr->u.copy_to_offset.offset = 8;
    tl->u.function.body            = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: copy_to_offset"), std::string::npos);
    EXPECT_NE(out.find("src:"), std::string::npos);
    EXPECT_NE(out.find("dst: agg"), std::string::npos);
    EXPECT_NE(out.find("offset: 8"), std::string::npos);
}

TEST_F(TacYamlTest, CopyFromOffsetInstruction)
{
    Tac_TopLevel *tl                 = make_empty_function("f", true);
    Tac_Instruction *instr           = tac_new_instruction(TAC_INSTRUCTION_COPY_FROM_OFFSET);
    instr->u.copy_from_offset.src    = xstrdup("agg");
    instr->u.copy_from_offset.offset = 16;
    instr->u.copy_from_offset.dst    = make_var("d");
    tl->u.function.body              = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: copy_from_offset"), std::string::npos);
    EXPECT_NE(out.find("src: agg"), std::string::npos);
    EXPECT_NE(out.find("offset: 16"), std::string::npos);
    EXPECT_NE(out.find("dst:"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Type conversion instructions
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, ConversionInstructions)
{
    const struct {
        Tac_InstructionKind kind;
        const char *name;
    } cases[] = {
        { TAC_INSTRUCTION_SIGN_EXTEND, "sign_extend" },
        { TAC_INSTRUCTION_TRUNCATE, "truncate" },
        { TAC_INSTRUCTION_ZERO_EXTEND, "zero_extend" },
        { TAC_INSTRUCTION_DOUBLE_TO_INT, "double_to_int" },
        { TAC_INSTRUCTION_DOUBLE_TO_UINT, "double_to_uint" },
        { TAC_INSTRUCTION_INT_TO_DOUBLE, "int_to_double" },
        { TAC_INSTRUCTION_UINT_TO_DOUBLE, "uint_to_double" },
    };

    for (const auto &tc : cases) {
        Tac_TopLevel *tl       = make_empty_function("f", true);
        Tac_Instruction *instr = tac_new_instruction(tc.kind);
        // All conversion structs share the same src/dst layout
        instr->u.sign_extend.src = make_var("s");
        instr->u.sign_extend.dst = make_var("d");
        tl->u.function.body      = instr;

        std::string out = capture(tl);
        tac_free_toplevel(tl);

        std::string kindstr = std::string("kind: ") + tc.name;
        EXPECT_NE(out.find(kindstr), std::string::npos) << "kind=" << tc.name;
        EXPECT_NE(out.find("src:"), std::string::npos);
        EXPECT_NE(out.find("dst:"), std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Jump and Label
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, JumpInstruction)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_JUMP);
    instr->u.jump.target   = xstrdup("loop_end");
    tl->u.function.body    = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: jump"), std::string::npos);
    EXPECT_NE(out.find("target: loop_end"), std::string::npos);
}

TEST_F(TacYamlTest, LabelInstruction)
{
    Tac_TopLevel *tl       = make_empty_function("f", true);
    Tac_Instruction *instr = tac_new_instruction(TAC_INSTRUCTION_LABEL);
    instr->u.label.name    = xstrdup("loop_start");
    tl->u.function.body    = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: label"), std::string::npos);
    EXPECT_NE(out.find("name: loop_start"), std::string::npos);
}

TEST_F(TacYamlTest, JumpIfZeroInstruction)
{
    Tac_TopLevel *tl                = make_empty_function("f", true);
    Tac_Instruction *instr          = tac_new_instruction(TAC_INSTRUCTION_JUMP_IF_ZERO);
    instr->u.jump_if_zero.condition = make_var("cond");
    instr->u.jump_if_zero.target    = xstrdup("false_lbl");
    tl->u.function.body             = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: jump_if_zero"), std::string::npos);
    EXPECT_NE(out.find("condition:"), std::string::npos);
    EXPECT_NE(out.find("target: false_lbl"), std::string::npos);
}

TEST_F(TacYamlTest, JumpIfNotZeroInstruction)
{
    Tac_TopLevel *tl                    = make_empty_function("f", true);
    Tac_Instruction *instr              = tac_new_instruction(TAC_INSTRUCTION_JUMP_IF_NOT_ZERO);
    instr->u.jump_if_not_zero.condition = make_var("cond");
    instr->u.jump_if_not_zero.target    = xstrdup("true_lbl");
    tl->u.function.body                 = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: jump_if_not_zero"), std::string::npos);
    EXPECT_NE(out.find("target: true_lbl"), std::string::npos);
}

// ---------------------------------------------------------------------------
// FunCall
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, FunCallWithArgs)
{
    Tac_TopLevel *tl           = make_empty_function("f", true);
    Tac_Instruction *instr     = tac_new_instruction(TAC_INSTRUCTION_FUN_CALL);
    instr->u.fun_call.fun_name = xstrdup("printf");
    Tac_Val *arg1              = make_var("fmt");
    Tac_Val *arg2              = make_var("x");
    arg1->next                 = arg2;
    instr->u.fun_call.args     = arg1;
    instr->u.fun_call.dst      = make_var("ret");
    tl->u.function.body        = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: fun_call"), std::string::npos);
    EXPECT_NE(out.find("fun_name: printf"), std::string::npos);
    EXPECT_NE(out.find("args:"), std::string::npos);
    EXPECT_NE(out.find("- val:"), std::string::npos);
    EXPECT_NE(out.find("dst:"), std::string::npos);
}

TEST_F(TacYamlTest, FunCallNoArgs)
{
    Tac_TopLevel *tl           = make_empty_function("f", true);
    Tac_Instruction *instr     = tac_new_instruction(TAC_INSTRUCTION_FUN_CALL);
    instr->u.fun_call.fun_name = xstrdup("rand");
    // args is NULL, dst is NULL (void)
    tl->u.function.body = instr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: fun_call"), std::string::npos);
    EXPECT_NE(out.find("fun_name: rand"), std::string::npos);
    EXPECT_EQ(out.find("args:"), std::string::npos);
    EXPECT_EQ(out.find("dst:"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Instruction sequence (linked via .next)
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, InstructionSequence)
{
    Tac_TopLevel *tl = make_empty_function("f", true);

    Tac_Instruction *lbl = tac_new_instruction(TAC_INSTRUCTION_LABEL);
    lbl->u.label.name    = xstrdup("entry");

    Tac_Instruction *ret = tac_new_instruction(TAC_INSTRUCTION_RETURN);
    ret->u.return_.src   = make_const_int(0);

    lbl->next           = ret;
    tl->u.function.body = lbl;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    // Both instructions appear
    EXPECT_NE(out.find("kind: label"), std::string::npos);
    EXPECT_NE(out.find("name: entry"), std::string::npos);
    EXPECT_NE(out.find("kind: return"), std::string::npos);
    // Two "- instruction:" entries
    size_t pos  = out.find("- instruction:");
    size_t pos2 = out.find("- instruction:", pos + 1);
    EXPECT_NE(pos2, std::string::npos);
}

// ---------------------------------------------------------------------------
// Static variable toplevel
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, StaticVariableInt)
{
    Tac_TopLevel *tl                = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    tl->u.static_variable.name      = xstrdup("g");
    tl->u.static_variable.global    = true;
    Tac_Type *ty                    = tac_new_type(TAC_TYPE_INT);
    tl->u.static_variable.type      = ty;
    Tac_StaticInit *si              = tac_new_static_init(TAC_STATIC_INIT_I32);
    si->u.int_val                   = 99;
    tl->u.static_variable.init_list = si;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: static_variable"), std::string::npos);
    EXPECT_NE(out.find("name: g"), std::string::npos);
    EXPECT_NE(out.find("global: true"), std::string::npos);
    EXPECT_NE(out.find("kind: int"), std::string::npos);
    EXPECT_NE(out.find("- init:"), std::string::npos);
    EXPECT_NE(out.find("kind: i32"), std::string::npos);
    EXPECT_NE(out.find("value: 99"), std::string::npos);
}

TEST_F(TacYamlTest, StaticVariableAllInits)
{
    Tac_TopLevel *tl             = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    tl->u.static_variable.name   = xstrdup("data");
    tl->u.static_variable.global = false;
    tl->u.static_variable.type   = tac_new_type(TAC_TYPE_SCHAR);

    Tac_StaticInit *i8            = tac_new_static_init(TAC_STATIC_INIT_I8);
    i8->u.char_val                = -1;
    Tac_StaticInit *i32           = tac_new_static_init(TAC_STATIC_INIT_I32);
    i32->u.int_val                = 1000;
    Tac_StaticInit *i64           = tac_new_static_init(TAC_STATIC_INIT_I64);
    i64->u.long_val               = 1000000000LL;
    Tac_StaticInit *u8            = tac_new_static_init(TAC_STATIC_INIT_U8);
    u8->u.uchar_val               = 255;
    Tac_StaticInit *u32           = tac_new_static_init(TAC_STATIC_INIT_U32);
    u32->u.uint_val               = 4000000000u;
    Tac_StaticInit *u64           = tac_new_static_init(TAC_STATIC_INIT_U64);
    u64->u.ulong_val              = 18000000000ull;
    Tac_StaticInit *flt           = tac_new_static_init(TAC_STATIC_INIT_FLOAT);
    flt->u.float_val              = 1.5f;
    Tac_StaticInit *dbl           = tac_new_static_init(TAC_STATIC_INIT_DOUBLE);
    dbl->u.double_val             = 2.718;
    Tac_StaticInit *zero          = tac_new_static_init(TAC_STATIC_INIT_ZERO);
    zero->u.zero_bytes            = 16;
    Tac_StaticInit *str           = tac_new_static_init(TAC_STATIC_INIT_STRING);
    str->u.string.val             = xstrdup("hello");
    str->u.string.len             = 5;
    str->u.string.null_terminated = false;
    Tac_StaticInit *ptr           = tac_new_static_init(TAC_STATIC_INIT_POINTER);
    ptr->u.pointer.name           = xstrdup("arr");

    i8->next                        = i32;
    i32->next                       = i64;
    i64->next                       = u8;
    u8->next                        = u32;
    u32->next                       = u64;
    u64->next                       = flt;
    flt->next                       = dbl;
    dbl->next                       = zero;
    zero->next                      = str;
    str->next                       = ptr;
    tl->u.static_variable.init_list = i8;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: i8"), std::string::npos);
    EXPECT_NE(out.find("kind: i32"), std::string::npos);
    EXPECT_NE(out.find("kind: i64"), std::string::npos);
    EXPECT_NE(out.find("kind: u8"), std::string::npos);
    EXPECT_NE(out.find("kind: u32"), std::string::npos);
    EXPECT_NE(out.find("kind: u64"), std::string::npos);
    EXPECT_NE(out.find("kind: float"), std::string::npos);
    EXPECT_NE(out.find("kind: double"), std::string::npos);
    EXPECT_NE(out.find("kind: zero"), std::string::npos);
    EXPECT_NE(out.find("bytes: 16"), std::string::npos);
    EXPECT_NE(out.find("kind: string"), std::string::npos);
    EXPECT_NE(out.find("value: hello"), std::string::npos);
    EXPECT_NE(out.find("null_terminated: false"), std::string::npos);
    EXPECT_NE(out.find("kind: pointer"), std::string::npos);
    EXPECT_NE(out.find("name: arr"), std::string::npos);
}

TEST_F(TacYamlTest, StaticVariableStringNullTerminated)
{
    Tac_TopLevel *tl             = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    tl->u.static_variable.name   = xstrdup("s");
    tl->u.static_variable.global = true;
    tl->u.static_variable.type   = tac_new_type(TAC_TYPE_SCHAR);

    Tac_StaticInit *si              = tac_new_static_init(TAC_STATIC_INIT_STRING);
    si->u.string.val                = xstrdup("world");
    si->u.string.len                = 5;
    si->u.string.null_terminated    = true;
    tl->u.static_variable.init_list = si;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("null_terminated: true"), std::string::npos);
    EXPECT_NE(out.find("value: world"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Static constant toplevel
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, StaticConstant)
{
    Tac_TopLevel *tl           = tac_new_toplevel(TAC_TOPLEVEL_STATIC_CONSTANT);
    tl->u.static_constant.name = xstrdup("PI");
    tl->u.static_constant.type = tac_new_type(TAC_TYPE_DOUBLE);
    Tac_StaticInit *si         = tac_new_static_init(TAC_STATIC_INIT_DOUBLE);
    si->u.double_val           = 3.14159;
    tl->u.static_constant.init = si;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: static_constant"), std::string::npos);
    EXPECT_NE(out.find("name: PI"), std::string::npos);
    EXPECT_NE(out.find("kind: double"), std::string::npos);
    EXPECT_NE(out.find("kind: double"), std::string::npos);
    // type and init sections both present
    EXPECT_NE(out.find("type:"), std::string::npos);
    EXPECT_NE(out.find("init:"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, AllScalarTypeKinds)
{
    const struct {
        Tac_TypeKind kind;
        const char *name;
    } scalars[] = {
        { TAC_TYPE_SCHAR, "schar" },
        { TAC_TYPE_UCHAR, "uchar" },
        { TAC_TYPE_SHORT, "short" },
        { TAC_TYPE_INT, "int" },
        { TAC_TYPE_LONG, "long" },
        { TAC_TYPE_LONG_LONG, "long_long" },
        { TAC_TYPE_USHORT, "ushort" },
        { TAC_TYPE_UINT, "uint" },
        { TAC_TYPE_ULONG, "ulong" },
        { TAC_TYPE_ULONG_LONG, "ulong_long" },
        { TAC_TYPE_FLOAT, "float" },
        { TAC_TYPE_DOUBLE, "double" },
        { TAC_TYPE_VOID, "void" },
    };

    for (const auto &s : scalars) {
        Tac_TopLevel *tl             = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
        tl->u.static_variable.name   = xstrdup("v");
        tl->u.static_variable.global = false;
        tl->u.static_variable.type   = tac_new_type(s.kind);

        std::string out = capture(tl);
        tac_free_toplevel(tl);

        std::string kindstr = std::string("kind: ") + s.name;
        EXPECT_NE(out.find(kindstr), std::string::npos) << "type=" << s.name;
    }
}

TEST_F(TacYamlTest, FunType)
{
    Tac_TopLevel *tl             = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    tl->u.static_variable.name   = xstrdup("fp");
    tl->u.static_variable.global = false;

    Tac_Type *fun               = tac_new_type(TAC_TYPE_FUN_TYPE);
    Tac_Type *param             = tac_new_type(TAC_TYPE_INT);
    Tac_Type *ret               = tac_new_type(TAC_TYPE_VOID);
    fun->u.fun_type.param_types = param;
    fun->u.fun_type.ret_type    = ret;
    tl->u.static_variable.type  = fun;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: fun_type"), std::string::npos);
    EXPECT_NE(out.find("param_types:"), std::string::npos);
    EXPECT_NE(out.find("ret_type:"), std::string::npos);
    EXPECT_NE(out.find("kind: int"), std::string::npos);
    EXPECT_NE(out.find("kind: void"), std::string::npos);
}

TEST_F(TacYamlTest, PointerType)
{
    Tac_TopLevel *tl             = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    tl->u.static_variable.name   = xstrdup("p");
    tl->u.static_variable.global = false;

    Tac_Type *ptr              = tac_new_type(TAC_TYPE_POINTER);
    Tac_Type *target           = tac_new_type(TAC_TYPE_LONG);
    ptr->u.pointer.target_type = target;
    tl->u.static_variable.type = ptr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: pointer"), std::string::npos);
    EXPECT_NE(out.find("target:"), std::string::npos);
    EXPECT_NE(out.find("kind: long"), std::string::npos);
}

TEST_F(TacYamlTest, ArrayType)
{
    Tac_TopLevel *tl             = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    tl->u.static_variable.name   = xstrdup("arr");
    tl->u.static_variable.global = false;

    Tac_Type *arr              = tac_new_type(TAC_TYPE_ARRAY);
    Tac_Type *elem             = tac_new_type(TAC_TYPE_INT);
    arr->u.array.elem_type     = elem;
    arr->u.array.size          = 10;
    tl->u.static_variable.type = arr;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: array"), std::string::npos);
    EXPECT_NE(out.find("elem_type:"), std::string::npos);
    EXPECT_NE(out.find("size: 10"), std::string::npos);
}

TEST_F(TacYamlTest, StructureType)
{
    Tac_TopLevel *tl             = tac_new_toplevel(TAC_TOPLEVEL_STATIC_VARIABLE);
    tl->u.static_variable.name   = xstrdup("obj");
    tl->u.static_variable.global = false;

    Tac_Type *st               = tac_new_type(TAC_TYPE_STRUCTURE);
    st->u.structure.tag        = xstrdup("Point");
    tl->u.static_variable.type = st;

    std::string out = capture(tl);
    tac_free_toplevel(tl);

    EXPECT_NE(out.find("kind: structure"), std::string::npos);
    EXPECT_NE(out.find("tag: Point"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Typed TAC
// ---------------------------------------------------------------------------

TEST_F(TacYamlTest, TypedFunction)
{
    Tac_TopLevel *tl                         = make_empty_function("f", true);
    tl->u.function.type                      = tac_new_type(TAC_TYPE_FUN_TYPE);
    tl->u.function.type->u.fun_type.param_types = tac_new_type(TAC_TYPE_LONG);
    tl->u.function.type->u.fun_type.ret_type = tac_new_type(TAC_TYPE_DOUBLE);
    Tac_Param *p                             = tac_new_param();
    p->name                                  = xstrdup("%n");
    p->type                                  = tac_new_type(TAC_TYPE_LONG);
    tl->u.function.params                    = p;
    Tac_Param *l                             = tac_new_param();
    l->name                                  = xstrdup("%1");
    l->type                                  = tac_new_type(TAC_TYPE_DOUBLE);
    tl->u.function.locals                    = l;

    std::string out = capture(tl);
    EXPECT_EQ(out, R"(- toplevel:
  kind: function
  name: f
  global: true
  type: fn(long) -> double
  params:
    - param: %n
      type: long
  locals:
    - local: %1
      type: double
)");

    tac_yaml_types = false;
    out            = capture(tl);
    tac_yaml_types = true;
    EXPECT_EQ(out, R"(- toplevel:
  kind: function
  name: f
  global: true
  params:
    - param: %n
)");
    tac_free_toplevel(tl);
}

TEST_F(TacYamlTest, Extern)
{
    Tac_TopLevel *tl   = tac_new_toplevel(TAC_TOPLEVEL_EXTERN);
    tl->u.extern_.name = xstrdup("errno");
    tl->u.extern_.type = tac_new_type(TAC_TYPE_INT);

    std::string out = capture(tl);
    tac_free_toplevel(tl);
    EXPECT_EQ(out, R"(- toplevel:
  kind: extern
  name: errno
  type:
    kind: int
)");
}
